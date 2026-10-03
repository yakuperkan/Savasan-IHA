/**
 * @file phase5_guidance.cpp
 * @brief Faz-5 Seyir Modu güdüm kontrol adımı ve otomatik tracker profil önerisi.
 */
#include "runners/phase5_guidance.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

#include "common/log.hpp"
#include "common/scope_profiler.hpp"
#include "evasion/potential_field_evasion.hpp"
#include "runners/phase5_mission_policy.hpp"
#include "runners/rival_telemetry_file.hpp"
#include "runners/bench_own_motion.hpp"
#include "runners/seyir_command_scheduler.hpp"
#include "runners/seyir_guidance.hpp"
#include "runners/telem_prediction_csv.hpp"

namespace savasan::runners {

namespace {

/// @brief Degrade modundan çıkmak için gereken ardışık başarılı seyir TX sayısı.
constexpr uint32_t kDegradedRecoverySuccessStreak = 3;

const char* ProfileToString(const TrackerProfileRecommendation p) {
  switch (p) {
    case TrackerProfileRecommendation::kAggressive:
      return "aggressive";
    case TrackerProfileRecommendation::kCalm:
      return "calm";
    default:
      return "default";
  }
}

TrackerProfileRecommendation DecideAutoRecommendation(
    const float speed_mps, const float lock_jitter,
    const TrackerProfileRecommendation previous, const AutoProfileConfig& cfg) {
  if (previous == TrackerProfileRecommendation::kDefault && speed_mps < 0.1f &&
      lock_jitter < 1e-3f) {
    return previous;
  }
  if (speed_mps >= cfg.aggressive_speed_mps || lock_jitter >= cfg.aggressive_jitter) {
    return TrackerProfileRecommendation::kAggressive;
  }
  if (speed_mps <= cfg.calm_speed_mps && lock_jitter <= cfg.calm_jitter) {
    return TrackerProfileRecommendation::kCalm;
  }
  return previous;
}

struct AutoTrackerProfileView {
  AutoProfileConfig cfg{};
  TrackerProfileRecommendation recommended = TrackerProfileRecommendation::kDefault;
  std::string active;
  std::chrono::steady_clock::time_point applied_tp{};
  std::chrono::steady_clock::time_point last_restart_request_tp{};
  std::chrono::steady_clock::time_point last_recommend_log_tp{};
  bool auto_restart = false;
  bool restart_requested = false;
  bool pipeline_healthy = false;
  bool telemetry_valid_recent = false;
  uint32_t comm_fail_count = 0;
  bool control_log_enable = true;
};

void UpdateAutoTrackerRecommendation(Phase5Runtime* rt,
                                     const std::chrono::steady_clock::time_point now,
                                     const float speed_mps, const float lock_jitter,
                                     const int frames_without_detection) {
  if (rt == nullptr || !rt->tracker.profile.auto_enabled) {
    return;
  }

  AutoTrackerProfileView view{};
  {
    std::lock_guard<std::mutex> lk(rt->mutex);
    view.cfg = rt->tracker.profile.auto_cfg;
    view.recommended = rt->tracker.profile.recommended;
    view.active = rt->tracker.profile.active;
    view.applied_tp = rt->tracker.profile.applied_tp;
    view.last_restart_request_tp = rt->tracker.profile.last_restart_request_tp;
    view.last_recommend_log_tp = rt->tracker.profile.last_recommend_log_tp;
    view.auto_restart = rt->tracker.profile.auto_restart;
    view.restart_requested = rt->tracker.profile.restart_requested.load();
    view.pipeline_healthy = rt->health.degraded.pipeline_healthy.load();
    view.telemetry_valid_recent = rt->health.degraded.telemetry_valid_recent.load();
    view.comm_fail_count = rt->health.degraded.comm_fail_count.load();
    view.control_log_enable = rt->guidance.control_log_enable.load();
  }

  const auto prev = view.recommended;
  const auto next = DecideAutoRecommendation(speed_mps, lock_jitter, prev, view.cfg);
  const bool recommend_changed =
      next != prev && (now - view.last_recommend_log_tp) >= view.cfg.recommend_cooldown;

  if (!recommend_changed) {
    if (view.control_log_enable &&
        common::ShouldLogEvery("phase5_auto_profile_watch", view.cfg.recommend_log_interval)) {
      SAVASAN_LOG_F(common::LogLevel::kInfo, "Faz5",
                    "AUTO tracker profile watch: current=%s speed_mps=%.3f jitter=%.3f miss=%d",
                    ProfileToString(prev), speed_mps, lock_jitter, frames_without_detection);
    }
    return;
  }

  const char* const next_profile = ProfileToString(next);
  const bool precheck_ok = view.pipeline_healthy && view.telemetry_valid_recent &&
                           (view.comm_fail_count <= view.cfg.restart_precheck_max_comm_fails) &&
                           (lock_jitter <= view.cfg.restart_precheck_max_jitter);
  const bool restart_eligible = view.auto_restart && !view.restart_requested &&
                                view.active != next_profile &&
                                (now - view.applied_tp) >= view.cfg.restart_min_dwell &&
                                (now - view.last_restart_request_tp) >= view.cfg.restart_cooldown;

  bool request_restart = false;
  bool precheck_failed = false;
  if (restart_eligible) {
    if (precheck_ok) {
      request_restart = true;
    } else {
      precheck_failed = true;
    }
  }

  {
    std::lock_guard<std::mutex> lk(rt->mutex);
    if (rt->tracker.profile.recommended != prev) {
      return;
    }
    rt->tracker.profile.recommended = next;
    rt->tracker.profile.last_recommend_log_tp = now;
    if (request_restart) {
      rt->tracker.profile.requested = next_profile;
      rt->tracker.profile.restart_requested.store(true);
      rt->tracker.profile.last_restart_request_tp = now;
    } else if (precheck_failed) {
      rt->tracker.profile.switch_health_failed = true;
      rt->tracker.profile.switch_health_fail_reason = "precheck_failed";
      rt->tracker.profile.switch_fail_count++;
      rt->tracker.profile.last_switch_tp = now;
    }
  }

  if (view.control_log_enable) {
    SAVASAN_LOG_F(common::LogLevel::kInfo, "Faz5",
                  "AUTO tracker profile onerisi: %s -> %s | speed_mps=%.3f jitter=%.3f miss=%d "
                  "(not: recommendation-only, runtime switch yok)",
                  ProfileToString(prev), next_profile, speed_mps, lock_jitter,
                  frames_without_detection);
  }
  if (request_restart && view.control_log_enable) {
    SAVASAN_LOG_F(common::LogLevel::kWarn, "Faz5",
                  "AUTO tracker restart istegi: active=%s target=%s "
                  "(pipeline controlled restart tetiklenecek)",
                  view.active.c_str(), next_profile);
  }
  if (precheck_failed && view.control_log_enable) {
    SAVASAN_LOG_F(common::LogLevel::kError, "Faz5",
                  "AUTO tracker restart iptal (pre-check fail): pipeline=%d telemetry=%d "
                  "comm_fail=%d jitter=%.3f -> restart atlandi",
                  view.pipeline_healthy ? 1 : 0, view.telemetry_valid_recent ? 1 : 0,
                  view.comm_fail_count, lock_jitter);
  }
}

bool SendSeyirOrDegrade(Phase5Runtime* rt, const savasan::autopilot::SeyirModePacket& pkt,
                        const bool dry_run) {
  if (rt == nullptr || rt->bridge == nullptr || !pkt.valid) {
    return false;
  }
  if (!rt->bridge->SendSeyirModeCommand(pkt, dry_run)) {
    std::lock_guard<std::mutex> lk(rt->mutex);
    const bool already_degraded = rt->health.degraded.active.load();
    rt->health.degraded.active.store(true);
    if (!already_degraded) {
      rt->health.degraded.comm_fail_count.fetch_add(1);
    }
    rt->health.degraded.comm_success_streak.store(0);
    return false;
  }
  if (!rt->health.degraded.active.load()) {
    rt->health.degraded.comm_fail_count.store(0);
  }
  const uint32_t streak = rt->health.degraded.comm_success_streak.fetch_add(1) + 1;
  if (rt->health.degraded.active.load() && streak >= kDegradedRecoverySuccessStreak) {
    std::lock_guard<std::mutex> lk(rt->mutex);
    rt->health.degraded.active.store(false);
    rt->health.degraded.comm_fail_count.store(0);
    rt->health.degraded.comm_success_streak.store(0);
  }
  return true;
}

void RefreshRfTelemetryCaches(Phase5Runtime* rt) {
  if (rt == nullptr) {
    return;
  }
  auto pool = ReadRivalPoolFile();
  auto hss = ReadHssRfFile();
  auto boundary = ReadBoundaryRfFile();
  // Nokta hedefi boş okuma da anlamlıdır: dosyanın silinmesi/valid=0 olması
  // operatörün iptal komutudur, o yüzden diğerlerinin aksine koşulsuz atanır.
  auto goto_target = ReadGotoFile();
  std::lock_guard<std::mutex> lk(rt->mutex);
  rt->guidance.goto_target = goto_target;
  if (!pool.targets.empty()) {
    rt->guidance.rival_pool = std::move(pool);
  }
  if (!hss.empty()) {
    rt->health.competition.hss_cache.zones = std::move(hss);
    rt->health.competition.hss_cache.last_update_tp = std::chrono::steady_clock::now();
  }
  if (!boundary.corners.empty()) {
    rt->guidance.boundary_rf = std::move(boundary);
  }
}

bool RunSeyirGuidanceStep(Phase5Runtime* rt, const std::chrono::steady_clock::time_point now,
                          const savasan::autopilot::TelemetrySnapshot& telem,
                          const bool lock_valid, const bool dry_run) {
  RefreshRfTelemetryCaches(rt);

  SeyirGuidanceConfig cfg{};
  RivalPoolSnapshot rivals{};
  BoundarySnapshot boundary{};
  std::vector<savasan::competition::HssKoordinatBilgisi> hss;
  GotoTargetSnapshot goto_target{};
  int vision_streak = 0;
  {
    std::lock_guard<std::mutex> lk(rt->mutex);
    cfg = rt->guidance.seyir_cfg;
    rivals = rt->guidance.rival_pool;
    boundary = rt->guidance.boundary_rf;
    goto_target = rt->guidance.goto_target;
    hss = rt->health.competition.hss_cache.zones;
    vision_streak = rt->guidance.seyir_vision_streak;
  }
  cfg.enabled = true;

  LockMetrics::Snapshot lock_snap{};
  {
    std::lock_guard<std::mutex> lk(rt->mutex);
    lock_snap = rt->tracker.lock_metrics.GetSnapshot();
  }

  const bool vision_hit = lock_valid && lock_snap.lock_valid;
  if (vision_hit) {
    vision_streak++;
  } else {
    vision_streak = 0;
  }

  const bool gps_valid =
      telem.valid && std::isfinite(telem.enlem) && std::isfinite(telem.boylam);

  SeyirGuidanceInput in{};
  in.t_s = std::chrono::duration<double>(now.time_since_epoch()).count();
  in.own_lat = telem.enlem;
  in.own_lon = telem.boylam;
  in.own_alt_m = telem.irtifa_m;
  in.own_yaw_deg = telem.yaw_deg;
  in.own_speed_mps = telem.gps_hiz_mps;
  in.gps_valid = gps_valid;
  // Duran uçakta GPS kaynaklı yön gürültülüdür; güdümün emniyet mantığı buna
  // dayandığı için yavaşken gidiş yönünü geçersiz sayıyoruz.
  in.own_yaw_valid = telem.valid && std::isfinite(telem.yaw_deg) && telem.gps_hiz_mps > 3.0f;
  const auto bench_own = ReadBenchOwnMotionFile();
  BenchOwnMotionSnapshot bench_mut = bench_own;
  ApplyBenchOwnMotionOverride(&bench_mut, &in.own_lat, &in.own_lon, &in.own_speed_mps,
                              &in.gps_valid);
  in.rivals = rivals;
  in.boundary = boundary;
  in.hss_zones = hss;
  in.goto_target = goto_target;
  in.vision_lock_valid = vision_hit;
  if (lock_snap.lock_valid) {
    in.vision_nx = lock_snap.hedef_norm_x;
    in.vision_ny = lock_snap.hedef_norm_y;
    in.vision_bbox_w = lock_snap.hedef_bbox_w_norm;
    in.vision_bbox_h = lock_snap.hedef_bbox_h_norm;
  }
  in.vision_streak = vision_streak;
  {
    std::lock_guard<std::mutex> lk(rt->mutex);
    in.lock_timer_active = rt->guidance.latest_av_lock_valid;
  }

  // seyir_state yalnızca bu tik tarafından kullanılır; kilit gerekmez.
  const auto decision = ComputeSeyirGuidance(cfg, in, &rt->guidance.seyir_state);
  AppendTelemPredictionCsv(in, decision, now);
  {
    std::lock_guard<std::mutex> lk(rt->mutex);
    rt->guidance.seyir_vision_streak = vision_streak;
    rt->guidance.seyir_phase = decision.phase;
    rt->guidance.seyir_selected_id = decision.selected_target_id;
    rt->guidance.goto_state = decision.goto_state;
    rt->guidance.goto_reason = decision.goto_reason;
    rt->guidance.goto_seq = decision.goto_seq;
  }

  if (rt->guidance.control_log_enable.load() &&
      common::ShouldLogEvery("phase5_goto", std::chrono::milliseconds(1000))) {
    if (decision.goto_state == GotoState::kActive) {
      SAVASAN_LOG_F(common::LogLevel::kInfo, "GOTO",
                    "[NOKTA] sira=%d hedef=%.7f,%.7f mesafe=%.0fm", decision.goto_seq,
                    goto_target.lat_deg, goto_target.lon_deg, decision.goto_distance_m);
    } else if (decision.goto_state == GotoState::kRejected) {
      SAVASAN_LOG_F(common::LogLevel::kWarn, "GOTO", "[NOKTA] sira=%d REDDEDILDI sebep=%d",
                    decision.goto_seq, static_cast<int>(decision.goto_reason));
    }
  }

  if (!decision.active || (!decision.has_coord && !decision.has_alt)) {
    rt->guidance.apf_active.store(false);
    return false;
  }

  // Kart bir tikte tek paket bekler; hangisinin gideceğine zamanlayıcı karar verir.
  SeyirScheduleResult scheduled{};
  {
    std::lock_guard<std::mutex> lk(rt->mutex);
    scheduled = SelectSeyirCommand(rt->guidance.seyir_scheduler_cfg, decision,
                                   &rt->guidance.seyir_scheduler, now);
  }

  if (rt->guidance.control_log_enable.load()) {
    if (decision.phase == SeyirPhase::kKacinma &&
        common::ShouldLogEvery("phase5_apf_kacis", std::chrono::milliseconds(100))) {
      SAVASAN_LOG_F(common::LogLevel::kInfo, "APF",
                    "[APF KACIS] F_toplam: %.3f | F_rakip: %.3f%s", decision.apf_forces.f_total,
                    decision.apf_forces.f_threat,
                    decision.apf_forces.local_minimum_escape ? " | LOCAL_MIN" : "");
    }
    if (decision.geofence_reason != evasion::GeofenceViolation::kNone &&
        common::ShouldLogEvery("phase5_geofence", std::chrono::milliseconds(500))) {
      SAVASAN_LOG_F(common::LogLevel::kWarn, "GEOFENCE",
                    "[GEOFENCE] sebep=%s hss_id=%d hedef=%.7f,%.7f",
                    evasion::GeofenceViolationName(decision.geofence_reason),
                    decision.geofence_hss_id, decision.aim_lat_deg, decision.aim_lon_deg);
    }
    if (decision.aim_rejected_by_geofence &&
        common::ShouldLogEvery("phase5_aim_reject", std::chrono::milliseconds(500))) {
      SAVASAN_LOG_F(common::LogLevel::kWarn, "GEOFENCE", "%s",
                    "[GEOFENCE] takip nisani saha disina/HSS'e dusuyor, komut uretilmedi");
    }
    if (common::ShouldLogEvery("phase5_seyir_gudum", std::chrono::milliseconds(200))) {
      SAVASAN_LOG_F(common::LogLevel::kInfo, "SEYIR",
                    "faz=%s asama=%s hedef_id=%d range=%.1f manevra=%.1f gorsel=%d kilit=%d "
                    "geo_kilit=%.1fs fren=%d band=%d gaz_hesap=%d sebep=%s istem=%u",
                    SeyirPhaseName(decision.phase),
                    control::PursuitStageName(decision.pursuit_stage),
                    decision.selected_target_id, decision.aim_range_m, decision.maneuver_dps,
                    decision.vision_trim_active ? 1 : 0, in.lock_timer_active ? 1 : 0,
                    decision.geom_lock_s, decision.safety_brake ? 1 : 0,
                    decision.band_guard_active ? 1 : 0, decision.desired_throttle_pct,
                    SeyirSendReasonName(scheduled.reason),
                    scheduled.send && scheduled.packet.length > 3 ? scheduled.packet.bytes[3]
                                                                  : 0U);
    }
  }

  rt->guidance.apf_active.store(decision.phase == SeyirPhase::kKacinma);

  // Kadans freni tuttuğunda güdüm yine de aktiftir; bu tik sessiz geçer.
  if (!scheduled.send) {
    rt->guidance.guidance_active_prev.store(true);
    return true;
  }

  if (!SendSeyirOrDegrade(rt, scheduled.packet, dry_run)) {
    return false;
  }
  {
    std::lock_guard<std::mutex> lk(rt->mutex);
    rt->guidance.last_valid_seyir = scheduled.packet;
    rt->guidance.has_last_valid_seyir = true;
    rt->guidance.last_valid_seyir_tp = now;
  }
  rt->guidance.guidance_active_prev.store(true);
  return true;
}

}  // namespace

bool RunGuidanceControlStep(Phase5Runtime* rt, const std::chrono::steady_clock::time_point now) {
  if (rt == nullptr || rt->bridge == nullptr || rt->guidance.state_estimator == nullptr) {
    if (common::ShouldLogEvery("phase5_guidance_null_deps", std::chrono::seconds(5))) {
      SAVASAN_LOG_F(common::LogLevel::kError, "Faz5",
                    "RunGuidanceControlStep atlandi: rt=%d bridge=%d vse=%d", rt != nullptr,
                    rt != nullptr && rt->bridge != nullptr,
                    rt != nullptr && rt->guidance.state_estimator != nullptr);
    }
    return false;
  }
  if (!MissionAllowsAirLockSerialTx(rt->guidance.mission_mode.load())) {
    rt->guidance.guidance_active_prev.store(false);
    rt->guidance.apf_active.store(false);
    return false;
  }

  SAVASAN_PROFILE_SCOPE("GuidanceControlStep");

  const auto telem = rt->bridge->GetTelemetrySnapshot();
  control::VehicleStateEstimator::TelemetryInput tin{};
  tin.valid = telem.valid;
  tin.vel_x_mps = telem.vel_x_mps;
  tin.vel_y_mps = telem.vel_y_mps;
  tin.vel_z_mps = telem.vel_z_mps;
  tin.yaw_rate_dps = telem.yaw_rate_dps;
  rt->health.degraded.telemetry_valid_recent.store(tin.valid);
  rt->guidance.state_estimator->UpdateTelemetry(tin);
  rt->guidance.state_estimator->Step(now);
  const auto vehicle = rt->guidance.state_estimator->GetState();
  const float speed_mps = std::sqrt(vehicle.vel_x_mps * vehicle.vel_x_mps +
                                    vehicle.vel_y_mps * vehicle.vel_y_mps +
                                    vehicle.vel_z_mps * vehicle.vel_z_mps);

  LockMetrics::Snapshot lock_metrics{};
  bool lock_valid = false;
  {
    std::lock_guard<std::mutex> lk(rt->mutex);
    lock_metrics = rt->tracker.lock_metrics.GetSnapshot();
    lock_valid = rt->guidance.latest_lock_valid;
    rt->tracker.profile.jitter_sum += static_cast<double>(lock_metrics.jitter);
    rt->tracker.profile.miss_sum +=
        static_cast<uint64_t>(std::max(lock_metrics.frames_without_detection, 0));
    if (lock_metrics.lock_valid) {
      rt->tracker.profile.lock_valid_samples++;
    }
    rt->tracker.profile.metric_samples++;
  }

  UpdateAutoTrackerRecommendation(rt, now, speed_mps, lock_metrics.jitter,
                                  lock_metrics.frames_without_detection);

  const bool dry_run = !rt->guidance.seyir_tx_enable.load();

  if (rt->health.degraded.active.load()) {
    rt->guidance.apf_active.store(false);
    if (!rt->guidance.send_only_on_lock.load()) {
      // Degrade: son geçerli paketi kadans freniyle tekrarla ki kart akış kesintisi
      // görüp hedef irtifayı unutmasın.
      savasan::autopilot::SeyirModePacket repeat{};
      bool do_repeat = false;
      {
        std::lock_guard<std::mutex> lk(rt->mutex);
        auto& sch = rt->guidance.seyir_scheduler;
        const float since_s =
            sch.has_sent ? std::chrono::duration<float>(now - sch.last_send_tp).count()
                         : std::numeric_limits<float>::max();
        if (rt->guidance.has_last_valid_seyir &&
            since_s >= rt->guidance.seyir_scheduler_cfg.min_interval_s) {
          repeat = rt->guidance.last_valid_seyir;
          do_repeat = true;
          sch.has_sent = true;
          sch.last_send_tp = now;
        }
      }
      if (do_repeat) {
        (void)SendSeyirOrDegrade(rt, repeat, dry_run);
      }
    }
    if (rt->health.degraded.active.load()) {
      rt->guidance.guidance_active_prev.store(false);
      return false;
    }
  }

  if (RunSeyirGuidanceStep(rt, now, telem, lock_valid, dry_run)) {
    return true;
  }
  rt->guidance.apf_active.store(false);
  rt->guidance.guidance_active_prev.store(false);
  return false;
}

}  // namespace savasan::runners
