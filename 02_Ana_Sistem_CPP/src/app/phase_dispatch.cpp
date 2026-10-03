/**
 * @file phase_dispatch.cpp
 * @brief Faz sevk uygulaması. Yapılandırmadaki faz numarasına göre uygun
 *        çalıştırıcıyı seçer; çalıştırıcı yoksa veya başarısızsa hata kodu döner.
 */
#include "app/phase_dispatch.hpp"

#include <string>

#include "common/log.hpp"

namespace savasan::app {

namespace {

const char* LockModeLabel(const tracking::LockMode mode) {
  return mode == tracking::LockMode::kHybrid ? "hybrid" : "baseline";
}

/// Geçersiz faz numarası (1..5 dışı) için uyarı üretir ve Faz 1'e düşer.
int ResolveDispatchPhase(const int requested_phase) {
  if (requested_phase >= 1 && requested_phase <= 5) {
    return requested_phase;
  }
  SAVASAN_LOG_F(savasan::common::LogLevel::kWarn, "phase_dispatch",
                "Gecersiz faz numarasi=%d; Faz 1 (kamera dogrulama) secildi", requested_phase);
  return 1;
}

}  // namespace

/// @copydoc savasan::app::ExecutePhase
int ExecutePhase(const StartupConfig& cfg, const PhaseDispatchRunners& runners) {
  // Hibrit bayrağına göre kilit modunu belirle (tüm fazlar için ortak).
  const auto lock_mode = cfg.phase4_hybrid ? tracking::LockMode::kHybrid
                                           : tracking::LockMode::kBaseline;
  const int phase = ResolveDispatchPhase(cfg.phase);

  SAVASAN_LOG_F(savasan::common::LogLevel::kInfo, "phase_dispatch",
                "faz=%d lock_mode=%s", phase, LockModeLabel(lock_mode));

  if (phase == 3) {
    return runners.run_phase3 != nullptr &&
                   runners.run_phase3(cfg.ingest, cfg.pgi_config, cfg.tracker_config, cfg.ll_lib,
                                      cfg.sink, cfg.run_seconds, lock_mode)
               ? 0
               : 1;
  }
  if (phase == 4) {
    return runners.run_phase3 != nullptr &&
                   runners.run_phase3(cfg.ingest, cfg.pgi_config, cfg.tracker_config, cfg.ll_lib,
                                      cfg.sink, cfg.run_seconds, lock_mode)
               ? 0
               : 1;
  }
  if (phase == 5) {
    return runners.run_phase5 != nullptr &&
                   runners.run_phase5(cfg.ingest, cfg.pgi_config, cfg.tracker_config, cfg.ll_lib,
                                      cfg.sink, cfg.run_seconds, lock_mode)
               ? 0
               : 1;
  }
  if (phase == 2) {
    return runners.run_phase2 != nullptr &&
                   runners.run_phase2(cfg.ingest, cfg.pgi_config, cfg.sink, cfg.run_seconds)
               ? 0
               : 1;
  }
  return runners.run_phase1 != nullptr && runners.run_phase1(cfg.ingest) ? 0 : 1;
}

}  // namespace savasan::app
