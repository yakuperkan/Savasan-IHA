/**
 * @file phase5_mission_policy.hpp
 * @brief Görev moduna bağlı politika kararları (seri TX izinleri vb.).
 */
#ifndef SAVASAN_RUNNERS_PHASE5_MISSION_POLICY_HPP_
#define SAVASAN_RUNNERS_PHASE5_MISSION_POLICY_HPP_

#include "runners/phase5_runtime.hpp"

namespace savasan::runners {

/// @brief Verilen görev modunun seri hat üzerinden kilit koordinatı gönderimine
/// izin verip vermediğini döndürür. Bu iş akışında tek mod (AIR_LOCK) izinlidir.
inline constexpr bool MissionAllowsAirLockSerialTx(MissionMode m) noexcept {
  return m == MissionMode::kAirLock;
}

}  // namespace savasan::runners

#endif  // SAVASAN_RUNNERS_PHASE5_MISSION_POLICY_HPP_
