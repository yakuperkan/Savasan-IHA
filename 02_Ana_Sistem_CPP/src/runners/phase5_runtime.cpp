/**
 * @file phase5_runtime.cpp
 * @brief @ref savasan::runners::Phase5Runtime üye fonksiyonlarının uygulaması.
 */
#include "runners/phase5_runtime.hpp"

#include <cstdlib>
#include <cstring>

#include "evasion/potential_field_evasion.hpp"
#include "runners/phase5_callbacks.hpp"
#include "runners/phase5_runtime_options.hpp"

namespace savasan::runners {

Phase5Runtime::~Phase5Runtime() {
  StopMissionModeWorker(this);
}

void Phase5Runtime::ApplyRuntimeFlags(const Phase5RuntimeFlags& flags) {
  guidance.seyir_tx_enable.store(flags.seyir_tx_enable);
  guidance.control_log_enable.store(flags.control_log_enable);
  guidance.send_only_on_lock.store(flags.send_only_on_lock);
  guidance.mission_mode.store(flags.initial_mission_mode);
  guidance.mission_mode_dynamic_enabled.store(flags.mission_mode_dynamic_enabled);
  guidance.mission_mode_file = flags.mission_mode_file;
  guidance.mission_mode_poll_interval_ms = flags.mission_mode_poll_interval_ms;

  guidance.seyir_cfg.enabled = flags.guidance_enabled;
  guidance.seyir_cfg.apf_config = evasion::LoadPotentialFieldConfigFromEnv();

  auto envf = [](const char* k, float def) -> float {
    const char* v = std::getenv(k);
    if (v == nullptr || v[0] == '\0') {
      return def;
    }
    return static_cast<float>(std::atof(v));
  };
  auto envi = [](const char* k, int def) -> int {
    const char* v = std::getenv(k);
    if (v == nullptr || v[0] == '\0') {
      return def;
    }
    return std::atoi(v);
  };

  guidance.seyir_cfg.own_cruise_speed_mps = envf("SAVASAN_SEYIR_CRUISE_MPS", 20.0f);
  guidance.seyir_cfg.rival_stale_max_s = envf("SAVASAN_SEYIR_RIVAL_STALE_S", 3.0f);
  guidance.seyir_cfg.approach_enter_m = envf("SAVASAN_SEYIR_APPROACH_M", 800.0f);
  guidance.seyir_cfg.visual_bbox_min = envf("SAVASAN_SEYIR_VISUAL_BBOX_MIN", 0.04f);
  guidance.seyir_cfg.visual_confirm_frames = envi("SAVASAN_SEYIR_VISUAL_FRAMES", 3);
  // Yasak bölge yönetimi geofence'te: HSS payı ve sınır payı buradan gelir.
  guidance.seyir_cfg.geofence.hss_margin_m = envf("SAVASAN_GEOFENCE_HSS_MARGIN_M", 20.0f);
  guidance.seyir_cfg.geofence.boundary_margin_m =
      envf("SAVASAN_GEOFENCE_BOUNDARY_MARGIN_M", 30.0f);
  guidance.seyir_cfg.geofence.band_min_m = envf("SAVASAN_GEOFENCE_BAND_MIN_M", 60.0f);
  guidance.seyir_cfg.geofence.band_multiplier = envf("SAVASAN_GEOFENCE_BAND_MULT", 1.3f);
  guidance.seyir_cfg.default_pitch_deg = envi("SAVASAN_SEYIR_PITCH_DEG", 8);
  guidance.seyir_cfg.heading_kp = envf("SAVASAN_SEYIR_HEADING_KP", 1.0f);
  guidance.seyir_cfg.vision_fov_deg = envf("SAVASAN_SEYIR_VISION_FOV_DEG", 60.0f);
  guidance.seyir_cfg.vision_trim_max_deg = envf("SAVASAN_SEYIR_VISION_TRIM_MAX_DEG", 25.0f);
  guidance.seyir_cfg.altitude_kp_m = envf("SAVASAN_SEYIR_ALT_KP_M", 30.0f);
  guidance.seyir_cfg.min_alt_m = envf("SAVASAN_SEYIR_MIN_ALT_M", 35.0f);
  guidance.seyir_cfg.max_alt_m = envf("SAVASAN_SEYIR_MAX_ALT_M", 500.0f);
  guidance.seyir_cfg.apf_escape_lead_m = envf("SAVASAN_SEYIR_APF_LEAD_M", 300.0f);

  // Kestirim: boru gecikmesi sahaya göre ayarlanır, kazançlar MSO ölçümüdür.
  guidance.seyir_cfg.estimator.pipeline_delay_s = envf("SAVASAN_SEYIR_PIPE_DELAY_S", 0.6f);

  // Takip güdümü: sahada en çok oynanacak üç mesafe eşiği env'e açıldı.
  guidance.seyir_cfg.pursuit.nominal_range_m = envf("SAVASAN_SEYIR_NOMINAL_RANGE_M", 28.0f);
  guidance.seyir_cfg.pursuit.max_track_m = envf("SAVASAN_SEYIR_MAX_TRACK_M", 900.0f);
  guidance.seyir_cfg.pursuit.aim_lead_s = envf("SAVASAN_SEYIR_AIM_LEAD_S", 0.8f);

  // Kilit bandı koruması: 0 ile kapatılır (rakibe sokulmaya izin verilir).
  guidance.seyir_cfg.goto_enabled = envi("SAVASAN_SEYIR_GOTO_ENABLE", 1) != 0;
  guidance.seyir_cfg.goto_arrive_radius_m = envf("SAVASAN_SEYIR_GOTO_ARRIVE_M", 40.0f);
  guidance.seyir_cfg.band_min_separation_m = envf("SAVASAN_SEYIR_BAND_MIN_SEP_M", 25.0f);
  guidance.seyir_cfg.band_break_angle_deg = envf("SAVASAN_SEYIR_BAND_BREAK_DEG", 25.0f);
  guidance.seyir_cfg.band_guard_enabled = guidance.seyir_cfg.band_min_separation_m > 0.0f;

  // Vendor uçan kodu int32 mikro-derece yazar; float32 yalnızca PDF örneğidir ve
  // saha A/B'si için açıkça istenmelidir.
  const char* fmt = std::getenv("SAVASAN_SEYIR_COORD_FORMAT");
  if (fmt != nullptr && std::strcmp(fmt, "float32") == 0) {
    guidance.seyir_cfg.coord_format =
        savasan::autopilot::seyir::LatLonWireFormat::kFloat32Degrees;
  } else {
    guidance.seyir_cfg.coord_format =
        savasan::autopilot::seyir::LatLonWireFormat::kInt32MicroDegrees;
  }

  // İstem 2 kuyruk varyantı: iki uçmuş vendor dosyası farklı yere yazıyor.
  const char* tail = std::getenv("SAVASAN_SEYIR_CMD2_TAIL");
  if (tail != nullptr && std::strcmp(tail, "13") == 0) {
    guidance.seyir_cfg.cmd2_tail = savasan::autopilot::seyir::Cmd2TailVariant::kTailAt13;
  } else {
    guidance.seyir_cfg.cmd2_tail = savasan::autopilot::seyir::Cmd2TailVariant::kTailAt12;
  }

  guidance.seyir_scheduler_cfg.min_interval_s = envf("SAVASAN_SEYIR_MIN_INTERVAL_S", 0.2f);
  guidance.seyir_scheduler_cfg.flow_gap_s = envf("SAVASAN_SEYIR_FLOW_GAP_S", 2.0f);
  guidance.seyir_scheduler_cfg.alt_epsilon_m = envi("SAVASAN_SEYIR_ALT_EPS_M", 2);
  guidance.seyir_scheduler_cfg.alt_refresh_s = envf("SAVASAN_SEYIR_ALT_REFRESH_S", 3.0f);
}

}  // namespace savasan::runners
