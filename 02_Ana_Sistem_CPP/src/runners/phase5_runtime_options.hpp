/**
 * @file phase5_runtime_options.hpp
 * @brief Faz-5 çalışma zamanı bayrakları ve kestirici yapılandırması.
 */
#ifndef SAVASAN_RUNNERS_PHASE5_RUNTIME_OPTIONS_HPP_
#define SAVASAN_RUNNERS_PHASE5_RUNTIME_OPTIONS_HPP_

#include <string>

#include "runners/phase5_runtime.hpp"

namespace savasan::control {
class VehicleStateEstimator;
}

namespace savasan::runners {

struct Phase5RuntimeFlags {
  bool seyir_tx_enable = false;      ///< Seyir komutu ACM0 TX etkin mi.
  bool control_log_enable = true;
  bool send_only_on_lock = false;
  bool guidance_enabled = true;        ///< Güdüm döngüsü etkin mi (off = kapalı).
  MissionMode initial_mission_mode = MissionMode::kAirLock;
  bool mission_mode_dynamic_enabled = false;
  std::string mission_mode_file;
  int mission_mode_poll_interval_ms = 250;
};

void ConfigureVehicleStateEstimator(control::VehicleStateEstimator* state_estimator);

Phase5RuntimeFlags ReadPhase5RuntimeFlags();

}  // namespace savasan::runners

#endif  // SAVASAN_RUNNERS_PHASE5_RUNTIME_OPTIONS_HPP_
