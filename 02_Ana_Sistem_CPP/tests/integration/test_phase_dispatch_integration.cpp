#include "app/phase_dispatch.hpp"

namespace {
int g_called = 0;
savasan::tracking::LockMode g_seen_mode = savasan::tracking::LockMode::kBaseline;

bool FakeRunPhase5(savasan::IngestConfig, const std::string&, const std::string&,
                   const std::string&, savasan::deepstream::SinkType, int run_seconds,
                   savasan::tracking::LockMode mode) {
  g_called = run_seconds;
  g_seen_mode = mode;
  return true;
}
}  // namespace

int main() {
  savasan::app::StartupConfig cfg{};
  cfg.phase = 5;
  cfg.phase4_hybrid = true;
  cfg.run_seconds = 42;

  savasan::app::PhaseDispatchRunners runners{};
  runners.run_phase5 = FakeRunPhase5;

  const int rc = savasan::app::ExecutePhase(cfg, runners);
  if (rc != 0) {
    return 1;
  }
  if (g_called != 42) {
    return 2;
  }
  if (g_seen_mode != savasan::tracking::LockMode::kHybrid) {
    return 3;
  }
  return 0;
}
