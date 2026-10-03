#include "runners/phase5_guidance.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

namespace {

class StressBridge final : public savasan::autopilot::IAlcLinkBridge {
 public:
  std::atomic<int> seyir_count{0};
  std::atomic<bool> fail_tx{false};
  savasan::autopilot::TelemetrySnapshot telemetry{};

  bool Connect() override { return true; }
  void Disconnect() override {}
  bool IsConnected() const override { return true; }
  bool SendLockCoordinates(const savasan::autopilot::LockCoordinates&) override { return true; }
  bool SendNoLock() override { return true; }
  bool SendEvasionCommand(uint8_t) override { return true; }
  bool SendSeyirModeCommand(const savasan::autopilot::SeyirModePacket& pkt, bool) override {
    seyir_count.fetch_add(1);
    return pkt.valid && !fail_tx.load();
  }
  void StartHeartbeat() override {}
  void StopHeartbeat() override {}
  savasan::autopilot::TelemetrySnapshot GetTelemetrySnapshot() const override { return telemetry; }
  std::string DescribeEndpoint() const override { return "mock://phase5-stress"; }
};

}  // namespace

int main() {
  auto bridge = std::make_shared<StressBridge>();
  bridge->telemetry.valid = true;
  bridge->telemetry.enlem = 41.0;
  bridge->telemetry.boylam = 36.0;
  bridge->telemetry.irtifa_m = 100.0f;
  bridge->telemetry.yaw_deg = 0.0f;
  bridge->telemetry.gps_hiz_mps = 10.0f;
  bridge->telemetry.vel_x_mps = 0.1f;
  bridge->telemetry.vel_y_mps = 0.1f;
  bridge->telemetry.vel_z_mps = 0.0f;
  bridge->telemetry.yaw_rate_dps = 0.0f;

  savasan::runners::Phase5Runtime rt{};
  rt.bridge = bridge;
  rt.guidance.state_estimator = std::make_shared<savasan::control::VehicleStateEstimator>();
  rt.guidance.seyir_cfg.enabled = true;
  rt.guidance.seyir_tx_enable.store(true);
  rt.guidance.send_only_on_lock.store(false);

  std::atomic<bool> running{true};
  std::atomic<int> guidance_ticks{0};
  std::atomic<bool> saw_degrade{false};

  std::thread producer([&]() {
    for (int i = 0; i < 200; ++i) {
      {
        std::lock_guard<std::mutex> lk(rt.mutex);
        const bool lock = (i % 3) != 0;
        rt.guidance.latest_lock_valid = lock;
        if (lock) {
          rt.tracker.lock_metrics.Update(true, 0.55f + (i % 5) * 0.01f, 0.50f, 0);
        }
      }
      if (i == 120) {
        bridge->fail_tx.store(true);
      }
      if (i == 150) {
        bridge->fail_tx.store(false);
        rt.health.degraded.active.store(false);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    running.store(false);
  });

  std::thread consumer([&]() {
    auto tp = std::chrono::steady_clock::now();
    while (running.load()) {
      (void)savasan::runners::RunGuidanceControlStep(&rt, tp);
      if (rt.health.degraded.active.load() || rt.health.degraded.comm_fail_count.load() > 0) {
        saw_degrade.store(true);
      }
      guidance_ticks.fetch_add(1);
      tp += std::chrono::milliseconds(5);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  producer.join();
  consumer.join();

  if (guidance_ticks.load() < 50) {
    return 1;
  }
  if (bridge->seyir_count.load() == 0) {
    return 2;
  }
  if (!saw_degrade.load()) {
    return 3;
  }
  if (rt.health.degraded.active.load()) {
    return 4;
  }

  return 0;
}
