#include "common/config_path.hpp"

#include <cstdlib>
#include <string>

#ifndef SAVASAN_SOURCE_DIR
#define SAVASAN_SOURCE_DIR "."
#endif

namespace {

void SetEnv(const char* key, const char* value) { ::setenv(key, value, 1); }
void UnsetEnv(const char* key) { ::unsetenv(key); }

}  // namespace

int main() {
  constexpr char kRel[] = "config/deepstream/tracker_config.yml";

  UnsetEnv("SAVASAN_TRACKER_CONFIG");
  UnsetEnv("SAVASAN_CONFIG_ROOT");

  const std::string fallback =
      savasan::common::ResolveConfigPath("SAVASAN_TRACKER_CONFIG", kRel);
  const std::string expected_fallback =
      std::string(SAVASAN_SOURCE_DIR) + "/config/deepstream/tracker_config.yml";
  if (fallback != expected_fallback) {
    return 1;
  }

  SetEnv("SAVASAN_CONFIG_ROOT", "/opt/savasan");
  const std::string from_root =
      savasan::common::ResolveConfigPath("SAVASAN_TRACKER_CONFIG", kRel);
  UnsetEnv("SAVASAN_CONFIG_ROOT");
  if (from_root != "/opt/savasan/config/deepstream/tracker_config.yml") {
    return 2;
  }

  SetEnv("SAVASAN_TRACKER_CONFIG", "/custom/tracker.yml");
  const std::string from_env =
      savasan::common::ResolveConfigPath("SAVASAN_TRACKER_CONFIG", kRel);
  UnsetEnv("SAVASAN_TRACKER_CONFIG");
  if (from_env != "/custom/tracker.yml") {
    return 3;
  }

  if (savasan::common::DeepstreamConfigRelPath("tracker_config_calm.yml") !=
      "config/deepstream/tracker_config_calm.yml") {
    return 4;
  }

  return 0;
}
