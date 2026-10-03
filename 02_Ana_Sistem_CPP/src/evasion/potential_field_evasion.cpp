/**
 * @file potential_field_evasion.cpp
 * @brief @ref savasan::evasion::ComputeApfSetpoint uygulaması.
 */
#include "evasion/potential_field_evasion.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "common/log.hpp"
#include "evasion/geo_kinematics.hpp"

namespace savasan::evasion {

namespace {

constexpr float kMinDistanceM = 1.0f;
/// Takım numarası yokken kendi GPS kaydını elemek için yakınlık eşiği (m).
constexpr float kSelfProximityFilterM = 5.0f;
constexpr double kPi = 3.14159265358979323846;

float EnvFloat(const char* key, const float fallback, const float min_v, const float max_v) {
  const char* v = std::getenv(key);
  if (v == nullptr || v[0] == '\0') {
    return fallback;
  }
  const float raw = static_cast<float>(std::atof(v));
  const float clamped = std::clamp(raw, min_v, max_v);
  if (clamped != raw) {
    SAVASAN_LOG_F(savasan::common::LogLevel::kWarn, "APF",
                  "%s=%.6g kirpildi -> [%.6g, %.6g] araliginda %.6g", key, raw, min_v, max_v,
                  clamped);
  }
  return clamped;
}

int EnvInt(const char* key, const int fallback, const int min_v, const int max_v) {
  const char* v = std::getenv(key);
  if (v == nullptr || v[0] == '\0') {
    return fallback;
  }
  const long raw = std::atol(v);
  const long clamped = std::clamp(raw, static_cast<long>(min_v), static_cast<long>(max_v));
  if (clamped != raw) {
    SAVASAN_LOG_F(savasan::common::LogLevel::kWarn, "APF",
                  "%s=%ld kirpildi -> [%d, %d] araliginda %ld", key, raw, min_v, max_v, clamped);
  }
  return static_cast<int>(clamped);
}

float RepulsiveMagnitude(const float k, const float d_m, const float d0_m) {
  const float d = std::max(d_m, kMinDistanceM);
  const float inv_d = 1.0f / d;
  const float inv_d0 = 1.0f / std::max(d0_m, kMinDistanceM);
  return k * (inv_d - inv_d0) * (inv_d * inv_d);
}

void AccumulateRepulsiveFromPoint(const double from_lat_deg, const double from_lon_deg,
                                  const double own_lat_deg, const double own_lon_deg,
                                  const float magnitude, double& f_n, double& f_e) {
  if (magnitude <= 0.0f) {
    return;
  }
  const float bearing = BearingDeg(from_lat_deg, from_lon_deg, own_lat_deg, own_lon_deg);
  const float bearing_rad = static_cast<float>(bearing * kPi / 180.0);
  f_n += static_cast<double>(magnitude) * static_cast<double>(std::cos(bearing_rad));
  f_e += static_cast<double>(magnitude) * static_cast<double>(std::sin(bearing_rad));
}

float VectorMagnitude(const double f_n, const double f_e) {
  return static_cast<float>(std::sqrt(f_n * f_n + f_e * f_e));
}

void ApplyLocalMinimumBias(const PotentialFieldConfig& cfg, const float own_yaw_deg,
                           const float magnitude_scale, double& f_n, double& f_e) {
  float base_heading_deg = own_yaw_deg;
  const float residual = VectorMagnitude(f_n, f_e);
  if (residual > 1e-6f) {
    base_heading_deg =
        static_cast<float>(std::atan2(f_e, f_n) * 180.0 / kPi);
  }
  // Simetrik sıkışmada her tick aynı yöne dönmemek için kuvvet bileşenine göre işaret seç.
  float bias_sign = 1.0f;
  if (std::abs(f_e) > 1e-3) {
    bias_sign = (f_e >= 0.0) ? 1.0f : -1.0f;
  } else if (std::abs(f_n) > 1e-3) {
    bias_sign = (f_n >= 0.0) ? -1.0f : 1.0f;
  } else {
    bias_sign = (std::fmod(own_yaw_deg, 180.0f) >= 0.0f) ? 1.0f : -1.0f;
  }
  const float escape_heading = base_heading_deg + bias_sign * cfg.local_min_bias_deg;
  const float escape_rad = static_cast<float>(escape_heading * kPi / 180.0);
  // Yapay kuvvet gerçek bileşenlerle aynı ölçekte olmalı; yoksa loglanan
  // F_toplam kapan anında yüzlerce kat sıçrayıp okunamaz hâle gelir.
  const float art_mag = magnitude_scale > 0.0f ? magnitude_scale : cfg.k_rep;
  f_n = static_cast<double>(art_mag * std::cos(escape_rad));
  f_e = static_cast<double>(art_mag * std::sin(escape_rad));
}

}  // namespace

PotentialFieldConfig LoadPotentialFieldConfigFromEnv() {
  PotentialFieldConfig cfg{};
  cfg.k_rep = EnvFloat("SAVASAN_APF_K_REP", cfg.k_rep, 1.0f, 100000.0f);
  cfg.d0_m = EnvFloat("SAVASAN_APF_D0", cfg.d0_m, 10.0f, 5000.0f);
  cfg.max_speed_mps = EnvFloat("SAVASAN_APF_MAX_SPEED_MPS", cfg.max_speed_mps, 0.5f, 32.0f);
  cfg.max_yaw_rate_dps =
      EnvFloat("SAVASAN_APF_MAX_YAW_DPS", cfg.max_yaw_rate_dps, 1.0f, 90.0f);
  cfg.k_yaw = EnvFloat("SAVASAN_APF_K_YAW", cfg.k_yaw, 0.05f, 5.0f);
  cfg.max_stale_ms = EnvInt("SAVASAN_APF_MAX_STALE_MS", cfg.max_stale_ms, 100, 30000);
  cfg.own_team_no = EnvInt("SAVASAN_COMPETITION_TAKIM_NO", cfg.own_team_no, 0, 9999);
  cfg.local_min_force_eps =
      EnvFloat("SAVASAN_APF_LOCAL_MIN_EPS", cfg.local_min_force_eps, 0.001f, 1.0f);
  cfg.local_min_bias_deg =
      EnvFloat("SAVASAN_APF_LOCAL_MIN_BIAS_DEG", cfg.local_min_bias_deg, 1.0f, 90.0f);
  if (cfg.own_team_no <= 0) {
    SAVASAN_LOG_F(savasan::common::LogLevel::kWarn, "APF",
                  "SAVASAN_COMPETITION_TAKIM_NO=0 veya bos; yakin konum filtresi (<%dm) aktif",
                  static_cast<int>(kSelfProximityFilterM));
  }
  return cfg;
}

std::vector<ThreatPrediction> UpdateThreatPredictions(
    const PotentialFieldConfig& cfg, const double own_lat_deg, const double own_lon_deg,
    const std::vector<savasan::competition::DigerKonumBilgisi>& sources,
    const std::chrono::steady_clock::time_point server_tp,
    const std::chrono::steady_clock::time_point now) {
  std::vector<ThreatPrediction> out;
  out.reserve(sources.size());

  const auto server_age_ms =
      server_tp.time_since_epoch().count() > 0
          ? std::chrono::duration_cast<std::chrono::milliseconds>(now - server_tp).count()
          : 0;

  for (const auto& src : sources) {
    if (cfg.own_team_no > 0 && src.takim_numarasi == cfg.own_team_no) {
      continue;
    }
    if (cfg.own_team_no <= 0) {
      const float self_dist =
          HaversineDistanceM(own_lat_deg, own_lon_deg, src.iha_enlem, src.iha_boylam);
      if (self_dist < kSelfProximityFilterM) {
        continue;
      }
    }
    if (src.zaman_farki_ms > cfg.max_stale_ms) {
      continue;
    }

    ThreatPrediction tp{};
    tp.team_no = src.takim_numarasi;
    tp.stale = false;

    const float elapsed_s =
        static_cast<float>(std::max<std::int64_t>(0, server_age_ms + src.zaman_farki_ms)) / 1000.0f;
    const LatLon predicted =
        PredictLatLon(src.iha_enlem, src.iha_boylam, static_cast<float>(src.iha_hizi),
                      static_cast<float>(src.iha_yonelme), elapsed_s);
    tp.lat_deg = predicted.lat_deg;
    tp.lon_deg = predicted.lon_deg;
    tp.distance_m =
        HaversineDistanceM(own_lat_deg, own_lon_deg, tp.lat_deg, tp.lon_deg);
    tp.in_range = tp.distance_m < cfg.d0_m;
    out.push_back(tp);
  }
  return out;
}

ApfResult ComputeApfSetpoint(const PotentialFieldConfig& cfg, const double own_lat_deg,
                             const double own_lon_deg, const float own_yaw_deg,
                             const float own_alt_m,
                             const std::vector<ThreatPrediction>& threats) {
  ApfResult result{};
  double f_threat_n = 0.0;
  double f_threat_e = 0.0;
  bool any_threat = false;
  float magnitude_sum = 0.0f;  ///< Bileşen büyüklüklerinin toplamı (kapan tespiti).

  for (const auto& threat : threats) {
    if (threat.stale || !threat.in_range) {
      continue;
    }
    any_threat = true;
    const float magnitude = RepulsiveMagnitude(cfg.k_rep, threat.distance_m, cfg.d0_m);
    magnitude_sum += magnitude;
    AccumulateRepulsiveFromPoint(threat.lat_deg, threat.lon_deg, own_lat_deg, own_lon_deg,
                                 magnitude, f_threat_n, f_threat_e);
  }

  if (!any_threat) {
    return result;
  }

  const float f_threat_mag = VectorMagnitude(f_threat_n, f_threat_e);

  double f_n = f_threat_n;
  double f_e = f_threat_e;
  float f_mag = VectorMagnitude(f_n, f_e);

  // Kapan tespiti ORANSALDIR. Repulsive büyüklük 1/d² ile ölçeklendiği için
  // mesafeye göre binlerce kat değişir; mutlak bir eşik uzaktaki tek bir
  // rakibi bile "kapan" sayar ve gerçek itme yönü yerine yapay bias yönü
  // kullanılır. Kapan, bileşenler büyükken toplamın küçük kalmasıdır:
  // kuvvetler birbirini götürüyordur.
  if (magnitude_sum > 1e-12f && f_mag < cfg.local_min_force_eps * magnitude_sum) {
    ApplyLocalMinimumBias(cfg, own_yaw_deg, magnitude_sum, f_n, f_e);
    f_mag = VectorMagnitude(f_n, f_e);
    result.forces.local_minimum_escape = true;
  }

  if (f_mag < 1e-6f) {
    return result;
  }

  const float desired_heading =
      static_cast<float>(std::atan2(f_e, f_n) * 180.0 / kPi);

  result.active = true;
  result.threat_active = any_threat;
  result.forces.f_total = f_mag;
  result.forces.f_threat = f_threat_mag;
  float hdg = desired_heading;
  while (hdg < 0.0f) {
    hdg += 360.0f;
  }
  while (hdg >= 360.0f) {
    hdg -= 360.0f;
  }
  result.escape_heading_deg = hdg;
  result.escape_alt_m =
      static_cast<int>(std::lround(std::max(35.0f, std::isfinite(own_alt_m) ? own_alt_m : 35.0f)));
  return result;
}

}  // namespace savasan::evasion
