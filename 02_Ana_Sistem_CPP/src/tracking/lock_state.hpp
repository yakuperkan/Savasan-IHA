/**
 * @file lock_state.hpp
 * @brief Hedef kilit (lock) durumu ve ilgili tipler.
 *
 * Takip seçici (TrackSelector) tarafından her karede güncellenen kilit durumunu,
 * kilit kaybı nedenlerini ve şartname kaynaklı zamanlama sabitlerini içerir.
 */
#ifndef SAVASAN_TRACKING_LOCK_STATE_HPP_
#define SAVASAN_TRACKING_LOCK_STATE_HPP_

#include <cstdint>
#include <chrono>

namespace savasan::tracking {

/// @brief Kilidin neden kaybedildiğini/durumunu açıklayan kod.
enum class LockReason : uint8_t {
  kNone = 0,     ///< Neden yok / normal.
  kRoiOut = 1,   ///< Hedef ilgi bölgesi (ROI) dışında.
  kLowConf = 2,  ///< Güven (confidence) eşiğin altında.
  kIdSwitch = 3, ///< Takip kimliği (track_id) değişti.
  kMissed = 4,   ///< Hedef art arda kareler boyunca tespit edilemedi.
  kGraceHold = 5,///< Tolerans (grace) süresi içinde kilit korunuyor.
  kAvOut = 6,    ///< Hedef merkezi Hedef Vuruş Alanı (AV) dışında.
};

/// @brief @ref LockReason değerini okunabilir kısa metne çevirir.
inline const char* ToString(const LockReason reason) {
  switch (reason) {
    case LockReason::kAvOut:
      return "av_out";
    case LockReason::kRoiOut:
      return "roi_out";
    case LockReason::kLowConf:
      return "low_conf";
    case LockReason::kIdSwitch:
      return "id_switch";
    case LockReason::kMissed:
      return "missed";
    case LockReason::kGraceHold:
      return "grace_hold";
    case LockReason::kNone:
    default:
      return "none";
  }
}

/// @brief Kilitlenen hedefin bilgisi (NvTracker'dan seçilen hedef).
///
/// TrackSelector her karede bu yapıyı günceller; dış modüller (alc_link, evasion)
/// bunu okur.
struct LockedTarget {
  uint64_t track_id = UINT64_MAX;   ///< Takip kimliği (NVDS_UNTRACKED_OBJECT_ID = 0xFFFFFFFFFFFFFFFF).
  float    cx = 0.0f;               ///< Kutu merkezi X (piksel).
  float    cy = 0.0f;               ///< Kutu merkezi Y (piksel).
  float    w  = 0.0f;               ///< Kutu genişliği (piksel).
  float    h  = 0.0f;               ///< Kutu yüksekliği (piksel).
  float    confidence = 0.0f;       ///< nvinfer tespit güveni.
  int      class_id  = -1;          ///< Sınıf kimliği.
  bool     locked    = false;       ///< Geçerli bir kilit var mı?
};

/// @brief Kilit durumu ve kayıp sayaçları (probe'da her kare güncellenir).
struct LockState {
  LockedTarget target;                 ///< Kilitli hedef bilgisi.
  int  frames_without_detection = 0;   ///< Art arda tespit edilemeyen kare sayısı.
  bool valid_lock = false;             ///< Şartname şartlarını sağlayan geçerli kilit mi.
  bool roi_in = false;                 ///< Hedef ilgi bölgesi (ROI) içinde mi.
  bool av_in = false;                  ///< Hedef merkezi Hedef Vuruş Alanı (AV) içinde mi.
  float lock_health = 0.0f;            ///< Kilit sağlığı [0..1], operatör görünürlük metriği.
  LockReason reason = LockReason::kNone;///< Güncel kilit durumu/kayıp nedeni.
  int grace_ms_remaining = 0;          ///< Tolerans (grace) süresinde kalan süre (ms, HUD).
  std::chrono::steady_clock::time_point lock_start_time{};       ///< Kilidin başladığı an.
  std::chrono::steady_clock::time_point last_policy_update_tp{}; ///< Son politika güncelleme anı.
  int lock_interior_bad_ms = 0;        ///< 4 sn'lik seri içinde toleranslı toplam ara kayıp (ms).
  // --- Şartname kaynaklı zamanlama sabitleri ---
  static constexpr int kMaxMissFrames = 45;  ///< Kilit sürdürülebilecek azami kayıp kare (0,75 sn @ 60 fps).
  static constexpr std::chrono::seconds kRequiredLockDuration{4}; ///< Geçerli kilit için gereken süre.
  static constexpr int kInteriorBadToleranceMs = 200;  ///< 4 sn için %5 toleranslı ara kayıp bütçesi (ms).

  /// @brief Tüm kilit durumunu başlangıç hâline sıfırlar.
  void Reset() {
    target = LockedTarget{};
    frames_without_detection = 0;
    valid_lock = false;
    roi_in = false;
    av_in = false;
    lock_health = 0.0f;
    reason = LockReason::kNone;
    grace_ms_remaining = 0;
    lock_start_time = std::chrono::steady_clock::time_point{};
    last_policy_update_tp = std::chrono::steady_clock::time_point{};
    lock_interior_bad_ms = 0;
  }

  /// @brief Geçerli bir kilit olup olmadığını döndürür (target.locked).
  bool IsLocked() const { return target.locked; }
};

}  // namespace savasan::tracking

#endif  // SAVASAN_TRACKING_LOCK_STATE_HPP_
