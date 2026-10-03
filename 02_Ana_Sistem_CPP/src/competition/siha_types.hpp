/**
 * @file siha_types.hpp
 * @brief RF / güdümün ihtiyaç duyduğu SIHA alan tipleri.
 *
 * Jetson yarışma sunucusuna HTTP atmaz. Telemetri, kilit ve HSS yer istasyonu
 * üzerinden gider. Bu başlık yalnızca RF dosyaları ve APF'nin kullandığı
 * kayıtları tutar.
 */
#ifndef SAVASAN_COMPETITION_SIHA_TYPES_HPP_
#define SAVASAN_COMPETITION_SIHA_TYPES_HPP_

#include <cstdint>

namespace savasan::competition {

/// @brief HSS yasak daire (RF 0x24 / geofence).
struct HssKoordinatBilgisi {
  int id = 0;
  double hss_enlem = 0.0;
  double hss_boylam = 0.0;
  float hss_yaricap = 0.0f;
};

/// @brief Rakip anlık konumu (APF dead reckoning girdisi).
///
/// Alan adları eski HTTP `konumBilgileri` öğesiyle aynıdır; kaynak artık
/// RF rakip havuzudur.
struct DigerKonumBilgisi {
  int takim_numarasi = 0;
  double iha_enlem = 0.0;
  double iha_boylam = 0.0;
  double iha_irtifa = 0.0;
  double iha_dikilme = 0.0;
  double iha_yonelme = 0.0;
  double iha_yatis = 0.0;
  double iha_hizi = 0.0;
  std::int64_t zaman_farki_ms = 0;
};

}  // namespace savasan::competition

#endif  // SAVASAN_COMPETITION_SIHA_TYPES_HPP_
