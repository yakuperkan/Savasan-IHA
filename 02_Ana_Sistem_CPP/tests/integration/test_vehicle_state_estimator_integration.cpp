#include "control/vehicle_state_estimator.hpp"

#include <chrono>

int main() {
  savasan::control::VehicleStateEstimator::Config cfg{};
  cfg.telemetry_weight = 0.8f;
  cfg.vision_weight = 0.2f;
  cfg.max_speed_mps = 30.0f;
  cfg.velocity_alpha = 1.0f;
  cfg.innovation_gate_mps = 1000.0f;
  cfg.yaw_rate_alpha = 1.0f;
  cfg.yaw_innovation_gate_dps = 1000.0f;
  savasan::control::VehicleStateEstimator est(cfg);

  savasan::control::VehicleStateEstimator::TelemetryInput tele{};
  tele.valid = true;
  tele.vel_x_mps = 2.0f;
  tele.vel_y_mps = -1.0f;
  tele.vel_z_mps = 0.5f;
  tele.yaw_rate_dps = 4.0f;
  est.UpdateTelemetry(tele);

  savasan::control::VehicleStateEstimator::VisionInput vis{};
  vis.valid = true;
  vis.vel_x_mps = 1.0f;
  vis.vel_y_mps = 1.0f;
  vis.vel_z_mps = -0.5f;
  est.UpdateVision(vis);

  const auto t0 = std::chrono::steady_clock::now();
  est.Step(t0);  // first tick initializes dt reference
  const auto t1 = t0 + std::chrono::milliseconds(100);
  est.Step(t1);

  const auto& s = est.GetState();
  // expected fused velocities: 0.8*tele + 0.2*vision
  if (s.vel_x_mps < 1.79f || s.vel_x_mps > 1.81f) {
    return 1;
  }
  if (s.vel_y_mps < -0.61f || s.vel_y_mps > -0.59f) {
    return 2;
  }
  if (s.vel_z_mps < 0.29f || s.vel_z_mps > 0.31f) {
    return 3;
  }
  if (s.yaw_rate_dps != 4.0f) {
    return 4;
  }
  if (!s.telemetry_valid) {
    return 5;
  }

  // position must advance with fused velocity.
  if (s.pos_x_m <= 0.0f) {
    return 6;
  }
  if (s.pos_y_m >= 0.0f) {
    return 7;
  }
  if (s.pos_z_m <= 0.0f) {
    return 8;
  }

  return 0;
}
