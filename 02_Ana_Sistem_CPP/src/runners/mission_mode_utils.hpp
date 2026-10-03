/**
 * @file mission_mode_utils.hpp
 * @brief Görev modu (MissionMode) ayrıştırma/biçimlendirme yardımcıları.
 */
#ifndef SAVASAN_RUNNERS_MISSION_MODE_UTILS_HPP_
#define SAVASAN_RUNNERS_MISSION_MODE_UTILS_HPP_

#include <string>

#include "runners/phase5_runtime.hpp"

namespace savasan::runners {

/// @brief Görev modunun okunabilir adını döndürür.
const char* MissionModeName(MissionMode mode);

/// @brief Bir metin belirtecini (token) görev moduna çözmeye çalışır.
/// @return Tanındıysa true ve @p out yazılır.
bool TryParseMissionModeToken(const std::string& raw, MissionMode* out);

/// @brief Görev modu dosya satırını normalize eder (BOM ve baş/son boşlukları temizler).
std::string NormalizeMissionModeFileLine(std::string line);

/// @brief Ortam değişkeni değerini görev moduna çözer; çözülemezse @p fallback döner.
MissionMode ParseMissionModeEnv(const char* raw, MissionMode fallback);

}  // namespace savasan::runners

#endif  // SAVASAN_RUNNERS_MISSION_MODE_UTILS_HPP_
