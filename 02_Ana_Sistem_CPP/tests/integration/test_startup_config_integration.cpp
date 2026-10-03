#include "app/startup_config.hpp"

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
  const std::string pgi_cfg_default =
      std::string(SAVASAN_SOURCE_DIR) + "/tests/fixtures/pgi_config_test_stub.txt";
  const std::string tracker_cfg_default =
      std::string(SAVASAN_SOURCE_DIR) + "/config/deepstream/tracker_config.yml";
  const std::string tracker_cfg_aggressive =
      std::string(SAVASAN_SOURCE_DIR) + "/config/deepstream/tracker_config_aggressive.yml";

  // Scenario 1: explicit tracker config path should be honored.
  SetEnv("SAVASAN_V4L2_DEVICE", "/dev/null");
  SetEnv("SAVASAN_RUN_SECONDS", "15");
  SetEnv("SAVASAN_PGI_CONFIG", pgi_cfg_default.c_str());
  SetEnv("SAVASAN_TRACKER_LIB", "/dev/null");
  SetEnv("SAVASAN_TRACKER_CONFIG", tracker_cfg_default.c_str());

  char arg0[] = "savasan_iha";
  char arg1[] = "phase4";
  char arg2[] = "usb";
  char arg3[] = "hybrid";
  char* argv[] = {arg0, arg1, arg2, arg3};

  savasan::app::StartupConfig cfg{};
  std::string error;
  const bool ok = savasan::app::ParseStartupConfig(4, argv, &cfg, &error);

  UnsetEnv("SAVASAN_V4L2_DEVICE");
  UnsetEnv("SAVASAN_RUN_SECONDS");
  UnsetEnv("SAVASAN_PGI_CONFIG");
  UnsetEnv("SAVASAN_TRACKER_LIB");
  UnsetEnv("SAVASAN_TRACKER_CONFIG");

  if (!ok) {
    return 1;
  }
  if (cfg.phase != 4) {
    return 2;
  }
  if (!cfg.phase4_hybrid) {
    return 3;
  }
  if (cfg.run_seconds != 15) {
    return 4;
  }
  if (cfg.tracker_profile != "default") {
    return 6;
  }

  // Scenario 2: tracker profile should select profile-specific default.
  SetEnv("SAVASAN_V4L2_DEVICE", "/dev/null");
  SetEnv("SAVASAN_RUN_SECONDS", "10");
  SetEnv("SAVASAN_PGI_CONFIG", pgi_cfg_default.c_str());
  SetEnv("SAVASAN_TRACKER_LIB", "/dev/null");
  SetEnv("SAVASAN_TRACKER_PROFILE", "aggressive");
  UnsetEnv("SAVASAN_TRACKER_CONFIG");

  char argb0[] = "savasan_iha";
  char argb1[] = "phase3";
  char argb2[] = "usb";
  char* argv_b[] = {argb0, argb1, argb2};

  savasan::app::StartupConfig cfg2{};
  std::string error2;
  const bool ok2 = savasan::app::ParseStartupConfig(3, argv_b, &cfg2, &error2);

  UnsetEnv("SAVASAN_V4L2_DEVICE");
  UnsetEnv("SAVASAN_RUN_SECONDS");
  UnsetEnv("SAVASAN_PGI_CONFIG");
  UnsetEnv("SAVASAN_TRACKER_LIB");
  UnsetEnv("SAVASAN_TRACKER_PROFILE");

  if (!ok2) {
    return 7;
  }
  if (cfg2.tracker_config != tracker_cfg_aggressive) {
    return 8;
  }
  if (cfg2.tracker_profile != "aggressive") {
    return 9;
  }

  // Scenario 3: auto profile should parse and keep default tracker file.
  SetEnv("SAVASAN_V4L2_DEVICE", "/dev/null");
  SetEnv("SAVASAN_RUN_SECONDS", "10");
  SetEnv("SAVASAN_PGI_CONFIG", pgi_cfg_default.c_str());
  SetEnv("SAVASAN_TRACKER_LIB", "/dev/null");
  SetEnv("SAVASAN_TRACKER_PROFILE", "auto");
  UnsetEnv("SAVASAN_TRACKER_CONFIG");

  char argc0[] = "savasan_iha";
  char argc1[] = "phase3";
  char argc2[] = "usb";
  char* argv_c[] = {argc0, argc1, argc2};

  savasan::app::StartupConfig cfg3{};
  std::string error3;
  const bool ok3 = savasan::app::ParseStartupConfig(3, argv_c, &cfg3, &error3);

  UnsetEnv("SAVASAN_V4L2_DEVICE");
  UnsetEnv("SAVASAN_RUN_SECONDS");
  UnsetEnv("SAVASAN_PGI_CONFIG");
  UnsetEnv("SAVASAN_TRACKER_LIB");
  UnsetEnv("SAVASAN_TRACKER_PROFILE");

  if (!ok3) {
    return 10;
  }
  if (cfg3.tracker_profile != "auto") {
    return 11;
  }
  if (cfg3.tracker_config != tracker_cfg_default) {
    return 12;
  }

  // Scenario 4: bilinmeyen profil adi default'a duser (uyari loglanir).
  SetEnv("SAVASAN_V4L2_DEVICE", "/dev/null");
  SetEnv("SAVASAN_RUN_SECONDS", "10");
  SetEnv("SAVASAN_PGI_CONFIG", pgi_cfg_default.c_str());
  SetEnv("SAVASAN_TRACKER_LIB", "/dev/null");
  SetEnv("SAVASAN_TRACKER_PROFILE", "agressive_typo");
  UnsetEnv("SAVASAN_TRACKER_CONFIG");

  char argd0[] = "savasan_iha";
  char argd1[] = "phase3";
  char argd2[] = "usb";
  char* argv_d[] = {argd0, argd1, argd2};

  savasan::app::StartupConfig cfg4{};
  std::string error4;
  const bool ok4 = savasan::app::ParseStartupConfig(3, argv_d, &cfg4, &error4);

  UnsetEnv("SAVASAN_V4L2_DEVICE");
  UnsetEnv("SAVASAN_RUN_SECONDS");
  UnsetEnv("SAVASAN_PGI_CONFIG");
  UnsetEnv("SAVASAN_TRACKER_LIB");
  UnsetEnv("SAVASAN_TRACKER_PROFILE");

  if (!ok4) {
    return 13;
  }
  if (cfg4.tracker_profile != "default") {
    return 14;
  }
  if (cfg4.tracker_config != tracker_cfg_default) {
    return 15;
  }

  // Scenario 5: bilinmeyen faz argumani Faz 1'e duser (uyari loglanir).
  SetEnv("SAVASAN_V4L2_DEVICE", "/dev/null");
  SetEnv("SAVASAN_RUN_SECONDS", "10");
  SetEnv("SAVASAN_PGI_CONFIG", pgi_cfg_default.c_str());

  char arge0[] = "savasan_iha";
  char arge1[] = "phase55";
  char arge2[] = "usb";
  char* argv_e[] = {arge0, arge1, arge2};

  savasan::app::StartupConfig cfg5{};
  std::string error5;
  const bool ok5 = savasan::app::ParseStartupConfig(3, argv_e, &cfg5, &error5);

  UnsetEnv("SAVASAN_V4L2_DEVICE");
  UnsetEnv("SAVASAN_RUN_SECONDS");
  UnsetEnv("SAVASAN_PGI_CONFIG");

  if (!ok5) {
    return 16;
  }
  if (cfg5.phase != 1) {
    return 17;
  }

  return 0;
}
