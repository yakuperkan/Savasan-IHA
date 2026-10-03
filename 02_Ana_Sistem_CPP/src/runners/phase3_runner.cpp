/**
 * @file phase3_runner.cpp
 * @brief Faz-3/Faz-5 ortak boru hattı çalıştırıcısı.
 *
 * nvinfer + nvtracker + kilit seçimini kurar. `phase5` verilirse üstüne seri
 * gönderim, güdüm/PID, kaçış, yarışma HTTP ve otomatik tracker profili yeniden
 * başlatma mantığı eklenir. Probe ekleme sırası LIFO olduğundan önce ALC/mission,
 * sonra TrackSelector eklenir; böylece TrackSelector aynı karede önce çalışır.
 */
#include "runners/phase3_runner.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <signal.h>

#include <glib-unix.h>
#include <gst/gst.h>

#include "common/log.hpp"
#include "deepstream/ds_app.hpp"
#include "runners/gst_app_helpers.hpp"
#include "runners/phase5_callbacks.hpp"
#include "tracking/track_selector.hpp"

#ifndef SAVASAN_DEBUG
#define SAVASAN_DEBUG 0
#endif

namespace savasan::runners {

namespace {
/// @brief Otomatik profil yeniden başlatma yoklayıcısının bağlamı.
struct AutoRestartLoopCtx {
  GMainLoop* loop = nullptr;  ///< Kapatılacak ana döngü.
  Phase5Runtime* rt = nullptr;///< İzlenecek çalışma zamanı.
};

// Tracker profili yeniden başlatma istendiyse ana döngüyü kapatır (üst katman
// pipeline'ı yeni profille tekrar kurar).
gboolean AutoRestartPollTick(gpointer user_data) {
  auto* ctx = static_cast<AutoRestartLoopCtx*>(user_data);
  if (ctx == nullptr || ctx->loop == nullptr || ctx->rt == nullptr) {
    return G_SOURCE_REMOVE;
  }
  if (ctx->rt->tracker.profile.restart_requested.load()) {
    std::cout << "[Faz5] Auto tracker profile restart tetiklendi, loop kapatiliyor...\n";
    g_main_loop_quit(ctx->loop);
    return G_SOURCE_REMOVE;
  }
  return G_SOURCE_CONTINUE;
}

/// @brief Yeniden başlatma sonrası sağlık doğrulamasının bağlamı.
struct RestartHealthCheckCtx {
  Phase5Runtime* rt = nullptr; ///< Doğrulanacak çalışma zamanı.
};

// Profil geçişi sonrası belirlenen süre dolduğunda sistem sağlığını doğrular;
// sağlıksızsa fallback profile dön ve kontrollü yeniden başlatma iste.
gboolean RestartHealthCheckTick(gpointer user_data) {
  auto* ctx = static_cast<RestartHealthCheckCtx*>(user_data);
  if (ctx == nullptr || ctx->rt == nullptr) {
    return G_SOURCE_REMOVE;
  }
  auto* rt = ctx->rt;
  const auto now = std::chrono::steady_clock::now();
  bool validation_pending = false;
  std::chrono::steady_clock::time_point validation_deadline{};
  {
    std::lock_guard<std::mutex> lk(rt->mutex);
    validation_pending = rt->tracker.profile.validation_pending;
    validation_deadline = rt->tracker.profile.validation_deadline;
  }
  if (!validation_pending) {
    return G_SOURCE_REMOVE;
  }
  if (now < validation_deadline) {
    return G_SOURCE_CONTINUE;
  }
  const bool healthy = rt->health.degraded.pipeline_healthy.load() &&
                       rt->health.degraded.telemetry_valid_recent.load() &&
                       rt->tracker.lock_valid_seen.load();
  std::string validation_target;
  std::string validation_fallback;
  {
    std::lock_guard<std::mutex> lk(rt->mutex);
    if (!rt->tracker.profile.validation_pending) {
      return G_SOURCE_REMOVE;
    }
    validation_target = rt->tracker.profile.validation_target;
    validation_fallback = rt->tracker.profile.validation_fallback;
    rt->tracker.profile.validation_pending = false;
    if (!healthy) {
      rt->tracker.profile.switch_health_failed = true;
      rt->tracker.profile.switch_health_fail_reason = "postcheck_failed";
      rt->tracker.profile.switch_fail_count++;
      rt->tracker.profile.last_switch_tp = now;
      rt->tracker.profile.requested = validation_fallback;
      rt->tracker.profile.restart_requested.store(true);
    }
  }
  if (healthy) {
    common::Log(common::LogLevel::kInfo, "Faz5",
                "Restart sonrasi health-check basarili: profile=" + validation_target);
    return G_SOURCE_REMOVE;
  }
  common::Log(common::LogLevel::kError, "Faz5",
              "Restart sonrasi health-check FAIL: target=" + validation_target +
                  " fallback=" + validation_fallback + " -> controlled fallback restart");
  return G_SOURCE_REMOVE;
}
}  // namespace

// Faz-3/Faz-5 boru hattını kurup çalıştıran ana fonksiyon (yukarıdaki dosya
// başlığındaki adımları uygular).
bool RunPhase3(savasan::IngestConfig cfg, const std::string& pgi_config,
               const std::string& tracker_config, const std::string& ll_lib,
               savasan::deepstream::SinkType sink, int seconds,
               savasan::tracking::LockMode lock_mode, Phase5Runtime* phase5) {
  gst_init(nullptr, nullptr);
  if (!savasan::deepstream::ValidateTrackerForLockPhase()) {
    return false;
  }
  if (phase5 != nullptr) {
    phase5->health.degraded.pipeline_healthy.store(false);
    phase5->health.degraded.telemetry_valid_recent.store(false);
    phase5->tracker.lock_valid_seen.store(false);
  }

  savasan::tracking::TrackSelector selector(lock_mode);
  AlcSerialProbeCtx alc_ctx{};
  guint guidance_source_id = 0;
  guint auto_restart_source_id = 0;
  guint restart_healthcheck_source_id = 0;
  BusWatchContext bus_ctx{};
  bus_ctx.v4l2_device = cfg.v4l2_device;
  bus_ctx.usb_reconnect_enabled = UsbCameraReconnectEnabledByEnv();
  bus_ctx.usb_max_attempts = 5;
  bus_ctx.usb_interval_ms = 2000;
#if SAVASAN_DEBUG
  StreamProbeState osd_probe_state;
  StreamProbeState sink_probe_state;
#endif

  const std::string pipeline_str = NormalizePipelineForParse(savasan::deepstream::BuildPhase3PipelineString(
      cfg, pgi_config, tracker_config, ll_lib, sink));
#if SAVASAN_DEBUG
  {
    std::ostringstream d;
    const char* sink_env = std::getenv("SAVASAN_DISPLAY_SINK");
    d << "{\"pipeline\":\"" << JsonEscape(pipeline_str)
      << "\",\"sinkEnv\":\"" << JsonEscape(sink_env ? sink_env : "")
      << "\",\"hasQuotedNvmmCaps\":"
      << (pipeline_str.find("'video/x-raw(memory:NVMM)") != std::string::npos ? "true" : "false")
      << "}";
    DebugLog("baseline", "H1_H2", "phase3_runner.cpp:RunPhase3", "pipeline_before_parse", d.str());
  }
#endif
  std::cout << "Faz3 pipeline: " << pipeline_str << '\n';

  GError* err = nullptr;
  GstElement* pipeline = gst_parse_launch(pipeline_str.c_str(), &err);
#if SAVASAN_DEBUG
  {
    std::ostringstream d;
    d << "{\"pipelineCreated\":" << (pipeline ? "true" : "false") << ",\"parseError\":\""
      << JsonEscape(err ? err->message : "") << "\"}";
    DebugLog("baseline", "H2", "phase3_runner.cpp:RunPhase3", "parse_result", d.str());
  }
#endif
  if (!pipeline) {
    std::cerr << "gst_parse_launch basarisiz: " << (err ? err->message : "?") << '\n';
    if (err) g_error_free(err);
    return false;
  }

  savasan::deepstream::ApplyLowLatencyPipelineTuning(pipeline);
  savasan::deepstream::AttachComponentLatencyProfiler(pipeline);

  if (phase5 != nullptr) {
    GstElement* nvi = gst_bin_get_by_name(GST_BIN(pipeline), "nvinfer0");
    if (nvi != nullptr) {
      phase5->tracker.gst_nvinfer_element = nvi;
      savasan::runners::ApplyNvinferIntervalForMission(nvi);
      gst_object_unref(nvi);
    }
  }

  std::vector<AttachedProbe> probes;

  if (IsClockOverlayEnabled()) {
    GstElement* osd_el = gst_bin_get_by_name(GST_BIN(pipeline), "nvdsosd0");
    if (osd_el) {
      GstPad* osd_sink = gst_element_get_static_pad(osd_el, "sink");
      if (osd_sink) {
        gulong pid = gst_pad_add_probe(
            osd_sink, GST_PAD_PROBE_TYPE_BUFFER, OSDSinkTimestampProbe,
            reinterpret_cast<gpointer>(static_cast<intptr_t>(
                static_cast<int>(OsdSinkProbeMode::kPhase3ClockOnly))),
            nullptr);
        probes.push_back({osd_sink, pid});
      }
      gst_object_unref(osd_el);
    }
  }

  savasan::deepstream::AttachEndTelemetryProbe(pipeline);
  savasan::deepstream::AttachIngestLatencyReferenceProbe(pipeline);

  savasan::tracking::TrackSelectorProbeCtx ts_probe_ctx{};
  ts_probe_ctx.selector = &selector;
  ts_probe_ctx.phase5 = phase5;

  GstElement* tracker_el = gst_bin_get_by_name(GST_BIN(pipeline), "nvtracker0");
  GstPad* probe_target_pad = nullptr;

  // Tracker varsa tracker src pad'ini, yoksa nvinfer src pad'ini probe hedefi yap.
  if (tracker_el) {
    probe_target_pad = gst_element_get_static_pad(tracker_el, "src");
    gst_object_unref(tracker_el);
  } else {
    std::cerr << "Uyari: nvtracker0 elementi bulunamadi, nvinfer0 kullaniliyor (tracking kapali).\n";
    GstElement* nvinfer_el = gst_bin_get_by_name(GST_BIN(pipeline), "nvinfer0");
    if (nvinfer_el) {
      probe_target_pad = gst_element_get_static_pad(nvinfer_el, "src");
      gst_object_unref(nvinfer_el);
    }
  }

  // Pad probe LIFO: önce ALC/mission, sonra TrackSelector eklenir -> TrackSelector
  // önce çalışır, AlcLock aynı karede güncel bbox okur (1 kare gecikme önlenir).
  if (phase5 != nullptr && probe_target_pad != nullptr) {
    if (phase5->bridge) {
      alc_ctx.selector = &selector;
      alc_ctx.bridge = phase5->bridge.get();
      alc_ctx.phase5_runtime = phase5;
      alc_ctx.min_interval_ms = 100;
      const char* ims = std::getenv("SAVASAN_ALC_MIN_INTERVAL_MS");
      if (ims != nullptr && ims[0] != '\0') {
        const int v = std::atoi(ims);
        if (v >= 10 && v <= 500) {
          alc_ctx.min_interval_ms = v;
        }
      }
      alc_ctx.reconnect_interval_ms = 1000;
      const char* ric = std::getenv("SAVASAN_ALC_RECONNECT_INTERVAL_MS");
      if (ric != nullptr && ric[0] != '\0') {
        const int rv = std::atoi(ric);
        if (rv >= 200 && rv <= 10000) {
          alc_ctx.reconnect_interval_ms = rv;
        }
      }
      gst_object_ref(probe_target_pad);
      const gulong pid_alc =
          gst_pad_add_probe(probe_target_pad, GST_PAD_PROBE_TYPE_BUFFER, AlcLockSerialProbe, &alc_ctx, nullptr);
      probes.push_back({probe_target_pad, pid_alc});
    } else {
      alc_ctx.selector = &selector;
      alc_ctx.bridge = nullptr;
      alc_ctx.phase5_runtime = phase5;
      alc_ctx.min_interval_ms = 100;
      alc_ctx.reconnect_interval_ms = 1000;
      gst_object_ref(probe_target_pad);
      const gulong pid_ds =
          gst_pad_add_probe(probe_target_pad, GST_PAD_PROBE_TYPE_BUFFER, Phase5DeepstreamOnlyProbe, &alc_ctx,
                            nullptr);
      probes.push_back({probe_target_pad, pid_ds});
    }
  }

  if (tracker_el != nullptr && probe_target_pad != nullptr) {
    gst_object_ref(probe_target_pad);
    const gulong pid_ts =
        gst_pad_add_probe(probe_target_pad, GST_PAD_PROBE_TYPE_BUFFER,
                          savasan::tracking::TrackSelector::ProbeCallback, &ts_probe_ctx, nullptr);
    probes.push_back({probe_target_pad, pid_ts});
  }

  if (probe_target_pad) {
    gst_object_unref(probe_target_pad);
  }

#if SAVASAN_DEBUG
  {
    GstElement* osd_el = gst_bin_get_by_name(GST_BIN(pipeline), "nvdsosd0");
    if (osd_el) {
      GstPad* osd_src = gst_element_get_static_pad(osd_el, "src");
      if (osd_src) {
        gulong pid =
            gst_pad_add_probe(osd_src, GST_PAD_PROBE_TYPE_BUFFER, OSDSrcProbe, &osd_probe_state, nullptr);
        probes.push_back({osd_src, pid});
        DebugLog("baseline", "H6", "phase3_runner.cpp:RunPhase3", "osd_probe_attached",
                 "{\"element\":\"nvdsosd0\"}");
      } else {
        DebugLog("baseline", "H6", "phase3_runner.cpp:RunPhase3", "osd_probe_no_src_pad",
                 "{\"element\":\"nvdsosd0\"}");
      }
      gst_object_unref(osd_el);
    } else {
      DebugLog("baseline", "H6", "phase3_runner.cpp:RunPhase3", "osd_element_not_found",
               "{\"expected\":\"nvdsosd0\"}");
    }
  }
#endif

#if SAVASAN_DEBUG
  {
    bool sink_probe_ok = false;
    std::vector<std::string> sink_candidates_seen;
    GstIterator* it = gst_bin_iterate_recurse(GST_BIN(pipeline));
    GValue item = G_VALUE_INIT;
    while (!sink_probe_ok) {
      const GstIteratorResult r = gst_iterator_next(it, &item);
      if (r == GST_ITERATOR_DONE) break;
      if (r != GST_ITERATOR_OK) continue;
      GstElement* el = GST_ELEMENT(g_value_get_object(&item));
      if (!el) {
        g_value_reset(&item);
        continue;
      }
      const char* type_name = G_OBJECT_TYPE_NAME(el);
      const char* name = GST_OBJECT_NAME(el);
      if (IsTargetSinkType(type_name)) {
        std::ostringstream cand;
        cand << (name ? name : "?") << ":" << (type_name ? type_name : "?");
        sink_candidates_seen.push_back(cand.str());
        GstPad* sink_pad = gst_element_get_static_pad(el, "sink");
        if (sink_pad) {
          gulong pid =
              gst_pad_add_probe(sink_pad, GST_PAD_PROBE_TYPE_BUFFER, SinkSinkProbe, &sink_probe_state, nullptr);
          probes.push_back({sink_pad, pid});
          std::ostringstream d;
          d << "{\"element\":\"" << JsonEscape(name ? name : "?")
            << "\",\"type\":\"" << JsonEscape(type_name ? type_name : "?") << "\"}";
          DebugLog("baseline", "H7", "phase3_runner.cpp:RunPhase3", "sink_probe_attached", d.str());
          sink_probe_ok = true;
        }
      }
      g_value_reset(&item);
    }
    gst_iterator_free(it);
    if (!sink_probe_ok) {
      std::ostringstream d;
      d << "{\"seen\":\"";
      for (std::size_t i = 0; i < sink_candidates_seen.size(); ++i) {
        if (i != 0) d << ",";
        d << JsonEscape(sink_candidates_seen[i]);
      }
      d << "\"}";
      DebugLog("baseline", "H7", "phase3_runner.cpp:RunPhase3", "sink_element_not_found", d.str());
    }
  }
#endif

  GMainLoop* loop = g_main_loop_new(nullptr, FALSE);
  bus_ctx.loop = loop;
  bus_ctx.pipeline = pipeline;
  if (phase5 != nullptr) {
    bus_ctx.pipeline_healthy = &phase5->health.degraded.pipeline_healthy;
  }
  {
    const char* max_env = std::getenv("SAVASAN_USB_RECONNECT_MAX_ATTEMPTS");
    if (max_env != nullptr && max_env[0] != '\0') {
      const int v = std::atoi(max_env);
      if (v >= 1 && v <= 100) {
        bus_ctx.usb_max_attempts = v;
      }
    }
    const char* interval_env = std::getenv("SAVASAN_USB_RECONNECT_INTERVAL_MS");
    if (interval_env != nullptr && interval_env[0] != '\0') {
      const int v = std::atoi(interval_env);
      if (v >= 200 && v <= 60000) {
        bus_ctx.usb_interval_ms = v;
      }
    }
  }

  g_unix_signal_add(SIGTERM, OnSignalQuit, loop);
  g_unix_signal_add(SIGINT, OnSignalQuit, loop);
  g_timeout_add_seconds(10, WatchdogHeartbeat, nullptr);

  GstBus* bus = gst_element_get_bus(pipeline);
  gst_bus_add_watch(bus, BusWatch, &bus_ctx);
  gst_object_unref(bus);

  if (seconds > 0) {
    g_timeout_add_seconds(static_cast<guint>(seconds), StopMainLoop, loop);
  }

  if (phase5 != nullptr) {
    savasan::runners::StartMissionModeWorker(phase5);
  }

  // Faz-5 köprüsü varsa: seri portu aç, heartbeat/kaçış/güdüm/yeniden başlatma
  // izleyici ve yarışma HTTP periyodik timer'larını kur.
  if (phase5 != nullptr && phase5->bridge) {
    if (!phase5->bridge->Connect()) {
      std::cerr << "[Faz5] Seri port (alc_link) acilamadi: " << phase5->bridge->DescribeEndpoint()
                << "\n";
      CancelUsbCameraReconnect(&bus_ctx);
      CleanupProbes(probes);
      gst_object_unref(pipeline);
      g_main_loop_unref(loop);
      savasan::runners::StopMissionModeWorker(phase5);
      return false;
    }
    const char* hb = std::getenv("SAVASAN_ALC_HEARTBEAT");
    if (hb != nullptr && hb[0] == '1') {
      phase5->bridge->StartHeartbeat();
    }
    if (phase5->guidance.state_estimator != nullptr) {
      int control_hz = 20;
      const char* hz_env = std::getenv("SAVASAN_CONTROL_HZ");
      if (hz_env != nullptr && hz_env[0] != '\0') {
        const int hz = std::atoi(hz_env);
        if (hz >= 5 && hz <= 100) {
          control_hz = hz;
        }
      }
      guidance_source_id = g_timeout_add(static_cast<guint>(std::max(10, 1000 / control_hz)),
                                           savasan::runners::GuidanceControlTick, phase5);
    }
    if (phase5->tracker.profile.auto_restart) {
      auto* poll_ctx = new AutoRestartLoopCtx{loop, phase5};
      auto_restart_source_id = g_timeout_add_full(
          G_PRIORITY_DEFAULT, 250, AutoRestartPollTick, poll_ctx,
          [](gpointer data) { delete static_cast<AutoRestartLoopCtx*>(data); });
    }
    if (phase5->tracker.profile.validation_pending) {
      auto* hc_ctx = new RestartHealthCheckCtx{phase5};
      restart_healthcheck_source_id = g_timeout_add_full(
          G_PRIORITY_DEFAULT, 500, RestartHealthCheckTick, hc_ctx,
          [](gpointer data) { delete static_cast<RestartHealthCheckCtx*>(data); });
    }
  }

  if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
#if SAVASAN_DEBUG
    DebugLog("baseline", "H3", "phase3_runner.cpp:RunPhase3", "set_state_failed",
             "{\"targetState\":\"PLAYING\"}");
#endif
    std::cerr << "PLAYING durumuna gecemedi. USB: SAVASAN_V4L2_DEVICE gercek yakalama nodu olmali "
                 "(BRIO'da /dev/video1 cogu zaman metadata). nv3dsink: DISPLAY ve "
                 "XAUTHORITY (GDM: /run/user/1000/gdm/Xauthority). Ayrinti: GST_DEBUG=2\n";
    if (guidance_source_id != 0) {
      g_source_remove(guidance_source_id);
      guidance_source_id = 0;
    }
    if (phase5 != nullptr) {
      savasan::runners::StopMissionModeWorker(phase5);
    }
    if (auto_restart_source_id != 0) {
      g_source_remove(auto_restart_source_id);
      auto_restart_source_id = 0;
    }
    if (restart_healthcheck_source_id != 0) {
      g_source_remove(restart_healthcheck_source_id);
      restart_healthcheck_source_id = 0;
    }
    if (phase5 != nullptr && phase5->bridge) {
      phase5->bridge->StopHeartbeat();
      phase5->bridge->Disconnect();
    }
    CancelUsbCameraReconnect(&bus_ctx);
    CleanupProbes(probes);
    gst_object_unref(pipeline);
    g_main_loop_unref(loop);
    return false;
  }
#if SAVASAN_DEBUG
  DebugLog("baseline", "H3", "phase3_runner.cpp:RunPhase3", "set_state_ok", "{\"targetState\":\"PLAYING\"}");
#endif
  if (phase5 != nullptr) {
    phase5->health.degraded.pipeline_healthy.store(true);
  }

  SdNotify("READY=1");
  SdNotify("WATCHDOG=1");
  if (seconds > 0) {
    if (phase5 != nullptr && phase5->bridge) {
      std::cout << "Faz5 calisiyor... (" << seconds << " sn, Ctrl+C ile durdur)\n";
    } else {
      std::cout << "Faz3 calisiyor... (" << seconds << " sn, Ctrl+C ile durdur)\n";
    }
  } else {
    if (phase5 != nullptr && phase5->bridge) {
      std::cout << "Faz5 calisiyor... (suresiz, SIGTERM/Ctrl+C ile durdur)\n";
    } else {
      std::cout << "Faz3 calisiyor... (suresiz, SIGTERM/Ctrl+C ile durdur)\n";
    }
  }
  g_main_loop_run(loop);

  if (guidance_source_id != 0) {
    g_source_remove(guidance_source_id);
    guidance_source_id = 0;
  }
  if (phase5 != nullptr) {
    savasan::runners::StopMissionModeWorker(phase5);
  }
  if (auto_restart_source_id != 0) {
    g_source_remove(auto_restart_source_id);
    auto_restart_source_id = 0;
  }
  if (restart_healthcheck_source_id != 0) {
    g_source_remove(restart_healthcheck_source_id);
    restart_healthcheck_source_id = 0;
  }
  if (phase5 != nullptr && phase5->bridge) {
    phase5->bridge->StopHeartbeat();
    phase5->bridge->Disconnect();
  }

  const auto state = selector.GetState();
  if (state.IsLocked()) {
    std::cout << "[Faz3 Sonuc] Kilit AKTIF: track_id=" << state.target.track_id
              << " cx=" << state.target.cx << " cy=" << state.target.cy
              << " conf=" << state.target.confidence << '\n';
  } else {
    std::cout << "[Faz3 Sonuc] Kilit YOK.\n";
  }

  SdNotify("STOPPING=1");
  CancelUsbCameraReconnect(&bus_ctx);
  if (!GracefulPipelineShutdown(pipeline)) {
    std::cerr << "[GST] Pipeline kapatma basarisiz; video kaydi kullanilamaz olabilir.\n";
  }
  CleanupProbes(probes);
  gst_element_set_state(pipeline, GST_STATE_NULL);
  if (phase5 != nullptr) {
    phase5->health.degraded.pipeline_healthy.store(false);
  }
  gst_object_unref(pipeline);
  g_main_loop_unref(loop);
  if (bus_ctx.usb_fatal_exhausted) {
    std::cerr << "[Faz3] USB kamera yeniden baglanma basarisiz; gorev sonlandi.\n";
    return false;
  }
  std::cout << (phase5 != nullptr && phase5->bridge ? "Faz5 kosusu tamam.\n" : "Faz3 kosusu tamam.\n");
  return true;
}

}  // namespace savasan::runners
