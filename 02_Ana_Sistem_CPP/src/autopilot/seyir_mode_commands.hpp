/**
 * @file seyir_mode_commands.hpp
 * @brief Alacakart Seyir Modu seri komut paketleyicisi (saf, test edilebilir).
 *
 * Protokol: 84 86 19 [istem] ... 42
 *
 * KAYNAK: `4_Seyir_Mod_Takip_Alg_1/Komut_Ayarlar.ino` — kartı yapanların UÇURDUĞU
 * kod. PDF ile çeliştiği yerlerde UÇAN KOD esastır; aşağıdaki notlar bu dosyadan
 * birebir doğrulanmıştır. Vendor fonksiyon isimleri istem numarasıyla kaymıştır:
 *   Komut_1_Dizilim → istem 0 | Komut_2_Dizilim → istem 2
 *   Komut_4_Dizilim → istem 3 | Komut_5_Dizilim → istem 4
 * İstem 1 (heading) için vendor kodunda fonksiyon YOKTUR (bkz. @ref PackHeadingChange).
 *
 * Koordinat: vendor `(int32_t)(derece * pow(10,6))` yazar — int32 mikro-derece,
 * sıfıra doğru kırpma. PDF örneği float32'dir ve uçan kodla ÇELİŞİR.
 *
 * ÜRETİMDE KULLANILAN: istem 0 (irtifa) + istem 2 (koordinat).
 * İstem 3/4 itki oranı taşır; gaz otopilota bırakıldığı için üretim yolunda
 * çağrılmaz (yalnızca bench/A-B için durur).
 */
#ifndef SAVASAN_AUTOPILOT_SEYIR_MODE_COMMANDS_HPP_
#define SAVASAN_AUTOPILOT_SEYIR_MODE_COMMANDS_HPP_

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#include "autopilot/alc_link_types.hpp"

namespace savasan::autopilot::seyir {

inline constexpr uint8_t kCtrl0 = 84;   ///< 'T'
inline constexpr uint8_t kCtrl1 = 86;   ///< 'V'
inline constexpr uint8_t kModeByte = 19;
inline constexpr uint8_t kTail = 42;    ///< '*'

inline constexpr uint8_t kIstemAltitude = 0;       ///< Yükselme/alçalma (8 bayt) — ÜRETİMDE
inline constexpr uint8_t kIstemHeading = 1;        ///< Yönelme / pusula (7 bayt) — HİÇ UÇMADI
inline constexpr uint8_t kIstemCoordinate = 2;     ///< Koordinat yönelim (14 bayt) — ÜRETİMDE
inline constexpr uint8_t kIstemAltitudeSpeed = 3;  ///< Açı+irtifa+itki (9 bayt) — gazlı, kullanılmaz
inline constexpr uint8_t kIstemCoordinateFull = 4; ///< Koordinat+açı+irtifa+itki (17 bayt) — gazlı

inline constexpr size_t kMaxPacketBytes = 17;

/// @brief Enlem/boylam wire formatı (saha A/B doğrulaması için seçilebilir).
enum class LatLonWireFormat : uint8_t {
  kInt32MicroDegrees = 0,  ///< lat/lon * 1e6 → int32 LE. UÇAN KOD BUDUR, varsayılan.
  kFloat32Degrees = 1,     ///< IEEE-754 float32 LE (yalnız PDF örneği; uçan kodla çelişir).
};

/**
 * @brief İstem 2 kuyruk baytının yeri — iki uçmuş varyant var, ikisi de 14 bayt.
 *
 * `4_Seyir_Mod_Takip_Alg_1` byte_veri[12]=42 yazıp write(...,14) çağırır; 13. bayt
 * yerel dizinin çöpüdür. `2_Seyir_Mod_Kord_ile_Yonelim` ise global dizi kullandığı
 * için [12]=0, [13]=42 gönderir. İkisi de uçtuğuna göre kart kuyruk baytını
 * konumdan bağımsız ele alıyor; yine de saha A/B'si için seçilebilir bıraktık.
 */
enum class Cmd2TailVariant : uint8_t {
  kTailAt12 = 0,  ///< [12]=42, [13]=0 — elimizdeki takip algoritması dosyası (varsayılan)
  kTailAt13 = 1,  ///< [12]=0,  [13]=42 — koordinat yönelim dosyası
};

/// @brief Paketlenmiş seyir komutu.
struct PackedCommand {
  std::array<uint8_t, kMaxPacketBytes> bytes{};
  size_t length = 0;
  bool valid = false;
};

inline void WriteI16Le(uint8_t* dst, const int16_t v) {
  dst[0] = static_cast<uint8_t>(v & 0xFF);
  dst[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

inline void WriteI32Le(uint8_t* dst, const int32_t v) {
  dst[0] = static_cast<uint8_t>(v & 0xFF);
  dst[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
  dst[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
  dst[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

inline void WriteF32Le(uint8_t* dst, const float v) {
  static_assert(sizeof(float) == 4, "float 32-bit olmali");
  std::memcpy(dst, &v, 4);
}

/// @brief Derece → int32 mikro-derece (×1e6), taşma güvenli.
///
/// Vendor `(int32_t)(derece * pow(10,6))` yazar; bu C dönüşümü SIFIRA DOĞRU kırpar.
/// Yuvarlama kullanırsak en çok 1e-6 derece (~0.11 m) sapar ama bayt eşitliği bozulur,
/// o yüzden bilerek kırpıyoruz.
inline bool DegToMicroDegrees(const double deg, int32_t* out) {
  if (out == nullptr || !std::isfinite(deg)) {
    return false;
  }
  const double scaled = deg * 1000000.0;
  if (scaled < static_cast<double>(std::numeric_limits<int32_t>::min()) ||
      scaled > static_cast<double>(std::numeric_limits<int32_t>::max())) {
    return false;
  }
  *out = static_cast<int32_t>(scaled);
  return true;
}

inline bool WriteLatLon(uint8_t* dst, const double lat_deg, const double lon_deg,
                        const LatLonWireFormat fmt) {
  if (dst == nullptr || !std::isfinite(lat_deg) || !std::isfinite(lon_deg)) {
    return false;
  }
  if (fmt == LatLonWireFormat::kFloat32Degrees) {
    WriteF32Le(dst, static_cast<float>(lat_deg));
    WriteF32Le(dst + 4, static_cast<float>(lon_deg));
    return true;
  }
  int32_t lat_u = 0;
  int32_t lon_u = 0;
  if (!DegToMicroDegrees(lat_deg, &lat_u) || !DegToMicroDegrees(lon_deg, &lon_u)) {
    return false;
  }
  WriteI32Le(dst, lat_u);
  WriteI32Le(dst + 4, lon_u);
  return true;
}

inline int8_t ClampPitchDeg(const int pitch_deg) {
  return static_cast<int8_t>(std::clamp(pitch_deg, -128, 127));
}

inline int16_t ClampAltitudeM(const int altitude_m) {
  return static_cast<int16_t>(std::clamp(altitude_m, 0, 32767));
}

inline uint8_t ClampSpeedPct(const int speed_pct) {
  return static_cast<uint8_t>(std::clamp(speed_pct, 0, 100));
}

inline uint16_t ClampHeadingDeg(const int heading_deg) {
  int h = heading_deg % 360;
  if (h < 0) {
    h += 360;
  }
  return static_cast<uint16_t>(h);
}

/// @brief İstem 0: yükselme/alçalma — burun (pitch) açısı + hedef irtifa. 8 bayt.
inline PackedCommand PackAltitudeChange(const int pitch_deg, const int altitude_m) {
  PackedCommand out{};
  out.length = 8;
  out.bytes[0] = kCtrl0;
  out.bytes[1] = kCtrl1;
  out.bytes[2] = kModeByte;
  out.bytes[3] = kIstemAltitude;
  out.bytes[4] = static_cast<uint8_t>(ClampPitchDeg(pitch_deg));
  WriteI16Le(&out.bytes[5], ClampAltitudeM(altitude_m));
  out.bytes[7] = kTail;
  out.valid = true;
  return out;
}

/// @brief İstem 1: yönelme — hedef pusula açısı 0..359. 7 bayt.
///
/// @warning ÜRETİMDE KULLANMAYIN. Vendor kaynağında bu istemi üreten hiçbir
/// fonksiyon yok; yalnızca PDF'te tarif edilmiş, hiçbir uçuş testinde
/// denenmemiştir. Yön değişimi istem 2 (koordinata yönel) ile yapılır.
/// Burada yalnızca ileride bench doğrulaması yapılırsa diye duruyor.
inline PackedCommand PackHeadingChange(const int heading_deg) {
  PackedCommand out{};
  out.length = 7;
  out.bytes[0] = kCtrl0;
  out.bytes[1] = kCtrl1;
  out.bytes[2] = kModeByte;
  out.bytes[3] = kIstemHeading;
  WriteI16Le(&out.bytes[4], static_cast<int16_t>(ClampHeadingDeg(heading_deg)));
  out.bytes[6] = kTail;
  out.valid = true;
  return out;
}

/// @brief İstem 2: koordinat ile yön değiştirme. 14 bayt (vendor `write(...,14)`).
///
/// Ana yatay güdüm komutudur. Uzunluk PDF'in dediği 13 değil 14'tür; vendor her iki
/// uçmuş varyantta da 14 bayt yazar (bkz. @ref Cmd2TailVariant).
inline PackedCommand PackCoordinateSteer(
    const double lat_deg, const double lon_deg,
    const LatLonWireFormat fmt = LatLonWireFormat::kInt32MicroDegrees,
    const Cmd2TailVariant tail = Cmd2TailVariant::kTailAt12) {
  PackedCommand out{};
  out.length = 14;
  out.bytes[0] = kCtrl0;
  out.bytes[1] = kCtrl1;
  out.bytes[2] = kModeByte;
  out.bytes[3] = kIstemCoordinate;
  if (!WriteLatLon(&out.bytes[4], lat_deg, lon_deg, fmt)) {
    return out;
  }
  if (tail == Cmd2TailVariant::kTailAt13) {
    out.bytes[12] = 0;
    out.bytes[13] = kTail;
  } else {
    out.bytes[12] = kTail;
    out.bytes[13] = 0;
  }
  out.valid = true;
  return out;
}

/// @brief İstem 3: pitch + irtifa + itki oranı (%). 9 bayt.
///
/// @warning ÜRETİMDE KULLANILMAZ — itki oranı taşır. Gaz otopilota bırakıldı;
/// güdüm yalnızca istem 0 ve istem 2 üretir. Bench/A-B için duruyor.
inline PackedCommand PackAltitudeSpeed(const int pitch_deg, const int altitude_m,
                                       const int speed_pct) {
  PackedCommand out{};
  out.length = 9;
  out.bytes[0] = kCtrl0;
  out.bytes[1] = kCtrl1;
  out.bytes[2] = kModeByte;
  out.bytes[3] = kIstemAltitudeSpeed;
  out.bytes[4] = static_cast<uint8_t>(ClampPitchDeg(pitch_deg));
  WriteI16Le(&out.bytes[5], ClampAltitudeM(altitude_m));
  out.bytes[7] = ClampSpeedPct(speed_pct);
  out.bytes[8] = kTail;
  out.valid = true;
  return out;
}

/// @brief İstem 4: koordinat + pitch + irtifa + itki. 17 bayt.
///
/// Vendor'ın GPS takip testinde 1 Hz ile uçan komut budur (itki %45 sabit).
/// @warning ÜRETİMDE KULLANILMAZ — itki oranı taşır. Gaz otopilota bırakıldı.
/// Bench/A-B için duruyor.
inline PackedCommand PackCoordinateFull(
    const double lat_deg, const double lon_deg, const int pitch_deg, const int altitude_m,
    const int speed_pct,
    const LatLonWireFormat fmt = LatLonWireFormat::kInt32MicroDegrees) {
  PackedCommand out{};
  out.length = 17;
  out.bytes[0] = kCtrl0;
  out.bytes[1] = kCtrl1;
  out.bytes[2] = kModeByte;
  out.bytes[3] = kIstemCoordinateFull;
  if (!WriteLatLon(&out.bytes[4], lat_deg, lon_deg, fmt)) {
    return out;
  }
  out.bytes[12] = static_cast<uint8_t>(ClampPitchDeg(pitch_deg));
  WriteI16Le(&out.bytes[13], ClampAltitudeM(altitude_m));
  out.bytes[15] = ClampSpeedPct(speed_pct);
  out.bytes[16] = kTail;
  out.valid = true;
  return out;
}

/// @brief Paketlenmiş komutu köprü TX tipine çevirir.
inline SeyirModePacket ToSeyirModePacket(const PackedCommand& packed) {
  SeyirModePacket pkt{};
  if (!packed.valid || packed.length == 0 || packed.length > SeyirModePacket::kMaxBytes) {
    return pkt;
  }
  std::copy_n(packed.bytes.begin(), packed.length, pkt.bytes.begin());
  pkt.length = packed.length;
  pkt.valid = true;
  return pkt;
}

}  // namespace savasan::autopilot::seyir

#endif  // SAVASAN_AUTOPILOT_SEYIR_MODE_COMMANDS_HPP_
