/**
 * @file test_seyir_goto.cpp
 * @brief Operatör nokta hedefinin (RF 0x2A) güdümdeki yerini kilitler.
 *
 * Şartname §6.1.7 yer istasyonundan "varış noktası tanımlama" komutunu
 * otonomiyi bozmayan komutlar arasında sayar; bu yüzden nokta hedefi takibin
 * önüne geçebilir. Ama hiçbir operatör komutu emniyetin önüne geçemez:
 *
 *   geofence kaçışı  >  APF  >  NOKTA HEDEFİ  >  takip
 *
 * Test bu sırayı ve noktanın süzgeçten geçme zorunluluğunu korur. Süzgeç
 * gevşerse operatörün yanlış tıklaması saha ihlaline (-200 puan, 10 sn'de
 * eleme) veya HSS'e (-5 puan/sn) girmemize yol açar.
 */
#include "runners/seyir_guidance.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>

namespace runners = savasan::runners;
namespace ctrl = savasan::control;

namespace {

constexpr double kOwnLat = 41.0;
constexpr double kOwnLon = 36.0;
constexpr double kLonScale = 111320.0 * 0.7547095802227720;  // cos(41°)

double LatNorthOf(const double north_m) {
  return kOwnLat + north_m / ctrl::LocalFrame::kLatScale;
}

double LonEastOf(const double east_m) { return kOwnLon + east_m / kLonScale; }

runners::SeyirGuidanceConfig MakeConfig() {
  runners::SeyirGuidanceConfig cfg{};
  cfg.enabled = true;
  return cfg;
}

/// @brief Kendi konumumuz merkezde olacak şekilde kare saha sınırı.
runners::BoundarySnapshot MakeBoundary(const double half_m) {
  runners::BoundarySnapshot b{};
  const double n = half_m;
  b.corners.push_back({LatNorthOf(n), LonEastOf(-n)});
  b.corners.push_back({LatNorthOf(n), LonEastOf(n)});
  b.corners.push_back({LatNorthOf(-n), LonEastOf(n)});
  b.corners.push_back({LatNorthOf(-n), LonEastOf(-n)});
  return b;
}

runners::SeyirGuidanceInput MakeInput(const double t_s) {
  runners::SeyirGuidanceInput in{};
  in.t_s = t_s;
  in.own_lat = kOwnLat;
  in.own_lon = kOwnLon;
  in.own_alt_m = 100.0f;
  in.own_yaw_deg = 0.0f;
  in.own_speed_mps = 16.0f;
  in.gps_valid = true;
  in.own_yaw_valid = true;
  in.boundary = MakeBoundary(1000.0);
  return in;
}

runners::GotoTargetSnapshot MakeGoto(const int seq, const double north_m, const double east_m,
                                     const bool has_alt = false, const float alt_m = 0.0f) {
  runners::GotoTargetSnapshot g{};
  g.valid = true;
  g.seq = seq;
  g.lat_deg = LatNorthOf(north_m);
  g.lon_deg = LonEastOf(east_m);
  g.has_alt = has_alt;
  g.alt_m = alt_m;
  return g;
}

/// @brief Kovalanabilir bir rakip (takip zincirini gerçekten çalıştırır).
runners::RivalTargetSnapshot MakeRival(const int id, const double north_m) {
  runners::RivalTargetSnapshot r{};
  r.valid = true;
  r.target_id = id;
  r.lat_deg = LatNorthOf(north_m);
  r.lon_deg = kOwnLon;
  r.alt_m = 100.0f;
  r.spd_mps = 1.6f;
  r.hdg_deg = 180.0f;
  r.time_diff_ms = 100;
  return r;
}

int Fail(const char* msg, const int code) {
  std::cerr << "FAIL: " << msg << "\n";
  return code;
}

}  // namespace

int main() {
  const auto cfg = MakeConfig();

  // 1) Geçerli nokta kabul edilir ve koordinat komutu üretir.
  {
    runners::SeyirGuidanceState st{};
    auto in = MakeInput(1000.0);
    in.goto_target = MakeGoto(1, 500.0, 0.0);
    const auto out = runners::ComputeSeyirGuidance(cfg, in, &st);
    if (out.goto_state != runners::GotoState::kActive) {
      return Fail("saha ici nokta kabul edilmedi", 1);
    }
    if (!out.goto_active || !out.has_coord) {
      return Fail("nokta kabul edildi ama koordinat komutu uretilmedi", 2);
    }
    if (std::abs(out.goto_distance_m - 500.0f) > 5.0f) {
      std::cerr << "FAIL: mesafe hatali=" << out.goto_distance_m << "\n";
      return 3;
    }
  }

  // 2) Nokta takibin ÜSTÜNDE: kovalanacak rakip varken bile noktaya gidilir.
  {
    runners::SeyirGuidanceState st{};
    runners::SeyirGuidanceOutput out{};
    for (int i = 0; i < 6; ++i) {
      auto in = MakeInput(1000.0 + i * 0.25);
      in.rivals.targets.push_back(MakeRival(7, 300.0 - i * 0.4));
      in.goto_target = MakeGoto(1, -500.0, 0.0);  // rakibin tam ters yönü
      out = runners::ComputeSeyirGuidance(cfg, in, &st);
    }
    if (!out.goto_active) {
      return Fail("rakip varken nokta hedefi ezildi", 4);
    }
    if (out.aim_lat_deg >= kOwnLat) {
      return Fail("nisan noktaya degil rakibe kurulmus", 5);
    }
  }

  // 3) Varış yarıçapına girilince nokta düşer ve takip devralır.
  {
    runners::SeyirGuidanceState st{};
    runners::SeyirGuidanceOutput out{};
    for (int i = 0; i < 6; ++i) {
      auto in = MakeInput(1000.0 + i * 0.25);
      in.rivals.targets.push_back(MakeRival(7, 300.0 - i * 0.4));
      // Nokta varış yarıçapının içinde: hemen "varıldı" sayılmalı.
      in.goto_target = MakeGoto(1, cfg.goto_arrive_radius_m * 0.5, 0.0);
      out = runners::ComputeSeyirGuidance(cfg, in, &st);
    }
    if (out.goto_state != runners::GotoState::kArrived) {
      return Fail("varis yaricapinda nokta dusmedi", 6);
    }
    if (out.goto_active) {
      return Fail("varildi ama nokta komutu uretilmeye devam ediyor", 7);
    }
    if (out.selected_target_id != 7) {
      return Fail("nokta dustukten sonra takip devralmadi", 8);
    }
  }

  // 4) Saha dışındaki nokta reddedilir — komut üretilmez.
  {
    runners::SeyirGuidanceState st{};
    auto in = MakeInput(1000.0);
    in.goto_target = MakeGoto(2, 5000.0, 0.0);
    const auto out = runners::ComputeSeyirGuidance(cfg, in, &st);
    if (out.goto_state != runners::GotoState::kRejected) {
      return Fail("saha disi nokta kabul edildi", 9);
    }
    if (out.goto_reason != runners::GotoRejectReason::kOutsideBoundary) {
      return Fail("saha disi red sebebi yanlis", 10);
    }
    if (out.goto_active) {
      return Fail("reddedilen noktaya komut uretildi", 11);
    }
  }

  // 5) HSS içindeki nokta reddedilir — hakem HSS'i açtıktan sonra operatör
  //    oraya tıklarsa uçak gitmemeli (-5 puan/sn).
  {
    runners::SeyirGuidanceState st{};
    auto in = MakeInput(1000.0);
    savasan::competition::HssKoordinatBilgisi z{};
    z.id = 4;
    z.hss_enlem = LatNorthOf(400.0);
    z.hss_boylam = kOwnLon;
    z.hss_yaricap = 100.0f;
    in.hss_zones.push_back(z);
    in.goto_target = MakeGoto(3, 400.0, 0.0);  // tam HSS merkezi
    const auto out = runners::ComputeSeyirGuidance(cfg, in, &st);
    if (out.goto_state != runners::GotoState::kRejected) {
      return Fail("HSS icindeki nokta kabul edildi", 12);
    }
    if (out.goto_reason != runners::GotoRejectReason::kInsideHss) {
      return Fail("HSS red sebebi yanlis", 13);
    }
  }

  // 6) Nokta güvenli ama yol HSS'in içinden geçiyorsa reddedilir.
  {
    runners::SeyirGuidanceState st{};
    auto in = MakeInput(1000.0);
    savasan::competition::HssKoordinatBilgisi z{};
    z.id = 5;
    z.hss_enlem = LatNorthOf(300.0);
    z.hss_boylam = kOwnLon;
    z.hss_yaricap = 100.0f;
    in.hss_zones.push_back(z);
    in.goto_target = MakeGoto(4, 600.0, 0.0);  // HSS'in ötesi, düz yol keser
    const auto out = runners::ComputeSeyirGuidance(cfg, in, &st);
    if (out.goto_state != runners::GotoState::kRejected) {
      return Fail("rotasi HSS kesen nokta kabul edildi", 14);
    }
    if (out.goto_reason != runners::GotoRejectReason::kRouteBlocked) {
      return Fail("rota red sebebi yanlis", 15);
    }
  }

  // 7) Sınır verisi yokken nokta doğrulanamaz, körlemesine uçulmaz.
  {
    runners::SeyirGuidanceState st{};
    auto in = MakeInput(1000.0);
    in.boundary.corners.clear();
    in.goto_target = MakeGoto(5, 500.0, 0.0);
    const auto out = runners::ComputeSeyirGuidance(cfg, in, &st);
    if (out.goto_state != runners::GotoState::kRejected ||
        out.goto_reason != runners::GotoRejectReason::kNoBoundaryData) {
      return Fail("sinir verisi yokken nokta reddedilmedi", 16);
    }
  }

  // 8) Operatör iptal ederse (valid=0) takip devralır.
  {
    runners::SeyirGuidanceState st{};
    runners::SeyirGuidanceOutput out{};
    for (int i = 0; i < 6; ++i) {
      auto in = MakeInput(1000.0 + i * 0.25);
      in.rivals.targets.push_back(MakeRival(7, 300.0 - i * 0.4));
      if (i < 3) {
        in.goto_target = MakeGoto(1, -500.0, 0.0);
      }
      out = runners::ComputeSeyirGuidance(cfg, in, &st);
    }
    if (out.goto_active) {
      return Fail("iptal sonrasi nokta komutu surdu", 17);
    }
    if (out.selected_target_id != 7 || !out.active) {
      return Fail("iptal sonrasi takip devralmadi", 18);
    }
  }

  // 9) EMNİYET: nokta aktifken bile geofence kaçışı önce gelir. Uçak sınıra
  //    yapışıksa operatör komutu beklemez.
  {
    runners::SeyirGuidanceState st{};
    auto in = MakeInput(1000.0);
    in.boundary = MakeBoundary(60.0);  // önleyici bandın içindeyiz
    in.goto_target = MakeGoto(1, 40.0, 0.0);
    const auto out = runners::ComputeSeyirGuidance(cfg, in, &st);
    if (out.phase != runners::SeyirPhase::kKacinma) {
      return Fail("sinira yapisikken geofence kacisi devreye girmedi", 19);
    }
    if (out.goto_active) {
      return Fail("geofence kacisi sirasinda nokta komutu uretildi", 20);
    }
  }

  // 10) İrtifa opsiyonu: 10 baytlık paket geldiyse istenen irtifa uygulanır.
  {
    runners::SeyirGuidanceState st{};
    auto in = MakeInput(1000.0);
    in.goto_target = MakeGoto(1, 500.0, 0.0, true, 150.0f);
    const auto out = runners::ComputeSeyirGuidance(cfg, in, &st);
    if (out.alt_cmd_m != 150) {
      std::cerr << "FAIL: istenen irtifa uygulanmadi, alinan=" << out.alt_cmd_m << "\n";
      return 21;
    }
  }

  // 11) İrtifa istenmediyse mevcut irtifa korunur (8 baytlık paket).
  {
    runners::SeyirGuidanceState st{};
    auto in = MakeInput(1000.0);
    in.goto_target = MakeGoto(1, 500.0, 0.0);
    const auto out = runners::ComputeSeyirGuidance(cfg, in, &st);
    if (out.alt_cmd_m != static_cast<int>(in.own_alt_m)) {
      std::cerr << "FAIL: irtifa korunmadi, alinan=" << out.alt_cmd_m << "\n";
      return 22;
    }
  }

  // 12) IPC biçimi — AlpaguLink'in yazdığı dosyayı okuyabilmeliyiz. Buradaki
  //     metin AlpaguLink.py `parse_goto` çıktısının birebir aynısıdır; iki
  //     taraftan biri değişip diğeri unutulursa nokta hedefi sessizce ölür.
  {
    const char* path = "/tmp/savasan_goto_test.env";
    {
      std::ofstream f(path);
      f << "valid=1\nseq=7\nlat=41.004500000\nlon=36.005500000\nhas_alt=1\nalt_m=120.0\n";
    }
    const auto g = runners::ReadGotoFile(path);
    std::remove(path);
    if (!g.valid || g.seq != 7 || !g.has_alt) {
      return Fail("AlpaguLink nokta dosyasi cozulemedi", 23);
    }
    if (std::abs(g.lat_deg - 41.0045) > 1e-7 || std::abs(g.lon_deg - 36.0055) > 1e-7) {
      return Fail("nokta koordinati hatali cozuldu", 24);
    }
    if (std::abs(g.alt_m - 120.0f) > 0.01f) {
      return Fail("nokta irtifasi hatali cozuldu", 25);
    }
  }

  // 13) İptal dosyası (valid=0) geçersiz nokta üretmeli.
  {
    const char* path = "/tmp/savasan_goto_test.env";
    {
      std::ofstream f(path);
      f << "valid=0\n";
    }
    const auto g = runners::ReadGotoFile(path);
    std::remove(path);
    if (g.valid) {
      return Fail("iptal dosyasi hala gecerli nokta veriyor", 26);
    }
  }

  std::cout << "OK test_seyir_goto\n";
  return 0;
}
