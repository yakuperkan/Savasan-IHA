/**
 * @file world_target_estimator.cpp
 * @brief @ref savasan::control::WorldTargetEstimator uygulaması.
 */
#include "control/world_target_estimator.hpp"

#include <algorithm>
#include <cmath>

namespace savasan::control {

WorldTargetEstimator::WorldTargetEstimate WorldTargetEstimator::Estimate(
    const TargetObservation& obs) {
  WorldTargetEstimate out{};
  if (!obs.valid) {
    return out;
  }

  const float w = std::clamp(obs.bbox_w_norm, 0.0f, 1.0f);
  const float h = std::clamp(obs.bbox_h_norm, 0.0f, 1.0f);
  const float area = std::max(w * h, 1e-4f);

  float distance = 0.0f;
  if (config_.focal_length_px > 0.0f && config_.target_real_width_m > 0.0f &&
      config_.image_width_px > 1.0f && w > 1e-4f) {
    const float bbox_w_px = std::max(w * config_.image_width_px, 1.0f);
    distance = (config_.focal_length_px * config_.target_real_width_m) / bbox_w_px;
  } else {
    distance = config_.distance_gain / std::sqrt(area);
  }
  distance = std::clamp(distance, config_.min_distance_m, config_.max_distance_m);

  // Görüntü merkezi referans: x' = nx - 0.5; hata = 0 - x'.
  const float x_prime = std::clamp(obs.nx, 0.0f, 1.0f) - 0.5f;
  const float y_prime = std::clamp(obs.ny, 0.0f, 1.0f) - 0.5f;
  const float gain = std::max(config_.pixel_to_deg_gain, 1e-3f);

  out.closure_error_m = distance - config_.desired_standoff_m;
  out.angular.yaw_deg = x_prime * gain;
  out.angular.pitch_deg = y_prime * gain;
  out.valid = true;
  out.distance_m = distance;

  const float center_score =
      1.0f - std::min((std::abs(x_prime) + std::abs(y_prime)), 1.0f);
  const float area_score = std::min(area / 0.12f, 1.0f);
  out.confidence = 0.5f * center_score + 0.5f * area_score;
  return out;
}

}  // namespace savasan::control
