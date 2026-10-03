/**
 * @file test_seyir_mode_commands.cpp
 * @brief Seyir Modu byte paketleyici birim testleri.
 *
 * Beklenen baytlar vendor'ın uçurduğu `Komut_Ayarlar.ino` ile hizalıdır; PDF ile
 * çeliştiği yerlerde uçan kod esas alınmıştır.
 *
 * Not: Release build'de assert no-op; bu yüzden dönüş kodu ile doğrulanır.
 */
#include "autopilot/seyir_mode_commands.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace seyir = savasan::autopilot::seyir;

namespace {

bool BytesEq(const seyir::PackedCommand& pkt, const uint8_t* expected, const size_t n) {
  if (!pkt.valid || pkt.length != n) {
    return false;
  }
  for (size_t i = 0; i < n; ++i) {
    if (pkt.bytes[i] != expected[i]) {
      return false;
    }
  }
  return true;
}

}  // namespace

int main() {
  // İstem 0: PDF örneği — pitch=10, irtifa=100 → 84 86 19 0 10 100 0 42
  {
    const auto pkt = seyir::PackAltitudeChange(10, 100);
    const uint8_t exp[] = {84, 86, 19, 0, 10, 100, 0, 42};
    if (!BytesEq(pkt, exp, sizeof(exp))) {
      std::cerr << "FAIL: PackAltitudeChange PDF ornegi\n";
      return 1;
    }
  }

  // İstem 1: PDF örneği — heading=180 → 84 86 19 1 180 0 42
  {
    const auto pkt = seyir::PackHeadingChange(180);
    const uint8_t exp[] = {84, 86, 19, 1, 180, 0, 42};
    if (!BytesEq(pkt, exp, sizeof(exp))) {
      std::cerr << "FAIL: PackHeadingChange PDF ornegi\n";
      return 2;
    }
  }

  // Heading sarma: 360 → 0, -90 → 270
  {
    const auto a = seyir::PackHeadingChange(360);
    if (!a.valid || a.bytes[4] != 0 || a.bytes[5] != 0) {
      std::cerr << "FAIL: heading 360 wrap\n";
      return 3;
    }
    const auto b = seyir::PackHeadingChange(-90);
    if (!b.valid || b.bytes[4] != static_cast<uint8_t>(270 & 0xFF) ||
        b.bytes[5] != static_cast<uint8_t>((270 >> 8) & 0xFF)) {
      std::cerr << "FAIL: heading -90 wrap\n";
      return 4;
    }
  }

  // İstem 2: int32×10⁶, 14 bayt — vendor Komut_2_Dizilim: [12]=42, write(...,14)
  {
    const auto pkt = seyir::PackCoordinateSteer(37.030148, 37.311442);
    if (!pkt.valid || pkt.length != 14 || pkt.bytes[3] != 2 || pkt.bytes[12] != 42 ||
        pkt.bytes[13] != 0) {
      std::cerr << "FAIL: PackCoordinateSteer int32 baslik/kuyruk\n";
      return 5;
    }
    int32_t lat = 0;
    int32_t lon = 0;
    std::memcpy(&lat, &pkt.bytes[4], 4);
    std::memcpy(&lon, &pkt.bytes[8], 4);
    if (lat != 37030148 || lon != 37311442) {
      std::cerr << "FAIL: PackCoordinateSteer int32 microdeg lat=" << lat << " lon=" << lon
                << "\n";
      return 6;
    }
  }

  // İstem 2 kuyruk varyantı: [12]=0, [13]=42 (koordinat yönelim dosyası)
  {
    const auto pkt = seyir::PackCoordinateSteer(37.030148, 37.311442,
                                                seyir::LatLonWireFormat::kInt32MicroDegrees,
                                                seyir::Cmd2TailVariant::kTailAt13);
    if (!pkt.valid || pkt.length != 14 || pkt.bytes[12] != 0 || pkt.bytes[13] != 42) {
      std::cerr << "FAIL: PackCoordinateSteer kTailAt13 varyanti\n";
      return 15;
    }
  }

  // Koordinat kırpması: vendor (int32_t) cast'i SIFIRA DOĞRU kırpar, yuvarlamaz.
  // 37.0301489 → 37030148.9; kırpma 37030148, yuvarlama 37030149 verirdi.
  {
    const auto pos = seyir::PackCoordinateSteer(37.0301489, -37.0301489);
    int32_t lat = 0;
    int32_t lon = 0;
    std::memcpy(&lat, &pos.bytes[4], 4);
    std::memcpy(&lon, &pos.bytes[8], 4);
    if (!pos.valid || lat != 37030148 || lon != -37030148) {
      std::cerr << "FAIL: int32 kirpma (truncation) lat=" << lat << " lon=" << lon << "\n";
      return 16;
    }
  }

  // İstem 2: float32 — yalnız PDF örneği; üretimde kullanılmaz, bench için doğrulanır
  {
    const auto pkt = seyir::PackCoordinateSteer(37.030148, 37.311442,
                                               seyir::LatLonWireFormat::kFloat32Degrees);
    if (!pkt.valid || pkt.length != 14 || pkt.bytes[12] != 42) {
      std::cerr << "FAIL: PackCoordinateSteer float32 baslik\n";
      return 7;
    }
    const uint8_t pdf_lat[] = {223, 30, 20, 66};
    for (int i = 0; i < 4; ++i) {
      if (pkt.bytes[4 + i] != pdf_lat[i]) {
        std::cerr << "FAIL: float32 lat PDF byte mismatch i=" << i << "\n";
        return 8;
      }
    }
    // PDF lon örneği 252 62 21 66 ≈ 37.311508; exact 37.311442 → 235 62 21 66
    const uint8_t exact_lon[] = {235, 62, 21, 66};
    for (int i = 0; i < 4; ++i) {
      if (pkt.bytes[8 + i] != exact_lon[i]) {
        std::cerr << "FAIL: float32 lon exact byte mismatch i=" << i
                  << " got=" << static_cast<int>(pkt.bytes[8 + i]) << "\n";
        return 9;
      }
    }
  }

  // İstem 3
  {
    const auto pkt = seyir::PackAltitudeSpeed(8, 120, 45);
    const uint8_t exp[] = {84, 86, 19, 3, 8, 120, 0, 45, 42};
    if (!BytesEq(pkt, exp, sizeof(exp))) {
      std::cerr << "FAIL: PackAltitudeSpeed\n";
      return 10;
    }
  }

  // İstem 4
  {
    const auto pkt = seyir::PackCoordinateFull(41.0, 36.0, 8, 100, 45);
    if (!pkt.valid || pkt.length != 17 || pkt.bytes[3] != 4 || pkt.bytes[12] != 8 ||
        pkt.bytes[13] != 100 || pkt.bytes[14] != 0 || pkt.bytes[15] != 45 ||
        pkt.bytes[16] != 42) {
      std::cerr << "FAIL: PackCoordinateFull\n";
      return 11;
    }
    int32_t lat = 0;
    std::memcpy(&lat, &pkt.bytes[4], 4);
    if (lat != 41000000) {
      std::cerr << "FAIL: PackCoordinateFull lat\n";
      return 12;
    }
  }

  // Geçersiz lat
  {
    const auto pkt = seyir::PackCoordinateSteer(NAN, 36.0);
    if (pkt.valid) {
      std::cerr << "FAIL: NAN lat kabul edildi\n";
      return 13;
    }
  }

  // ToSeyirModePacket
  {
    const auto packed = seyir::PackHeadingChange(90);
    const auto wire = seyir::ToSeyirModePacket(packed);
    if (!wire.valid || wire.length != 7 || wire.bytes[4] != 90 || wire.bytes[6] != 42) {
      std::cerr << "FAIL: ToSeyirModePacket\n";
      return 14;
    }
  }

  std::cout << "OK test_seyir_mode_commands\n";
  return 0;
}
