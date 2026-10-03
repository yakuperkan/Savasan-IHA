/**
 * @file phase5_callbacks.hpp
 * @brief Faz-5 GStreamer probe'ları, periyodik tick'ler ve arka plan iş parçacıkları.
 */
#ifndef SAVASAN_RUNNERS_PHASE5_CALLBACKS_HPP_
#define SAVASAN_RUNNERS_PHASE5_CALLBACKS_HPP_

#include <chrono>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include <glib.h>
#include <gst/gst.h>

#include "autopilot/ialc_link_bridge.hpp"
#include "runners/phase5_runtime.hpp"
#include "tracking/track_selector.hpp"

namespace savasan::runners {

/// @brief Probe'un yazıp idle işleyicinin tükettiği bekleyen kilit kare verisi.
struct PendingLockFrame {
  float nx = 0.5f;            ///< Hedef merkezi normalize X.
  float ny = 0.5f;            ///< Hedef merkezi normalize Y.
  float bbox_w_norm = 0.0f;   ///< Kutu genişliği (normalize).
  float bbox_h_norm = 0.0f;   ///< Kutu yüksekliği (normalize).
  bool valid_lock = false;    ///< Geçerli (4 sn AV) kilit mi.
  bool state_is_locked = false; ///< Durumda kilit var mı.
  bool allow_air_lock = false;  ///< Görev modu seri TX'e izin veriyor mu.
  uint64_t track_id = 0;      ///< Takip kimliği.
  int frames_without_detection = 0; ///< Tespitsiz kare sayısı.
  MissionMode mission_mode = MissionMode::kAirLock; ///< O kareye ait görev modu.
  gint64 capture_us = 0;      ///< Yakalama zamanı (us).
  unsigned int pipeline_w = 0; ///< Pipeline genişliği (piksel).
  unsigned int pipeline_h = 0; ///< Pipeline yüksekliği (piksel).
};

/// @brief AlcLockSerialProbe geri çağırımının paylaşılan bağlamı.
struct AlcSerialProbeCtx {
  tracking::TrackSelector* selector = nullptr;          ///< Kilit durumu kaynağı.
  savasan::autopilot::IAlcLinkBridge* bridge = nullptr; ///< Otopilot köprüsü.
  Phase5Runtime* phase5_runtime = nullptr;              ///< Faz-5 çalışma zamanı.
  std::atomic<gint64> last_send_us{0};   ///< Son gönderim anı (us); probe + idle paylaşır.
  gint64 last_reconnect_try_us = 0;      ///< Son yeniden bağlanma denemesi anı (us); yalnızca probe.
  std::atomic<gint64> last_motion_us{0}; ///< Son hareket tespiti anı (us); probe + idle paylaşır.
  gint64 last_mode_poll_us = 0;          ///< Son görev modu yoklama anı (us); yalnızca probe.
  MissionMode last_observed_mode = MissionMode::kAirLock; ///< Son gözlenen görev modu.
  std::atomic<float> last_nx{0.5f}; ///< Önceki normalize X (ilk kare merkezde varsayılır).
  std::atomic<float> last_ny{0.5f}; ///< Önceki normalize Y.
  int min_interval_ms = 100;        ///< Asgari gönderim aralığı (ms).
  int reconnect_interval_ms = 1000; ///< Yeniden bağlanma deneme aralığı (ms).
  // Probe inceltme: GStreamer iş parçacığı yalnızca anlık görüntü yazar; ağır iş main-loop idle'da yapılır.
  std::mutex pending_lock_mu;       ///< pending_lock koruması.
  PendingLockFrame pending_lock{};  ///< Bekleyen kilit kare verisi.
  std::atomic<bool> lock_process_idle_scheduled{false}; ///< Idle işleyici zaten planlandı mı.
};

/// @brief Kilit durumunu okuyup seri üzerinden otopilota ileten pad probe'u.
GstPadProbeReturn AlcLockSerialProbe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);

/// @brief Seri (alc_link) yokken çalışan probe: yalnızca görev modu dosyasını yoklar (ctx->bridge == nullptr olmalı).
GstPadProbeReturn Phase5DeepstreamOnlyProbe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);
/// @brief Periyodik güdüm kontrol tick'i.
gboolean GuidanceControlTick(gpointer user_data);

/// @brief Görev modu dosyası yoklama iş parçacığını başlatır.
void StartMissionModeWorker(Phase5Runtime* rt);
/// @brief Görev modu dosyası yoklama iş parçacığını durdurur.
void StopMissionModeWorker(Phase5Runtime* rt);

/// @brief nvinfer0 çıkarım aralığını ortam değişkeninden okur (AIR_LOCK; varsayılan config ile aynı).
/// Override: SAVASAN_AIRLOCK_NVINFER_INTERVAL.
guint MissionNvinferIntervalFromEnv();
/// @brief Göreve uygun nvinfer çıkarım aralığını verilen öğeye uygular.
void ApplyNvinferIntervalForMission(GstElement* nvinfer);

}  // namespace savasan::runners

#endif  // SAVASAN_RUNNERS_PHASE5_CALLBACKS_HPP_
