#include "competition/siha_types.hpp"
#include "runners/phase5_guidance.hpp"
#include "runners/rival_telemetry_file.hpp"

#include <chrono>
#include <memory>
#include <string>

namespace {

class CapturingBridge final : public savasan::autopilot::IAlcLinkBridge {
 public:
  bool connected = true;
  bool seyir_ok = true;
  int seyir_count = 0;
  bool last_dry_run = false;
  savasan::autopilot::SeyirModePacket last_seyir{};
  savasan::autopilot::TelemetrySnapshot telemetry{};

  bool Connect() override {
    connected = true;
    return true;
  }
  void Disconnect() override { connected = false; }
  bool IsConnected() const override { return connected; }

  bool SendLockCoordinates(const savasan::autopilot::LockCoordinates&) override { return true; }
  bool SendNoLock() override { return true; }
  bool SendEvasionCommand(uint8_t) override { return true; }
  bool SendSeyirModeCommand(const savasan::autopilot::SeyirModePacket& pkt, bool dry_run) override {
    ++seyir_count;
    last_seyir = pkt;
    last_dry_run = dry_run;
    return seyir_ok && pkt.valid;
  }

  void StartHeartbeat() override {}
  void StopHeartbeat() override {}
  savasan::autopilot::TelemetrySnapshot GetTelemetrySnapshot() const override { return telemetry; }
  std::string DescribeEndpoint() const override { return "mock://guidance-loop"; }
};

void SetupRuntimeBase(savasan::runners::Phase5Runtime& rt,
                      const std::shared_ptr<CapturingBridge>& bridge) {
  rt.bridge = bridge;
  savasan::control::VehicleStateEstimator::Config vse_cfg{};
  vse_cfg.velocity_alpha = 1.0f;
  vse_cfg.innovation_gate_mps = 1000.0f;
  vse_cfg.yaw_rate_alpha = 1.0f;
  vse_cfg.yaw_innovation_gate_dps = 1000.0f;
  rt.guidance.state_estimator = std::make_shared<savasan::control::VehicleStateEstimator>(vse_cfg);
  rt.guidance.seyir_cfg.enabled = true;
  rt.guidance.seyir_cfg.visual_confirm_frames = 1;
  rt.guidance.seyir_cfg.visual_bbox_min = 0.01f;
  // Kadans freninin kendi birim testi var (test_seyir_command_scheduler); burada
  // tikler 20 ms arayla ilerlediği için freni kapatıp komut seçimini sınıyoruz.
  rt.guidance.seyir_scheduler_cfg.min_interval_s = 0.0f;
  rt.guidance.seyir_tx_enable.store(true);
  rt.guidance.send_only_on_lock.store(false);
  bridge->telemetry.valid = true;
  bridge->telemetry.enlem = 41.0;
  bridge->telemetry.boylam = 36.0;
  bridge->telemetry.yaw_deg = 0.0f;
  bridge->telemetry.irtifa_m = 100.0f;
  bridge->telemetry.gps_hiz_mps = 15.0f;
  bridge->telemetry.vel_x_mps = 0.1f;
  bridge->telemetry.vel_y_mps = 0.2f;
  bridge->telemetry.vel_z_mps = -0.1f;
}

}  // namespace

int main() {
  auto bridge = std::make_shared<CapturingBridge>();

  savasan::runners::Phase5Runtime rt{};
  SetupRuntimeBase(rt, bridge);

  // 1) Görsel kilit + telemetri → ilk tik irtifa (akış kesintisi), sonraki tik koordinat.
  savasan::runners::RivalPoolSnapshot pool_vis{};
  savasan::runners::RivalTargetSnapshot rival_vis{};
  rival_vis.valid = true;
  rival_vis.target_id = 3;
  rival_vis.lat_deg = 41.002;
  rival_vis.lon_deg = 36.0;
  rival_vis.alt_m = 100.0f;
  rival_vis.spd_mps = 12.0f;
  rival_vis.hdg_deg = 90.0f;
  pool_vis.targets.push_back(rival_vis);
  (void)savasan::runners::WriteRivalPoolFile(pool_vis);

  rt.guidance.latest_lock_valid = true;
  rt.tracker.lock_metrics.Update(true, 0.62f, 0.48f, 0, 0.12f, 0.10f);

  const auto t0 = std::chrono::steady_clock::now();
  (void)savasan::runners::RunGuidanceControlStep(&rt, t0);
  // Akış kesintisinden sonraki ilk paket daima irtifa olmalı (kart irtifayı unutur).
  if (bridge->last_seyir.bytes[3] != savasan::autopilot::seyir::kIstemAltitude) {
    return 19;
  }
  const auto t1 = t0 + std::chrono::milliseconds(20);
  if (!savasan::runners::RunGuidanceControlStep(&rt, t1)) {
    return 1;
  }
  if (bridge->seyir_count <= 0 || !bridge->last_seyir.valid) {
    return 2;
  }
  if (!rt.guidance.guidance_active_prev.load()) {
    return 3;
  }
  if (bridge->last_dry_run) {
    return 4;
  }
  if (rt.guidance.seyir_phase != savasan::runners::SeyirPhase::kGorsel) {
    return 5;
  }
  if (bridge->seyir_count < 2) {
    return 6;
  }
  // İrtifa tazelendikten sonra yatay güdüm koordinatla verilir; istem 1 (heading)
  // vendor kaynağında hiç uçmadığı için üretim yolunda asla çıkmamalı.
  if (bridge->last_seyir.bytes[3] != savasan::autopilot::seyir::kIstemCoordinate) {
    return 6;
  }
  if (bridge->last_seyir.length != 14) {
    return 20;
  }

  // 1b) 4 sn AV kilidi dolunca faz KILIT'e geçmeli (sayaç güdüme bağlı).
  rt.guidance.latest_av_lock_valid = true;
  const auto t1b = t1 + std::chrono::milliseconds(20);
  (void)savasan::runners::RunGuidanceControlStep(&rt, t1b);
  if (rt.guidance.seyir_phase != savasan::runners::SeyirPhase::kKilit) {
    return 21;
  }
  rt.guidance.latest_av_lock_valid = false;

  // 2) TX başarısız → degrade.
  bridge->seyir_ok = false;
  const uint32_t fails_before = rt.health.degraded.comm_fail_count.load();
  const auto t2 = t1 + std::chrono::milliseconds(20);
  (void)savasan::runners::RunGuidanceControlStep(&rt, t2);
  if (!rt.health.degraded.active.load()) {
    return 7;
  }
  if (rt.health.degraded.comm_fail_count.load() != fails_before + 1U) {
    return 8;
  }

  // 3) Ardışık başarılı TX → degrade kurtarma.
  bridge->seyir_ok = true;
  auto t_recover = t2;
  for (int i = 0; i < 3; ++i) {
    t_recover += std::chrono::milliseconds(20);
    (void)savasan::runners::RunGuidanceControlStep(&rt, t_recover);
  }
  if (rt.health.degraded.active.load()) {
    return 9;
  }

  // 4) APF kaçınma → KACINMA fazı; kaçış yönü koordinatla verilir (istem 1 yok).
  savasan::runners::Phase5Runtime rt_apf{};
  SetupRuntimeBase(rt_apf, bridge);
  rt_apf.guidance.seyir_cfg.apf_config.d0_m = 500.0f;
  rt_apf.guidance.seyir_cfg.apf_config.k_rep = 500.0f;
  rt_apf.guidance.seyir_cfg.apf_config.own_team_no = 1;

  savasan::runners::RivalPoolSnapshot apf_pool{};
  savasan::runners::RivalTargetSnapshot threat{};
  threat.valid = true;
  threat.target_id = 2;
  threat.lat_deg = 41.0004;
  threat.lon_deg = 36.0;
  threat.spd_mps = 10.0f;
  threat.hdg_deg = 180.0f;
  threat.time_diff_ms = 50;
  apf_pool.targets.push_back(threat);
  (void)savasan::runners::WriteRivalPoolFile(apf_pool);

  const auto apf_now = std::chrono::steady_clock::now();
  {
    std::lock_guard<std::mutex> lk(rt_apf.mutex);
    rt_apf.guidance.rival_pool = apf_pool;
  }

  const int before_apf = bridge->seyir_count;
  const auto ta0 = apf_now + std::chrono::milliseconds(20);
  (void)savasan::runners::RunGuidanceControlStep(&rt_apf, ta0);
  const auto ta1 = ta0 + std::chrono::milliseconds(20);
  if (!savasan::runners::RunGuidanceControlStep(&rt_apf, ta1)) {
    return 10;
  }
  if (!rt_apf.guidance.apf_active.load()) {
    return 11;
  }
  if (bridge->seyir_count <= before_apf) {
    return 12;
  }
  if (rt_apf.guidance.seyir_phase != savasan::runners::SeyirPhase::kKacinma) {
    return 13;
  }
  if (bridge->last_seyir.bytes[3] == savasan::autopilot::seyir::kIstemHeading) {
    return 22;
  }

  // 5) Telemetri yaklaşma: fake rakip → istem 2 koordinat.
  savasan::runners::Phase5Runtime rt_lead{};
  SetupRuntimeBase(rt_lead, bridge);
  savasan::runners::RivalPoolSnapshot pool{};
  savasan::runners::RivalTargetSnapshot rival{};
  rival.valid = true;
  rival.target_id = 7;
  rival.lat_deg = 41.002;
  rival.lon_deg = 36.0;
  rival.alt_m = 100.0f;
  rival.spd_mps = 12.0f;
  rival.hdg_deg = 90.0f;
  pool.targets.push_back(rival);
  (void)savasan::runners::WriteRivalPoolFile(pool);
  rt_lead.guidance.latest_lock_valid = false;

  const int before_lead = bridge->seyir_count;
  const auto tl0 = ta1 + std::chrono::milliseconds(20);
  (void)savasan::runners::RunGuidanceControlStep(&rt_lead, tl0);
  const auto tl1 = tl0 + std::chrono::milliseconds(20);
  if (!savasan::runners::RunGuidanceControlStep(&rt_lead, tl1)) {
    return 14;
  }
  if (bridge->seyir_count <= before_lead) {
    return 15;
  }
  if (rt_lead.guidance.seyir_phase != savasan::runners::SeyirPhase::kYaklasma &&
      rt_lead.guidance.seyir_phase != savasan::runners::SeyirPhase::kArama) {
    return 16;
  }

  // 6) Degrade aktifken güdüm üretilmez.
  savasan::runners::Phase5Runtime rt_deg{};
  SetupRuntimeBase(rt_deg, bridge);
  rt_deg.guidance.latest_lock_valid = true;
  rt_deg.tracker.lock_metrics.Update(true, 0.62f, 0.48f, 0);
  rt_deg.health.degraded.active.store(true);
  const int before_deg = bridge->seyir_count;
  const auto td0 = tl1 + std::chrono::milliseconds(20);
  if (savasan::runners::RunGuidanceControlStep(&rt_deg, td0)) {
    return 17;
  }
  if (bridge->seyir_count != before_deg) {
    return 18;
  }

  return 0;
}
