/**
 * @file pid_controller.cpp
 * @brief @ref savasan::control::PidControllerXYZYaw uygulaması.
 */
#include "control/pid_controller.hpp"

#include <algorithm>
#include <cmath>

namespace savasan::control {

// Tek eksen PID hesabı: ölü bant uygulanır, türev/integral aday değerleri
// hesaplanır, anti-windup ile koşullu integral biriktirilir, çıkış doygunluğa
// kırpılır ve son olarak slew-rate (değişim hızı) sınırlaması uygulanır.
PidControllerXYZYaw::AxisStepResult PidControllerXYZYaw::UpdateAxis(const AxisConfig& cfg,
                                                                    AxisState* st, float error,
                                                                    float dt) {
  AxisStepResult result{};

  // Ölü bant: küçük hataları sıfırlayarak titreme/aşınmayı azaltır.
  if (std::abs(error) < cfg.deadband) {
    error = 0.0f;
  }

  const float derivative = (dt > 1e-6f) ? ((error - st->prev_error) / dt) : 0.0f;
  const float integral_candidate =
      std::clamp(st->integral + error * dt, -cfg.i_limit, cfg.i_limit);

  // Doygunlukta hatayı daha da büyütecek integral birikimini engelle (anti-windup).
  const float unsat_candidate =
      cfg.kp * error + cfg.ki * integral_candidate + cfg.kd * derivative;
  const bool pushing_high = (unsat_candidate > cfg.output_limit) && (error > 0.0f);
  const bool pushing_low = (unsat_candidate < -cfg.output_limit) && (error < 0.0f);
  if (!(pushing_high || pushing_low)) {
    st->integral = integral_candidate;
  }

  const float unsat_output = cfg.kp * error + cfg.ki * st->integral + cfg.kd * derivative;
  float output = std::clamp(unsat_output, -cfg.output_limit, cfg.output_limit);
  result.output_saturated = (unsat_output != output);

  // Slew-rate sınırı: çıkışın bir adımda değişebileceği miktarı kısıtlar.
  if (cfg.slew_rate > 0.0f && dt > 0.0f) {
    const float max_delta = cfg.slew_rate * dt;
    const float lo = st->prev_output - max_delta;
    const float hi = st->prev_output + max_delta;
    const float pre_slew = output;
    output = std::clamp(output, lo, hi);
    result.slew_clipped = (output != pre_slew);
  }

  st->prev_error = error;
  st->prev_output = output;
  result.output = output;
  return result;
}

void PidControllerXYZYaw::AccumulateAxisDiagnostics(AxisDiagnostics* diag,
                                                  const AxisStepResult& step) {
  if (diag == nullptr) {
    return;
  }
  if (step.output_saturated) {
    ++diag->output_saturated_ticks;
  }
  if (step.slew_clipped) {
    ++diag->slew_clipped_ticks;
  }
}

PidControllerXYZYaw::Config PidControllerXYZYaw::GetConfig() const {
  std::lock_guard<std::mutex> lk(state_mutex_);
  return cfg_;
}

void PidControllerXYZYaw::SetConfig(const Config& cfg) {
  std::lock_guard<std::mutex> lk(state_mutex_);
  cfg_ = cfg;
}

PidControllerXYZYaw::Diagnostics PidControllerXYZYaw::GetDiagnostics() const {
  std::lock_guard<std::mutex> lk(state_mutex_);
  return diagnostics_;
}

void PidControllerXYZYaw::ResetLocked() {
  x_ = AxisState{};
  y_ = AxisState{};
  z_ = AxisState{};
  yaw_ = AxisState{};
  diagnostics_ = Diagnostics{};
  last_tp_ = std::chrono::steady_clock::time_point{};
}

// Kontrol adımı: tüm okuma/yazma state_mutex_ altında; hesap hafif olduğu için
// kilit dışı kopya deseni kullanılmaz (eşzamanlı Update kaybı riski giderildi).
PidControllerXYZYaw::Output PidControllerXYZYaw::Update(
    const ErrorInput& err, std::chrono::steady_clock::time_point now_tp) {
  std::lock_guard<std::mutex> lk(state_mutex_);

  Output out{};
  if (!err.valid) {
    ResetLocked();
    return out;
  }

  if (last_tp_.time_since_epoch().count() == 0) {
    last_tp_ = now_tp;
    return out;
  }
  const float dt = std::chrono::duration<float>(now_tp - last_tp_).count();
  if (dt <= 0.0f || dt > 0.5f) {
    last_tp_ = now_tp;
    return out;
  }

  out.valid = true;
  const auto x_step = UpdateAxis(cfg_.x, &x_, err.ex, dt);
  const auto y_step = UpdateAxis(cfg_.y, &y_, err.ey, dt);
  const auto z_step = UpdateAxis(cfg_.z, &z_, err.ez, dt);
  out.vx_cmd = x_step.output;
  out.vy_cmd = y_step.output;
  out.vz_cmd = z_step.output;
  AccumulateAxisDiagnostics(&diagnostics_.x, x_step);
  AccumulateAxisDiagnostics(&diagnostics_.y, y_step);
  AccumulateAxisDiagnostics(&diagnostics_.z, z_step);

  if (cfg_.enable_yaw_axis) {
    const auto yaw_step = UpdateAxis(cfg_.yaw, &yaw_, err.eyaw, dt);
    out.yaw_rate_cmd = yaw_step.output;
    AccumulateAxisDiagnostics(&diagnostics_.yaw, yaw_step);
  } else {
    yaw_ = AxisState{};
    out.yaw_rate_cmd = 0.0f;
  }

  last_tp_ = now_tp;
  return out;
}

void PidControllerXYZYaw::Reset() {
  std::lock_guard<std::mutex> lk(state_mutex_);
  ResetLocked();
}

}  // namespace savasan::control
