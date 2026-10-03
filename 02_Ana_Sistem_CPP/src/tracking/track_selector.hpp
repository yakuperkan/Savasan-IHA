/**
 * @file track_selector.hpp
 * @brief DeepStream çıktısından hedef seçen ve kilit durumunu yöneten bileşen.
 */
#ifndef SAVASAN_TRACKING_TRACK_SELECTOR_HPP_
#define SAVASAN_TRACKING_TRACK_SELECTOR_HPP_

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <gst/gst.h>
#include "tracking/lock_mode.hpp"
#include "tracking/lock_state.hpp"

namespace savasan::runners {
struct Phase5Runtime;
}

namespace savasan::tracking {

class TrackSelector;

/// @brief Probe geri çağırımına taşınan bağlam.
/// @note GstPad probe LIFO sırasıyla çalışır: TrackSelector AlcLock'tan sonra
///       eklendiğinden aynı buffer'da ondan önce çalışır.
struct TrackSelectorProbeCtx {
  TrackSelector* selector = nullptr;                   ///< Zorunlu: seçici örneği.
  savasan::runners::Phase5Runtime* phase5 = nullptr;  ///< Opsiyonel: HUD/görev bilgisi.
};

/**
 * @brief GstPad probe ile nvinfer + nvtracker çıktısından kilit seçer.
 *
 * Her kare için metadata'yı gezer, en iyi hedefi seçip @ref LockState'i günceller
 * ve OSD (ekran üstü gösterim) öğelerini (kutu, AV karesi, hata vektörü, banner)
 * çizer.
 *
 * Kilit seçim politikası:
 *   1. Aktif kilit varsa: aynı track_id'yi takip et (istikrar).
 *   2. Kilit yoksa/kaybolursa: en yüksek güvenli adayı yeni hedef seç.
 *   3. kMaxMissFrames kare art arda tespit yoksa kilidi sıfırla.
 */
class TrackSelector {
 public:
  /// @brief Verilen kilit moduyla seçici oluşturur.
  explicit TrackSelector(LockMode mode = LockMode::kBaseline) : mode_(mode) {}

  /// @brief GstPad probe geri çağırımı; nvtracker src pad'ine bağlanır.
  /// @param userdata @ref TrackSelectorProbeCtx* (selector zorunlu; phase5 HUD için opsiyonel).
  static GstPadProbeReturn ProbeCallback(GstPad* pad, GstPadProbeInfo* info,
                                         gpointer userdata);

  /// @brief Thread-safe kilit durumu okuma (alc_link, evasion gibi dış modüller kullanır).
  LockState GetState() const;

  /// @brief Kilidi manuel sıfırlar (örn. yeni görev başlangıcı).
  void Reset();

 private:
  /// @brief Tek karedeki tespitlere göre durumu ve OSD'yi günceller.
  /// @note Probe içinden (GStreamer akış iş parçacığı) çağrılır.
  void UpdateFromBuffer(GstBuffer* buffer, savasan::runners::Phase5Runtime* phase5);

  mutable std::mutex mutex_;                ///< state_ ve last_logged_id_ koruması.
  LockState state_;                         ///< Güncel kilit durumu.
  LockMode mode_ = LockMode::kBaseline;     ///< Aktif kilit seçim modu.
  uint64_t last_logged_id_ = UINT64_MAX;    ///< Son loglanan track_id (tekrar logu önler).
  float last_frame_w_ = 640.0f;             ///< Son bilinen kare genişliği (metadata yokken).
  float last_frame_h_ = 480.0f;             ///< Son bilinen kare yüksekliği (metadata yokken).
  /// @brief track_id -> ardışık görünür kare sayısı (yalnızca probe iş parçacığı).
  std::unordered_map<uint64_t, int> track_consecutive_frames_;
};

}  // namespace savasan::tracking

#endif  // SAVASAN_TRACKING_TRACK_SELECTOR_HPP_
