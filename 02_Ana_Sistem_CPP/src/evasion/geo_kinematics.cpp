/**
 * @file geo_kinematics.cpp
 * @brief @ref savasan::evasion geo yardımcıları uygulaması.
 */
#include "evasion/geo_kinematics.hpp"

#include <algorithm>
#include <cmath>

namespace savasan::evasion {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadiusM = 6371000.0;
/// Dead reckoning tahmin ufkunu sınırlar (sunucu gecikmesi sapması).
constexpr float kMaxPredictionDtS = 2.0f;

double ToRad(const double deg) { return deg * kPi / 180.0; }

double ToDeg(const double rad) { return rad * 180.0 / kPi; }

}  // namespace

float HaversineDistanceM(const double lat1_deg, const double lon1_deg, const double lat2_deg,
                         const double lon2_deg) {
  const double lat1 = ToRad(lat1_deg);
  const double lat2 = ToRad(lat2_deg);
  const double dlat = lat2 - lat1;
  const double dlon = ToRad(lon2_deg - lon1_deg);
  const double a =
      std::sin(dlat * 0.5) * std::sin(dlat * 0.5) +
      std::cos(lat1) * std::cos(lat2) * std::sin(dlon * 0.5) * std::sin(dlon * 0.5);
  const double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(std::max(0.0, 1.0 - a)));
  return static_cast<float>(kEarthRadiusM * c);
}

float BearingDeg(const double from_lat_deg, const double from_lon_deg, const double to_lat_deg,
                 const double to_lon_deg) {
  const double lat1 = ToRad(from_lat_deg);
  const double lat2 = ToRad(to_lat_deg);
  const double dlon = ToRad(to_lon_deg - from_lon_deg);
  const double y = std::sin(dlon) * std::cos(lat2);
  const double x =
      std::cos(lat1) * std::sin(lat2) - std::sin(lat1) * std::cos(lat2) * std::cos(dlon);
  const double brng = ToDeg(std::atan2(y, x));
  return static_cast<float>(brng < 0.0 ? brng + 360.0 : brng);
}

LatLon OffsetLatLon(const double lat_deg, const double lon_deg, const float bearing_deg,
                    const float distance_m) {
  if (distance_m <= 0.0f || !std::isfinite(distance_m) || !std::isfinite(bearing_deg)) {
    return LatLon{lat_deg, lon_deg};
  }
  const double bearing_rad = ToRad(static_cast<double>(bearing_deg));
  const double dist_m = static_cast<double>(distance_m);
  const double dlat = (dist_m * std::cos(bearing_rad)) / kEarthRadiusM;
  const double lat_rad = ToRad(lat_deg);
  const double dlon =
      (dist_m * std::sin(bearing_rad)) / (kEarthRadiusM * std::max(1e-6, std::cos(lat_rad)));
  return LatLon{lat_deg + ToDeg(dlat), lon_deg + ToDeg(dlon)};
}

LatLon PredictLatLon(const double lat_deg, const double lon_deg, const float speed_mps,
                     const float heading_deg, const float dt_s) {
  if (dt_s <= 0.0f || speed_mps <= 0.0f || !std::isfinite(dt_s) ||
      !std::isfinite(speed_mps) || !std::isfinite(heading_deg)) {
    return LatLon{lat_deg, lon_deg};
  }
  const float capped_dt_s = std::min(dt_s, kMaxPredictionDtS);
  return OffsetLatLon(lat_deg, lon_deg, heading_deg, speed_mps * capped_dt_s);
}

float WrapAngleDeg180(float angle_deg) {
  if (!std::isfinite(angle_deg)) {
    return 0.0f;
  }
  return static_cast<float>(std::remainder(static_cast<double>(angle_deg), 360.0));
}

}  // namespace savasan::evasion
