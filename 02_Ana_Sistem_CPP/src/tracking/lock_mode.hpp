/**
 * @file lock_mode.hpp
 * @brief Kilit seçim politikasının çalışma modunu tanımlar.
 */
#ifndef SAVASAN_TRACKING_LOCK_MODE_HPP_
#define SAVASAN_TRACKING_LOCK_MODE_HPP_

namespace savasan::tracking {

/// @brief Hedef kilit seçim politikası modu.
enum class LockMode {
  kBaseline, ///< Temel mod: en yüksek güvenli adayı seçer, kilitli iken aynı track_id'yi takip eder.
  kHybrid,   ///< Hibrit mod: kilidi koruma/yeni kilit için ayrı güven eşikleri uygular.
};

}  // namespace savasan::tracking

#endif  // SAVASAN_TRACKING_LOCK_MODE_HPP_
