/**
 * @file mission_mode_utils.cpp
 * @brief @ref mission_mode_utils.hpp uygulaması.
 *
 * Mevcut iş akışında tek görev modu (AIR_LOCK) desteklenir; yardımcılar ileride
 * mod eklenebilmesi için ayrıştırma/normalize altyapısını sağlar.
 */
#include "runners/mission_mode_utils.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

namespace savasan::runners {

// Tek desteklenen mod; her durumda "AIR_LOCK" döner.
const char* MissionModeName(const MissionMode mode) {
  (void)mode;
  return "AIR_LOCK";
}

// UTF-8 BOM'unu ve baştaki/sondaki boşlukları temizler.
std::string NormalizeMissionModeFileLine(std::string s) {
  if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF &&
      static_cast<unsigned char>(s[1]) == 0xBB && static_cast<unsigned char>(s[2]) == 0xBF) {
    s.erase(0, 3);
  }
  auto not_space = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}

// Belirteci küçük harfe çevirip bilinen takma adlarla karşılaştırır.
bool TryParseMissionModeToken(const std::string& raw, MissionMode* out) {
  if (out == nullptr) return false;
  std::string v(raw);
  std::transform(v.begin(), v.end(), v.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (v == "air_lock" || v == "airlock" || v == "lock") {
    *out = MissionMode::kAirLock;
    return true;
  }
  return false;
}

MissionMode ParseMissionModeEnv(const char* raw, const MissionMode fallback) {
  if (raw == nullptr || raw[0] == '\0') return fallback;
  MissionMode parsed = fallback;
  if (TryParseMissionModeToken(std::string(raw), &parsed)) return parsed;
  return fallback;
}

}  // namespace savasan::runners
