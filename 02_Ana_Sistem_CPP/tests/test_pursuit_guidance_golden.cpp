/**
 * @file test_pursuit_guidance_golden.cpp
 * @brief C++ PursuitGuidance portunu MSO `takip_gudum.py` çıktısıyla karşılaştırır.
 *
 * Senaryo kapalı döngüdür: kendi uçağımız nişan noktasına sınırlı dönüş
 * hızıyla ilerletilir, böylece YAKLASMA/TAKIP/KOPMA dallarının hepsi ve
 * öngörülü TCA emniyeti tetiklenir. Referans `scripts/mso_reference_gen.py`
 * tarafından MSO'nun kendi kaynağı koşularak üretilir.
 */
#include "control/pursuit_guidance.hpp"

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
constexpr double kTol = 1e-6;

struct Row {
  double t, bx, by, balt, bhiz, btrack;
  double hx, hy, halt, hvx, hvy, hhiz, hyon, turn_rate;
  int has_intent, stage;
  double aim_x, aim_y;
  int altitude, throttle, pitch;
  int has_geo;
  double geo_range, geo_fill;
  int in_hit_area;
  double lock_s;
};

bool LoadReference(const std::string& path, std::vector<Row>* rows) {
  std::ifstream f(path);
  if (!f.is_open()) {
    return false;
  }
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#' || line[0] == 't') {
      continue;
    }
    std::istringstream ss(line);
    std::string cell;
    std::vector<double> v;
    while (std::getline(ss, cell, ',')) {
      v.push_back(std::atof(cell.c_str()));
    }
    if (v.size() != 26) {
      return false;
    }
    Row r{};
    r.t = v[0];
    r.bx = v[1];
    r.by = v[2];
    r.balt = v[3];
    r.bhiz = v[4];
    r.btrack = v[5];
    r.hx = v[6];
    r.hy = v[7];
    r.halt = v[8];
    r.hvx = v[9];
    r.hvy = v[10];
    r.hhiz = v[11];
    r.hyon = v[12];
    r.turn_rate = v[13];
    r.has_intent = static_cast<int>(v[14]);
    r.stage = static_cast<int>(v[15]);
    r.aim_x = v[16];
    r.aim_y = v[17];
    r.altitude = static_cast<int>(v[18]);
    r.throttle = static_cast<int>(v[19]);
    r.pitch = static_cast<int>(v[20]);
    r.has_geo = static_cast<int>(v[21]);
    r.geo_range = v[22];
    r.geo_fill = v[23];
    r.in_hit_area = static_cast<int>(v[24]);
    r.lock_s = v[25];
    rows->push_back(r);
  }
  return !rows->empty();
}

bool Close(const double a, const double b, const double tol = kTol) {
  return std::abs(a - b) <= tol;
}

int StageCode(const ctrl::PursuitStage s) {
  switch (s) {
    case ctrl::PursuitStage::kApproach:
      return 1;
    case ctrl::PursuitStage::kTrack:
      return 2;
    case ctrl::PursuitStage::kBreakaway:
      return 3;
    default:
      return 0;
  }
}

}  // namespace

int main(int argc, char** argv) {
  const std::string path =
      argc > 1 ? argv[1] : std::string("tests/fixtures/mso_takip_gudum_ref.csv");

  std::vector<Row> rows;
  if (!LoadReference(path, &rows)) {
    std::cerr << "FAIL: referans okunamadi: " << path << "\n";
    return 1;
  }

  const ctrl::LocalFrame frame(kRefLat, kRefLon);
  ctrl::PursuitGuidance guidance(frame);
  ctrl::LockCounter counter;

  int idx = 0;
  for (const auto& r : rows) {
    ++idx;

    ctrl::PursuitOwnState own{};
    own.x = r.bx;
    own.y = r.by;
    own.alt_m = r.balt;
    own.speed_mps = r.bhiz;
    own.has_track = true;
    own.track_rad = r.btrack;

    ctrl::TargetEstimate tgt{};
    tgt.x = r.hx;
    tgt.y = r.hy;
    tgt.alt_m = r.halt;
    tgt.vx = r.hvx;
    tgt.vy = r.hvy;
    tgt.speed_mps = r.hhiz;
    tgt.heading_deg = r.hyon;
    tgt.turn_rate_dps = r.turn_rate;

    const auto geo = guidance.ComputeLockGeometry(own, tgt);
    const auto cmd = guidance.Tick(r.t, own, tgt, true);
    const double lock_s = counter.Update(r.t, geo.in_hit_area);

    if (geo.valid != (r.has_geo != 0)) {
      std::cerr << "FAIL satir " << idx << ": geo_var beklenen=" << r.has_geo << "\n";
      return 2;
    }
    if (geo.valid) {
      if (!Close(geo.range_m, r.geo_range) || !Close(geo.fill_ratio, r.geo_fill)) {
        std::cerr << "FAIL satir " << idx << ": geometri menzil beklenen=" << r.geo_range
                  << " alinan=" << geo.range_m << " oran beklenen=" << r.geo_fill
                  << " alinan=" << geo.fill_ratio << "\n";
        return 3;
      }
      if (geo.in_hit_area != (r.in_hit_area != 0)) {
        std::cerr << "FAIL satir " << idx << ": vurus_alaninda beklenen=" << r.in_hit_area
                  << "\n";
        return 4;
      }
    }
    if (!Close(lock_s, r.lock_s)) {
      std::cerr << "FAIL satir " << idx << ": kilit suresi beklenen=" << r.lock_s
                << " alinan=" << lock_s << "\n";
      return 5;
    }

    if (cmd.valid != (r.has_intent != 0)) {
      std::cerr << "FAIL satir " << idx << ": niyet_var beklenen=" << r.has_intent << "\n";
      return 6;
    }
    if (!cmd.valid) {
      continue;
    }
    if (StageCode(cmd.stage) != r.stage) {
      std::cerr << "FAIL satir " << idx << ": asama beklenen=" << r.stage
                << " alinan=" << StageCode(cmd.stage) << "\n";
      return 7;
    }
    if (!Close(cmd.aim_x, r.aim_x, 1e-5) || !Close(cmd.aim_y, r.aim_y, 1e-5)) {
      std::cerr << "FAIL satir " << idx << ": nisan beklenen=(" << r.aim_x << "," << r.aim_y
                << ") alinan=(" << cmd.aim_x << "," << cmd.aim_y << ")\n";
      return 8;
    }
    if (cmd.altitude_m != r.altitude) {
      std::cerr << "FAIL satir " << idx << ": irtifa beklenen=" << r.altitude
                << " alinan=" << cmd.altitude_m << "\n";
      return 9;
    }
    if (cmd.throttle_pct != r.throttle) {
      std::cerr << "FAIL satir " << idx << ": gaz beklenen=" << r.throttle
                << " alinan=" << cmd.throttle_pct << "\n";
      return 10;
    }
    if (cmd.pitch_deg != r.pitch) {
      std::cerr << "FAIL satir " << idx << ": aci beklenen=" << r.pitch << "\n";
      return 11;
    }
  }

  std::cout << "OK test_pursuit_guidance_golden (" << rows.size() << " satir)\n";
  return 0;
}
