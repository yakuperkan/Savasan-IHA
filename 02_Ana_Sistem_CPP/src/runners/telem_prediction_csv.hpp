/**
 * @file telem_prediction_csv.hpp
 * @brief Telemetri kestirimi / takip güdümü karşılaştırma CSV kaydı (bench + saha).
 *
 * SAVASAN_TELEM_PRED_CSV=/tmp/telem_pred.csv ile etkinleştirilir.
 * fake_rival_telemetry.py truth dosyası ile plot_telemetry_prediction.py üzerinden grafiklenir.
 *
 * @c throttle sütunu karta GİTMEYEN hesaplanmış gaz değeridir; gaz otopilota
 * bırakıldı. Sütun uçuş sonrası "gaz gönderseydik ne olurdu" analizi içindir.
 */
#ifndef SAVASAN_RUNNERS_TELEM_PREDICTION_CSV_HPP_
#define SAVASAN_RUNNERS_TELEM_PREDICTION_CSV_HPP_

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include "runners/seyir_guidance.hpp"

namespace savasan::runners {

namespace detail {

inline std::mutex& TelemPredCsvMutex() {
  static std::mutex m;
  return m;
}

inline FILE*& TelemPredCsvFile() {
  static FILE* fp = nullptr;
  return fp;
}

inline bool& TelemPredCsvHeaderWritten() {
  static bool written = false;
  return written;
}

inline const char* TelemPredCsvPath() {
  const char* p = std::getenv("SAVASAN_TELEM_PRED_CSV");
  return (p != nullptr && p[0] != '\0') ? p : nullptr;
}

}  // namespace detail

/// @brief YAKLASMA / telemetri yolunda konum alanlarını CSV'ye yazar (~20 Hz).
inline void AppendTelemPredictionCsv(const SeyirGuidanceInput& in,
                                     const SeyirGuidanceOutput& decision,
                                     const std::chrono::steady_clock::time_point now) {
  const char* path = detail::TelemPredCsvPath();
  if (path == nullptr) {
    return;
  }
  if (decision.selected_target_id < 0) {
    return;
  }
  if (!in.gps_valid) {
    return;
  }

  std::lock_guard<std::mutex> lk(detail::TelemPredCsvMutex());
  FILE*& fp = detail::TelemPredCsvFile();
  if (fp == nullptr) {
    fp = std::fopen(path, "w");
    if (fp == nullptr) {
      return;
    }
    detail::TelemPredCsvHeaderWritten() = false;
  }
  if (!detail::TelemPredCsvHeaderWritten()) {
    std::fprintf(fp,
                 "t_ms,phase,target_id,own_lat,own_lon,"
                 "rival_rx_lat,rival_rx_lon,rival_dr_lat,rival_dr_lon,"
                 "aim_lat,aim_lon,stale_s,range_m,bearing_deg,"
                 "stage,throttle,maneuver_dps,safety_brake,band_guard,"
                 "geom_fill,geom_hit,geom_lock_s\n");
    detail::TelemPredCsvHeaderWritten() = true;
  }

  const auto t_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  std::fprintf(fp,
               "%lld,%s,%d,%.8f,%.8f,%.8f,%.8f,%.8f,%.8f,%.8f,%.8f,%.4f,%.2f,%.2f,"
               "%s,%d,%.2f,%d,%d,%.4f,%d,%.2f\n",
               static_cast<long long>(t_ms), SeyirPhaseName(decision.phase),
               decision.selected_target_id, in.own_lat, in.own_lon, decision.rival_rx_lat_deg,
               decision.rival_rx_lon_deg, decision.rival_dr_lat_deg, decision.rival_dr_lon_deg,
               decision.aim_lat_deg, decision.aim_lon_deg, decision.stale_s,
               decision.aim_range_m, decision.aim_bearing_deg,
               control::PursuitStageName(decision.pursuit_stage), decision.desired_throttle_pct,
               decision.maneuver_dps, decision.safety_brake ? 1 : 0,
               decision.band_guard_active ? 1 : 0, decision.geom_fill_ratio,
               decision.geom_in_hit_area ? 1 : 0, decision.geom_lock_s);
  std::fflush(fp);
}

inline void CloseTelemPredictionCsv() {
  std::lock_guard<std::mutex> lk(detail::TelemPredCsvMutex());
  if (detail::TelemPredCsvFile() != nullptr) {
    std::fclose(detail::TelemPredCsvFile());
    detail::TelemPredCsvFile() = nullptr;
    detail::TelemPredCsvHeaderWritten() = false;
  }
}

}  // namespace savasan::runners

#endif  // SAVASAN_RUNNERS_TELEM_PREDICTION_CSV_HPP_
