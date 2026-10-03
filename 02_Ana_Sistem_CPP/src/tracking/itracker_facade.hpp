/**
 * @file itracker_facade.hpp
 * @brief Takip (tracker) cephe (facade) arayüzü.
 *
 * Takip bileşeninin üst düzey soyutlaması; testlerde sahte (mock) implementasyon
 * kullanılabilmesi için tanımlanmıştır.
 */
#ifndef SAVASAN_TRACKING_ITRACKER_FACADE_HPP_
#define SAVASAN_TRACKING_ITRACKER_FACADE_HPP_

#include "tracking/lock_state.hpp"

namespace savasan::tracking {

/// @brief Takip cephesi arayüzü: durum sıfırlama, güncelleme ve okuma.
class ITrackerFacade {
 public:
  virtual ~ITrackerFacade() = default;
  /// @brief Takip durumunu sıfırlar.
  virtual void Reset() = 0;
  /// @brief Bir takip adımı çalıştırır.
  virtual void Update() = 0;
  /// @brief Güncel kilit durumunu döndürür.
  virtual LockState GetState() const = 0;
};

}  // namespace savasan::tracking

#endif  // SAVASAN_TRACKING_ITRACKER_FACADE_HPP_
