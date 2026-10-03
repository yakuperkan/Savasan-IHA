#include "control/world_target_estimator.hpp"

#include <cmath>

int main() {
  savasan::control::WorldTargetEstimator est;

  savasan::control::WorldTargetEstimator::TargetObservation invalid{};
  auto out = est.Estimate(invalid);
  if (out.valid) {
    return 1;
  }

  savasan::control::WorldTargetEstimator::TargetObservation obs{};
  obs.valid = true;
  obs.nx = 0.6f;
  obs.ny = 0.4f;
  obs.bbox_w_norm = 0.2f;
  obs.bbox_h_norm = 0.2f;

  out = est.Estimate(obs);
  if (!out.valid) {
    return 2;
  }
  if (!(out.distance_m > 0.0f) || !std::isfinite(out.confidence)) {
    return 3;
  }
  if (out.angular.yaw_deg <= 0.0f || out.angular.pitch_deg >= 0.0f) {
    return 4;
  }

  // Pinhole kalibrasyon yolu: focal + hedef genişliği tanımlıysa alan modeli atlanır.
  savasan::control::WorldTargetEstimator::Config pinhole_cfg{};
  pinhole_cfg.focal_length_px = 640.0f;
  pinhole_cfg.target_real_width_m = 1.0f;
  pinhole_cfg.image_width_px = 640.0f;
  savasan::control::WorldTargetEstimator pinhole(pinhole_cfg);
  const auto pinhole_out = pinhole.Estimate(obs);
  if (!pinhole_out.valid) {
    return 5;
  }
  // w_norm=0.2 → bbox_w_px=128 → d = 640*1/128 = 5 m
  if (std::abs(pinhole_out.distance_m - 5.0f) > 0.5f) {
    return 6;
  }
  return 0;
}
