/**
 * @file test_seyir_guidance_safety.cpp
 * @brief Kilit ile çarpışma emniyeti arasındaki mesafe kararlarını kilitler.
 *
 * Buradaki sayılar birbirine bağlıdır ve biri değişirse diğerleri anlamsız
 * kalır; test bu zinciri korur:
 *  - 57 m: kilidin sayılmaya başladığı azami mesafe. Şartname yatay VEYA dikey
 *    eksende %5 kaplama istiyor; dikey eksen dar olduğu için belirleyici odur
 *    (aynı hedef yatayda ancak 34 m'de %5 yapar).
 *  - 28 m: güdümün nişan aldığı nominal takip mesafesi — kilidi 4 sn boyunca
 *    kesintisiz tutabilmek için marjlı hedef.
 *  - 40 m: APF rakip itme yarıçapı. Nominal takip mesafesinden büyük olduğu
 *    için kovalanan hedef muaf tutulmazsa güdüm nişanına hiç yaklaşamaz.
 *  - 25 m: kilit bandı. Nominal mesafeyi serbest bırakmalı.
 *  - 18 m: KOPMA. Bandın altında, son çare.
 */
#include "runners/seyir_guidance.hpp"

#include <cmath>
#include <iostream>

namespace runners = savasan::runners;
namespace ctrl = savasan::control;

namespace {

constexpr double kOwnLat = 41.0;
constexpr double kOwnLon = 36.0;

/// @brief Kuzeye @p north_m metre ötedeki enlem (LocalFrame ölçeğiyle birebir).
double LatNorthOf(const double north_m) {
  return kOwnLat + north_m / ctrl::LocalFrame::kLatScale;
}

runners::SeyirGuidanceConfig MakeConfig() {
  runners::SeyirGuidanceConfig cfg{};
  cfg.enabled = true;
  return cfg;
}

runners::SeyirGuidanceInput MakeInput(const double t_s) {
  runners::SeyirGuidanceInput in{};
  in.t_s = t_s;
  in.own_lat = kOwnLat;
  in.own_lon = kOwnLon;
  in.own_alt_m = 100.0f;
  in.own_yaw_deg = 0.0f;  // kuzeye bakıyoruz
  in.own_speed_mps = 16.0f;
  in.gps_valid = true;
  in.own_yaw_valid = true;
  return in;
}

/// Hedef güneye, yani bize doğru yavaşça geliyor. Kaçan hedefte kestirici
/// gecikme telafisi için ileri sarma yapar ve test edilen mesafe kayar.
runners::RivalTargetSnapshot MakeRival(const int id, const double north_m, const double lon_off) {
  runners::RivalTargetSnapshot r{};
  r.valid = true;
  r.target_id = id;
  r.lat_deg = LatNorthOf(north_m);
  r.lon_deg = kOwnLon + lon_off;
  r.alt_m = 100.0f;
  r.spd_mps = 1.6f;
  r.hdg_deg = 180.0f;
  r.time_diff_ms = 100;
  return r;
}

/// @brief Kestiriciyi ısıtır: tek ölçümle tahmin üretilmeyebilir.
///
/// @param north_m Hedefin son (değerlendirilecek) mesafesi.
runners::SeyirGuidanceOutput RunTicks(const runners::SeyirGuidanceConfig& cfg,
                                      runners::SeyirGuidanceState* state, const int id,
                                      const double north_m, const int extra_rival_north_m = -1) {
  runners::SeyirGuidanceOutput out{};
  const int kTicks = 6;
  for (int i = 0; i < kTicks; ++i) {
    const double t = 1000.0 + i * 0.25;
    auto in = MakeInput(t);
    // Hedef hafifçe ilerlesin; aynı içerikli rapor tekrar sayılıp elenir.
    const double approach = static_cast<double>(kTicks - 1 - i) * 0.4;
    in.rivals.targets.push_back(MakeRival(id, north_m + approach, 0.0));
    if (extra_rival_north_m > 0) {
      in.rivals.targets.push_back(
          MakeRival(id + 50, static_cast<double>(extra_rival_north_m) + approach, 0.0002));
    }
    out = runners::ComputeSeyirGuidance(cfg, in, state);
  }
  return out;
}

int Fail(const char* msg, const int code) {
  std::cerr << "FAIL: " << msg << "\n";
  return code;
}

}  // namespace

int main() {
  const auto cfg = MakeConfig();

  // 1) Kilit fiziği: doluluk %5'e 57 m'de düşer (dikey eksen belirleyici).
  //    Bu sayı diğer tüm eşiklerin dayanağı; kamera/uçak varsayımı değişirse
  //    burası düşer ve mesafe kararlarının hepsi gözden geçirilmelidir.
  {
    ctrl::LocalFrame frame(kOwnLat, kOwnLon);
    ctrl::PursuitGuidance g(frame);
    ctrl::PursuitOwnState own{};
    own.has_track = true;
    own.track_rad = 3.14159265358979323846 / 2.0;  // kuzey
    ctrl::TargetEstimate tgt{};
    tgt.y = 57.0;
    const auto geo = g.ComputeLockGeometry(own, tgt);
    if (!geo.valid || std::abs(geo.fill_ratio - 0.05) > 0.002) {
      std::cerr << "FAIL: 57 m'de doluluk %5 olmali, alinan=" << geo.fill_ratio << "\n";
      return 1;
    }
    // Daha uzakta kilit sayılmamalı.
    tgt.y = 70.0;
    if (g.ComputeLockGeometry(own, tgt).in_hit_area) {
      return Fail("70 m'de kilit sayildi", 11);
    }
  }

  // 2) Kovalanan hedef APF itme yarıçapının (40 m) içinde olsa bile kaçış
  //    tetiklenmemeli; yoksa kilit mesafesine hiç inemeyiz.
  {
    runners::SeyirGuidanceState st{};
    const auto out = RunTicks(cfg, &st, 3, 30.0);
    if (out.phase == runners::SeyirPhase::kKacinma && out.selected_target_id < 0) {
      return Fail("kovalanan hedef APF'yi tetikledi", 2);
    }
    if (out.selected_target_id != 3) {
      return Fail("kovalanan hedef secilmedi", 3);
    }
    if (!out.active) {
      return Fail("kestirim/gudum uretilmedi (zincir kopuk)", 30);
    }
  }

  // 3) Kovalamadığımız bir rakip aynı yarıçapa girerse kaçış tetiklenmeli:
  //    muafiyet yalnızca tek hedefe verilir, diğer uçaklara koruma sürer.
  {
    runners::SeyirGuidanceState st{};
    const auto out = RunTicks(cfg, &st, 3, 30.0, 35);
    if (out.phase != runners::SeyirPhase::kKacinma) {
      return Fail("ucuncu ucak APF'yi tetiklemedi", 4);
    }
  }

  // 4) Kilit penceresi (28-34 m) bant korumasından etkilenmemeli.
  {
    runners::SeyirGuidanceState st{};
    const auto out = RunTicks(cfg, &st, 3, 30.0);
    if (!out.active) {
      return Fail("kilit penceresinde gudum susmus", 50);
    }
    if (out.band_guard_active) {
      return Fail("30 m'de band korumasi kilidi engelledi", 5);
    }
  }

  // 5) Band eşiğinin (25 m) altında nişan yana kırılmalı ve açılmalı.
  {
    runners::SeyirGuidanceState st{};
    const auto out = RunTicks(cfg, &st, 3, 15.0);
    if (!out.band_guard_active) {
      return Fail("15 m'de band korumasi devreye girmedi", 6);
    }
    if (out.aim_range_m < cfg.band_min_separation_m) {
      return Fail("band korumasi nisani yeterince acmadi", 7);
    }
    // Kırılma görüş konisinin içinde kalmalı: hedef kuzeyde, nişan da kuzeye
    // yakın olmalı — aksi hâlde hedefi kaybederiz.
    const float bearing = out.aim_bearing_deg;
    const float off = std::abs(bearing > 180.0f ? bearing - 360.0f : bearing);
    if (off > cfg.band_break_angle_deg + 1.0f) {
      std::cerr << "FAIL: kirilma cok genis, kerteriz=" << bearing << "\n";
      return 8;
    }
  }

  // 6) Bant eşiği kilit mesafesinin altında kalmalı — 34 m'nin üstüne
  //    çıkarılırsa kilit matematiksel olarak imkânsız olur.
  if (cfg.band_min_separation_m >= cfg.pursuit.nominal_range_m) {
    return Fail("band esigi nominal takip mesafesini yutuyor", 9);
  }
  if (cfg.band_min_separation_m <= cfg.pursuit.breakaway_range_m) {
    return Fail("band esigi KOPMA'nin altina dusmus, yumusak katman kalmadi", 10);
  }

  std::cout << "OK test_seyir_guidance_safety\n";
  return 0;
}
