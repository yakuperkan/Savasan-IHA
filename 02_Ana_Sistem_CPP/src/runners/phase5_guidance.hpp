/**
 * @file phase5_guidance.hpp
 * @brief Faz-5 güdüm (guidance) kontrol adımı arayüzü.
 */
#ifndef SAVASAN_RUNNERS_PHASE5_GUIDANCE_HPP_
#define SAVASAN_RUNNERS_PHASE5_GUIDANCE_HPP_

#include <chrono>

#include "runners/phase5_runtime.hpp"

namespace savasan::runners {

/// @brief Tek bir güdüm kontrol adımı çalıştırır (durumsuz yardımcı).
///
/// Hedef kestirimi/oklüzyon tahmininden hata üretir, PID ile hız komutu hesaplar
/// ve gerekiyorsa otopilota setpoint gönderir.
/// @param rt Faz-5 çalışma zamanı durumu.
/// @param now Adımın zaman damgası.
/// @return Güdümün bu adımda aktif (komut üretmiş) olup olmadığı.
bool RunGuidanceControlStep(Phase5Runtime* rt, std::chrono::steady_clock::time_point now);

}  // namespace savasan::runners

#endif  // SAVASAN_RUNNERS_PHASE5_GUIDANCE_HPP_
