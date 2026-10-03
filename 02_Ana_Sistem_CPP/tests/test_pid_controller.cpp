#include "control/pid_controller.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>

int main() {
  using savasan::control::PidControllerXYZYaw;
  using Clock = std::chrono::steady_clock;
  PidControllerXYZYaw pid;
  auto now = Clock::now();

  const auto cfg = pid.GetConfig();
  if (cfg.x.output_limit >= cfg.y.output_limit) {
    return 10;  // X (m/s) sınırı açısal eksenden düşük olmalı.
  }
  if (cfg.enable_yaw_axis) {
    return 11;  // Yaw kanalı varsayılan kapalı.
  }

  PidControllerXYZYaw::ErrorInput invalid{};
  auto out = pid.Update(invalid, now);
  if (out.valid) {
    return 1;
  }

  PidControllerXYZYaw::ErrorInput err{};
  err.valid = true;
  err.ex = 1.0f;
  err.ey = -0.5f;
  err.ez = 0.2f;
  err.eyaw = 10.0f;

  out = pid.Update(err, now + std::chrono::milliseconds(10));
  if (out.valid) {
    return 2;
  }

  out = pid.Update(err, now + std::chrono::milliseconds(30));
  if (!out.valid) {
    return 3;
  }
  if (!std::isfinite(out.vx_cmd) || !std::isfinite(out.vy_cmd) || !std::isfinite(out.vz_cmd)) {
    return 4;
  }
  if (out.yaw_rate_cmd != 0.0f) {
    return 5;  // enable_yaw_axis=false iken yaw hesaplanmamalı.
  }

  // Büyük hata ile çıkış doygunluğu sayacı artmalı.
  PidControllerXYZYaw sat_pid;
  PidControllerXYZYaw::ErrorInput big_err{};
  big_err.valid = true;
  big_err.ex = 100.0f;
  auto sat_now = Clock::now();
  (void)sat_pid.Update(big_err, sat_now + std::chrono::milliseconds(10));
  for (int i = 0; i < 5; ++i) {
  out = sat_pid.Update(big_err, sat_now + std::chrono::milliseconds(20 + i * 20));
    if (!out.valid) {
      return 6;
    }
  }
  if (sat_pid.GetDiagnostics().x.output_saturated_ticks == 0) {
    return 7;
  }
  if (std::abs(out.vx_cmd) > cfg.x.output_limit + 1e-3f) {
    return 8;
  }

  // Eşzamanlı Update: kilit altında seri hale getirildiği için crash/NaN olmamalı.
  std::atomic<bool> stop{false};
  std::thread t1([&]() {
    PidControllerXYZYaw::ErrorInput e{};
    e.valid = true;
    e.ex = 2.0f;
    auto tp = Clock::now();
    while (!stop.load()) {
      (void)pid.Update(e, tp);
      tp += std::chrono::milliseconds(5);
    }
  });
  std::thread t2([&]() {
    PidControllerXYZYaw::ErrorInput e{};
    e.valid = true;
    e.ey = 1.0f;
    auto tp = Clock::now();
    while (!stop.load()) {
      (void)pid.Update(e, tp);
      tp += std::chrono::milliseconds(5);
    }
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  stop.store(true);
  t1.join();
  t2.join();

  return 0;
}
