/**
 * @file phase5_runtime_options.cpp
 * @brief @ref phase5_runtime_options.hpp uygulaması.
 */
#include "runners/phase5_runtime_options.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "common/log.hpp"
#include "control/vehicle_state_estimator.hpp"
#include "runners/mission_mode_utils.hpp"

namespace savasan::runners {

namespace {

std::string FormatFloat(const float v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.4g", v);
  return std::string(buf);
}

bool EnvBoolStrict(const char* key, const bool default_value) {
  const char* raw = std::getenv(key);
  if (raw == nullptr || raw[0] == '\0') {
    return default_value;
  }
  if (std::strcmp(raw, "1") == 0) {
    return true;
  }
  if (std::strcmp(raw, "0") == 0) {
    return false;
  }
  common::Log(common::LogLevel::kWarn, "Phase5Config",
              std::string(key) + " gecersiz deger '" + raw + "', varsayilan=" +
                  (default_value ? "1" : "0"));
  return default_value;
}

float EnvFloatClamped(const char* key, const float fallback, const float min_v, const float max_v) {
  const char* raw = std::getenv(key);
  if (raw == nullptr || raw[0] == '\0') {
    return fallback;
  }
  char* end = nullptr;
  const float v = std::strtof(raw, &end);
  if (end == raw) {
    common::Log(common::LogLevel::kError, "Phase5Config",
                std::string(key) + " parse hatasi: '" + raw + "', varsayilan=" + FormatFloat(fallback));
    return fallback;
  }
  return std::clamp(v, min_v, max_v);
}

int EnvIntClamped(const char* key, const int fallback, const int min_v, const int max_v) {
  const char* raw = std::getenv(key);
  if (raw == nullptr || raw[0] == '\0') {
    return fallback;
  }
  char* end = nullptr;
  const long v = std::strtol(raw, &end, 10);
  if (end == raw) {
    return fallback;
  }
  return static_cast<int>(std::clamp(v, static_cast<long>(min_v), static_cast<long>(max_v)));
}

void LogVehicleStateConfig(const control::VehicleStateEstimator::Config& c) {
  common::Log(common::LogLevel::kInfo, "Phase5Config",
              "VehicleState tele_w=" + FormatFloat(c.telemetry_weight) +
                  " vis_w=" + FormatFloat(c.vision_weight) +
                  " vel_alpha=" + FormatFloat(c.velocity_alpha) +
                  " innov_gate_mps=" + FormatFloat(c.innovation_gate_mps) +
                  " yaw_alpha=" + FormatFloat(c.yaw_rate_alpha) +
                  " yaw_gate_dps=" + FormatFloat(c.yaw_innovation_gate_dps));
}

void LogRuntimeFlags(const Phase5RuntimeFlags& flags) {
  std::string msg = "RuntimeFlags seyir_tx=" + std::string(flags.seyir_tx_enable ? "1" : "0") +
                    " control_log=" + (flags.control_log_enable ? "1" : "0") +
                    " send_only_on_lock=" + (flags.send_only_on_lock ? "1" : "0") +
                    " guidance=" + (flags.guidance_enabled ? "1" : "0") +
                    " mission_mode=" + MissionModeName(flags.initial_mission_mode);
  if (flags.mission_mode_dynamic_enabled) {
    msg += " mission_mode_file=" + flags.mission_mode_file +
           " poll_ms=" + std::to_string(flags.mission_mode_poll_interval_ms);
  }
  common::Log(common::LogLevel::kInfo, "Phase5Config", msg);
}

}  // namespace

void ConfigureVehicleStateEstimator(control::VehicleStateEstimator* state_estimator) {
  if (state_estimator == nullptr) {
    return;
  }
  control::VehicleStateEstimator::Config c = state_estimator->GetConfig();
  c.telemetry_weight =
      EnvFloatClamped("SAVASAN_STATE_TELEMETRY_WEIGHT", c.telemetry_weight, 0.0f, 1.0f);
  c.vision_weight = EnvFloatClamped("SAVASAN_STATE_VISION_WEIGHT", c.vision_weight, 0.0f, 1.0f);
  c.velocity_alpha = EnvFloatClamped("SAVASAN_STATE_VELOCITY_ALPHA", c.velocity_alpha, 0.0f, 1.0f);
  c.innovation_gate_mps =
      EnvFloatClamped("SAVASAN_STATE_INNOVATION_GATE_MPS", c.innovation_gate_mps, 0.0f, 200.0f);
  c.yaw_rate_alpha = EnvFloatClamped("SAVASAN_STATE_YAW_ALPHA", c.yaw_rate_alpha, 0.0f, 1.0f);
  c.yaw_innovation_gate_dps =
      EnvFloatClamped("SAVASAN_STATE_YAW_GATE_DPS", c.yaw_innovation_gate_dps, 0.0f, 720.0f);
  state_estimator->SetConfig(c);
  LogVehicleStateConfig(c);
}

Phase5RuntimeFlags ReadPhase5RuntimeFlags() {
  Phase5RuntimeFlags out{};
  // Geriye uyumluluk: eski SETPOINT_TX_ENABLE anahtarı da kabul edilir.
  if (std::getenv("SAVASAN_SEYIR_TX_ENABLE") != nullptr) {
    out.seyir_tx_enable = EnvBoolStrict("SAVASAN_SEYIR_TX_ENABLE", false);
  } else {
    out.seyir_tx_enable = EnvBoolStrict("SAVASAN_SETPOINT_TX_ENABLE", false);
  }
  out.control_log_enable = EnvBoolStrict("SAVASAN_CONTROL_LOG_ENABLE", true);
  out.send_only_on_lock = EnvBoolStrict("SAVASAN_SEND_ONLY_ON_LOCK", false);
  const char* guidance_mode = std::getenv("SAVASAN_GUIDANCE_MODE");
  out.guidance_enabled = !(guidance_mode != nullptr && std::strcmp(guidance_mode, "off") == 0);

  const char* mm = std::getenv("SAVASAN_MISSION_MODE");
  out.initial_mission_mode = ParseMissionModeEnv(mm, MissionMode::kAirLock);

  const char* mmf = std::getenv("SAVASAN_MISSION_MODE_FILE");
  if (mmf != nullptr && mmf[0] != '\0') {
    out.mission_mode_file = mmf;
    out.mission_mode_dynamic_enabled = true;
  }
  out.mission_mode_poll_interval_ms =
      EnvIntClamped("SAVASAN_MISSION_MODE_POLL_MS", out.mission_mode_poll_interval_ms, 50, 5000);
  LogRuntimeFlags(out);
  return out;
}

}  // namespace savasan::runners
