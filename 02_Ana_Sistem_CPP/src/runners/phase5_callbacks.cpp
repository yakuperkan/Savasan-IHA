/**
 * @file phase5_callbacks.cpp
 * @brief Faz-5 probe geri çağırımları, periyodik tick'ler ve arka plan iş parçacıkları.
 *
 * Pad probe'ları GStreamer akış iş parçacığında yalnızca hafif anlık görüntü
 * yazar; ağır işler (seri gönderim, kestirim güncelleme, RF hub) main-loop
 * idle veya ayrı çalışan iş parçacıklarına devredilerek pipeline gecikmesi düşük
 * tutulur.
 */
#include "runners/phase5_callbacks.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <limits>
#include <condition_variable>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

#include "common/log.hpp"
#include "common/scope_profiler.hpp"
#include "autopilot/alc_link_bridge.hpp"
#include "runners/alc_hub_file.hpp"
#include "runners/lock_state_file.hpp"
#include "runners/mission_mode_utils.hpp"
#include "runners/phase5_mission_policy.hpp"
#include "runners/phase5_guidance.hpp"
#include "gstnvdsmeta.h"
#include "nvdsmeta.h"
#include <gst/gst.h>

namespace savasan::runners {

namespace {
/// @brief Kilit koordinatını blokesiz gönderir: gerçek köprüde TX kuyruğuna
/// koyar, mock/test'te senkron @c SendLockCoordinates çağırır. @c dynamic_cast yoktur;
/// karar @c IAlcLinkBridge::EnqueueLockNonBlocking sanal metoduyla derlenir.
inline bool TryEnqueueLockNonBlocking(savasan::autopilot::IAlcLinkBridge* bridge,
                                      const savasan::autopilot::LockCoordinates& c) {
  return bridge != nullptr && bridge->EnqueueLockNonBlocking(c);
}

/// @brief "Kilit yok" durumunu blokesiz gönderir (bkz. @ref TryEnqueueLockNonBlocking).
inline bool TryEnqueueNoLockNonBlocking(savasan::autopilot::IAlcLinkBridge* bridge) {
  return bridge != nullptr && bridge->EnqueueNoLockNonBlocking();
}
}  // namespace

// nvinfer çıkarım aralığını ortam değişkeninden okur; geçersizse varsayılan döner.
guint MissionNvinferIntervalFromEnv() {
  constexpr guint kDefaultInterval = 2u;  // Tracker odaklı: her 3. karede YOLO (0=her kare).
  const char* s = std::getenv("SAVASAN_AIRLOCK_NVINFER_INTERVAL");
  if (s == nullptr || s[0] == '\0') {
    return kDefaultInterval;
  }
  const int v = std::atoi(s);
  if (v < 0) {
    return kDefaultInterval;
  }
  return static_cast<guint>(v);
}

// nvinfer "interval" özelliğini göreve göre ayarlar ve değerin uygulanıp
// uygulanmadığını doğrular (PLAYING durumunda kilitli olabilir).
void ApplyNvinferIntervalForMission(GstElement* nvinfer) {
  if (nvinfer == nullptr || !GST_IS_ELEMENT(nvinfer)) {
    return;
  }
  const guint mission_interval = MissionNvinferIntervalFromEnv();
  g_object_set(nvinfer, "interval", mission_interval, nullptr);
  {
    guint verify = 0U;
    g_object_get(nvinfer, "interval", &verify, nullptr);
    if (verify != mission_interval &&
        common::ShouldLogEvery("nvinfer_airlock_mismatch", std::chrono::seconds(6))) {
      common::Log(common::LogLevel::kWarn, "Faz5",
                  "AIR_LOCK: nvinfer interval=" + std::to_string(mission_interval) +
                      " ayarlanamadi (okunan=" + std::to_string(verify) +
                      ") — PLAYING'de kilitli olabilir; gerekirse servisi yeniden baslatin.");
    }
  }
}

namespace {

/// @brief Görev modu dosyasını periyodik yoklayan worker durumu.
struct MissionModeWorkerState {
  Phase5Runtime* rt = nullptr;   ///< İlgili çalışma zamanı.
  std::mutex mu;                 ///< stop koruması.
  std::condition_variable cv;    ///< Durdurma/uyandırma koşulu.
  bool stop = false;             ///< Worker durdurma isteği.
  std::thread worker;            ///< Arka plan iş parçacığı.
};

std::mutex g_mission_mode_worker_mu;
std::unordered_map<Phase5Runtime*, std::unique_ptr<MissionModeWorkerState>> g_mission_mode_workers;

/// @brief 64-bit takip kimliğini protokol için 32-bit'e indirger
/// (UNTRACKED -> 0; taşmada uint32 üst sınırına doyurulur).
uint32_t ToProtocolTrackId(const uint64_t track_id) {
  if (track_id == std::numeric_limits<uint64_t>::max()) {
    return 0U;
  }
  if (track_id > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
    if (common::ShouldLogEvery("phase5_track_id_truncated", std::chrono::seconds(1))) {
      common::Log(common::LogLevel::kWarn, "Faz5",
                  "Track ID uint32 araligini asti, protokol icin saturate edildi");
    }
    return std::numeric_limits<uint32_t>::max();
  }
  return static_cast<uint32_t>(track_id);
}

/// @brief Görüntü hızını (norm/s) m/s'ye dönüştürme kazancını env'den okur (varsayılan 1.0).
float VisionNormRateToMpsGain() {
  static const float kGain = []() {
    const char* raw = std::getenv("SAVASAN_VISION_NORM_RATE_TO_MPS");
    // Varsayilan 1.0: goruntu hizi (norm/s) ile telemetri (m/s) ayni alanda
    // karsilastirilir; gercek m/s icin ortam veya sahada kalibre edin.
    if (raw == nullptr || raw[0] == '\0') return 1.0f;
    const float parsed = std::strtof(raw, nullptr);
    if (!std::isfinite(parsed) || parsed < 0.0f) return 1.0f;
    return parsed;
  }();
  return kGain;
}

// Görev modu dosyasını bir kez okur, geçerli belirteci ayrıştırır ve mod
// değiştiyse uygular (nvinfer aralığını günceller, değişimi loglar).
void PollMissionModeFromFileOnce(Phase5Runtime* rt) {
  if (rt == nullptr || !rt->guidance.mission_mode_dynamic_enabled.load()) return;
  if (rt->guidance.mission_mode_file.empty()) return;

  std::ifstream f(rt->guidance.mission_mode_file);
  if (!f.good()) {
    if (common::ShouldLogEvery("phase5_mission_file_open_fail", std::chrono::seconds(5))) {
      common::Log(common::LogLevel::kWarn, "Faz5",
                  "Mission dosyasi acilamiyor veya yok: " + rt->guidance.mission_mode_file +
                      " (izin/yol kontrol; savasan_mission_set.sh ile yazin)");
    }
    return;
  }
  std::string token;
  std::getline(f, token);
  token = NormalizeMissionModeFileLine(std::move(token));
  MissionMode parsed = MissionMode::kAirLock;
  if (!TryParseMissionModeToken(token, &parsed)) {
    if (common::ShouldLogEvery("phase5_mission_token_bad", std::chrono::seconds(3))) {
      common::Log(common::LogLevel::kWarn, "Faz5",
                  "Mission dosyasi gecersiz satir (air_lock|lock): [" + token + "]");
    }
    return;
  }

  const MissionMode prev = rt->guidance.mission_mode.exchange(parsed);
  if (prev != parsed) {
    if (rt->tracker.gst_nvinfer_element != nullptr) {
      ApplyNvinferIntervalForMission(static_cast<GstElement*>(rt->tracker.gst_nvinfer_element));
    }
    if (common::ShouldLogEvery("phase5_mission_mode_change", std::chrono::milliseconds(100))) {
      common::Log(common::LogLevel::kWarn, "Faz5",
                  std::string("Mission mode degisti: ") + MissionModeName(prev) + " -> " +
                      MissionModeName(parsed) + " (file=" + rt->guidance.mission_mode_file + ")");
    }
  }
}

// Probe içinden görev modunu, ayrı worker yoksa ve poll aralığı dolduysa yoklar.
void RefreshMissionModeFromFile(AlcSerialProbeCtx* ctx, const gint64 now_us) {
  if (ctx == nullptr || ctx->phase5_runtime == nullptr) return;
  auto* rt = ctx->phase5_runtime;
  {
    std::lock_guard<std::mutex> lk(g_mission_mode_worker_mu);
    if (g_mission_mode_workers.find(rt) != g_mission_mode_workers.end()) {
      return;
    }
  }
  if (!rt->guidance.mission_mode_dynamic_enabled.load()) return;
  if (rt->guidance.mission_mode_file.empty()) return;
  const gint64 min_interval_us =
      static_cast<gint64>(std::max(50, rt->guidance.mission_mode_poll_interval_ms)) * 1000;
  if (ctx->last_mode_poll_us > 0 && (now_us - ctx->last_mode_poll_us) < min_interval_us) return;
  ctx->last_mode_poll_us = now_us;
  PollMissionModeFromFileOnce(rt);
}

// Main-loop idle'da seri köprüyü yeniden bağlamayı dener (probe'u bloklamamak için).
gboolean AlcSerialReconnectIdle(gpointer user_data) {
  auto* ctx = static_cast<AlcSerialProbeCtx*>(user_data);
  if (ctx != nullptr && ctx->bridge != nullptr) {
    if (ctx->bridge->Connect()) {
      common::Log(common::LogLevel::kInfo, "Faz5", "Alc serial reconnect basarili.");
    }
  }
  return G_SOURCE_REMOVE;
}

// Yeniden bağlanma isteğini main-loop idle kuyruğuna ekler.
void RequestAlcSerialReconnect(AlcSerialProbeCtx* ctx) {
  if (ctx == nullptr) {
    return;
  }
  g_idle_add(AlcSerialReconnectIdle, ctx);
}

/// @brief Bekleyen kilit karesinin ağır işlemesi (main-loop idle'da çalışır).
///
/// Adımlar: hız sınırı kontrolü -> kilit metriklerini güncelle -> görev modu
/// izin vermiyorsa "kilit yok" gönder -> kaçış/tehdit durumu -> hedef dünya
/// kestirimi -> görüntü tabanlı hız füzyonu -> kilit/no-lock seri gönderimi.
/// Gönderim başarısız olursa degrade mod işaretlenir.
void UpdateLockMetricsFromFrame(Phase5Runtime* rt, const PendingLockFrame& frame, const float nx,
                                const float ny, const std::int64_t idle_delay_us) {
  if (rt == nullptr) {
    return;
  }
  rt->tracker.lock_metrics.Update(frame.valid_lock, nx, ny, frame.frames_without_detection,
                                frame.bbox_w_norm, frame.bbox_h_norm);
  rt->tracker.lock_metrics.SetIdleDelayUs(idle_delay_us);
  constexpr std::int64_t kIdleDelayDegradeUs = 50000;  // 50 ms
  if (idle_delay_us > kIdleDelayDegradeUs) {
    rt->health.degraded.active.store(true);
  }
}

void ProcessLockPipelineFrame(AlcSerialProbeCtx* ctx) {
  if (ctx == nullptr || ctx->bridge == nullptr) {
    return;
  }

  SAVASAN_PROFILE_SCOPE("LockPipelineProcess");

  PendingLockFrame frame{};
  {
    std::lock_guard<std::mutex> lk(ctx->pending_lock_mu);
    frame = ctx->pending_lock;
  }

  const gint64 process_start_us = g_get_monotonic_time();
  const std::int64_t idle_delay_us =
      (frame.capture_us > 0) ? (process_start_us - frame.capture_us) : 0;

  const gint64 now_us = frame.capture_us > 0 ? frame.capture_us : process_start_us;
  const gint64 last_send = ctx->last_send_us.load(std::memory_order_relaxed);
  if (last_send > 0 &&
      (now_us - last_send) < static_cast<gint64>(ctx->min_interval_ms) * 1000) {
    return;
  }

  const bool allow_air_lock = frame.allow_air_lock;
  const bool has_valid_lock = frame.valid_lock;
  const float nx = frame.nx;
  const float ny = frame.ny;
  Phase5Runtime* rt = ctx->phase5_runtime;

  if (!allow_air_lock) {
    const bool send_only_on_lock =
        rt != nullptr ? rt->guidance.send_only_on_lock.load() : false;
    bool no_lock_ok = true;
    if (!send_only_on_lock) {
      no_lock_ok = TryEnqueueNoLockNonBlocking(ctx->bridge);
    }
    if (rt != nullptr) {
      std::lock_guard<std::mutex> lk(rt->mutex);
      UpdateLockMetricsFromFrame(rt, frame, nx, ny, idle_delay_us);
      rt->tracker.lock_track_id_competition.store(0U);
      rt->guidance.latest_lock_valid = false;
      rt->guidance.latest_av_lock_valid = false;
      if (!send_only_on_lock && !no_lock_ok) {
        rt->health.degraded.active.store(true);
        rt->health.degraded.comm_fail_count.fetch_add(1);
      }
    }
    // AIR_LOCK kapalıyken RF köprüsü için kilit=0 yayınla.
    PublishLockStateToFile(false);
    ctx->last_send_us.store(now_us, std::memory_order_relaxed);
    return;
  }

  const bool apf_active = rt != nullptr && rt->guidance.apf_active.load();

  if (rt != nullptr && rt->guidance.state_estimator != nullptr) {
    const gint64 last_motion = ctx->last_motion_us.load(std::memory_order_relaxed);
    const gint64 dt_us = (last_motion > 0) ? (now_us - last_motion) : 0;
    if (dt_us > 0) {
      const float dt_s = static_cast<float>(dt_us) / 1000000.0f;
      if (dt_s > 1e-4f) {
        const float vision_gain = VisionNormRateToMpsGain();
        const float prev_nx = ctx->last_nx.load(std::memory_order_relaxed);
        const float prev_ny = ctx->last_ny.load(std::memory_order_relaxed);
        control::VehicleStateEstimator::VisionInput vis{};
        vis.valid = frame.state_is_locked && vision_gain > 0.0f;
        vis.vel_x_mps = 0.0f;
        vis.vel_y_mps = vis.valid ? (-(nx - prev_nx) / dt_s) * vision_gain : 0.0f;
        vis.vel_z_mps = vis.valid ? (-(ny - prev_ny) / dt_s) * vision_gain : 0.0f;
        rt->guidance.state_estimator->UpdateVision(vis);
      }
    }
    ctx->last_motion_us.store(now_us, std::memory_order_relaxed);
    ctx->last_nx.store(nx, std::memory_order_relaxed);
    ctx->last_ny.store(ny, std::memory_order_relaxed);
  }

  const bool send_only_on_lock =
      rt != nullptr ? rt->guidance.send_only_on_lock.load() : false;
  bool lock_ok = true;
  bool no_lock_ok = true;
  bool attempted_lock = false;
  bool attempted_no_lock = false;

  if (!apf_active) {
    if (frame.state_is_locked) {
      attempted_lock = true;
      savasan::autopilot::LockCoordinates c{};
      c.x = std::clamp(nx, 0.0f, 1.0f);
      c.y = std::clamp(ny, 0.0f, 1.0f);
      c.valid = true;
      c.track_id = ToProtocolTrackId(frame.track_id);
      lock_ok = TryEnqueueLockNonBlocking(ctx->bridge, c);
      if (!lock_ok &&
          common::ShouldLogEvery("phase5_lock_send_fail", std::chrono::seconds(1))) {
        SAVASAN_LOG_IF(common::LogLevel::kWarn, "Faz5",
                       "Kilit koordinati gonderimi basarisiz, degrade moda geciliyor");
      }
    } else if (!send_only_on_lock) {
      attempted_no_lock = true;
      no_lock_ok = TryEnqueueNoLockNonBlocking(ctx->bridge);
    }
  } else if (!send_only_on_lock) {
    // APF aktif: kilit paketi yok; otopilota en az no-lock bildir (sessiz bosluk onlenir).
    attempted_no_lock = true;
    no_lock_ok = TryEnqueueNoLockNonBlocking(ctx->bridge);
  }

  if (rt != nullptr) {
    std::lock_guard<std::mutex> lk(rt->mutex);
    UpdateLockMetricsFromFrame(rt, frame, nx, ny, idle_delay_us);
    if (has_valid_lock) {
      rt->tracker.lock_track_id_competition.store(ToProtocolTrackId(frame.track_id));
      rt->tracker.lock_valid_seen.store(true);
    } else {
      rt->tracker.lock_track_id_competition.store(0U);
    }
    rt->guidance.latest_lock_valid = frame.state_is_locked;
    rt->guidance.latest_av_lock_valid = has_valid_lock;
    if (attempted_lock) {
      if (lock_ok) {
        rt->health.degraded.active.store(false);
      } else {
        rt->health.degraded.active.store(true);
        rt->health.degraded.comm_fail_count.fetch_add(1);
      }
    }
    if (attempted_no_lock && !no_lock_ok) {
      rt->health.degraded.active.store(true);
      rt->health.degraded.comm_fail_count.fetch_add(1);
    }
  }

  // AlpaguLink RF downlink: mutex dışında; lock_metrics ile aynı kaynak.
  PublishLockStateToFile(has_valid_lock);

  ctx->last_send_us.store(now_us, std::memory_order_relaxed);
}

// Idle işleyici: bekleyen kareyi işler ve planlama bayrağını sıfırlar.
gboolean AlcLockPipelineProcessIdle(gpointer user_data) {
  auto* ctx = static_cast<AlcSerialProbeCtx*>(user_data);
  if (ctx != nullptr) {
    ProcessLockPipelineFrame(ctx);
    ctx->lock_process_idle_scheduled.store(false);
  }
  return G_SOURCE_REMOVE;
}

// Ağır işlemeyi yalnızca zaten planlanmamışsa main-loop idle'a ekler
// (atomik CAS ile çoklu planlamayı engeller).
void ScheduleLockPipelineProcess(AlcSerialProbeCtx* ctx) {
  if (ctx == nullptr) {
    return;
  }
  bool expected = false;
  if (!ctx->lock_process_idle_scheduled.compare_exchange_strong(expected, true)) {
    return;
  }
  g_idle_add(AlcLockPipelineProcessIdle, ctx);
}

}  // namespace

// Seri köprü yokken (yalnızca DeepStream) probe: yalnızca görev modu dosyasını yoklar.
GstPadProbeReturn Phase5DeepstreamOnlyProbe(GstPad* /*pad*/, GstPadProbeInfo* info,
                                            gpointer user_data) {
  if ((info->type & GST_PAD_PROBE_TYPE_BUFFER) == 0) {
    return GST_PAD_PROBE_OK;
  }
  auto* ctx = static_cast<AlcSerialProbeCtx*>(user_data);
  GstBuffer* buf = GST_PAD_PROBE_INFO_BUFFER(info);
  if (!buf || !ctx || !ctx->selector || ctx->phase5_runtime == nullptr) {
    return GST_PAD_PROBE_OK;
  }

  const gint64 now_us = g_get_monotonic_time();
  RefreshMissionModeFromFile(ctx, now_us);

  return GST_PAD_PROBE_OK;
}

// Kilit durumunu DeepStream meta'sından okur, normalize koordinatı hesaplar ve
// bekleyen kareye yazıp ağır işlemeyi idle'a planlar (probe iş parçacığını
// bloklamamak için). Bağlantı yoksa periyodik yeniden bağlanma dener.
GstPadProbeReturn AlcLockSerialProbe(GstPad* /*pad*/, GstPadProbeInfo* info,
                                     gpointer user_data) {
  if ((info->type & GST_PAD_PROBE_TYPE_BUFFER) == 0) {
    return GST_PAD_PROBE_OK;
  }
  auto* ctx = static_cast<AlcSerialProbeCtx*>(user_data);
  GstBuffer* buf = GST_PAD_PROBE_INFO_BUFFER(info);
  if (!buf || !ctx->bridge || !ctx->selector) {
    return GST_PAD_PROBE_OK;
  }

  const gint64 now_us = g_get_monotonic_time();

  const gint64 last_send = ctx->last_send_us.load(std::memory_order_relaxed);
  if (last_send > 0 &&
      (now_us - last_send) < static_cast<gint64>(ctx->min_interval_ms) * 1000) {
    return GST_PAD_PROBE_OK;
  }
  if (!ctx->bridge->IsConnected()) {
    if (ctx->last_reconnect_try_us <= 0 ||
        (now_us - ctx->last_reconnect_try_us) >=
            static_cast<gint64>(ctx->reconnect_interval_ms) * 1000) {
      ctx->last_reconnect_try_us = now_us;
      RequestAlcSerialReconnect(ctx);
    }
    return GST_PAD_PROBE_OK;
  }

  SAVASAN_PROFILE_SCOPE("AlcLockSerialProbe");

  NvDsBatchMeta* batch_meta = gst_buffer_get_nvds_batch_meta(buf);
  if (!batch_meta) {
    return GST_PAD_PROBE_OK;
  }

  unsigned int fw = 640;
  unsigned int fh = 480;
  for (NvDsFrameMetaList* l = batch_meta->frame_meta_list; l; l = l->next) {
    auto* frame = static_cast<NvDsFrameMeta*>(l->data);
    if (!frame) continue;
    if (frame->pipeline_width > 0) {
      fw = frame->pipeline_width;
    }
    if (frame->pipeline_height > 0) {
      fh = frame->pipeline_height;
    }
    break;
  }
  if (fw == 0) {
    fw = 1;
  }
  if (fh == 0) {
    fh = 1;
  }

  const auto state = ctx->selector->GetState();
  MissionMode mission_mode = MissionMode::kAirLock;
  if (ctx->phase5_runtime != nullptr) {
    mission_mode = ctx->phase5_runtime->guidance.mission_mode.load();
  }
  const bool allow_air_lock = MissionAllowsAirLockSerialTx(mission_mode);
  const bool has_valid_lock = allow_air_lock && state.valid_lock && state.IsLocked();
  float nx = state.target.cx / static_cast<float>(fw);
  float ny = state.target.cy / static_cast<float>(fh);
  const float bbox_w_norm = state.target.w / static_cast<float>(fw);
  const float bbox_h_norm = state.target.h / static_cast<float>(fh);
  const bool coords_finite =
      std::isfinite(nx) && std::isfinite(ny) && std::isfinite(bbox_w_norm) &&
      std::isfinite(bbox_h_norm);
  if (!coords_finite) {
    if (common::ShouldLogEvery("phase5_invalid_lock_norm", std::chrono::seconds(1))) {
      common::Log(common::LogLevel::kCritical, "Faz5",
                  "Invalid lock coordinates (NaN/Inf), no-lock olarak isleniyor");
    }
    nx = 0.5f;
    ny = 0.5f;
  }

  if (ctx->phase5_runtime != nullptr) {
    ctx->phase5_runtime->tracker.pipeline_width.store(fw);
    ctx->phase5_runtime->tracker.pipeline_height.store(fh);
  }

  {
    PendingLockFrame frame{};
    frame.nx = nx;
    frame.ny = ny;
    frame.bbox_w_norm = coords_finite ? bbox_w_norm : 0.0f;
    frame.bbox_h_norm = coords_finite ? bbox_h_norm : 0.0f;
    frame.valid_lock = coords_finite && has_valid_lock;
    frame.state_is_locked = coords_finite && state.valid_lock && state.IsLocked();
    frame.allow_air_lock = allow_air_lock;
    frame.track_id = coords_finite ? state.target.track_id : 0U;
    frame.frames_without_detection = state.frames_without_detection;
    frame.mission_mode = mission_mode;
    frame.capture_us = now_us;
    frame.pipeline_w = fw;
    frame.pipeline_h = fh;
    std::lock_guard<std::mutex> lk(ctx->pending_lock_mu);
    ctx->pending_lock = frame;
  }
  ScheduleLockPipelineProcess(ctx);
  return GST_PAD_PROBE_OK;
}

// Periyodik güdüm kontrol adımını tetikler (timer geri çağırımı).
gboolean GuidanceControlTick(gpointer user_data) {
  auto* rt = static_cast<Phase5Runtime*>(user_data);
  RunGuidanceControlStep(rt, std::chrono::steady_clock::now());
  if (rt != nullptr && rt->bridge) {
    bool lock_valid = false;
    float hx = 0.0f;
    float hy = 0.0f;
    GotoFeedback goto_fb{};
    {
      std::lock_guard<std::mutex> lk(rt->mutex);
      const auto snap = rt->tracker.lock_metrics.GetSnapshot();
      lock_valid = snap.lock_valid;
      hx = snap.hedef_norm_x;
      hy = snap.hedef_norm_y;
      goto_fb.state = static_cast<int>(rt->guidance.goto_state);
      goto_fb.seq = rt->guidance.goto_seq;
      goto_fb.reason = static_cast<int>(rt->guidance.goto_reason);
    }
    const auto telem = rt->bridge->GetTelemetrySnapshot();
    PublishAlcHubToFile(telem, lock_valid, hx, hy, goto_fb);
    (void)TryForwardAlcUplinkFile(rt->bridge.get());
  }
  return G_SOURCE_CONTINUE;
}

// Görev modu dosyasını periyodik yoklayan worker'ı başlatır (dinamik mod açıksa).
void StartMissionModeWorker(Phase5Runtime* rt) {
  if (rt == nullptr || !rt->guidance.mission_mode_dynamic_enabled.load() || rt->guidance.mission_mode_file.empty()) {
    return;
  }
  std::lock_guard<std::mutex> lk(g_mission_mode_worker_mu);
  if (g_mission_mode_workers.find(rt) != g_mission_mode_workers.end()) {
    return;
  }
  auto st = std::make_unique<MissionModeWorkerState>();
  st->rt = rt;
  MissionModeWorkerState* raw = st.get();
  st->worker = std::thread([raw]() {
    while (true) {
      const int poll_ms = std::max(50, raw->rt->guidance.mission_mode_poll_interval_ms);
      {
        std::unique_lock<std::mutex> lk(raw->mu);
        if (raw->cv.wait_for(lk, std::chrono::milliseconds(poll_ms), [raw]() { return raw->stop; })) {
          break;
        }
      }
      PollMissionModeFromFileOnce(raw->rt);
    }
  });
  g_mission_mode_workers.emplace(rt, std::move(st));
}

// Görev modu yoklama worker'ını durdurur ve iş parçacığını join eder.
void StopMissionModeWorker(Phase5Runtime* rt) {
  std::unique_ptr<MissionModeWorkerState> st;
  {
    std::lock_guard<std::mutex> lk(g_mission_mode_worker_mu);
    auto it = g_mission_mode_workers.find(rt);
    if (it == g_mission_mode_workers.end()) {
      return;
    }
    st = std::move(it->second);
    g_mission_mode_workers.erase(it);
  }
  {
    std::lock_guard<std::mutex> lk(st->mu);
    st->stop = true;
  }
  st->cv.notify_all();
  if (st->worker.joinable()) {
    st->worker.join();
  }
}

}  // namespace savasan::runners
