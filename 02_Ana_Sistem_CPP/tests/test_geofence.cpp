/**
 * @file test_geofence.cpp
 * @brief Saha sınırı + HSS süzgeci ve kaçış hedefi üretimi.
 *
 * Kaçışın iki bilinen tuzağı burada sabitlenir:
 *  - Kaçış rotası HSS'in içinden geçmemeli (eski "hep merkeze git" davranışı
 *    HSS merkez hattına giriyordu).
 *  - Sol ve sağ eşit yarışmalı; sabit merkez hedefi devriye yönü yüzünden
 *    kaçışların çoğunu aynı tarafa döndürüyordu.
 */
#include "evasion/geofence.hpp"

#include <cmath>
#include <iostream>

namespace ev = savasan::evasion;
namespace ctrl = savasan::control;

namespace {

constexpr double kRefLat = 41.0;
constexpr double kRefLon = 36.0;

/// @brief Merkezi referans noktası olan, yaklaşık 1000x1000 m kare saha.
ev::Geofence MakeField(const ev::GeofenceSettings& s = ev::GeofenceSettings{}) {
  const ctrl::LocalFrame frame(kRefLat, kRefLon);
  ev::Geofence g(frame, s);
  std::vector<ctrl::GeoPoint> corners;
  for (const auto& xy : {std::pair<double, double>{-500.0, -500.0},
                         {500.0, -500.0},
                         {500.0, 500.0},
                         {-500.0, 500.0}}) {
    const auto geo = frame.ToGeo(xy.first, xy.second);
    corners.push_back(geo);
  }
  g.SetBoundaryLatLon(corners);
  return g;
}

int Fail(const char* msg, const int code) {
  std::cerr << "FAIL: " << msg << "\n";
  return code;
}

}  // namespace

int main() {
  const ctrl::LocalFrame frame(kRefLat, kRefLon);

  // 1) Sınır verisi yoksa hiçbir nokta güvenli sayılmamalı: veri gelmeden
  //    sahaya güvenmek, sahanın tamamını serbest ilan etmekle aynı şeydir.
  {
    ev::Geofence empty(frame);
    if (empty.has_boundary() || empty.PointSafe(0.0, 0.0)) {
      return Fail("sinir yokken nokta guvenli sayildi", 1);
    }
  }

  // 2) İçeride/dışarıda ve kenar payı.
  {
    auto g = MakeField();
    if (!g.InsidePolygon(0.0, 0.0) || g.InsidePolygon(600.0, 0.0)) {
      return Fail("poligon icinde/disinda hatasi", 2);
    }
    if (!g.PointSafe(0.0, 0.0)) {
      return Fail("saha merkezi guvenli degil", 3);
    }
    // Kenara 10 m kala (pay 30 m) reddedilmeli.
    if (g.PointSafe(490.0, 0.0)) {
      return Fail("kenar payi uygulanmadi", 4);
    }
    if (g.Violation(600.0, 0.0) != ev::GeofenceViolation::kOutsideBoundary) {
      return Fail("sinir disi ihlali gorulmedi", 5);
    }
  }

  // 3) HSS: içi ihlal, payı içinde güvensiz, uzağı güvenli.
  {
    auto g = MakeField();
    const auto c = frame.ToGeo(200.0, 0.0);
    g.SetHssLatLon({ev::HssDisc{7, c.lat_deg, c.lon_deg, 100.0}});

    int id = -1;
    if (g.Violation(200.0, 0.0, &id) != ev::GeofenceViolation::kInsideHss || id != 7) {
      return Fail("HSS ici ihlali gorulmedi", 6);
    }
    if (g.PointSafe(310.0, 0.0)) {  // 110 m: yaricap 100 + pay 20 icinde
      return Fail("HSS payi uygulanmadi", 7);
    }
    if (!g.PointSafe(360.0, 0.0)) {  // 160 m: pay disinda
      return Fail("HSS'ten uzak nokta reddedildi", 8);
    }
    // Rota HSS'in içinden geçiyorsa reddedilmeli (uç noktalar güvenli olsa da).
    if (g.RouteSafe(-400.0, 0.0, 400.0, 0.0)) {
      return Fail("HSS'i kesen rota kabul edildi", 9);
    }
    // Kuzeyden dolanan rota geçmeli.
    if (!g.RouteSafe(-400.0, 300.0, 400.0, 300.0)) {
      return Fail("temiz rota reddedildi", 10);
    }
  }

  // 4) Önleyici bant: ihlal olmasa da kenara yaklaşınca kaçış istenmeli.
  {
    auto g = MakeField();
    const double band = g.PreventiveBandM(16.0);
    if (band < 60.0) {
      return Fail("onleyici bant taban degerin altinda", 11);
    }
    // Merkez: kaçış gerekmez.
    if (g.EvaluateEscape(0.0, 0.0, 16.0, true, 0.0).needed) {
      return Fail("merkezde gereksiz kacis", 12);
    }
    // Kenara bant kadar yaklaş: kaçış gerekli ve hedefi olmalı.
    const auto esc = g.EvaluateEscape(500.0 - band * 0.5, 0.0, 16.0, true, 0.0);
    if (!esc.needed || esc.reason != ev::GeofenceViolation::kNearBoundary) {
      return Fail("sinir yakininda kacis tetiklenmedi", 13);
    }
    if (!esc.has_target) {
      return Fail("kacis hedefsiz kaldi", 14);
    }
    if (!g.PointSafe(esc.target_x, esc.target_y)) {
      return Fail("kacis hedefi guvenli degil", 15);
    }
  }

  // 5) HSS'in içindeysek en kısa radyal çıkış verilmeli.
  {
    auto g = MakeField();
    const auto c = frame.ToGeo(0.0, 0.0);
    g.SetHssLatLon({ev::HssDisc{3, c.lat_deg, c.lon_deg, 120.0}});
    const auto esc = g.EvaluateEscape(50.0, 0.0, 16.0, true, 3.14159265358979323846);
    if (!esc.needed || esc.reason != ev::GeofenceViolation::kInsideHss) {
      return Fail("HSS icinde kacis tetiklenmedi", 16);
    }
    if (!esc.has_target) {
      return Fail("HSS cikisi hedefsiz", 17);
    }
    // Radyal: bulunduğumuz yön (+x) korunmalı, HSS dışına çıkmalı.
    if (esc.target_x <= 120.0 || std::abs(esc.target_y) > 1.0) {
      std::cerr << "FAIL: radyal cikis beklenirken hedef=(" << esc.target_x << ","
                << esc.target_y << ")\n";
      return 18;
    }
  }

  // 6) Kaçış rotası HSS'i kesmemeli: burnumuz HSS'e dönükken bile aday
  //    puanlaması içinden geçen yolu elemeli.
  {
    auto g = MakeField();
    const auto c = frame.ToGeo(0.0, 0.0);
    g.SetHssLatLon({ev::HssDisc{5, c.lat_deg, c.lon_deg, 150.0}});
    // Sınıra yakın, burnu HSS'e (merkeze) dönük.
    const double px = 460.0;
    const auto esc = g.EvaluateEscape(px, 0.0, 16.0, true, 3.14159265358979323846);
    if (!esc.needed) {
      return Fail("sinir+HSS arasinda kacis tetiklenmedi", 19);
    }
    if (esc.has_target && !g.RouteSafe(px, 0.0, esc.target_x, esc.target_y)) {
      return Fail("kacis rotasi HSS'i kesiyor", 20);
    }
  }

  // 7) Sol ve sağ eşit yarışmalı: aynı geometride burun yönü ters çevrilince
  //    kaçış hedefi de simetrik tarafa geçmeli (sabit merkez davranışı değil).
  {
    auto g = MakeField();
    const double band = g.PreventiveBandM(16.0);
    const double py = 500.0 - band * 0.5;  // kuzey kenara yakın
    const auto right = g.EvaluateEscape(0.0, py, 16.0, true, 0.0);           // doğuya
    const auto left = g.EvaluateEscape(0.0, py, 16.0, true, 3.14159265358979323846);  // batıya
    if (!right.has_target || !left.has_target) {
      return Fail("simetri senaryosunda hedef uretilmedi", 21);
    }
    if (right.target_x <= 0.0 || left.target_x >= 0.0) {
      std::cerr << "FAIL: kacis burun yonunu izlemedi, sag_x=" << right.target_x
                << " sol_x=" << left.target_x << "\n";
      return 22;
    }
  }

  std::cout << "OK test_geofence\n";
  return 0;
}
