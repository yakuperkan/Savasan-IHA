/**
 * @file vehicle_state_estimator.hpp
 * @brief Telemetri + görüntü hızlarını birleştiren araç durum kestiricisi.
 */
#ifndef SAVASAN_CONTROL_VEHICLE_STATE_ESTIMATOR_HPP_
#define SAVASAN_CONTROL_VEHICLE_STATE_ESTIMATOR_HPP_

#include <chrono>
#include <mutex>

namespace savasan::control {

/**
 * @brief Aracın hız/konum durumunu kestiren füzyon bileşeni.
 *
 * NWU (North-West-Up) yerel teğet düzleminde çalışır: X=Kuzey, Y=Batı,
 * Z=Yukarı (m / m/s). Telemetri ve çıkış durumu bu eksenlerdedir; eksenler arası
 * dönüşüm yapılmaz. Görüntü kaynaklı hız (@ref VisionInput) yalnızca Y/Z
 * eksenlerine türetilir; X ekseni telemetriden gelir. Telemetri ve görüntü
 * ağırlıklı ortalama ile birleştirilir, ardından innovation kapılı düşük geçiren
 * filtreden (LPF) geçirilip hız sınırına kırpılır. Thread-safe (@c state_mutex_).
 */
class VehicleStateEstimator {
 public:
  /// @brief Füzyon ağırlıkları, hız sınırı ve filtre parametreleri.
  struct Config {
    float telemetry_weight = 0.7f;        ///< Telemetri füzyon ağırlığı.
    float vision_weight = 0.3f;           ///< Görüntü füzyon ağırlığı.
    float max_speed_mps = 25.0f;          ///< Eksen başına mutlak hız sınırı (m/s).
    float velocity_alpha = 0.4f;          ///< Hız LPF katsayısı (1.0 = filtre kapalı).
    float innovation_gate_mps = 5.0f;     ///< Hız innovation kırpma eşiği (m/s).
    float yaw_rate_alpha = 0.4f;          ///< Yaw hızı LPF katsayısı.
    float yaw_innovation_gate_dps = 50.0f;///< Yaw hızı innovation kırpma eşiği (derece/s).
  };

  /// @brief Otopilottan gelen hız/yaw telemetri girişi.
  struct TelemetryInput {
    bool valid = false;        ///< Giriş geçerli mi.
    float vel_x_mps = 0.0f;    ///< Kuzey hız (m/s).
    float vel_y_mps = 0.0f;    ///< Batı hız (m/s).
    float vel_z_mps = 0.0f;    ///< Yukarı hız (m/s).
    float yaw_rate_dps = 0.0f; ///< Yaw açısal hızı (derece/s).
  };

  /// @brief Görüntü işlemeden türetilen hız girişi (yalnızca Y/Z anlamlı).
  struct VisionInput {
    bool valid = false;     ///< Giriş geçerli mi.
    float vel_x_mps = 0.0f; ///< Kuzey hız (genelde kullanılmaz).
    float vel_y_mps = 0.0f; ///< Batı hız (m/s).
    float vel_z_mps = 0.0f; ///< Yukarı hız (m/s).
  };

  /// @brief Kestirilen araç durumu (konum, hız, yaw hızı).
  struct State {
    float pos_x_m = 0.0f;        ///< Kuzey konumu (hız integralinden, m).
    float pos_y_m = 0.0f;        ///< Batı konumu (m).
    float pos_z_m = 0.0f;        ///< Yukarı konumu (m).
    float vel_x_mps = 0.0f;      ///< Filtrelenmiş Kuzey hız (m/s).
    float vel_y_mps = 0.0f;      ///< Filtrelenmiş Batı hız (m/s).
    float vel_z_mps = 0.0f;      ///< Filtrelenmiş Yukarı hız (m/s).
    float yaw_rate_dps = 0.0f;   ///< Filtrelenmiş yaw açısal hızı (derece/s).
    bool telemetry_valid = false;///< Son adımda telemetri geçerli miydi.
  };

  /// @brief Verilen yapılandırma ile kestiriciyi oluşturur.
  explicit VehicleStateEstimator(const Config& cfg) : config_(cfg) {}
  /// @brief Varsayılan yapılandırma ile kestiriciyi oluşturur.
  explicit VehicleStateEstimator() = default;

  /// @brief En güncel telemetri girişini saklar (bir sonraki @ref Step'te kullanılır).
  void UpdateTelemetry(const TelemetryInput& t);
  /// @brief En güncel görüntü hız girişini saklar.
  void UpdateVision(const VisionInput& v);
  /// @brief Bir füzyon adımı çalıştırır (dt'yi zaman damgasından hesaplar).
  /// @note Tek yazıcı sözleşmesi: aynı anda yalnızca bir thread @ref Step çağırmalıdır.
  void Step(std::chrono::steady_clock::time_point now_tp);

  /// @brief Kestirilen durumun kopyasını döndürür.
  State GetState() const;
  /// @brief Aktif yapılandırmanın kopyasını döndürür.
  Config GetConfig() const;
  /// @brief Yapılandırmayı günceller.
  void SetConfig(const Config& cfg);

 private:
  Config config_{};        ///< Aktif yapılandırma.
  State state_{};          ///< Güncel kestirilmiş durum.
  TelemetryInput telemetry_{}; ///< En son telemetri girişi.
  VisionInput vision_{};       ///< En son görüntü girişi.
  std::chrono::steady_clock::time_point last_step_tp_{}; ///< Son adım zaman damgası.
  mutable std::mutex state_mutex_;                       ///< Tüm durumu koruyan kilit.
};

}  // namespace savasan::control

#endif  // SAVASAN_CONTROL_VEHICLE_STATE_ESTIMATOR_HPP_
