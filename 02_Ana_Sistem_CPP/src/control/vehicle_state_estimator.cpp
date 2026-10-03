/**
 * @file vehicle_state_estimator.cpp
 * @brief @ref savasan::control::VehicleStateEstimator uygulaması.
 */
#include "control/vehicle_state_estimator.hpp"

#include <algorithm>
#include <cmath>

namespace savasan::control {

namespace {

/// @brief Innovation-kapılı düşük geçiren filtre (LPF).
/// Yenilik (ölçüm - önceki) önce ±gate_abs ile kırpılır (aykırı değer reddi),
/// ardından alpha kazancıyla önceki değere eklenir.
/// @param alpha 1.0 ise filtre etkisizdir (ölçümü doğrudan kullanır).
/// @param gate_abs 0 ise kırpma yapılmaz.
float FilterWithInnovationGate(const float prev,
                               const float measurement,
                               const float alpha,
                               const float gate_abs) {
  float innovation = measurement - prev;
  if (gate_abs > 0.0f) {
    innovation = std::clamp(innovation, -gate_abs, gate_abs);
  }
  return prev + (alpha * innovation);
}

/// @brief Telemetri ve görüntü hızlarını birleştirip yeni durumu hesaplar.
///
/// Ağırlıklar normalize edilir; her iki kaynak da geçerliyse ağırlıklı ortalama,
/// yalnızca biri geçerliyse o kaynak kullanılır. Sonuç innovation-kapılı LPF'ten
/// geçirilir, hız sınırına kırpılır ve konum hız integraliyle güncellenir.
VehicleStateEstimator::State FuseVehicleState(
    const VehicleStateEstimator::TelemetryInput& telemetry,
    const VehicleStateEstimator::VisionInput& vision,
    const VehicleStateEstimator::Config& config,
    const VehicleStateEstimator::State& prev_state,
    const float dt) {
  VehicleStateEstimator::State next = prev_state;

  const float tw = std::clamp(config.telemetry_weight, 0.0f, 1.0f);
  const float vw = std::clamp(config.vision_weight, 0.0f, 1.0f);
  const float wsum = std::max(tw + vw, 1e-3f);
  const float ntw = tw / wsum;
  const float nvw = vw / wsum;

  const bool t_ok = telemetry.valid;
  const bool v_ok = vision.valid;

  const float tvx = t_ok ? telemetry.vel_x_mps : 0.0f;
  const float tvy = t_ok ? telemetry.vel_y_mps : 0.0f;
  const float tvz = t_ok ? telemetry.vel_z_mps : 0.0f;
  const float vvx = v_ok ? vision.vel_x_mps : 0.0f;
  const float vvy = v_ok ? vision.vel_y_mps : 0.0f;
  const float vvz = v_ok ? vision.vel_z_mps : 0.0f;

  // Geçerli ölçüm yokken filtreyi güncelleme; son değeri koru (sıfıra çekme).
  if (t_ok || v_ok) {
    float fused_vx = 0.0f;
    float fused_vy = 0.0f;
    float fused_vz = 0.0f;
    if (t_ok && v_ok) {
      fused_vx = ntw * tvx + nvw * vvx;
      fused_vy = ntw * tvy + nvw * vvy;
      fused_vz = ntw * tvz + nvw * vvz;
    } else if (t_ok) {
      fused_vx = tvx;
      fused_vy = tvy;
      fused_vz = tvz;
    } else {
      fused_vx = vvx;
      fused_vy = vvy;
      fused_vz = vvz;
    }
    const float alpha = std::clamp(config.velocity_alpha, 0.0f, 1.0f);
    const float vel_gate = std::max(config.innovation_gate_mps, 0.0f);
    next.vel_x_mps = FilterWithInnovationGate(next.vel_x_mps, fused_vx, alpha, vel_gate);
    next.vel_y_mps = FilterWithInnovationGate(next.vel_y_mps, fused_vy, alpha, vel_gate);
    next.vel_z_mps = FilterWithInnovationGate(next.vel_z_mps, fused_vz, alpha, vel_gate);
  }

  if (t_ok) {
    const float yaw_alpha = std::clamp(config.yaw_rate_alpha, 0.0f, 1.0f);
    const float yaw_gate = std::max(config.yaw_innovation_gate_dps, 0.0f);
    next.yaw_rate_dps =
        FilterWithInnovationGate(next.yaw_rate_dps, telemetry.yaw_rate_dps, yaw_alpha, yaw_gate);
  }

  const float vmax = std::max(config.max_speed_mps, 0.1f);
  next.vel_x_mps = std::clamp(next.vel_x_mps, -vmax, vmax);
  next.vel_y_mps = std::clamp(next.vel_y_mps, -vmax, vmax);
  next.vel_z_mps = std::clamp(next.vel_z_mps, -vmax, vmax);

  next.pos_x_m += next.vel_x_mps * dt;
  next.pos_y_m += next.vel_y_mps * dt;
  next.pos_z_m += next.vel_z_mps * dt;
  next.telemetry_valid = telemetry.valid;
  return next;
}

}  // namespace

void VehicleStateEstimator::UpdateTelemetry(const TelemetryInput& t) {
  std::lock_guard<std::mutex> lk(state_mutex_);
  telemetry_ = t;
}

void VehicleStateEstimator::UpdateVision(const VisionInput& v) {
  std::lock_guard<std::mutex> lk(state_mutex_);
  vision_ = v;
}

VehicleStateEstimator::State VehicleStateEstimator::GetState() const {
  std::lock_guard<std::mutex> lk(state_mutex_);
  return state_;
}

VehicleStateEstimator::Config VehicleStateEstimator::GetConfig() const {
  std::lock_guard<std::mutex> lk(state_mutex_);
  return config_;
}

void VehicleStateEstimator::SetConfig(const Config& cfg) {
  std::lock_guard<std::mutex> lk(state_mutex_);
  config_ = cfg;
}

// Füzyon adımı: dt'yi son adımdan hesaplar (ilk adımda yalnızca zaman damgasını
// kurar; dt geçersiz/çok büyükse atlar). Okuma+füzyon+yazım tek kilit altında;
// @ref Step yalnızca tek yazıcı thread'den çağrılmalıdır (üretimde guidance döngüsü).
void VehicleStateEstimator::Step(std::chrono::steady_clock::time_point now_tp) {
  std::lock_guard<std::mutex> lk(state_mutex_);
  if (last_step_tp_.time_since_epoch().count() == 0) {
    last_step_tp_ = now_tp;
    return;
  }
  const float dt = std::chrono::duration<float>(now_tp - last_step_tp_).count();
  last_step_tp_ = now_tp;
  if (dt <= 0.0f || dt > 0.5f) {
    return;
  }
  state_ = FuseVehicleState(telemetry_, vision_, config_, state_, dt);
}

}  // namespace savasan::control
