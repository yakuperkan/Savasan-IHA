/**
 * @file local_frame.hpp
 * @brief Yerel düzlem projeksiyonu — MSO `gorev_beyni.Cerceve` portu.
 *
 * Bir referans noktası (kalkış/saha merkezi) etrafında lat/lon ile metre
 * arasında dönüşüm yapar. Eksen düzeni: x = doğu, y = kuzey.
 *
 * @warning Ölçek sabitleri (110540 / 111320·cos) bilerek MSO'daki değerlerdir.
 * @ref savasan::evasion::HaversineDistanceM küresel yarıçap kullanır ve burayla
 * birkaç metre fark verir. Kestirici/güdüm hattı MSO referans çıktılarıyla
 * karşılaştırıldığı için bu dosyada sabitler DEĞİŞTİRİLMEMELİDİR.
 */
#ifndef SAVASAN_CONTROL_LOCAL_FRAME_HPP_
#define SAVASAN_CONTROL_LOCAL_FRAME_HPP_

#include <cmath>

namespace savasan::control {

struct LocalPoint {
  double x = 0.0;  ///< Doğu (metre).
  double y = 0.0;  ///< Kuzey (metre).
};

struct GeoPoint {
  double lat_deg = 0.0;
  double lon_deg = 0.0;
};

/// @brief Sabit ölçekli düz-yer çerçevesi (MSO Cerceve ile birebir).
class LocalFrame {
 public:
  LocalFrame() = default;

  LocalFrame(const double lat0_deg, const double lon0_deg)
      : lat0_deg_(lat0_deg),
        lon0_deg_(lon0_deg),
        k_lon_(111320.0 * std::cos(lat0_deg * kDegToRad)) {}

  /// @brief Coğrafi → yerel metre.
  LocalPoint ToLocal(const double lat_deg, const double lon_deg) const {
    return LocalPoint{(lon_deg - lon0_deg_) * k_lon_, (lat_deg - lat0_deg_) * kLatScale};
  }

  /// @brief Yerel metre → coğrafi.
  GeoPoint ToGeo(const double x, const double y) const {
    return GeoPoint{lat0_deg_ + y / kLatScale, lon0_deg_ + x / k_lon_};
  }

  double origin_lat_deg() const { return lat0_deg_; }
  double origin_lon_deg() const { return lon0_deg_; }

  static constexpr double kLatScale = 110540.0;

 private:
  static constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

  double lat0_deg_ = 0.0;
  double lon0_deg_ = 0.0;
  double k_lon_ = 111320.0;
};

}  // namespace savasan::control

#endif  // SAVASAN_CONTROL_LOCAL_FRAME_HPP_
