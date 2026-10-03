#include "control/vehicle_state_estimator.hpp"

#include <chrono>
#include <cmath>

int main() {
  using savasan::control::VehicleStateEstimator;
  using Clock = std::chrono::steady_clock;
  VehicleStateEstimator est;
  const auto t0 = Clock::now();

  VehicleStateEstimator::TelemetryInput tele{};
  tele.valid = true;
  tele.vel_x_mps = 2.0f;
  tele.vel_y_mps = 1.0f;
  tele.vel_z_mps = -0.5f;
  tele.yaw_rate_dps = 4.0f;
  est.UpdateTelemetry(tele);

  VehicleStateEstimator::VisionInput vis{};
  vis.valid = true;
  vis.vel_x_mps = 1.0f;
  vis.vel_y_mps = 0.5f;
  vis.vel_z_mps = -0.2f;
  est.UpdateVision(vis);

  est.Step(t0);
  est.Step(t0 + std::chrono::milliseconds(50));

  const auto& state = est.GetState();
  if (!state.telemetry_valid) {
    return 1;
  }
  if (!std::isfinite(state.pos_x_m) || !std::isfinite(state.vel_x_mps)) {
    return 2;
  }
  if (std::abs(state.vel_x_mps) < 0.01f) {
    return 3;
  }

  VehicleStateEstimator::Config filt_cfg{};
  filt_cfg.telemetry_weight = 1.0f;
  filt_cfg.vision_weight = 0.0f;
  filt_cfg.max_speed_mps = 50.0f;
  filt_cfg.velocity_alpha = 1.0f;
  filt_cfg.innovation_gate_mps = 1.0f;
  filt_cfg.yaw_rate_alpha = 1.0f;
  filt_cfg.yaw_innovation_gate_dps = 2.0f;
  VehicleStateEstimator filt_est(filt_cfg);

  VehicleStateEstimator::TelemetryInput tele_gate{};
  tele_gate.valid = true;
  tele_gate.vel_x_mps = 10.0f;
  tele_gate.yaw_rate_dps = 20.0f;
  filt_est.UpdateTelemetry(tele_gate);
  filt_est.Step(t0);
  filt_est.Step(t0 + std::chrono::milliseconds(100));
  filt_est.Step(t0 + std::chrono::milliseconds(200));

  const auto& filtered = filt_est.GetState();
  if (filtered.vel_x_mps < 1.99f || filtered.vel_x_mps > 2.01f) {
    return 4;
  }
  if (filtered.yaw_rate_dps < 3.99f || filtered.yaw_rate_dps > 4.01f) {
    return 5;
  }

  // Telemetri kesintisinde yaw/hız son değerde kalmalı (sıfıra çekilmemeli).
  VehicleStateEstimator::Config hold_cfg{};
  hold_cfg.telemetry_weight = 1.0f;
  hold_cfg.vision_weight = 0.0f;
  hold_cfg.velocity_alpha = 1.0f;
  hold_cfg.yaw_rate_alpha = 1.0f;
  hold_cfg.innovation_gate_mps = 1000.0f;
  hold_cfg.yaw_innovation_gate_dps = 1000.0f;
  VehicleStateEstimator hold_est(hold_cfg);

  VehicleStateEstimator::TelemetryInput tele_hold{};
  tele_hold.valid = true;
  tele_hold.vel_x_mps = 5.0f;
  tele_hold.yaw_rate_dps = 30.0f;
  hold_est.UpdateTelemetry(tele_hold);
  hold_est.Step(t0);
  hold_est.Step(t0 + std::chrono::milliseconds(100));
  const auto held_before = hold_est.GetState();

  VehicleStateEstimator::TelemetryInput tele_gap{};
  tele_gap.valid = false;
  hold_est.UpdateTelemetry(tele_gap);
  hold_est.Step(t0 + std::chrono::milliseconds(200));
  hold_est.Step(t0 + std::chrono::milliseconds(300));
  const auto held_after = hold_est.GetState();

  if (std::abs(held_after.yaw_rate_dps - held_before.yaw_rate_dps) > 0.01f) {
    return 6;
  }
  if (std::abs(held_after.vel_x_mps - held_before.vel_x_mps) > 0.01f) {
    return 7;
  }
  if (held_after.telemetry_valid) {
    return 8;
  }

  return 0;
}
