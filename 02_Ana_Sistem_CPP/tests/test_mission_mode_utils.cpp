#include <cassert>

#include "runners/mission_mode_utils.hpp"

int main() {
  using savasan::runners::MissionMode;
  using savasan::runners::NormalizeMissionModeFileLine;
  using savasan::runners::ParseMissionModeEnv;
  using savasan::runners::TryParseMissionModeToken;

  MissionMode m = MissionMode::kAirLock;
  assert(!TryParseMissionModeToken("nosuch_mode", &m));
  assert(!TryParseMissionModeToken("qr", &m));
  assert(TryParseMissionModeToken("AIR_LOCK", &m) && m == MissionMode::kAirLock);
  assert(!TryParseMissionModeToken("garbage", &m));

  {
    std::string bom;
    bom.push_back(static_cast<char>(0xEF));
    bom.push_back(static_cast<char>(0xBB));
    bom.push_back(static_cast<char>(0xBF));
    bom += "air_lock";
    bom = NormalizeMissionModeFileLine(std::move(bom));
    assert(TryParseMissionModeToken(bom, &m) && m == MissionMode::kAirLock);
  }

  assert(ParseMissionModeEnv(nullptr, MissionMode::kAirLock) == MissionMode::kAirLock);
  assert(ParseMissionModeEnv("", MissionMode::kAirLock) == MissionMode::kAirLock);
  assert(ParseMissionModeEnv("lock", MissionMode::kAirLock) == MissionMode::kAirLock);
  assert(ParseMissionModeEnv("invalid", MissionMode::kAirLock) == MissionMode::kAirLock);

  return 0;
}
