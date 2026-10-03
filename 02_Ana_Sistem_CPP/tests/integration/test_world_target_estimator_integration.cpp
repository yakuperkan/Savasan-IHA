#include "control/world_target_estimator.hpp"
#include "control/pid_controller.hpp"

#include <chrono>

int main() {
  savasan::control::WorldTargetEstimator::Config wcfg{};
  wcfg.desired_standoff_m = 15.0f;
  wcfg.focal_length_px = 800.0f;
  wcfg.target_real_width_m = 1.2f;
  wcfg.image_width_px = 640.0f;
  wcfg.min_distance_m = 2.0f;
  wcfg.max_distance_m = 80.0f;
  savasan::control::WorldTargetEstimator world(wcfg);

  savasan::control::WorldTargetEstimator::TargetObservation obs{};
  obs.valid = true;
  obs.nx = 0.70f;
  obs.ny = 0.40f;
  obs.bbox_w_norm = 0.20f;
  obs.bbox_h_norm = 0.30f;

  const auto est = world.Estimate(obs);
  if (!est.valid) {
    return 1;
  }
  if (est.distance_m < 2.0f || est.distance_m > 80.0f) {
    return 2;
  }
  // Hedef sağda → NED Doğu hatası pozitif.
  if (est.angular.yaw_deg <= 0.0f) {
    return 3;
  }
  // Hedef yukarıda → NED Aşağı hatası negatif (tırman).
  if (est.angular.pitch_deg >= 0.0f) {
    return 4;
  }
  if (est.confidence <= 0.0f) {
    return 5;
  }

  // Pipeline: NED hata işaretleri PID çıkışına yansır.
  savasan::control::PidControllerXYZYaw pid;
  savasan::control::PidControllerXYZYaw::ErrorInput err{};
  err.valid = true;
  err.ex = est.closure_error_m;
  err.ey = est.angular.yaw_deg;
  err.ez = est.angular.pitch_deg;

  const auto t0 = std::chrono::steady_clock::now();
  (void)pid.Update(err, t0);  // warmup tick
  const auto out = pid.Update(err, t0 + std::chrono::milliseconds(20));
  if (!out.valid) {
    return 6;
  }
  if (!(out.vy_cmd > 0.0f && out.vz_cmd < 0.0f)) {
    return 7;
  }

  return 0;
}
