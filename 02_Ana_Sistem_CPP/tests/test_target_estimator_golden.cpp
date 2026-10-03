/**
 * @file test_target_estimator_golden.cpp
 * @brief C++ TargetEstimator portunu MSO Python çıktısıyla satır satır karşılaştırır.
 *
 * Referans dosya `scripts/mso_reference_gen.py` tarafından MSO'nun kendi
 * `hedef_kestirici.py` kaynağı koşularak üretilir. Port bir formülü kaydırırsa
 * bu test o satırda patlar.
 */
#include "control/target_estimator.hpp"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace ctrl = savasan::control;

namespace {

constexpr double kRefLat = 40.230000;
constexpr double kRefLon = 29.010000;

/// Konum/hız metre-saniye toleransı; kalan fark yalnızca double yuvarlamasıdır.
constexpr double kTolM = 1e-6;
constexpr double kTolDeg = 1e-6;

struct RefRow {
  double lat = 0.0;
  double lon = 0.0;
  double alt = 0.0;
  double speed = 0.0;
  double heading = 0.0;
  int time_diff_ms = 0;
  double t_recv = 0.0;
  int accepted = 0;
  int has_prediction = 0;
  int valid = 0;
  double x = 0.0;
  double y = 0.0;
  double alt_pred = 0.0;
  double speed_pred = 0.0;
  double heading_pred = 0.0;
  double turn_rate = 0.0;
};

bool LoadReference(const std::string& path, std::vector<RefRow>* rows) {
  std::ifstream f(path);
  if (!f.is_open()) {
    return false;
  }
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#' || line[0] == 'l') {
      continue;  // yorum satırı veya başlık
    }
    std::istringstream ss(line);
    std::string cell;
    std::vector<double> v;
    while (std::getline(ss, cell, ',')) {
      v.push_back(std::atof(cell.c_str()));
    }
    if (v.size() != 16) {
      return false;
    }
    RefRow r{};
    r.lat = v[0];
    r.lon = v[1];
    r.alt = v[2];
    r.speed = v[3];
    r.heading = v[4];
    r.time_diff_ms = static_cast<int>(v[5]);
    r.t_recv = v[6];
    r.accepted = static_cast<int>(v[7]);
    r.has_prediction = static_cast<int>(v[8]);
    r.valid = static_cast<int>(v[9]);
    r.x = v[10];
    r.y = v[11];
    r.alt_pred = v[12];
    r.speed_pred = v[13];
    r.heading_pred = v[14];
    r.turn_rate = v[15];
    rows->push_back(r);
  }
  return !rows->empty();
}

bool Close(const double a, const double b, const double tol) { return std::abs(a - b) <= tol; }

}  // namespace

int main(int argc, char** argv) {
  const std::string path =
      argc > 1 ? argv[1] : std::string("tests/fixtures/mso_hedef_kestirici_ref.csv");

  std::vector<RefRow> rows;
  if (!LoadReference(path, &rows)) {
    std::cerr << "FAIL: referans okunamadi: " << path << "\n";
    return 1;
  }

  ctrl::TargetEstimator est(ctrl::LocalFrame(kRefLat, kRefLon));

  int idx = 0;
  for (const auto& r : rows) {
    ++idx;
    const bool accepted = est.AddMeasurement(r.lat, r.lon, r.alt, r.speed, r.heading,
                                             r.time_diff_ms, r.t_recv, 1);
    if (accepted != (r.accepted != 0)) {
      std::cerr << "FAIL satir " << idx << ": kabul beklenen=" << r.accepted
                << " alinan=" << accepted << "\n";
      return 2;
    }

    ctrl::TargetEstimate est_out{};
    const bool has_pred = est.Predict(r.t_recv, 0.3, &est_out);
    if (has_pred != (r.has_prediction != 0)) {
      std::cerr << "FAIL satir " << idx << ": tahmin_var beklenen=" << r.has_prediction << "\n";
      return 3;
    }
    if (est.IsValid(r.t_recv) != (r.valid != 0)) {
      std::cerr << "FAIL satir " << idx << ": gecerli beklenen=" << r.valid << "\n";
      return 4;
    }
    if (!has_pred) {
      continue;
    }

    if (!Close(est_out.x, r.x, kTolM) || !Close(est_out.y, r.y, kTolM)) {
      std::cerr << "FAIL satir " << idx << ": konum beklenen=(" << r.x << "," << r.y
                << ") alinan=(" << est_out.x << "," << est_out.y << ")\n";
      return 5;
    }
    if (!Close(est_out.alt_m, r.alt_pred, kTolM)) {
      std::cerr << "FAIL satir " << idx << ": irtifa beklenen=" << r.alt_pred
                << " alinan=" << est_out.alt_m << "\n";
      return 6;
    }
    if (!Close(est_out.speed_mps, r.speed_pred, kTolM)) {
      std::cerr << "FAIL satir " << idx << ": hiz beklenen=" << r.speed_pred
                << " alinan=" << est_out.speed_mps << "\n";
      return 7;
    }
    if (!Close(est_out.heading_deg, r.heading_pred, kTolDeg)) {
      std::cerr << "FAIL satir " << idx << ": yon beklenen=" << r.heading_pred
                << " alinan=" << est_out.heading_deg << "\n";
      return 8;
    }
    if (!Close(est_out.turn_rate_dps, r.turn_rate, kTolDeg)) {
      std::cerr << "FAIL satir " << idx << ": donus_orani beklenen=" << r.turn_rate
                << " alinan=" << est_out.turn_rate_dps << "\n";
      return 9;
    }
  }

  std::cout << "OK test_target_estimator_golden (" << rows.size() << " satir)\n";
  return 0;
}
