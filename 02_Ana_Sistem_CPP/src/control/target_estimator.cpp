/**
 * @file target_estimator.cpp
 * @brief @ref savasan::control::TargetEstimator uygulaması (MSO portu).
 */
#include "control/target_estimator.hpp"

#include <algorithm>
#include <cmath>

namespace savasan::control {

namespace {

constexpr double kPi = 3.14159265358979323846;

double ToRad(const double deg) { return deg * kPi / 180.0; }

double ToDeg(const double rad) { return rad * 180.0 / kPi; }

/// @brief Python `x % 360.0` semantiği: sonuç daima [0, 360).
double Mod360(const double x) {
  double m = std::fmod(x, 360.0);
  if (m < 0.0) {
    m += 360.0;
  }
  return m;
}

/// @brief a - b, [-180, 180] aralığına sarılmış (MSO `_aci_farki`).
double AngleDiffDeg(const double a, const double b) { return Mod360(a - b + 180.0) - 180.0; }

/// @brief Tekrar tespiti için ondalık basamağa yuvarlanmış tamsayı anahtar.
int64_t Quantize(const double v, const double scale) {
  return static_cast<int64_t>(std::llround(v * scale));
}

}  // namespace

void TargetEstimator::Reset() {
  has_state_ = false;
  x_ = y_ = alt_ = 0.0;
  t_ = 0.0;
  vx_ = vy_ = vz_ = omega_dps_ = 0.0;
  has_last_heading_ = false;
  last_heading_deg_ = 0.0;
  has_last_meas_t_ = false;
  last_meas_t_ = 0.0;
  has_last_content_ = false;
  measurement_count_ = 0;
  repeat_count_ = 0;
  // MSO `sifirla()` dönüş oranı oynaklığını bilerek korur: hedef kimliği
  // değişse de aynı sahadaki manevra karakteri hakkındaki bilgi atılmaz.
}

bool TargetEstimator::AddMeasurement(const double lat_deg, const double lon_deg,
                                     const double alt_m, const double speed_mps,
                                     const double heading_deg, const int time_diff_ms,
                                     const double t_recv_s, const int target_id) {
  if (target_id >= 0 && target_id != target_id_) {
    Reset();
    target_id_ = target_id;
  }

  // Ölçümün gerçek anı: varış - boru gecikmesi - sunucudaki veri yaşı.
  double t_meas = t_recv_s - cfg_.pipeline_delay_s - static_cast<double>(time_diff_ms) / 1000.0;

  // Rakip biz sorduğumuzdan yavaş veri basınca sunucu aynı kaydı döndürür.
  // En güvenilir test içerik karşılaştırmasıdır; t_meas testi tek başına
  // yetmez çünkü boru gecikmesi oynadıkça aynı veri farklı ana düşebilir.
  const int64_t lat_q = Quantize(lat_deg, 1e7);
  const int64_t lon_q = Quantize(lon_deg, 1e7);
  const int64_t speed_q = Quantize(speed_mps, 1e3);
  const int64_t heading_q = Quantize(heading_deg, 1e3);
  if (has_last_content_ && lat_q == last_lat_q_ && lon_q == last_lon_q_ &&
      speed_q == last_speed_q_ && heading_q == last_heading_q_) {
    ++repeat_count_;
    return false;
  }
  has_last_content_ = true;
  last_lat_q_ = lat_q;
  last_lon_q_ = lon_q;
  last_speed_q_ = speed_q;
  last_heading_q_ = heading_q;

  if (has_last_meas_t_ && t_meas <= last_meas_t_) {
    // İçerik yeni ama gecikme oynaması zamanı geriye itti: ölçümü atmak
    // yerine en az bir tık ileri kabul et.
    t_meas = last_meas_t_ + 0.05;
  }

  const LocalPoint p = frame_.ToLocal(lat_deg, lon_deg);
  const double alt = alt_m;
  const double speed = std::max(0.0, std::min(kMaxSpeedMps, speed_mps));
  const double heading = Mod360(heading_deg);

  // Ölçülen hız vektörü (pusula: 0=kuzey, 90=doğu).
  const double hr = ToRad(heading);
  const double ovx = speed * std::sin(hr);
  const double ovy = speed * std::cos(hr);

  if (!has_state_) {
    x_ = p.x;
    y_ = p.y;
    alt_ = alt;
    vx_ = ovx;
    vy_ = ovy;
    vz_ = 0.0;
    omega_dps_ = 0.0;
    t_ = t_meas;
    has_state_ = true;
    has_last_heading_ = true;
    last_heading_deg_ = heading;
    has_last_meas_t_ = true;
    last_meas_t_ = t_meas;
    measurement_count_ = 1;
    return true;
  }

  const double dt_internal = t_meas - t_;
  if (dt_internal > 0.0) {
    Advance(dt_internal);
  }

  // Uyarlanabilir kazanç: tahmin ile ölçüm arası fark büyükse hedef manevra
  // yapıyordur; o an modele değil ölçüme güvenmek gerekir, yoksa manevrayı
  // geç yakalayıp sürekli arkada kalırız.
  const double innovation = std::hypot(p.x - x_, p.y - y_);
  const double factor = 1.0 + std::min(2.0, innovation / kInnovationThresholdM);
  const double a = std::min(0.95, cfg_.alpha * factor);
  const double b = std::min(0.95, cfg_.beta * factor);
  const double g = std::min(0.95, cfg_.gamma * factor);

  x_ += a * (p.x - x_);
  y_ += a * (p.y - y_);
  vx_ += b * (ovx - vx_);
  vy_ += b * (ovy - vy_);

  const double dt_meas = t_meas - last_meas_t_;
  if (dt_meas > 0.05) {
    const double measured_vz = (alt - alt_) / dt_meas;
    vz_ += b * (measured_vz - vz_);
  }
  alt_ += a * (alt - alt_);

  if (has_last_heading_ && dt_meas > 0.05) {
    double measured_omega = AngleDiffDeg(heading, last_heading_deg_) / dt_meas;
    measured_omega = std::max(-kMaxTurnRateDps, std::min(kMaxTurnRateDps, measured_omega));
    // Dönüş oranı ne kadar oynak? Hedef dönüşünü tersine çeviriyorsa eski
    // oranı ileri sarmak hatayı büyütür.
    if (has_last_omega_meas_) {
      const double change = std::abs(measured_omega - last_omega_meas_) / std::max(0.2, dt_meas);
      omega_volatility_ += 0.5 * (change - omega_volatility_);
    }
    has_last_omega_meas_ = true;
    last_omega_meas_ = measured_omega;
    omega_dps_ += g * (measured_omega - omega_dps_);
  }

  has_last_heading_ = true;
  last_heading_deg_ = heading;
  t_ = t_meas;
  has_last_meas_t_ = true;
  last_meas_t_ = t_meas;
  ++measurement_count_;
  return true;
}

void TargetEstimator::Advance(const double dt) {
  const double v = std::hypot(vx_, vy_);
  if (v < 0.1) {
    alt_ += vz_ * dt;
    return;
  }

  const double h = Mod360(ToDeg(std::atan2(vx_, vy_)));
  const double w = omega_dps_;

  if (std::abs(w) < 0.5) {
    x_ += vx_ * dt;
    y_ += vy_ * dt;
  } else {
    const double h0 = ToRad(h);
    const double wr = ToRad(w);
    const double h1 = h0 + wr * dt;
    // Sabit dönüş oranı yay integrali.
    x_ += (v / wr) * (std::cos(h0) - std::cos(h1));
    y_ += (v / wr) * (std::sin(h1) - std::sin(h0));
    vx_ = v * std::sin(h1);
    vy_ = v * std::cos(h1);
  }

  alt_ += vz_ * dt;
  t_ += dt;
}

bool TargetEstimator::Predict(const double t_s, const double extra_lead_s,
                              TargetEstimate* out) const {
  if (out == nullptr || !has_state_ || !has_last_meas_t_) {
    return false;
  }
  if (t_s - last_meas_t_ > kMeasurementTimeoutS) {
    return false;  // Kestirim güvenilmez.
  }

  double dt = (t_s + extra_lead_s) - t_;
  if (dt < 0.0) {
    dt = 0.0;
  }

  double x = x_;
  double y = y_;
  double alt = alt_;
  double vx = vx_;
  double vy = vy_;
  const double vz = vz_;

  // Dönüş oranı güveni: hedefin dönüşü oynaksa (sert kaçış manevraları) eski
  // oranı ileri sarmak hatayı büyütür. Oynaklık arttıkça tahmini düz uçuşa
  // yaklaştır — yanlış dönüşü uzatmaktansa düz varsaymak daha ucuz.
  const double confidence = 1.0 / (1.0 + omega_volatility_ / kVolatilityThresholdDps2);
  const double w = omega_dps_ * confidence;
  const double v = std::hypot(vx, vy);

  if (v >= 0.1 && dt > 0.0) {
    const double h0 = std::atan2(vx, vy);
    if (std::abs(w) < 0.5) {
      x += vx * dt;
      y += vy * dt;
    } else {
      const double wr = ToRad(w);
      const double h1 = h0 + wr * dt;
      x += (v / wr) * (std::cos(h0) - std::cos(h1));
      y += (v / wr) * (std::sin(h1) - std::sin(h0));
      vx = v * std::sin(h1);
      vy = v * std::cos(h1);
    }
  }
  alt += vz * dt;

  const GeoPoint geo = frame_.ToGeo(x, y);
  out->x = x;
  out->y = y;
  out->lat_deg = geo.lat_deg;
  out->lon_deg = geo.lon_deg;
  out->alt_m = alt;
  out->vx = vx;
  out->vy = vy;
  out->vz = vz;
  out->speed_mps = std::hypot(vx, vy);
  out->heading_deg = Mod360(ToDeg(std::atan2(vx, vy)));
  out->turn_rate_dps = w;
  out->data_age_s = t_s - last_meas_t_;
  return true;
}

bool TargetEstimator::IsValid(const double t_s) const {
  return has_last_meas_t_ && (t_s - last_meas_t_) <= kMeasurementTimeoutS &&
         measurement_count_ >= 2;
}

}  // namespace savasan::control
