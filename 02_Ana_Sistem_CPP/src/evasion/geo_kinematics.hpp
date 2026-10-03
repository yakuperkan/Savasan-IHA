/**
 * @file geo_kinematics.hpp
 * @brief Düz-yer yaklaşımlı coğrafi kinematik yardımcıları (APF kaçış).
 */
#ifndef SAVASAN_EVASION_GEO_KINEMATICS_HPP_
#define SAVASAN_EVASION_GEO_KINEMATICS_HPP_

namespace savasan::evasion {

struct LatLon {
  double lat_deg = 0.0;
  double lon_deg = 0.0;
};

/// @brief İki nokta arası büyük daire mesafesi (metre).
float HaversineDistanceM(double lat1_deg, double lon1_deg, double lat2_deg, double lon2_deg);

/// @brief from -> to yönü (derece, 0..360, kuzey=0 doğu=90).
float BearingDeg(double from_lat_deg, double from_lon_deg, double to_lat_deg, double to_lon_deg);

/// @brief Verilen yönde belirli mesafedeki noktayı döndürür (düz-yer).
///
/// @ref PredictLatLon'dan farkı: zaman/hız değil doğrudan mesafe alır ve tahmin
/// ufku sınırlaması uygulanmaz. Kaçış/nişan noktası üretiminde kullanılır.
LatLon OffsetLatLon(double lat_deg, double lon_deg, float bearing_deg, float distance_m);

/// @brief Basit düz-yer dead reckoning: hız + yönelme ile konum ilerletir.
LatLon PredictLatLon(double lat_deg, double lon_deg, float speed_mps, float heading_deg,
                     float dt_s);

/// @brief Açı farkını [-180, 180] aralığına sarar.
float WrapAngleDeg180(float angle_deg);

}  // namespace savasan::evasion

#endif  // SAVASAN_EVASION_GEO_KINEMATICS_HPP_
