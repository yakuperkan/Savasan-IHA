/**
 * @file phase2_runner.cpp
 * @brief Faz-2 çalıştırıcısı: nvinfer tespit boru hattı (takipçisiz).
 */
#include "runners/phase2_runner.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include <signal.h>

#include <glib-unix.h>
#include <gst/gst.h>

#include "deepstream/ds_app.hpp"
#include "runners/gst_app_helpers.hpp"

namespace savasan::runners {

// Tespit boru hattını kurar; OSD/telemetri/pre-mux probe'larını ekler, sinyal
// ve bus izleyicilerini bağlar, main-loop'u çalıştırır ve düzgünce kapatır.
bool RunPhase2(savasan::IngestConfig cfg, const std::string& pgi_config,
               savasan::deepstream::SinkType sink, int seconds) {
  gst_init(nullptr, nullptr);
  const std::string pipeline_str = NormalizePipelineForParse(
      savasan::deepstream::BuildPhase2PipelineString(cfg, pgi_config, sink));
  std::cout << "Faz2 pipeline: " << pipeline_str << '\n';

  GError* err = nullptr;
  GstElement* pipeline = gst_parse_launch(pipeline_str.c_str(), &err);
  if (!pipeline) {
    std::cerr << "gst_parse_launch basarisiz: " << (err ? err->message : "?") << '\n';
    if (err) g_error_free(err);
    return false;
  }

  savasan::deepstream::ApplyLowLatencyPipelineTuning(pipeline);
  savasan::deepstream::AttachComponentLatencyProfiler(pipeline);

  std::vector<AttachedProbe> probes;

  {
    GstElement* osd_el = gst_bin_get_by_name(GST_BIN(pipeline), "nvdsosd0");
    if (osd_el) {
      GstPad* osd_sink = gst_element_get_static_pad(osd_el, "sink");
      if (osd_sink) {
        gulong pid = gst_pad_add_probe(
            osd_sink, GST_PAD_PROBE_TYPE_BUFFER, OSDSinkTimestampProbe,
            reinterpret_cast<gpointer>(static_cast<intptr_t>(
                static_cast<int>(OsdSinkProbeMode::kPhase2StyleAndMaybeClock))),
            nullptr);
        probes.push_back({osd_sink, pid});
      }
      gst_object_unref(osd_el);
    }
  }

  savasan::deepstream::AttachEndTelemetryProbe(pipeline);
  savasan::deepstream::AttachIngestLatencyReferenceProbe(pipeline);

  GMainLoop* loop = g_main_loop_new(nullptr, FALSE);

  g_unix_signal_add(SIGTERM, OnSignalQuit, loop);
  g_unix_signal_add(SIGINT, OnSignalQuit, loop);
  g_timeout_add_seconds(10, WatchdogHeartbeat, nullptr);

  GstBus* bus = gst_element_get_bus(pipeline);
  BusWatchContext bus_ctx{};
  bus_ctx.loop = loop;
  bus_ctx.pipeline = pipeline;
  gst_bus_add_watch(bus, BusWatch, &bus_ctx);
  gst_object_unref(bus);

  if (seconds > 0) {
    g_timeout_add_seconds(static_cast<guint>(seconds), StopMainLoop, loop);
  }

  if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    std::cerr << "PLAYING durumuna gecemedi (engine/onnx yolu veya DISPLAY=:0 eksik olabilir).\n";
    CleanupProbes(probes);
    gst_object_unref(pipeline);
    g_main_loop_unref(loop);
    return false;
  }

  SdNotify("READY=1");
  SdNotify("WATCHDOG=1");
  if (seconds > 0) {
    std::cout << "Faz2 calisiyor... (" << seconds << " sn, Ctrl+C ile durdur)\n";
  } else {
    std::cout << "Faz2 calisiyor... (suresiz, SIGTERM/Ctrl+C ile durdur)\n";
  }
  g_main_loop_run(loop);

  SdNotify("STOPPING=1");
  CleanupProbes(probes);
  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(pipeline);
  g_main_loop_unref(loop);
  std::cout << "Faz2 kosusu tamam.\n";
  return true;
}

}  // namespace savasan::runners
