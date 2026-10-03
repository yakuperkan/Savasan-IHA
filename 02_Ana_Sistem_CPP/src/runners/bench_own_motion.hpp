/**
 * @file bench_own_motion.hpp
 * @brief Bench: sanal IHA konumu (lead pursuit testi).
 *
 * SAVASAN_BENCH_OWN_MOTION=1 ve SAVASAN_BENCH_OWN_FILE=/tmp/savasan_bench_own.env
 * scripts/fake_own_motion.py tarafindan yazilir; yalnizca gudum girdisini override eder.
 */
#ifndef SAVASAN_RUNNERS_BENCH_OWN_MOTION_HPP_
#define SAVASAN_RUNNERS_BENCH_OWN_MOTION_HPP_

#include <cstdlib>

#include "runners/rival_telemetry_file.hpp"

namespace savasan::runners {

inline constexpr const char* kDefaultBenchOwnFilePath = "/tmp/savasan_bench_own.env";

struct BenchOwnMotionSnapshot {
  bool enabled = false;
  bool valid = false;
  double lat_deg = 0.0;
  double lon_deg = 0.0;
  float speed_mps = 0.0f;
  float yaw_deg = 0.0f;
};

inline bool EnvBenchOwnMotionEnabled() {
  const char* e = std::getenv("SAVASAN_BENCH_OWN_MOTION");
  return e != nullptr && (e[0] == '1' || (e[0] == 't' && e[1] == 'r'));
}

inline BenchOwnMotionSnapshot ReadBenchOwnMotionFile() {
  BenchOwnMotionSnapshot out{};
  out.enabled = EnvBenchOwnMotionEnabled();
  if (!out.enabled) {
    return out;
  }
  const char* path = std::getenv("SAVASAN_BENCH_OWN_FILE");
  if (path == nullptr || path[0] == '\0') {
    path = kDefaultBenchOwnFilePath;
  }
  const auto m = detail::ParseEnvFile(path);
  if (m.empty()) {
    return out;
  }
  out.valid = detail::GetI(m, "valid", 0) != 0;
  if (!out.valid) {
    return out;
  }
  out.lat_deg = detail::GetD(m, "lat");
  out.lon_deg = detail::GetD(m, "lon");
  out.speed_mps = static_cast<float>(detail::GetD(m, "speed_mps", 22.0));
  out.yaw_deg = static_cast<float>(detail::GetD(m, "yaw_deg", 0.0));
  return out;
}

inline void ApplyBenchOwnMotionOverride(BenchOwnMotionSnapshot* bench, double* own_lat,
                                        double* own_lon, float* own_speed_mps, bool* gps_valid) {
  if (bench == nullptr || own_lat == nullptr || own_lon == nullptr || own_speed_mps == nullptr ||
      gps_valid == nullptr || !bench->enabled || !bench->valid) {
    return;
  }
  *own_lat = bench->lat_deg;
  *own_lon = bench->lon_deg;
  if (bench->speed_mps > 0.1f) {
    *own_speed_mps = bench->speed_mps;
  }
  *gps_valid = true;
}

}  // namespace savasan::runners

#endif  // SAVASAN_RUNNERS_BENCH_OWN_MOTION_HPP_
