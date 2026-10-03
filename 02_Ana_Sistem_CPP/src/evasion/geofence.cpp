#include "evasion/geofence.hpp"

#include <algorithm>
#include <cmath>

namespace savasan::evasion {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kGravity = 9.81;

double Hypot(const double ax, const double ay, const double bx, const double by) {
  return std::hypot(bx - ax, by - ay);
}

/// @brief Noktanın doğru parçasına uzaklığı.
double PointSegmentDistance(const double px, const double py, const double ax, const double ay,
                            const double bx, const double by) {
  const double dx = bx - ax;
  const double dy = by - ay;
  const double l2 = dx * dx + dy * dy;
  if (l2 == 0.0) {
    return Hypot(px, py, ax, ay);
  }
  double t = ((px - ax) * dx + (py - ay) * dy) / l2;
  t = std::max(0.0, std::min(1.0, t));
  return Hypot(px, py, ax + t * dx, ay + t * dy);
}

}  // namespace

const char* GeofenceViolationName(const GeofenceViolation v) {
  switch (v) {
    case GeofenceViolation::kOutsideBoundary:
      return "SINIR_DISI";
    case GeofenceViolation::kInsideHss:
      return "HSS_ICINDE";
    case GeofenceViolation::kNearBoundary:
      return "SINIR_YAKIN";
    case GeofenceViolation::kNearHss:
      return "HSS_YAKIN";
    default:
      return "YOK";
  }
}

void Geofence::SetFrame(const control::LocalFrame& frame) {
  frame_ = frame;
  // Eski yerel koordinatlar yeni çerçevede anlamsız; kaynak coğrafi veriden
  // yeniden kurulmalı.
  polygon_.clear();
  hss_.clear();
}

void Geofence::SetBoundaryLatLon(const std::vector<control::GeoPoint>& corners) {
  polygon_.clear();
  polygon_.reserve(corners.size());
  for (const auto& c : corners) {
    polygon_.push_back(frame_.ToLocal(c.lat_deg, c.lon_deg));
  }
}

void Geofence::SetHssLatLon(const std::vector<HssDisc>& discs_latlon) {
  hss_.clear();
  hss_.reserve(discs_latlon.size());
  for (const auto& d : discs_latlon) {
    // Girişte x=enlem, y=boylam taşınır; yerel düzleme burada çevrilir.
    const auto p = frame_.ToLocal(d.x, d.y);
    HssDisc out{};
    out.id = d.id;
    out.x = p.x;
    out.y = p.y;
    out.radius_m = d.radius_m;
    hss_.push_back(out);
  }
}

bool Geofence::InsidePolygon(const double x, const double y) const {
  if (polygon_.size() < 3) {
    return false;
  }
  bool inside = false;
  const size_t n = polygon_.size();
  size_t j = n - 1;
  for (size_t i = 0; i < n; ++i) {
    const double xi = polygon_[i].x;
    const double yi = polygon_[i].y;
    const double xj = polygon_[j].x;
    const double yj = polygon_[j].y;
    if (((yi > y) != (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi) + xi)) {
      inside = !inside;
    }
    j = i;
  }
  return inside;
}

double Geofence::BoundaryEdgeDistanceM(const double x, const double y) const {
  if (polygon_.size() < 3) {
    return 0.0;
  }
  double best = -1.0;
  const size_t n = polygon_.size();
  for (size_t i = 0; i < n; ++i) {
    const auto& a = polygon_[i];
    const auto& b = polygon_[(i + 1) % n];
    const double d = PointSegmentDistance(x, y, a.x, a.y, b.x, b.y);
    if (best < 0.0 || d < best) {
      best = d;
    }
  }
  return best < 0.0 ? 0.0 : best;
}

void Geofence::PolygonCenter(double* cx, double* cy) const {
  double sx = 0.0;
  double sy = 0.0;
  for (const auto& p : polygon_) {
    sx += p.x;
    sy += p.y;
  }
  const double n = static_cast<double>(polygon_.size());
  *cx = n > 0.0 ? sx / n : 0.0;
  *cy = n > 0.0 ? sy / n : 0.0;
}

bool Geofence::PointSafe(const double x, const double y, const double extra_margin_m) const {
  // Sınır bilinmiyorsa hiçbir nokta güvenli sayılmaz: veri gelmeden sahaya
  // güvenmek, sahanın tamamını serbest ilan etmekle aynı şeydir.
  if (polygon_.size() < 3) {
    return false;
  }
  if (!InsidePolygon(x, y)) {
    return false;
  }
  if (BoundaryEdgeDistanceM(x, y) < s_.boundary_margin_m + extra_margin_m) {
    return false;
  }
  for (const auto& h : hss_) {
    if (Hypot(x, y, h.x, h.y) < h.radius_m + s_.hss_margin_m + extra_margin_m) {
      return false;
    }
  }
  return true;
}

bool Geofence::RouteSafe(const double ax, const double ay, const double bx, const double by,
                         const double extra_margin_m) const {
  for (const auto& h : hss_) {
    if (PointSegmentDistance(h.x, h.y, ax, ay, bx, by) <
        h.radius_m + s_.hss_margin_m + extra_margin_m) {
      return false;
    }
  }
  if (polygon_.size() < 3) {
    return false;
  }
  // Rota sınır içinde kalmalı: 25 m adımlarla örneklenir.
  constexpr double kStepM = 25.0;
  const double length = Hypot(ax, ay, bx, by);
  const int n = std::max(2, static_cast<int>(length / kStepM));
  for (int i = 0; i <= n; ++i) {
    const double t = static_cast<double>(i) / static_cast<double>(n);
    if (!InsidePolygon(ax + t * (bx - ax), ay + t * (by - ay))) {
      return false;
    }
  }
  return true;
}

double Geofence::TurnRadiusM(const double speed_mps) const {
  const double v = speed_mps > 1.0 ? speed_mps : s_.assumed_speed_mps;
  const double bank = std::max(10.0, std::min(60.0, s_.nominal_bank_deg));
  const double r = (v * v) / (kGravity * std::tan(bank * kPi / 180.0));
  return std::max(s_.turn_radius_min_m, std::min(s_.turn_radius_max_m, r));
}

double Geofence::PreventiveBandM(const double speed_mps) const {
  return std::max(s_.band_min_m, TurnRadiusM(speed_mps) * s_.band_multiplier);
}

GeofenceViolation Geofence::Violation(const double x, const double y, int* hss_id) const {
  if (hss_id != nullptr) {
    *hss_id = -1;
  }
  if (polygon_.size() >= 3 && !InsidePolygon(x, y)) {
    return GeofenceViolation::kOutsideBoundary;
  }
  for (const auto& h : hss_) {
    if (Hypot(x, y, h.x, h.y) < h.radius_m) {
      if (hss_id != nullptr) {
        *hss_id = h.id;
      }
      return GeofenceViolation::kInsideHss;
    }
  }
  return GeofenceViolation::kNone;
}

bool Geofence::EscapeTarget(const double x, const double y, const double band_m,
                            const bool has_track, const double track_rad, double* out_x,
                            double* out_y) const {
  // HSS'in İÇİNDEYSEK pazarlık yok: en kısa radyal çıkış. İçerideyken her aday
  // rota "ihlalde başlıyor" diye elenir, puanlama burada çalışamaz.
  for (const auto& h : hss_) {
    const double d = Hypot(x, y, h.x, h.y);
    if (d >= h.radius_m) {
      continue;
    }
    double ux = 0.0;
    double uy = 0.0;
    if (d < 1.0) {
      // Tam merkezdeyiz, radyal yön tanımsız: saha merkezine doğru çık.
      double cx = 0.0;
      double cy = 0.0;
      PolygonCenter(&cx, &cy);
      const double len = Hypot(x, y, cx, cy);
      if (len < 1e-6) {
        ux = 1.0;
      } else {
        ux = (cx - x) / len;
        uy = (cy - y) / len;
      }
    } else {
      ux = (x - h.x) / d;
      uy = (y - h.y) / d;
    }
    const double out_r = h.radius_m + s_.hss_margin_m + s_.escape_extra_m;
    *out_x = h.x + ux * out_r;
    *out_y = h.y + uy * out_r;
    return true;
  }

  if (polygon_.size() < 3) {
    return false;
  }

  double cx = 0.0;
  double cy = 0.0;
  PolygonCenter(&cx, &cy);

  if (!s_.candidate_scoring) {
    *out_x = cx;
    *out_y = cy;
    return true;
  }

  // Aday üretimi: gidiş yönüne göre 8 yön. Sol ve sağ eşit yarışır.
  const double ref = has_track ? track_rad : 0.0;
  const double reach = std::max(100.0, band_m * s_.escape_distance_factor);
  const double offsets_deg[] = {0.0, 40.0, -40.0, 80.0, -80.0, 120.0, -120.0, 180.0};

  bool found = false;
  double best_x = 0.0;
  double best_y = 0.0;
  double best_score = 0.0;
  for (const double off : offsets_deg) {
    const double ang = ref + off * kPi / 180.0;
    const double ax = x + std::cos(ang) * reach;
    const double ay = y + std::sin(ang) * reach;
    if (!PointSafe(ax, ay)) {
      continue;
    }
    if (!RouteSafe(x, y, ax, ay)) {
      continue;  // yol HSS/sınır kesiyor
    }
    double clearance = BoundaryEdgeDistanceM(ax, ay);
    for (const auto& h : hss_) {
      clearance = std::min(clearance, Hypot(ax, ay, h.x, h.y) - h.radius_m);
    }
    const double score = clearance - std::abs(off) * s_.escape_turn_penalty;
    if (!found || score > best_score) {
      found = true;
      best_score = score;
      best_x = ax;
      best_y = ay;
    }
  }
  if (found) {
    *out_x = best_x;
    *out_y = best_y;
    return true;
  }

  // Yedek: merkez. Kaçış asla hedefsiz kalmamalı.
  *out_x = cx;
  *out_y = cy;
  return true;
}

GeofenceEscape Geofence::EvaluateEscape(const double x, const double y, const double speed_mps,
                                        const bool has_track, const double track_rad) const {
  GeofenceEscape esc{};
  if (polygon_.size() < 3 && hss_.empty()) {
    return esc;  // saha verisi yok, kısıt da yok
  }

  int hss_id = -1;
  const auto real = Violation(x, y, &hss_id);
  if (real != GeofenceViolation::kNone) {
    esc.needed = true;
    esc.reason = real;
    esc.hss_id = hss_id;
  } else {
    const double band = PreventiveBandM(speed_mps);
    for (const auto& h : hss_) {
      if (Hypot(x, y, h.x, h.y) < h.radius_m + band) {
        esc.needed = true;
        esc.reason = GeofenceViolation::kNearHss;
        esc.hss_id = h.id;
        break;
      }
    }
    if (!esc.needed && polygon_.size() >= 3 && BoundaryEdgeDistanceM(x, y) < band) {
      esc.needed = true;
      esc.reason = GeofenceViolation::kNearBoundary;
    }
  }
  if (!esc.needed) {
    return esc;
  }

  const double band = PreventiveBandM(speed_mps);
  double tx = 0.0;
  double ty = 0.0;
  if (EscapeTarget(x, y, band, has_track, track_rad, &tx, &ty)) {
    esc.has_target = true;
    esc.target_x = tx;
    esc.target_y = ty;
    const auto geo = frame_.ToGeo(tx, ty);
    esc.target_lat_deg = geo.lat_deg;
    esc.target_lon_deg = geo.lon_deg;
  }
  return esc;
}

}  // namespace savasan::evasion
