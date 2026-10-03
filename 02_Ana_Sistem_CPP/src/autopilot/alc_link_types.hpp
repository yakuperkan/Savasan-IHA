/**
 * @file alc_link_types.hpp
 * @brief Alacakart (ALC_LINK) otopilot haberleşme protokolünün veri tipleri.
 *
 * Bu başlık, Jetson tarafındaki yazılım ile Alacakart otopilot arasında
 * seri hat üzerinden taşınan üst düzey veri yapılarını tanımlar:
 *  - @ref LockCoordinates : kameradan tespit edilen hedef kilit koordinatı.
 *  - @ref TelemetrySnapshot : otopilottan gelen 0x54 telemetri paketinin
 *    çözümlenmiş hâli (konum, yönelim, batarya, GPS vb.).
 */
#ifndef SAVASAN_AUTOPILOT_ALC_LINK_TYPES_HPP_
#define SAVASAN_AUTOPILOT_ALC_LINK_TYPES_HPP_

#include <array>
#include <cstdint>

namespace savasan::autopilot {

/**
 * @brief Hedef kilit koordinatı (görüntü düzleminde).
 *
 * Takip sistemi tarafından üretilir ve otopilota gönderilir. Koordinatlar,
 * köprü yapılandırmasına göre normalize ([0,1]) veya piksel cinsinden olabilir.
 */
struct LockCoordinates {
  float x = 0.0f;          ///< Yatay koordinat (normalize [0,1] veya piksel).
  float y = 0.0f;          ///< Dikey koordinat (normalize [0,1] veya piksel).
  bool valid = false;      ///< Koordinatın geçerli (gönderilebilir) olup olmadığı.
  uint32_t track_id = 0;   ///< Takip edilen nesnenin kimliği (V2 pakette taşınır).
};

/**
 * @brief Seyir Modu ham komut paketi (84 86 19 ... 42).
 *
 * @ref seyir_mode_commands.hpp paketleyicileri ile üretilir.
 */
struct SeyirModePacket {
  static constexpr size_t kMaxBytes = 17;
  std::array<uint8_t, kMaxBytes> bytes{};
  size_t length = 0;
  bool valid = false;
};

/**
 * @brief Otopilottan gelen telemetri paketinin çözümlenmiş anlık görüntüsü.
 *
 * ALC_LINK V2.0.1 (ALACAKART V2.2.9) 32 baytlık 0x54 telemetri paketinden
 * çözümlenen alanları içerir. Hız vektörü (@c vel_*) ve @c yaw_rate alanları
 * güdüm/durum-kestirim (state-estimator) hattı için GPS hızı + pusuladan
 * türetilir; paket doğrudan gövde-hızı (body velocity) sağlamaz.
 */
struct TelemetrySnapshot {
  bool valid = false;        ///< En az bir geçerli telemetri paketi alındı mı.
  uint64_t frame_count = 0;  ///< Alınan toplam geçerli kare sayısı (sayaç).

  // --- Güdüm/durum-kestirim hız girişleri (yer hızı + pusuladan türetilir, NED varsayımı) ---
  float vel_x_mps = 0.0f;    ///< Kuzey bileşeni hız (m/s).
  float vel_y_mps = 0.0f;    ///< Doğu bileşeni hız (m/s).
  float vel_z_mps = 0.0f;    ///< Dikey hız (paket sağlamadığı için 0).
  float yaw_rate_dps = 0.0f; ///< Pusula farkından türetilen yaw hızı (derece/s).

  // --- Araç bilgisi ---
  uint8_t arac_modu = 0;     ///< 0 manuel,1 denge,2 otonom,3 RTL,4 eğitim,5 aktif takip,6-12 seri.
  uint8_t sistem_durum = 0;  ///< Otopilot sistem durum bayrağı.
  uint8_t arac_id = 0;       ///< Araç kimliği.

  // --- Konum / yönelim ---
  double enlem = 0.0;        ///< Enlem (derece).
  double boylam = 0.0;       ///< Boylam (derece).
  uint8_t uydu_sayisi = 0;   ///< Görülen GPS uydu sayısı.
  float gps_hiz_mps = 0.0f;  ///< Yer hızı (kart km/h değerinden m/s'ye dönüştürülür).
  float yaw_deg = 0.0f;      ///< Pusula açısı 0..360 (z ekseni).
  float roll_deg = 0.0f;     ///< Yatış açısı (x ekseni).
  float pitch_deg = 0.0f;    ///< Yunuslama/dikilme açısı (y ekseni).

  // --- Değişken kuyruk (seçim bitine göre dönüşümlü gelir; son bilinen değerler saklanır) ---
  float irtifa_m = 0.0f;       ///< İrtifa (m).
  float voltaj_v = 0.0f;       ///< Batarya voltajı (V).
  float akim_a = 0.0f;         ///< Anlık akım (A).
  uint16_t harc_akim_mah = 0;  ///< Harcanan toplam akım (mAh).
  uint8_t motor_durumu = 0;    ///< Motor durum bayrağı.
  uint8_t gps_durum = 0;       ///< Ham 3 bit GPS sabitlenme durumu: 2=2D, 3=3D, 7=GPS yok.
  uint8_t gps_saat = 0;        ///< GPS saat (UTC).
  uint8_t gps_dakika = 0;      ///< GPS dakika (UTC).
  uint8_t gps_saniye = 0;      ///< GPS saniye (UTC).
  bool gps_zaman_gecerli = false; ///< GPS zaman alanlarının geçerli olup olmadığı.
  uint8_t gps_gun = 0;         ///< GPS gün.
  uint8_t gps_ay = 0;          ///< GPS ay.
  uint16_t gps_yil = 0;        ///< GPS yıl (tam yıl, örn. 2026).
  uint8_t sicaklik_c = 0;      ///< Sıcaklık (santigrat derece).

  std::array<uint8_t, 32> raw{}; ///< Çözümlenen ham 32 baytlık paketin kopyası.
};

}  // namespace savasan::autopilot

#endif  // SAVASAN_AUTOPILOT_ALC_LINK_TYPES_HPP_
