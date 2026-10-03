/**
 * @file test_phase5_runner_config.cpp
 * @brief Faz-5 runner ortam yapılandırma yardımcıları birim testleri.
 */
#include "runners/phase5_runner_config.hpp"

#include <cmath>
#include <cstdlib>
#include <string>

namespace {

class ScopedEnv {
 public:
  ScopedEnv(const char* key, const char* value) : key_(key) {
    const char* prev = std::getenv(key);
    if (prev != nullptr) {
      had_prev_ = true;
      prev_ = prev;
    }
    if (value != nullptr) {
      setenv(key, value, 1);
    } else {
      unsetenv(key);
    }
  }
  ~ScopedEnv() {
    if (had_prev_) {
      setenv(key_, prev_.c_str(), 1);
    } else {
      unsetenv(key_);
    }
  }

 private:
  const char* key_;
  bool had_prev_ = false;
  std::string prev_;
};

bool NearlyEqual(const float a, const float b) {
  return std::fabs(a - b) < 1e-5f;
}

}  // namespace

int main() {
  {
    savasan::runners::AutoProfileConfig cfg{};
    cfg.aggressive_speed_mps = 8.0f;
    cfg.calm_speed_mps = 8.0f;
    cfg.aggressive_jitter = 0.20f;
    cfg.calm_jitter = 0.20f;
    if (!savasan::runners::NormalizeAutoProfileHysteresis(&cfg)) {
      return 1;
    }
    if (!NearlyEqual(cfg.calm_speed_mps, 7.0f)) {
      return 2;
    }
    if (!NearlyEqual(cfg.calm_jitter, 0.15f)) {
      return 3;
    }
  }

  {
    ScopedEnv aggressive("SAVASAN_AUTO_AGGRESSIVE_SPEED_MPS", "9");
    ScopedEnv calm("SAVASAN_AUTO_CALM_SPEED_MPS", "9");
    const auto cfg = savasan::runners::ReadAutoProfileConfigFromEnv();
    if (!NearlyEqual(cfg.aggressive_speed_mps, 9.0f)) {
      return 4;
    }
    if (!NearlyEqual(cfg.calm_speed_mps, 8.0f)) {
      return 5;
    }
  }

  {
    ScopedEnv retries("SAVASAN_PHASE5_PIPELINE_MAX_RETRIES", "4");
    if (savasan::runners::ReadPhase5PipelineMaxRetriesFromEnv() != 4) {
      return 6;
    }
  }

  {
    ScopedEnv lock_ver("SAVASAN_ALC_LOCK_PACKET_VERSION", "3");
    const auto cfg = savasan::runners::BuildAlcConfigFromEnv();
    if (cfg.lock_packet_version != 1) {
      return 7;
    }
  }

  {
    ScopedEnv lock_ver("SAVASAN_ALC_LOCK_PACKET_VERSION", "2");
    const auto cfg = savasan::runners::BuildAlcConfigFromEnv();
    if (cfg.lock_packet_version != 2) {
      return 8;
    }
  }

  return 0;
}
