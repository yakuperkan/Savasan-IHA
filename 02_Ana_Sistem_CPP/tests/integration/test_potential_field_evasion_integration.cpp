#include "competition/siha_types.hpp"
#include "evasion/potential_field_evasion.hpp"

#include <chrono>
#include <cmath>

int main() {
  using savasan::competition::DigerKonumBilgisi;
  using savasan::evasion::ComputeApfSetpoint;
  using savasan::evasion::PotentialFieldConfig;
  using savasan::evasion::UpdateThreatPredictions;

  PotentialFieldConfig cfg{};
  cfg.d0_m = 300.0f;
  cfg.k_rep = 500.0f;
  cfg.max_speed_mps = 8.0f;
  cfg.own_team_no = 42;

  const double own_lat = 39.925533;
  const double own_lon = 32.866287;
  const float own_yaw = 45.0f;

  DigerKonumBilgisi own{};
  own.takim_numarasi = 42;
  own.iha_enlem = own_lat;
  own.iha_boylam = own_lon;

  DigerKonumBilgisi threat{};
  threat.takim_numarasi = 7;
  threat.iha_enlem = own_lat + 0.0008;
  threat.iha_boylam = own_lon;
  threat.iha_hizi = 18.0;
  threat.iha_yonelme = 270.0;
  threat.zaman_farki_ms = 120;

  DigerKonumBilgisi stale{};
  stale.takim_numarasi = 8;
  stale.iha_enlem = own_lat + 0.0002;
  stale.iha_boylam = own_lon;
  stale.iha_hizi = 12.0;
  stale.iha_yonelme = 90.0;
  stale.zaman_farki_ms = 5000;

  const auto server_tp = std::chrono::steady_clock::now() - std::chrono::milliseconds(250);
  const auto now = std::chrono::steady_clock::now();
  const auto threats =
      UpdateThreatPredictions(cfg, own_lat, own_lon, {own, threat, stale}, server_tp, now);

  if (threats.size() != 1U || threats[0].team_no != 7) {
    return 1;
  }
  if (!threats[0].in_range) {
    return 2;
  }

  const auto apf = ComputeApfSetpoint(cfg, own_lat, own_lon, own_yaw, 120.0f, threats);
  if (!apf.active) {
    return 3;
  }
  if (!std::isfinite(apf.escape_heading_deg) || apf.escape_alt_m < 35) {
    return 4;
  }
  if (!apf.threat_active) {
    return 5;
  }

  return 0;
}
