/**
 * @file target_estimator.hpp
 * @brief Gecikme telafili rakip kestirimi — MSO `hedef_kestirici.py` portu.
 *
 * Rakibin konumunu her zaman geçmişten öğreniyoruz: sunucudaki veri yaşı
 * (~0.3-0.7 sn) üstüne sunucu→GCS→telsiz→Jetson boru gecikmesi (~0.4-0.8 sn)
 * biniyor. Ham konuma nişan alırsak rakibin BULUNDUĞU yere değil, BULUNDUĞU
 * yere uçarız ve hep arkada kalırız.
 *
 * Çözüm: ölçümün gerçek anını hesapla (@c t_recv - boru_gecikmesi - veri_yaşı),
 * durumu oradan bugüne ileri sar. Model sabit hız değil SABİT DÖNÜŞ ORANI;
 * rakipler kaçarken sürekli döner ve sabit hız modeli savrulur.
 *
 * Sunucu hız ve yönelimi de verdiği için hızı konum farkından türetmiyoruz.
 * Rakip biz sorduğumuzdan yavaş veri bastığında aynı kayıt tekrar gelir;
 * içerik karşılaştırmasıyla eleniyor, yoksa hız/dönüş kestirimi bozulur.
 *
 * @warning Davranış MSO Python'u ile bayt düzeyinde eşleşmelidir; doğrulaması
 * `test_target_estimator_golden` (fixtures/mso_hedef_kestirici_ref.csv).
 * Buradaki formüller ve sıralama keyfi değildir, değiştirmeden önce referansı
 * yeniden üretin (scripts/mso_reference_gen.py).
 */
#ifndef SAVASAN_CONTROL_TARGET_ESTIMATOR_HPP_
#define SAVASAN_CONTROL_TARGET_ESTIMATOR_HPP_

#include <cstdint>

#include "control/local_frame.hpp"

namespace savasan::control {

struct TargetEstimatorConfig {
  double pipeline_delay_s = 0.6;  ///< Sunucu→GCS→telsiz→Jetson gecikmesi.
  double alpha = 0.65;            ///< Konum düzeltme kazancı.
  double beta = 0.55;             ///< Hız düzeltme kazancı.
  double gamma = 0.45;            ///< Dönüş oranı düzeltme kazancı.
};

/// @brief Kestirilen rakip durumu (MSO `tahmin()` sözlüğünün karşılığı).
struct TargetEstimate {
  double x = 0.0;              ///< Yerel doğu (m).
  double y = 0.0;              ///< Yerel kuzey (m).
  double lat_deg = 0.0;
  double lon_deg = 0.0;
  double alt_m = 0.0;
  double vx = 0.0;             ///< Doğu hızı (m/s).
  double vy = 0.0;             ///< Kuzey hızı (m/s).
  double vz = 0.0;
  double speed_mps = 0.0;
  double heading_deg = 0.0;    ///< Pusula (0=kuzey, 90=doğu).
  double turn_rate_dps = 0.0;  ///< Güven katsayısı uygulanmış dönüş oranı.
  double data_age_s = 0.0;
};

/// @brief Tek bir rakip İHA'nın durumunu kestirir.
class TargetEstimator {
 public:
  /// Talon sınıfı sabit kanat için fiziksel sınırlar.
  static constexpr double kMaxTurnRateDps = 45.0;
  static constexpr double kInnovationThresholdM = 6.0;   ///< Üstü "manevra" sayılır.
  static constexpr double kVolatilityThresholdDps2 = 12.0;
  static constexpr double kMaxSpeedMps = 30.0;
  static constexpr double kMeasurementTimeoutS = 4.0;

  TargetEstimator() = default;
  explicit TargetEstimator(const LocalFrame& frame,
                           const TargetEstimatorConfig& cfg = TargetEstimatorConfig{})
      : frame_(frame), cfg_(cfg) {}

  void Reset();

  /// @brief Yeni bir rakip konum raporu işler.
  /// @param t_recv_s Raporun Jetson'a varış anı (saniye, monoton).
  /// @return true = kabul edildi, false = tekrar/geçersiz, atıldı.
  bool AddMeasurement(double lat_deg, double lon_deg, double alt_m, double speed_mps,
                      double heading_deg, int time_diff_ms, double t_recv_s,
                      int target_id = -1);

  /// @brief Hedefin @p t_s (artı @p extra_lead_s) anındaki durumunu kestirir.
  /// @param extra_lead_s Kendi komut gecikmemiz için ek ileri sarma.
  /// @return false = yeterli veri yok veya kestirim çok bayat.
  bool Predict(double t_s, double extra_lead_s, TargetEstimate* out) const;

  bool IsValid(double t_s) const;

  int measurement_count() const { return measurement_count_; }
  int repeat_count() const { return repeat_count_; }
  int target_id() const { return target_id_; }

 private:
  /// @brief Durumu sabit dönüş oranıyla @p dt kadar ileri sarar.
  void Advance(double dt);

  LocalFrame frame_{};
  TargetEstimatorConfig cfg_{};

  int target_id_ = -1;
  bool has_state_ = false;
  double x_ = 0.0;
  double y_ = 0.0;
  double alt_ = 0.0;
  double vx_ = 0.0;
  double vy_ = 0.0;
  double vz_ = 0.0;
  double omega_dps_ = 0.0;
  double t_ = 0.0;  ///< Durumun ait olduğu an.

  bool has_last_heading_ = false;
  double last_heading_deg_ = 0.0;
  bool has_last_meas_t_ = false;
  double last_meas_t_ = 0.0;  ///< En son KABUL edilen ölçümün anı.

  int measurement_count_ = 0;
  int repeat_count_ = 0;

  /// Tekrar tespiti için ham içeriğin yuvarlanmış hâli.
  bool has_last_content_ = false;
  int64_t last_lat_q_ = 0;
  int64_t last_lon_q_ = 0;
  int64_t last_speed_q_ = 0;
  int64_t last_heading_q_ = 0;

  bool has_last_omega_meas_ = false;
  double last_omega_meas_ = 0.0;
  double omega_volatility_ = 0.0;
};

}  // namespace savasan::control

#endif  // SAVASAN_CONTROL_TARGET_ESTIMATOR_HPP_
