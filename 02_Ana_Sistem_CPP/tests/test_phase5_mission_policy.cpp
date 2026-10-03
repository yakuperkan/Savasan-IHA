#include <cassert>

#include "runners/phase5_mission_policy.hpp"

int main() {
  using savasan::runners::MissionAllowsAirLockSerialTx;
  using savasan::runners::MissionMode;

  assert(MissionAllowsAirLockSerialTx(MissionMode::kAirLock));
  return 0;
}
