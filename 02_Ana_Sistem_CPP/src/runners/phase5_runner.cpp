/**
 * @file phase5_runner.cpp
 * @brief Faz-5 üst seviye çalıştırıcı: ALC_LINK köprüsü, kontrolcüler ve otomatik
 *        tracker profili yeniden başlatma döngüsü.
 *
 * RunPhase5WithAlc, gerekli kontrolcüleri (dünya/araç kestirici, PID, kaçış) ve
 * yarışma istemcisini kurup RunPhase3'ü çağırır. Otomatik profil etkinse, dönüş
 * sonrası metrikleri toplar, sağlık doğrulaması yapar ve gerekirse yeni profille
 * pipeline'ı yeniden başlatır (kontrollü, sınırlı sayıda).
 */
#include "runners/phase5_runner.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include <string>

#include "autopilot/alc_link_bridge.hpp"
#include "control/vehicle_state_estimator.hpp"
#include "control/world_target_estimator.hpp"
#include "runners/gst_app_helpers.hpp"
#include "runners/phase3_runner.hpp"
#include "runners/phase5_callbacks.hpp"
#include "runners/mission_mode_utils.hpp"
#include "runners/phase5_runtime_options.hpp"
#include "runners/phase5_runner_config.hpp"
#include "common/config_path.hpp"
#include "common/log.hpp"

namespace savasan::runners {

namespace {

constexpr char kTrackerConfigDefaultRel[] = "config/deepstream/tracker_config.yml";       ///< Varsayılan profil config (göreli).
constexpr char kTrackerConfigAggressiveRel[] =
    "config/deepstream/tracker_config_aggressive.yml";                                     ///< Agresif profil config (göreli).
constexpr char kTrackerConfigCalmRel[] = "config/deepstream/tracker_config_calm.yml";      ///< Sakin profil config (göreli).

/// @brief Profil adına karşılık gelen tracker config yolunu çözer (env override'lı).
/// @param profile "aggressive"/"calm"/"default" veya bilinmeyen.
/// @param fallback_current Bilinmeyen profilde döndürülecek mevcut yol.
std::string TrackerConfigPathForProfile(const std::string& profile,
                                        const std::string& fallback_current) {
  if (profile == "aggressive") {
    return savasan::common::ResolveConfigPath("SAVASAN_TRACKER_CONFIG_AGGRESSIVE",
                                              kTrackerConfigAggressiveRel);
  }
  if (profile == "calm") {
    return savasan::common::ResolveConfigPath("SAVASAN_TRACKER_CONFIG_CALM",
                                              kTrackerConfigCalmRel);
  }
  if (profile == "default") {
    return savasan::common::ResolveConfigPath("SAVASAN_TRACKER_CONFIG_DEFAULT",
                                              kTrackerConfigDefaultRel);
  }
  return fallback_current;
}

/// @brief Yapılandırılan profil değerinden başlangıç aktif profilini belirler
/// ("auto" dahil bilinmeyenler "default" sayılır).
std::string ResolveInitialActiveProfile(const std::string& profile_env) {
  if (profile_env == "aggressive") return "aggressive";
  if (profile_env == "calm") return "calm";
  return "default";
}

/// @brief Profil adını istatistik dizisi indeksine çevirir (0=agresif, 1=sakin, 2=default).
int ProfileIndex(const std::string& profile) {
  if (profile == "aggressive") return 0;
  if (profile == "calm") return 1;
  return 2;
}

/// @brief Profil bazında biriktirilen kalite istatistikleri.
struct ProfileStats {
  double jitter_sum = 0.0;        ///< Toplam kilit titremesi.
  uint64_t miss_sum = 0;          ///< Toplam tespitsiz kare.
  uint32_t samples = 0;           ///< Örnek sayısı.
  uint32_t lock_valid_samples = 0;///< Geçerli kilit örnek sayısı.
};

/// @brief Profil yeniden başlatmaları arasında RF HSS önbelleğini taşır.
struct PersistedCompetitionState {
  HssCache hss_cache{};
};

void RestoreCompetitionState(PersistedCompetitionState& src, Phase5Runtime* dst) {
  if (dst == nullptr) {
    return;
  }
  dst->health.competition.hss_cache = src.hss_cache;
}

void SaveCompetitionState(Phase5Runtime* src, PersistedCompetitionState* dst) {
  if (src == nullptr || dst == nullptr) {
    return;
  }
  dst->hss_cache = src->health.competition.hss_cache;
}

/// @brief RunPhase3'ü geçici hatalarda sınırlı sayıda yeniden dener.
bool RunPhase3WithRetry(savasan::IngestConfig cfg, const std::string& pgi_config,
                        const std::string& tracker_config, const std::string& ll_lib,
                        savasan::deepstream::SinkType sink, const int run_seconds,
                        savasan::tracking::LockMode lock_mode, Phase5Runtime* phase5) {
  const int max_retries = ReadPhase5PipelineMaxRetriesFromEnv();
  for (int attempt = 0; attempt <= max_retries; ++attempt) {
    if (attempt > 0) {
      savasan::common::Log(savasan::common::LogLevel::kWarn, "Phase5",
                           "RunPhase3 yeniden deneme " + std::to_string(attempt) + "/" +
                               std::to_string(max_retries));
    }
    if (RunPhase3(cfg, pgi_config, tracker_config, ll_lib, sink, run_seconds, lock_mode,
                  phase5)) {
      return true;
    }
    if (attempt >= max_retries) {
      savasan::common::Log(savasan::common::LogLevel::kError, "Phase5",
                           "RunPhase3 tum denemeler basarisiz (" +
                               std::to_string(max_retries + 1) + " deneme)");
      return false;
    }
  }
  return false;
}

/// @brief Faz-5'e özgü kritik ortam bayraklarını tek satırda loglar.
void LogPhase5StartupBanner(const Phase5RuntimeFlags& flags, const bool alc_disabled,
                            const savasan::tracking::LockMode lock_mode) {
  const char* lock_label =
      (lock_mode == savasan::tracking::LockMode::kHybrid) ? "hybrid" : "baseline";
  std::string msg = std::string("lock_mode=") + lock_label + " seyir_tx=" +
                    (flags.seyir_tx_enable ? "1" : "0") + " mission_mode=" +
                    MissionModeName(flags.initial_mission_mode) + " alc_disable=" +
                    (alc_disabled ? "1" : "0") + " rivalry=rf guidance=" +
                    (flags.guidance_enabled ? "1" : "0") + " send_only_on_lock=" +
                    (flags.send_only_on_lock ? "1" : "0");
  if (flags.mission_mode_dynamic_enabled) {
    msg += " mission_mode_file=" + flags.mission_mode_file;
  }
  savasan::common::Log(savasan::common::LogLevel::kInfo, "Phase5", msg);
}

}  // namespace

// Faz-5 üst seviye giriş noktası (dosya başlığındaki akışı uygular).
bool RunPhase5WithAlc(savasan::IngestConfig cfg, const std::string& pgi_config,
                      const std::string& tracker_config, const std::string& ll_lib,
                      savasan::deepstream::SinkType sink, int run_seconds,
                      savasan::tracking::LockMode lock_mode) {
  const auto startup_flags = ReadPhase5RuntimeFlags();
  const char* alc_dis = std::getenv("SAVASAN_ALC_DISABLE");
  const bool alc_disabled = (alc_dis != nullptr && alc_dis[0] == '1');
  LogPhase5StartupBanner(startup_flags, alc_disabled, lock_mode);

  // SAVASAN_ALC_DISABLE=1: seri köprü olmadan yalnızca DeepStream + mission probe.
  if (alc_disabled) {
    Phase5Runtime p5;
    p5.ApplyRuntimeFlags(startup_flags);
    return RunPhase3WithRetry(cfg, pgi_config, tracker_config, ll_lib, sink, run_seconds,
                              lock_mode, &p5);
  }

  const savasan::autopilot::AlcLinkBridge::Config alc_cfg = BuildAlcConfigFromEnv();
  const char* tracker_profile_env = std::getenv("SAVASAN_TRACKER_PROFILE");
  const std::string configured_profile =
      (tracker_profile_env != nullptr && tracker_profile_env[0] != '\0')
          ? std::string(tracker_profile_env)
          : "default";
  const bool auto_profile = (configured_profile == "auto");
  const bool explicit_tracker_config =
      (std::getenv("SAVASAN_TRACKER_CONFIG") != nullptr &&
       std::getenv("SAVASAN_TRACKER_CONFIG")[0] != '\0');
  const bool auto_restart_enabled =
      (std::getenv("SAVASAN_TRACKER_AUTO_RESTART") != nullptr &&
       std::getenv("SAVASAN_TRACKER_AUTO_RESTART")[0] == '1');
  const AutoProfileConfig auto_cfg = ReadAutoProfileConfigFromEnv();

  if (auto_profile && auto_restart_enabled && explicit_tracker_config) {
    std::cout << "[Faz5] Uyari: SAVASAN_TRACKER_CONFIG explicit verildi, auto restart devre disi.\n";
  }

  std::string active_profile = ResolveInitialActiveProfile(configured_profile);
  std::string active_tracker_config = tracker_config;
  int restart_count = 0;
  uint32_t profile_switch_count = 0;
  uint32_t profile_switch_fail_count = 0;
  int consecutive_switch_failures = 0;
  std::deque<std::chrono::steady_clock::time_point> failure_window;
  bool pending_validation = false;
  std::string pending_validation_target;
  std::string pending_validation_fallback;
  ProfileStats stats_by_profile[3];
  PersistedCompetitionState persisted_competition;

  // Çalıştır-değerlendir-(gerekirse)yeniden başlat döngüsü: her tur tek bir
  // pipeline koşusudur; otomatik profil kapalıysa tek turda döner.
  while (true) {
    Phase5Runtime p5;
    p5.bridge = std::make_shared<savasan::autopilot::AlcLinkBridge>(alc_cfg);
    p5.guidance.state_estimator = std::make_shared<savasan::control::VehicleStateEstimator>();
    ConfigureVehicleStateEstimator(p5.guidance.state_estimator.get());
    p5.ApplyRuntimeFlags(startup_flags);
    RestoreCompetitionState(persisted_competition, &p5);
    p5.tracker.profile.configured = configured_profile;
    p5.tracker.profile.auto_enabled = auto_profile;
    p5.tracker.profile.auto_cfg = auto_cfg;
    p5.tracker.profile.auto_restart =
        auto_profile && auto_restart_enabled && !explicit_tracker_config;
    p5.tracker.profile.active = active_profile;
    p5.tracker.profile.applied_tp = std::chrono::steady_clock::now();
    p5.tracker.profile.validation_pending = pending_validation;
    p5.tracker.profile.validation_target = pending_validation_target;
    p5.tracker.profile.validation_fallback = pending_validation_fallback;
    p5.tracker.profile.validation_deadline =
        std::chrono::steady_clock::now() + p5.tracker.profile.auto_cfg.restart_postcheck_timeout;
    p5.tracker.profile.switch_count = profile_switch_count;
    p5.tracker.profile.switch_fail_count = profile_switch_fail_count;
    if (!startup_flags.guidance_enabled) {
      p5.guidance.state_estimator.reset();
    }

    const bool ok = RunPhase3WithRetry(cfg, pgi_config, active_tracker_config, ll_lib, sink,
                                       run_seconds, lock_mode, &p5);
    SaveCompetitionState(&p5, &persisted_competition);
    if (!ok) {
      return false;
    }

    // Koşu sonrası: profil kalite metriklerini özetle ve biriktir.
    if (p5.tracker.profile.metric_samples > 0) {
      const double avg_jitter = p5.tracker.profile.jitter_sum / static_cast<double>(p5.tracker.profile.metric_samples);
      const double avg_miss =
          static_cast<double>(p5.tracker.profile.miss_sum) / static_cast<double>(p5.tracker.profile.metric_samples);
      const double stability =
          static_cast<double>(p5.tracker.profile.lock_valid_samples) / static_cast<double>(p5.tracker.profile.metric_samples);
      std::cout << "[INFO] PROFILE_METRICS: " << active_profile << " stability=" << stability
                << " jitter=" << avg_jitter << " miss=" << avg_miss << '\n';
      ProfileStats& ps = stats_by_profile[ProfileIndex(active_profile)];
      ps.jitter_sum += p5.tracker.profile.jitter_sum;
      ps.miss_sum += p5.tracker.profile.miss_sum;
      ps.samples += p5.tracker.profile.metric_samples;
      ps.lock_valid_samples += p5.tracker.profile.lock_valid_samples;
    }

    // Bekleyen geçiş doğrulaması: sağlık başarısızsa hata sayaçlarını/penceresini
    // güncelle, başarılıysa ardışık hata sayacını sıfırla.
    if (pending_validation) {
      if (p5.tracker.profile.switch_health_failed || p5.tracker.profile.validation_pending) {
        ++profile_switch_fail_count;
        ++consecutive_switch_failures;
        const auto now = std::chrono::steady_clock::now();
        failure_window.push_back(now);
        while (!failure_window.empty() &&
               (now - failure_window.front()) > p5.tracker.profile.auto_cfg.failure_window) {
          failure_window.pop_front();
        }
        std::cout << "[TELEMETRY] PROFILE_SWITCH: count=" << profile_switch_count << " "
                  << pending_validation_fallback << "->" << pending_validation_target
                  << " success=false (" << (p5.tracker.profile.switch_health_fail_reason.empty()
                                                  ? "health-check failed"
                                                  : p5.tracker.profile.switch_health_fail_reason)
                  << ")\n";
      } else {
        consecutive_switch_failures = 0;
        std::cout << "[TELEMETRY] PROFILE_SWITCH: count=" << profile_switch_count << " "
                  << pending_validation_fallback << "->" << pending_validation_target
                  << " success=true\n";
      }
      pending_validation = false;
      pending_validation_target.clear();
      pending_validation_fallback.clear();
    }

    // Tekrarlayan hatalarda (ardışık veya pencere içi) otomatik yeniden başlatmayı kapat.
    bool disable_auto_restart_now = false;
    if (consecutive_switch_failures >=
        static_cast<int>(p5.tracker.profile.auto_cfg.max_consecutive_failures)) {
      disable_auto_restart_now = true;
    }
    if (failure_window.size() >= p5.tracker.profile.auto_cfg.max_failures_in_window) {
      disable_auto_restart_now = true;
    }
    if (disable_auto_restart_now) {
      std::cout << "[Faz5] Auto-restart disabled due to repeated failures; gorev sonlandiriliyor.\n";
      return true;
    }

    if (!p5.tracker.profile.auto_restart || !p5.tracker.profile.restart_requested.load()) {
      return true;
    }
    if (restart_count >= static_cast<int>(p5.tracker.profile.auto_cfg.max_auto_restarts)) {
      std::cout << "[Faz5] Auto tracker restart limiti asildi, mevcut profil ile devam.\n";
      return true;
    }
    if (p5.tracker.profile.requested.empty() || p5.tracker.profile.requested == active_profile) {
      return true;
    }

    // Yeni profile geç: config yolunu çöz, sayaçları artır ve sonraki tur için
    // doğrulamayı beklet.
    const std::string previous_profile = active_profile;
    active_profile = p5.tracker.profile.requested;
    active_tracker_config =
        TrackerConfigPathForProfile(active_profile, active_tracker_config);
    ++restart_count;
    ++profile_switch_count;
    pending_validation = true;
    pending_validation_target = active_profile;
    pending_validation_fallback = previous_profile;
    std::cout << "[Faz5] Auto tracker restart: yeni profil=" << active_profile
              << " config=" << active_tracker_config << '\n';
  }
}

}  // namespace savasan::runners
