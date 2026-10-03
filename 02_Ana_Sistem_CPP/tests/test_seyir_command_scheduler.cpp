/**
 * @file test_seyir_command_scheduler.cpp
 * @brief Tek paket düzeni: kadans freni, akış kesintisi, irtifa değişimi/tazeleme.
 */
#include "runners/seyir_command_scheduler.hpp"

#include <chrono>
#include <iostream>

namespace runners = savasan::runners;
namespace seyir = savasan::autopilot::seyir;

namespace {

/// @brief Zamanlayıcıya verilecek iki niyetli (koordinat + irtifa) çıktı üretir.
runners::SeyirGuidanceOutput MakeOutput(const int alt_m) {
  runners::SeyirGuidanceOutput out{};
  out.active = true;
  out.coord_cmd = seyir::ToSeyirModePacket(seyir::PackCoordinateSteer(41.0, 36.0));
  out.has_coord = out.coord_cmd.valid;
  out.alt_cmd_m = alt_m;
  out.alt_cmd = seyir::ToSeyirModePacket(seyir::PackAltitudeChange(8, alt_m));
  out.has_alt = out.alt_cmd.valid;
  return out;
}

uint8_t Istem(const runners::SeyirScheduleResult& r) {
  return r.packet.length > 3 ? r.packet.bytes[3] : 255U;
}

}  // namespace

int main() {
  const runners::SeyirSchedulerConfig cfg{};  // 0.2 s kadans, 2 s kesinti, 2 m eps, 3 s tazeleme
  runners::SeyirSchedulerState st{};
  const auto t0 = std::chrono::steady_clock::time_point{} + std::chrono::seconds(1000);

  // İlk gönderim: hiç paket gitmemiş → akış kesintisi sayılır, irtifa gider.
  {
    const auto r = runners::SelectSeyirCommand(cfg, MakeOutput(100), &st, t0);
    if (!r.send || Istem(r) != seyir::kIstemAltitude ||
        r.reason != runners::SeyirSendReason::kAltFlowGap) {
      std::cerr << "FAIL: ilk paket irtifa olmali\n";
      return 1;
    }
  }

  // Kadans freni: 100 ms sonra gönderim olmamalı.
  {
    const auto r =
        runners::SelectSeyirCommand(cfg, MakeOutput(100), &st, t0 + std::chrono::milliseconds(100));
    if (r.send || r.reason != runners::SeyirSendReason::kRateLimited) {
      std::cerr << "FAIL: kadans freni tutmadi\n";
      return 2;
    }
  }

  // Fren açıldıktan sonra irtifa değişmediyse koordinat gider.
  auto t = t0 + std::chrono::milliseconds(250);
  {
    const auto r = runners::SelectSeyirCommand(cfg, MakeOutput(100), &st, t);
    if (!r.send || Istem(r) != seyir::kIstemCoordinate ||
        r.reason != runners::SeyirSendReason::kCoord) {
      std::cerr << "FAIL: koordinat beklenirken istem=" << static_cast<int>(Istem(r)) << "\n";
      return 3;
    }
  }

  // İrtifa eşik kadar kayınca o tikte irtifa gider.
  t += std::chrono::milliseconds(250);
  {
    const auto r = runners::SelectSeyirCommand(cfg, MakeOutput(105), &st, t);
    if (!r.send || Istem(r) != seyir::kIstemAltitude ||
        r.reason != runners::SeyirSendReason::kAltChanged) {
      std::cerr << "FAIL: irtifa degisimi yakalanmadi\n";
      return 4;
    }
  }

  // Eşik altı sapma (1 m) irtifa göndertmemeli.
  t += std::chrono::milliseconds(250);
  {
    const auto r = runners::SelectSeyirCommand(cfg, MakeOutput(106), &st, t);
    if (!r.send || Istem(r) != seyir::kIstemCoordinate) {
      std::cerr << "FAIL: esik alti sapma irtifa gonderdi\n";
      return 5;
    }
  }

  // Akış sürerken (250 ms'de bir tik) irtifa sabit kalsa da 3 sn'de bir tazelenmeli.
  // Kesintisiz akış olduğu için bu kAltFlowGap değil kAltRefresh olmalı.
  {
    bool refreshed = false;
    for (int i = 0; i < 20 && !refreshed; ++i) {
      t += std::chrono::milliseconds(250);
      const auto r = runners::SelectSeyirCommand(cfg, MakeOutput(106), &st, t);
      if (!r.send) {
        std::cerr << "FAIL: kesintisiz akista gonderim durdu\n";
        return 6;
      }
      if (r.reason == runners::SeyirSendReason::kAltFlowGap) {
        std::cerr << "FAIL: kesintisiz akis yanlislikla kesinti sayildi\n";
        return 6;
      }
      refreshed = r.reason == runners::SeyirSendReason::kAltRefresh &&
                  Istem(r) == seyir::kIstemAltitude;
    }
    if (!refreshed) {
      std::cerr << "FAIL: periyodik irtifa tazeleme yok\n";
      return 6;
    }
  }

  // Akış 2 sn'den uzun kesilirse kesintiden sonraki ilk paket yine irtifa olmalı.
  t += std::chrono::milliseconds(2500);
  {
    const auto r = runners::SelectSeyirCommand(cfg, MakeOutput(105), &st, t);
    if (!r.send || Istem(r) != seyir::kIstemAltitude ||
        r.reason != runners::SeyirSendReason::kAltFlowGap) {
      std::cerr << "FAIL: akis kesintisi sonrasi irtifa tazelenmedi\n";
      return 7;
    }
  }

  // Güdüm pasifken hiçbir şey gönderilmez.
  {
    runners::SeyirGuidanceOutput idle{};
    const auto r = runners::SelectSeyirCommand(cfg, idle, &st, t + std::chrono::seconds(1));
    if (r.send) {
      std::cerr << "FAIL: pasif gudumde paket gitti\n";
      return 8;
    }
  }

  std::cout << "OK test_seyir_command_scheduler\n";
  return 0;
}
