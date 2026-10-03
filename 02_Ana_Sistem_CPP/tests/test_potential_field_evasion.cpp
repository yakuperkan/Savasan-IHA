#include "evasion/geo_kinematics.hpp"
#include "evasion/potential_field_evasion.hpp"

#include <chrono>
#include <cmath>
#include <limits>

namespace {

bool Near(const float a, const float b, const float eps = 1e-3f) {
  return std::fabs(a - b) <= eps;
}

}  // namespace

int main() {
  using savasan::evasion::BearingDeg;
  using savasan::evasion::ComputeApfSetpoint;
  using savasan::evasion::HaversineDistanceM;
  using savasan::evasion::LatLon;
  using savasan::evasion::PotentialFieldConfig;
  using savasan::evasion::PredictLatLon;
  using savasan::evasion::ThreatPrediction;
  using savasan::evasion::UpdateThreatPredictions;
  using savasan::evasion::WrapAngleDeg180;
  using savasan::competition::DigerKonumBilgisi;
  using savasan::competition::HssKoordinatBilgisi;

  // Haversine: ~111 m per 0.001 deg latitude at equator (approx).
  const float d = HaversineDistanceM(41.0, 36.0, 41.001, 36.0);
  if (d < 100.0f || d > 130.0f) {
    return 1;
  }

  const LatLon p = PredictLatLon(41.0, 36.0, 10.0f, 0.0f, 1.0f);
  if (p.lat_deg <= 41.0) {
    return 2;
  }

  // Tahmin ufku: dt_s > 2 s iken ilerleme 2 s ile sinirli.
  const LatLon p_cap_2s = PredictLatLon(41.0, 36.0, 10.0f, 0.0f, 2.0f);
  const LatLon p_cap_10s = PredictLatLon(41.0, 36.0, 10.0f, 0.0f, 10.0f);
  if (!Near(static_cast<float>(p_cap_2s.lat_deg), static_cast<float>(p_cap_10s.lat_deg), 1e-6f) ||
      !Near(static_cast<float>(p_cap_2s.lon_deg), static_cast<float>(p_cap_10s.lon_deg), 1e-6f)) {
    return 22;
  }

  // Aci sarma: dongu yerine remainder; NaN/asiri deger sonsuz donguye girmez.
  if (!Near(WrapAngleDeg180(370.0f), 10.0f) || !Near(WrapAngleDeg180(-370.0f), -10.0f) ||
      !Near(WrapAngleDeg180(179.0f + 3.0f), -178.0f)) {
    return 23;
  }
  if (!Near(WrapAngleDeg180(std::numeric_limits<float>::quiet_NaN()), 0.0f)) {
    return 24;
  }

  PotentialFieldConfig cfg{};
  cfg.d0_m = 200.0f;
  cfg.k_rep = 500.0f;
  cfg.own_team_no = 1;

  DigerKonumBilgisi near{};
  near.takim_numarasi = 2;
  near.iha_enlem = 41.0005;
  near.iha_boylam = 36.0;
  near.iha_hizi = 20.0;
  near.iha_yonelme = 90.0;
  near.zaman_farki_ms = 100;

  DigerKonumBilgisi far{};
  far.takim_numarasi = 3;
  far.iha_enlem = 41.05;
  far.iha_boylam = 36.05;
  far.iha_hizi = 15.0;
  far.iha_yonelme = 0.0;
  far.zaman_farki_ms = 50;

  DigerKonumBilgisi own{};
  own.takim_numarasi = 1;
  own.iha_enlem = 41.0;
  own.iha_boylam = 36.0;

  const auto now = std::chrono::steady_clock::now();
  const auto server_tp = now - std::chrono::milliseconds(100);
  const auto threats = UpdateThreatPredictions(cfg, 41.0, 36.0, {near, far, own}, server_tp, now);
  if (threats.size() != 2U) {
    return 3;
  }
  bool has_near = false;
  bool has_far = false;
  for (const auto& t : threats) {
    if (t.team_no == 2 && t.in_range) {
      has_near = true;
    }
    if (t.team_no == 3 && !t.in_range) {
      has_far = true;
    }
  }
  if (!has_near || !has_far) {
    return 4;
  }

  // Takim no=0: kendi konum kaydi yakinlik filtresi ile elenmeli.
  {
    PotentialFieldConfig cfg_zero_team{};
    cfg_zero_team.d0_m = 200.0f;
    cfg_zero_team.own_team_no = 0;
    const auto threats_zero =
        UpdateThreatPredictions(cfg_zero_team, 41.0, 36.0, {near, far, own}, server_tp, now);
    if (threats_zero.size() != 2U) {
      return 25;
    }
    for (const auto& t : threats_zero) {
      if (t.team_no == 1) {
        return 26;
      }
    }
  }

  const auto apf =
      ComputeApfSetpoint(cfg, 41.0, 36.0, 0.0f, 100.0f, threats);
  if (!apf.active) {
    return 5;
  }
  if (!std::isfinite(apf.escape_heading_deg)) {
    return 6;
  }

  ThreatPrediction far_only{};
  for (const auto& t : threats) {
    if (t.team_no == 3) {
      far_only = t;
      break;
    }
  }
  const auto apf_far_only = ComputeApfSetpoint(cfg, 41.0, 36.0, 0.0f, 100.0f, {far_only});
  if (apf_far_only.active) {
    return 8;
  }

  const float brg = BearingDeg(41.0, 36.0, 41.0, 36.001);
  if (!Near(brg, 90.0f, 5.0f)) {
    return 9;
  }

  // Kuvvet ayrışımı: rakip tehdidi tek kaynak (HSS geofence'e taşındı).
  {
    ThreatPrediction near_threat{};
    near_threat.team_no = 2;
    near_threat.lat_deg = 41.0005;
    near_threat.lon_deg = 36.0;
    near_threat.distance_m = 50.0f;
    near_threat.in_range = true;

    const auto apf_threat_only =
        ComputeApfSetpoint(cfg, 41.0, 36.0, 0.0f, 100.0f, {near_threat});
    if (!apf_threat_only.active || apf_threat_only.forces.f_threat <= 0.0f) {
      return 17;
    }
    // Tek kaynak kaldığı için toplam kuvvet rakip kuvvetine eşit olmalı.
    if (apf_threat_only.forces.local_minimum_escape ||
        !Near(apf_threat_only.forces.f_total, apf_threat_only.forces.f_threat, 1e-3f)) {
      return 18;
    }
    if (apf_threat_only.escape_alt_m < 35) {
      return 12;
    }
  }

  // Local minimum: karsilikli iki rakip arasinda F_total ~ 0 iken bias kacisi.
  // Kapanda kalmak dururken durmaktan kotudur; yapay bir yon uretilir.
  {
    ThreatPrediction north{};
    north.team_no = 10;
    north.lat_deg = 41.0002;
    north.lon_deg = 36.0;
    north.distance_m = 22.0f;
    north.in_range = true;

    ThreatPrediction south{};
    south.team_no = 20;
    south.lat_deg = 40.9998;
    south.lon_deg = 36.0;
    south.distance_m = 22.0f;
    south.in_range = true;

    cfg.local_min_force_eps = 0.05f;
    cfg.local_min_bias_deg = 15.0f;

    const auto apf_trap = ComputeApfSetpoint(cfg, 41.0, 36.0, 0.0f, 100.0f, {north, south});
    if (!apf_trap.active) {
      return 19;
    }
    if (!apf_trap.forces.local_minimum_escape) {
      return 20;
    }
    if (!std::isfinite(apf_trap.escape_heading_deg)) {
      return 21;
    }
  }

  return 0;
}
