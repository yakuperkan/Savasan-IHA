/**
 * @file phase5_runner_config.cpp
 * @brief Faz-5 çalıştırıcı ortam yapılandırması uygulaması.
 */
#include "runners/phase5_runner_config.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "common/log.hpp"

namespace savasan::runners {

namespace {

constexpr float kMinSpeedHysteresisGapMps = 1.0f;
constexpr float kMinJitterHysteresisGap = 0.05f;

std::string EnvOrDefault(const char* env_var, const char* fallback) {
  const char* v = std::getenv(env_var);
  if (v != nullptr && v[0] != '\0') {
    return std::string(v);
  }
  return std::string(fallback);
}

float EnvFloatClamped(const char* key, const float fallback, const float min_v,
                      const float max_v) {
  const char* raw = std::getenv(key);
  if (raw == nullptr || raw[0] == '\0') return fallback;
  char* end = nullptr;
  const float v = std::strtof(raw, &end);
  if (end == raw) return fallback;
  return std::clamp(v, min_v, max_v);
}

int EnvIntClamped(const char* key, const int fallback, const int min_v, const int max_v) {
  const char* raw = std::getenv(key);
  if (raw == nullptr || raw[0] == '\0') return fallback;
  char* end = nullptr;
  const long v = std::strtol(raw, &end, 10);
  if (end == raw) return fallback;
  return static_cast<int>(std::clamp(v, static_cast<long>(min_v), static_cast<long>(max_v)));
}

}  // namespace

bool NormalizeAutoProfileHysteresis(AutoProfileConfig* cfg) {
  if (cfg == nullptr) {
    return false;
  }
  bool adjusted = false;

  const float max_calm_speed = cfg->aggressive_speed_mps - kMinSpeedHysteresisGapMps;
  if (cfg->calm_speed_mps > max_calm_speed) {
    const float previous = cfg->calm_speed_mps;
    cfg->calm_speed_mps = std::max(0.0f, max_calm_speed);
    common::Log(common::LogLevel::kWarn, "Phase5",
                "AUTO profil histerezis: calm_speed_mps " + std::to_string(previous) +
                    " -> " + std::to_string(cfg->calm_speed_mps) +
                    " (aggressive_speed_mps=" + std::to_string(cfg->aggressive_speed_mps) +
                    ", min_gap=" + std::to_string(kMinSpeedHysteresisGapMps) + ")");
    adjusted = true;
  }

  const float max_calm_jitter = cfg->aggressive_jitter - kMinJitterHysteresisGap;
  if (cfg->calm_jitter > max_calm_jitter) {
    const float previous = cfg->calm_jitter;
    cfg->calm_jitter = std::max(0.0f, max_calm_jitter);
    common::Log(common::LogLevel::kWarn, "Phase5",
                "AUTO profil histerezis: calm_jitter " + std::to_string(previous) +
                    " -> " + std::to_string(cfg->calm_jitter) +
                    " (aggressive_jitter=" + std::to_string(cfg->aggressive_jitter) +
                    ", min_gap=" + std::to_string(kMinJitterHysteresisGap) + ")");
    adjusted = true;
  }

  return adjusted;
}

AutoProfileConfig ReadAutoProfileConfigFromEnv() {
  AutoProfileConfig c{};
  c.aggressive_speed_mps = EnvFloatClamped("SAVASAN_AUTO_AGGRESSIVE_SPEED_MPS",
                                           c.aggressive_speed_mps, 0.1f, 100.0f);
  c.calm_speed_mps = EnvFloatClamped("SAVASAN_AUTO_CALM_SPEED_MPS", c.calm_speed_mps,
                                     0.0f, c.aggressive_speed_mps);
  c.aggressive_jitter = EnvFloatClamped("SAVASAN_AUTO_AGGRESSIVE_JITTER",
                                        c.aggressive_jitter, 0.0f, 2.0f);
  c.calm_jitter = EnvFloatClamped("SAVASAN_AUTO_CALM_JITTER", c.calm_jitter, 0.0f,
                                  c.aggressive_jitter);
  c.recommend_cooldown = std::chrono::seconds(
      EnvIntClamped("SAVASAN_AUTO_RECOMMEND_COOLDOWN_SEC",
                    static_cast<int>(c.recommend_cooldown.count()), 1, 120));
  c.recommend_log_interval = std::chrono::seconds(
      EnvIntClamped("SAVASAN_AUTO_WATCH_LOG_INTERVAL_SEC",
                    static_cast<int>(c.recommend_log_interval.count()), 1, 120));
  c.restart_min_dwell = std::chrono::seconds(
      EnvIntClamped("SAVASAN_AUTO_RESTART_MIN_DWELL_SEC",
                    static_cast<int>(c.restart_min_dwell.count()), 1, 300));
  c.restart_cooldown = std::chrono::seconds(
      EnvIntClamped("SAVASAN_AUTO_RESTART_COOLDOWN_SEC",
                    static_cast<int>(c.restart_cooldown.count()), 1, 300));
  c.restart_precheck_max_jitter = EnvFloatClamped(
      "SAVASAN_AUTO_PRECHECK_MAX_JITTER", c.restart_precheck_max_jitter, 0.01f, 5.0f);
  c.restart_precheck_max_comm_fails = static_cast<uint32_t>(
      EnvIntClamped("SAVASAN_AUTO_PRECHECK_MAX_COMM_FAILS",
                    static_cast<int>(c.restart_precheck_max_comm_fails), 0, 1000));
  c.restart_postcheck_timeout = std::chrono::seconds(
      EnvIntClamped("SAVASAN_AUTO_POSTCHECK_TIMEOUT_SEC",
                    static_cast<int>(c.restart_postcheck_timeout.count()), 1, 120));
  c.max_auto_restarts = static_cast<uint32_t>(
      EnvIntClamped("SAVASAN_AUTO_MAX_RESTARTS", static_cast<int>(c.max_auto_restarts), 1, 100));
  c.max_consecutive_failures = static_cast<uint32_t>(EnvIntClamped(
      "SAVASAN_AUTO_MAX_CONSECUTIVE_FAILS", static_cast<int>(c.max_consecutive_failures), 1, 20));
  c.max_failures_in_window = static_cast<uint32_t>(EnvIntClamped(
      "SAVASAN_AUTO_MAX_FAILS_IN_WINDOW", static_cast<int>(c.max_failures_in_window), 1, 50));
  c.failure_window = std::chrono::seconds(
      EnvIntClamped("SAVASAN_AUTO_FAIL_WINDOW_SEC", static_cast<int>(c.failure_window.count()),
                    5, 600));
  (void)NormalizeAutoProfileHysteresis(&c);
  return c;
}

savasan::autopilot::AlcLinkBridge::Config BuildAlcConfigFromEnv() {
  savasan::autopilot::AlcLinkBridge::Config alc_cfg;
  alc_cfg.device_path = EnvOrDefault("SAVASAN_ALC_DEVICE", "/dev/ttyACM0");
  const char* baud_env = std::getenv("SAVASAN_ALC_BAUD");
  if (baud_env != nullptr && baud_env[0] != '\0') {
    const int b = std::atoi(baud_env);
    if (b > 0) {
      alc_cfg.baud_rate = b;
    }
  }
  const char* lock_proto_env = std::getenv("SAVASAN_ALC_LOCK_PACKET_VERSION");
  if (lock_proto_env != nullptr && lock_proto_env[0] != '\0') {
    const int version = std::atoi(lock_proto_env);
    if (version == 2) {
      alc_cfg.lock_packet_version = 2;
    } else if (version != 1) {
      common::Log(common::LogLevel::kWarn, "Phase5",
                  std::string("SAVASAN_ALC_LOCK_PACKET_VERSION='") + lock_proto_env +
                      "' gecersiz; desteklenen: 1 veya 2. Varsayilan v1 kullaniliyor.");
    }
  }
  const char* no_lock_checksum_env = std::getenv("SAVASAN_ALC_NOLOCK_CHECKSUM_MODE");
  if (no_lock_checksum_env != nullptr && no_lock_checksum_env[0] != '\0') {
    if (std::strcmp(no_lock_checksum_env, "legacy_ff") == 0) {
      alc_cfg.no_lock_checksum_mode = 1;
    } else {
      alc_cfg.no_lock_checksum_mode = 0;
    }
  }
  return alc_cfg;
}

int ReadPhase5PipelineMaxRetriesFromEnv() {
  return EnvIntClamped("SAVASAN_PHASE5_PIPELINE_MAX_RETRIES", 2, 0, 5);
}

}  // namespace savasan::runners
