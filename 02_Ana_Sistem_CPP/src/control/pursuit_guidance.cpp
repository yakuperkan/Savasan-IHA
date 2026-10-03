/**
 * @file pursuit_guidance.cpp
 * @brief @ref savasan::control::PursuitGuidance uygulaması (MSO portu).
 */
#include "control/pursuit_guidance.hpp"

#include <algorithm>
#include <cmath>

namespace savasan::control {

namespace {

constexpr double kPi = 3.14159265358979323846;

double ToRad(const double deg) { return deg * kPi / 180.0; }

double ToDeg(const double rad) { return rad * 180.0 / kPi; }

/// @brief Açıyı [-pi, pi] aralığına sarar (MSO `_sar`).
double WrapPi(const double a) {
  double m = std::fmod(a + kPi, 2.0 * kPi);
  if (m < 0.0) {
    m += 2.0 * kPi;
  }
  return m - kPi;
}

/// Gaz → denge hızı, sahada ölçüldü (km/s → m/s).
struct ThrottleSpeedPoint {
  double throttle_pct;
  double speed_mps;
};
constexpr ThrottleSpeedPoint kThrottleSpeedCurve[] = {
    {0.0, 12.0}, {25.0, 14.3}, {50.0, 16.7}, {75.0, 23.8}, {100.0, 31.0}};
constexpr int kCurveSize = 5;

struct ArcState {
  double x;
  double y;
  double vx;
  double vy;
};

/// @brief Durumu sabit dönüş oranıyla dt kadar YAY üzerinde ilerletir (MSO `_yay`).
///
/// Düz ilerletme dönen hedefte sistematik olarak geride kalır.
/// @param turn_rate_compass_dps PUSULA derece/sn (saat yönü pozitif); matematiksel
/// açıda işareti TERSTİR — bu hata bir kez ısırmış.
ArcState AdvanceArc(const double x, const double y, const double vx, const double vy,
                    const double turn_rate_compass_dps, const double dt) {
  const double v = std::hypot(vx, vy);
  if (v < 0.1 || dt <= 0.0) {
    return ArcState{x, y, vx, vy};
  }
  const double w = -ToRad(turn_rate_compass_dps);
  const double h0 = std::atan2(vy, vx);
  if (std::abs(w) < 0.01) {
    return ArcState{x + vx * dt, y + vy * dt, vx, vy};
  }
  const double h1 = h0 + w * dt;
  const double r = v / w;
  return ArcState{x + r * (std::sin(h1) - std::sin(h0)),
                  y - r * (std::cos(h1) - std::cos(h0)), v * std::cos(h1), v * std::sin(h1)};
}

}  // namespace

double TurnAcceleration() { return kGravity * std::tan(ToRad(kBankAngleDeg)); }

double ThrottleForSpeed(const double desired_speed_mps) {
  if (desired_speed_mps <= kThrottleSpeedCurve[0].speed_mps) {
    return kThrottleSpeedCurve[0].throttle_pct;
  }
  for (int i = 0; i < kCurveSize - 1; ++i) {
    const double g0 = kThrottleSpeedCurve[i].throttle_pct;
    const double v0 = kThrottleSpeedCurve[i].speed_mps;
    const double g1 = kThrottleSpeedCurve[i + 1].throttle_pct;
    const double v1 = kThrottleSpeedCurve[i + 1].speed_mps;
    if (desired_speed_mps <= v1) {
      return g0 + (g1 - g0) * (desired_speed_mps - v0) / (v1 - v0);
    }
  }
  return kThrottleSpeedCurve[kCurveSize - 1].throttle_pct;
}

double SpeedForThrottle(const double throttle_pct) {
  if (throttle_pct <= kThrottleSpeedCurve[0].throttle_pct) {
    return kThrottleSpeedCurve[0].speed_mps;
  }
  for (int i = 0; i < kCurveSize - 1; ++i) {
    const double g0 = kThrottleSpeedCurve[i].throttle_pct;
    const double v0 = kThrottleSpeedCurve[i].speed_mps;
    const double g1 = kThrottleSpeedCurve[i + 1].throttle_pct;
    const double v1 = kThrottleSpeedCurve[i + 1].speed_mps;
    if (throttle_pct <= g1) {
      return v0 + (v1 - v0) * (throttle_pct - g0) / (g1 - g0);
    }
  }
  return kThrottleSpeedCurve[kCurveSize - 1].speed_mps;
}

const char* PursuitStageName(const PursuitStage s) {
  switch (s) {
    case PursuitStage::kApproach:
      return "YAKLASMA";
    case PursuitStage::kTrack:
      return "TAKIP";
    case PursuitStage::kBreakaway:
      return "KOPMA";
    default:
      return "YOK";
  }
}

void PursuitGuidance::Reset() {
  stage_ = PursuitStage::kNone;
  has_breakaway_end_ = false;
  breakaway_end_t_ = 0.0;
  last_range_m_ = 0.0;
  has_last_heading_ = false;
  maneuver_dps_ = 0.0;
}

LockGeometry PursuitGuidance::ComputeLockGeometry(const PursuitOwnState& own,
                                                  const TargetEstimate& tgt) const {
  LockGeometry g{};
  const double dx = tgt.x - own.x;
  const double dy = tgt.y - own.y;
  const double horizontal = std::hypot(dx, dy);
  const double dz = tgt.alt_m - own.alt_m;
  const double range = std::hypot(horizontal, dz);
  if (range < 0.5) {
    return g;
  }
  g.valid = true;
  g.range_m = range;
  // Şartname: hedef YATAY *VEYA* DİKEY eksenin en az %5'ini kaplamalı. Dikey
  // eksen daha dar olduğu için aynı hedef orada daha uzaktan yeterli sayılır:
  // %5 kaplama yatayda 34 m'de, dikeyde 57 m'de oluşur.
  const double angle_deg = 2.0 * ToDeg(std::atan(kTargetWingspanM / (2.0 * range)));
  g.fill_ratio = std::max(angle_deg / kFovHorizontalDeg, angle_deg / kFovVerticalDeg);
  if (!own.has_track) {
    return g;
  }
  g.has_errors = true;
  g.horizontal_error_deg = ToDeg(WrapPi(std::atan2(dy, dx) - own.track_rad));
  g.vertical_error_deg = ToDeg(std::atan2(dz, horizontal));
  g.in_hit_area = std::abs(g.horizontal_error_deg) <= kHitAreaHalfHorizontalDeg &&
                  std::abs(g.vertical_error_deg) <= kHitAreaHalfVerticalDeg &&
                  g.fill_ratio >= 0.05;
  return g;
}

void PursuitGuidance::UpdateManeuver(const double t_s, const double heading_deg) {
  // Kestiricinin dönüş oranı oynak manevrada sönümlendiği için manevra
  // TESPİTİ ham yön farkından yapılmalı.
  if (has_last_heading_) {
    const double dt = t_s - last_heading_t_;
    if (dt > 0.05 && dt < 2.0) {
      double diff = std::fmod(heading_deg - last_heading_deg_ + 180.0, 360.0);
      if (diff < 0.0) {
        diff += 360.0;
      }
      const double w = std::abs(diff - 180.0) / dt;
      maneuver_dps_ += s_.maneuver_filter_gain * (std::min(45.0, w) - maneuver_dps_);
    }
  }
  has_last_heading_ = true;
  last_heading_deg_ = heading_deg;
  last_heading_t_ = t_s;
}

TargetEstimate PursuitGuidance::SmoothTarget(const double t_s, const TargetEstimate& tgt) {
  if (!has_smooth_ || !has_smooth_t_ || (t_s - smooth_t_) > 2.0) {
    sx_ = tgt.x;
    sy_ = tgt.y;
    svx_ = tgt.vx;
    svy_ = tgt.vy;
    has_smooth_ = true;
  } else {
    const double dt = std::max(1e-3, t_s - smooth_t_);
    // Filtre durumu düz değil YAY üzerinde ilerletilir; düz ilerletirsek dönen
    // hedefte geri kalır ve yumuşatmanın kazancı zararla gelir.
    const ArcState a = AdvanceArc(sx_, sy_, svx_, svy_, tgt.turn_rate_dps, dt);
    sx_ = a.x;
    sy_ = a.y;
    svx_ = a.vx;
    svy_ = a.vy;
    // Filtre sabiti manevrayla açılır: sakin hedefte gürültü baskındır (ağır
    // filtre), manevrada gecikme baskındır (hafif filtre).
    const double tau = s_.smoothing_s / (1.0 + maneuver_dps_ / s_.maneuver_tau_ref);
    const double k = 1.0 - std::exp(-dt / std::max(0.05, tau));
    sx_ += k * (tgt.x - sx_);
    sy_ += k * (tgt.y - sy_);
    svx_ += k * (tgt.vx - svx_);
    svy_ += k * (tgt.vy - svy_);
  }
  has_smooth_t_ = true;
  smooth_t_ = t_s;

  TargetEstimate out = tgt;
  out.x = sx_;
  out.y = sy_;
  out.vx = svx_;
  out.vy = svy_;
  out.speed_mps = std::hypot(svx_, svy_);
  return out;
}

double PursuitGuidance::DriftSpeed(const double t_s, const TargetEstimate& tgt) {
  // Manevra yapan hedefin hava hızı 15.9 m/s olabilir ama yerdeki net
  // sürüklenmesi 10.7 m/s'dir. Hava hızına eşlenmek gereksiz hızlı uçmaktır,
  // gereksiz hız da büyük dönüş yarıçapı demektir.
  if (!has_drift_ || !has_drift_t_ || (t_s - drift_t_) > 3.0) {
    drift_vx_ = tgt.vx;
    drift_vy_ = tgt.vy;
    has_drift_ = true;
  } else {
    const double k = 1.0 - std::exp(-(t_s - drift_t_) / std::max(0.5, s_.drift_tau_s));
    drift_vx_ += k * (tgt.vx - drift_vx_);
    drift_vy_ += k * (tgt.vy - drift_vy_);
  }
  has_drift_t_ = true;
  drift_t_ = t_s;
  return std::hypot(drift_vx_, drift_vy_);
}

bool PursuitGuidance::UpdateWind(const double t_s, const PursuitOwnState& own) {
  // Sunucu hedefin YER hızını bildirir, gaz-hız eğrisi ise HAVA hızı verir.
  // Telafisiz hâlde rüzgâr iki kez sayılır. Hava hızını gazdan modellemek
  // rüzgârsız havada profili bozduğu için telafi yalnızca ölçüm varsa çalışır.
  if (!own.has_track || !own.has_airspeed) {
    last_own_x_ = own.x;
    last_own_y_ = own.y;
    has_last_own_pos_ = true;
    has_wind_t_ = true;
    wind_t_ = t_s;
    return false;
  }
  const double dt = has_wind_t_ ? (t_s - wind_t_) : 0.0;
  if (dt <= 0.05 || dt > 3.0 || !has_last_own_pos_) {
    last_own_x_ = own.x;
    last_own_y_ = own.y;
    has_last_own_pos_ = true;
    has_wind_t_ = true;
    wind_t_ = t_s;
    return false;
  }
  const double ground_vx = (own.x - last_own_x_) / dt;
  const double ground_vy = (own.y - last_own_y_) / dt;
  const double k = 1.0 - std::exp(-dt / std::max(0.5, s_.wind_tau_s));
  wind_x_ += k * ((ground_vx - own.airspeed_mps * std::cos(own.track_rad)) - wind_x_);
  wind_y_ += k * ((ground_vy - own.airspeed_mps * std::sin(own.track_rad)) - wind_y_);
  const double w = std::hypot(wind_x_, wind_y_);
  if (w > s_.wind_max_mps) {
    wind_x_ *= s_.wind_max_mps / w;
    wind_y_ *= s_.wind_max_mps / w;
  }
  last_own_x_ = own.x;
  last_own_y_ = own.y;
  has_last_own_pos_ = true;
  has_wind_t_ = true;
  wind_t_ = t_s;
  return true;
}

bool PursuitGuidance::BreakawayNeeded(const double t_s, const PursuitOwnState& own,
                                      const TargetEstimate& tgt, const double dx,
                                      const double dy, const double range_m) {
  double predicted_range = range_m;
  if (own.has_track) {
    const double v_own = std::max(8.0, own.speed_mps);
    const double rvx = tgt.vx - v_own * std::cos(own.track_rad);
    const double rvy = tgt.vy - v_own * std::sin(own.track_rad);
    if (range_m > 0.5) {
      const double range_dot = (dx * rvx + dy * rvy) / range_m;
      predicted_range = range_m + range_dot * s_.reaction_delay_s;
    }
  }
  const bool danger =
      range_m < s_.breakaway_range_m || predicted_range < s_.breakaway_range_m;
  // Sabit 3 sn kaçmak menzili gereksiz açıyor. Güvenli olur olmaz takibe dön,
  // ama asgari 1 sn kal ki komut gecikmesi içinde aynı hataya düşmeyelim.
  if (danger) {
    const double candidate = t_s + s_.breakaway_min_s;
    breakaway_end_t_ = has_breakaway_end_ ? std::max(breakaway_end_t_, candidate) : candidate;
    has_breakaway_end_ = true;
  }
  bool ongoing = has_breakaway_end_ && t_s < breakaway_end_t_;
  if (ongoing && !danger && range_m > s_.min_separation_m * 1.2) {
    ongoing = false;
    has_breakaway_end_ = false;
  }
  return danger || ongoing;
}

PursuitCommand PursuitGuidance::Tick(const double t_s, const PursuitOwnState& own,
                                     const TargetEstimate& tgt, const bool has_target) {
  PursuitCommand cmd{};
  if (!has_target) {
    stage_ = PursuitStage::kNone;
    return cmd;
  }

  double dx = tgt.x - own.x;
  double dy = tgt.y - own.y;
  double range = std::hypot(dx, dy);
  last_range_m_ = range;
  const double v_own = std::max(8.0, own.speed_mps);

  UpdateManeuver(t_s, tgt.heading_deg);
  const bool wind_valid = UpdateWind(t_s, own);

  const TargetEstimate smoothed = SmoothTarget(t_s, tgt);
  dx = smoothed.x - own.x;
  dy = smoothed.y - own.y;
  range = std::hypot(dx, dy);
  last_range_m_ = range;

  if (range > s_.max_track_m) {
    stage_ = PursuitStage::kNone;
    return cmd;
  }

  const double drift_speed = DriftSpeed(t_s, smoothed);

  // Aspekt: hedefin kuyruğuna göre nerede duruyoruz? 0° = tam arkasında.
  double aspect_deg = 0.0;
  {
    const double bx = own.x - smoothed.x;
    const double by = own.y - smoothed.y;
    if (std::hypot(bx, by) >= 1.0) {
      aspect_deg = std::abs(ToDeg(
          WrapPi(std::atan2(by, bx) - (std::atan2(smoothed.vy, smoothed.vx) + kPi))));
    }
  }

  cmd.range_m = range;
  cmd.aspect_deg = aspect_deg;
  cmd.maneuver_dps = maneuver_dps_;

  // ---------------- KOPMA (çarpışma önleme) ----------------
  // Bu kilitlenmeden ÖNCE gelir: 34 m'de hata payı yok.
  if (BreakawayNeeded(t_s, own, smoothed, dx, dy, range)) {
    stage_ = PursuitStage::kBreakaway;
    // Hangi yana? Hedefin dönüşünün TERSİNE çık, yoksa peşinden döndüğü
    // tarafa kaçıp tekrar önüne düşeriz.
    double sign = 1.0;
    if (own.has_track) {
      sign = WrapPi(std::atan2(dy, dx) - own.track_rad) < 0.0 ? 1.0 : -1.0;
    }
    const double bearing = std::atan2(dy, dx) + sign * kPi / 2.0;
    const double ax = own.x + std::cos(bearing) * s_.breakaway_aim_m;
    const double ay = own.y + std::sin(bearing) * s_.breakaway_aim_m;
    const GeoPoint geo = frame_.ToGeo(ax, ay);
    cmd.valid = true;
    cmd.stage = PursuitStage::kBreakaway;
    cmd.aim_x = ax;
    cmd.aim_y = ay;
    cmd.aim_lat_deg = geo.lat_deg;
    cmd.aim_lon_deg = geo.lon_deg;
    cmd.altitude_m = static_cast<int>(
        std::max(s_.altitude_floor_m, own.alt_m - s_.breakaway_altitude_drop_m));
    cmd.throttle_pct = s_.breakaway_throttle;
    cmd.pitch_deg = 10;
    return cmd;
  }
  has_breakaway_end_ = false;

  stage_ = range <= s_.approach_threshold_m ? PursuitStage::kTrack : PursuitStage::kApproach;

  // İleri sarma tavanı: hedef ω derece/sn dönüyorsa τ saniye ileri sarmak
  // τ·ω derecelik yön hatası demektir. Payı aşan ileri sarma boş havaya nişan.
  const double tau_cap = s_.maneuver_heading_margin_deg / std::max(2.0, maneuver_dps_);

  double aim_x = 0.0;
  double aim_y = 0.0;
  if (stage_ == PursuitStage::kApproach) {
    const double closing = std::max(2.0, v_own - smoothed.speed_mps * 0.5);
    const double tau = std::min(range / closing, tau_cap);
    const ArcState p =
        AdvanceArc(smoothed.x, smoothed.y, smoothed.vx, smoothed.vy, smoothed.turn_rate_dps, tau);
    // PERCH (kuyruk yaklaşması): hedefin ÖNÜNE değil KUYRUĞUNA git. Dönen
    // hedefte gelecek konuma nişan almak boş havaya uçmaktır; kuyruk noktası
    // hedef nasıl manevra yaparsa yapsın hep doğru taraftadır.
    double pv = std::hypot(p.vx, p.vy);
    if (pv == 0.0) {
      pv = 1.0;
    }
    aim_x = p.x - p.vx / pv * s_.nominal_range_m;
    aim_y = p.y - p.vy / pv * s_.nominal_range_m;
  } else {
    // TAKİP: burnu hedefe çevir, sadece kendi komut gecikmemizi telafi et.
    const double tau = std::min(s_.aim_lead_s, tau_cap);
    const ArcState p =
        AdvanceArc(smoothed.x, smoothed.y, smoothed.vx, smoothed.vy, smoothed.turn_rate_dps, tau);
    aim_x = p.x;
    aim_y = p.y;
  }

  // ---------------- ÖNGÖRÜLÜ EMNİYET ----------------
  // En yakın yaklaşma anını (TCA) ve kaçırma mesafesini hesapla; kaçırma
  // asgari ayrımın altındaysa nişanı yana kaydır. Reaktif tetik burada yetmez.
  bool safety_brake = false;
  if (own.has_track) {
    const double bvx = v_own * std::cos(own.track_rad);
    const double bvy = v_own * std::sin(own.track_rad);
    const double rvx = smoothed.vx - bvx;
    const double rvy = smoothed.vy - bvy;
    const double v2 = rvx * rvx + rvy * rvy;
    if (v2 > 0.01) {
      const double tca = -(dx * rvx + dy * rvy) / v2;
      if (tca > 0.0 && tca < s_.safety_horizon_s) {
        const double miss = std::hypot(dx + rvx * tca, dy + rvy * tca);
        if (miss < s_.min_separation_m) {
          safety_brake = true;
          const double deviation = std::asin(
              std::min(0.9, (s_.min_separation_m - miss) / std::max(10.0, range)));
          const double sign = (dx * rvy - dy * rvx) > 0.0 ? 1.0 : -1.0;
          const double bearing =
              std::atan2(aim_y - own.y, aim_x - own.x) + sign * deviation;
          aim_x = own.x + std::cos(bearing) * std::max(40.0, range);
          aim_y = own.y + std::sin(bearing) * std::max(40.0, range);
        }
      }
    }
  }
  cmd.safety_brake = safety_brake;

  // ---------------- GAZ (mesafe kontrolü) — hesaplanır, karta gitmez -------
  // PUSU: kilit iki şart ister, menzil ≤ 34 m VE burun hedefte. İkincisi bir
  // dönüş hızı şartıdır ve menzil küçüldükçe gereken hız büyür. Tutamayacağımız
  // menzile inmek kilidi değil spirali getirir.
  double nominal = s_.nominal_range_m;
  if (range > 0.5) {
    const double ux = dx / range;
    const double uy = dy / range;
    const double v_perp = std::abs(-uy * smoothed.vx + ux * smoothed.vy);
    const double turn_capability = TurnAcceleration() / v_own;
    const double hold_radius = s_.ambush_margin * v_perp / std::max(0.05, turn_capability);
    nominal = std::max(nominal, std::min(s_.ambush_ceiling_m, hold_radius));
  }

  // Karekök kapanma profili: fazla menzili sabit yavaşlamayla tam nominal
  // mesafede sıfırlayan hız farkı c = sqrt(2·a·Δs). Oransal kontrolün integrali
  // yoktu, kalıcı hata bırakıp tam kilit sınırında dengeye giriyordu.
  const double excess = range - nominal;
  const double c = excess > 0.0
                       ? std::sqrt(2.0 * s_.deceleration_mps2 * excess)
                       : -std::sqrt(2.0 * s_.deceleration_mps2 * std::min(20.0, -excess));
  double desired_speed = drift_speed + c;
  if (safety_brake) {
    desired_speed = std::min(desired_speed, drift_speed);
  }
  if (range < s_.min_separation_m) {
    desired_speed = std::min(desired_speed, drift_speed - 2.0);
  }

  if (own.has_track) {
    const double dpsi =
        std::abs(WrapPi(std::atan2(aim_y - own.y, aim_x - own.x) - own.track_rad));
    if (!has_deviation_t_) {
      deviation_filtered_rad_ = dpsi;
    } else {
      const double kf = 1.0 - std::exp(-std::max(1e-3, t_s - deviation_t_) / s_.deviation_tau_s);
      deviation_filtered_rad_ += kf * (dpsi - deviation_filtered_rad_);
    }
    has_deviation_t_ = true;
    deviation_t_ = t_s;

    // ENERJİ KAPAĞI: nişana Δψ dönmem gerekiyorsa o dönüşü menzil içinde
    // tamamlayabileceğim yarıçap R ≤ menzil/(pay·sin Δψ). "Yetişmek için gazla"
    // sezgisinin yanlış olduğu yer burası.
    const double s0 = std::sin(std::min(dpsi, kPi / 2.0));
    if (s0 > 0.05) {
      desired_speed = std::min(
          desired_speed,
          std::sqrt(std::max(1.0, range / (s_.turn_cap_margin * s0) * TurnAcceleration())));
    }

    // DÖNÜŞ EŞLEME: hedef R_h = v_h/ω_h yarıçapıyla dönüyor. Yarıçapımız
    // R_h'nin kappa katını aşarsa onun çemberinin dışında kalıp savruluruz.
    // Asla sürüklenme hızının altına inme; yoksa hiç yetişemeyiz.
    const double w_h = ToRad(std::max(s_.maneuver_floor_dps, maneuver_dps_));
    const double r_target = std::max(10.0, smoothed.speed_mps / w_h);
    const double v_maneuver =
        std::max(drift_speed * 1.05, std::sqrt(s_.kappa * r_target * TurnAcceleration()));
    // Ölü bant şart: bantsız kapak kuyruk takibinde bile gaz kesiyor.
    const double excess_deg =
        std::max(0.0, ToDeg(deviation_filtered_rad_) - s_.deviation_deadband_deg);
    const double ratio = std::min(1.0, excess_deg / 45.0);
    desired_speed = std::min(desired_speed, desired_speed * (1.0 - ratio) + v_maneuver * ratio);
  }

  // desired_speed buraya kadar bir YER hızı isteğidir; gaz eğrisi HAVA hızı
  // ister. Hava hızı ölçümümüz varsa rüzgârın gittiğimiz yöndeki bileşenini düş.
  if (wind_valid && own.has_track) {
    desired_speed -= wind_x_ * std::cos(own.track_rad) + wind_y_ * std::sin(own.track_rad);
  }
  desired_speed = std::max(s_.speed_floor_mps, std::min(s_.speed_ceiling_mps, desired_speed));
  cmd.throttle_pct = static_cast<int>(
      std::max(static_cast<double>(s_.throttle_min),
               std::min(static_cast<double>(s_.throttle_max), ThrottleForSpeed(desired_speed))));

  // ---------------- İRTİFA ----------------
  // Kestirilen hedef irtifası gürültülü; filtrelemezsek irtifa komutu sürekli
  // oynar, uçak dikeyde salınıp vuruş alanının dışına çıkar.
  double target_alt = smoothed.alt_m;
  if (!has_alt_filter_ || !has_alt_filter_t_ || (t_s - alt_filter_t_) > 3.0) {
    alt_filtered_m_ = target_alt;
    has_alt_filter_ = true;
  } else {
    const double kh = 1.0 - std::exp(-(t_s - alt_filter_t_) / std::max(0.2, s_.altitude_tau_s));
    alt_filtered_m_ += kh * (target_alt - alt_filtered_m_);
  }
  has_alt_filter_t_ = true;
  alt_filter_t_ = t_s;
  target_alt = alt_filtered_m_;

  double offset =
      range * std::tan(ToRad(s_.vertical_hit_margin * kHitAreaHalfVerticalDeg));
  offset = std::max(s_.vertical_offset_min_m, std::min(s_.vertical_offset_max_m, offset));

  const double altitude = std::max(s_.altitude_floor_m,
                                   std::min(s_.altitude_ceiling_m, target_alt - offset));

  const GeoPoint geo = frame_.ToGeo(aim_x, aim_y);
  cmd.valid = true;
  cmd.stage = stage_;
  cmd.aim_x = aim_x;
  cmd.aim_y = aim_y;
  cmd.aim_lat_deg = geo.lat_deg;
  cmd.aim_lon_deg = geo.lon_deg;
  cmd.altitude_m = static_cast<int>(altitude);
  cmd.pitch_deg = 12;
  return cmd;
}

double LockCounter::Update(const double t_s, const bool in_hit_area) {
  if (in_hit_area) {
    if (!has_start_) {
      start_t_ = t_s;
      has_start_ = true;
    }
    last_valid_t_ = t_s;
    has_last_valid_ = true;
    longest_s_ = std::max(longest_s_, t_s - start_t_);
    return t_s - start_t_;
  }
  // Kısa kesinti tolerans içindeyse sayacı bozma.
  if (has_start_ && has_last_valid_ && (t_s - last_valid_t_) <= tolerance_s_ * 0.2) {
    return t_s - start_t_;
  }
  if (has_start_ && has_last_valid_ && (last_valid_t_ - start_t_) >= required_s_) {
    ++completed_;
  }
  has_start_ = false;
  return 0.0;
}

}  // namespace savasan::control
