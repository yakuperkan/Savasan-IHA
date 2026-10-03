/**
 * @file world_target_estimator.hpp
 * @brief Görüntü gözleminden NED güdüm hatası üreten kestirici.
 *
 * Piksel merkez sapmasını dereceye çevirir; menzil kestirimi bbox alanı veya
 * pinhole modelinden gelir. Tüm PID Jetson tarafında çalışır.
 */
#ifndef SAVASAN_CONTROL_WORLD_TARGET_ESTIMATOR_HPP_
#define SAVASAN_CONTROL_WORLD_TARGET_ESTIMATOR_HPP_

namespace savasan::control {

/// @brief Görüntü merkez sapmasından türetilen açısal hatalar (derece).
struct ImageAngularError {
  float yaw_deg = 0.0f;    ///< NED Doğu: hedef sağda → pozitif.
  float pitch_deg = 0.0f;  ///< NED Aşağı: hedef altta → pozitif; yukarı → negatif.
};

/**
 * @brief Görüntü gözleminden NED güdüm hatasını üreten bileşen.
 */
class WorldTargetEstimator {
 public:
  /// @brief Mesafe modeli ve piksel→derece kalibrasyonu.
  struct Config {
    float desired_standoff_m = 20.0f; ///< Hedefin ideal takip mesafesi (m).
    /// Basit mesafe modeli: distance ~= distance_gain / sqrt(bbox_alan_norm).
    float distance_gain = 0.20f;
    /// Pinhole: distance ~= (focal_length_px * target_real_width_m) / bbox_w_px.
    float focal_length_px = 0.0f;
    float target_real_width_m = 0.0f;
    float image_width_px = 800.0f;
    float min_distance_m = 3.0f;
    float max_distance_m = 120.0f;
    /// Normalize piksel sapması → derece: açı_hata_deg = offset * gain.
    float pixel_to_deg_gain = 60.0f;
  };

  /// @brief Tek kareye ait hedef gözlemi (normalize merkez + bbox).
  struct TargetObservation {
    bool valid = false;
    float nx = 0.5f;
    float ny = 0.5f;
    float bbox_w_norm = 0.0f;
    float bbox_h_norm = 0.0f;
  };

  /// @brief Kestirim sonucu: NED güdüm hataları (PID girişi).
  struct WorldTargetEstimate {
    bool valid = false;
    float closure_error_m = 0.0f;  ///< NED Kuzey kapanma hatası (m); + = hedef uzak.
    ImageAngularError angular{};     ///< Yatay/dikey açısal hatalar (deg).
    float distance_m = 0.0f;
    float confidence = 0.0f;
  };

  explicit WorldTargetEstimator(const Config& cfg) : config_(cfg) {}
  explicit WorldTargetEstimator() = default;

  WorldTargetEstimate Estimate(const TargetObservation& obs);

  const Config& GetConfig() const { return config_; }
  void SetConfig(const Config& cfg) { config_ = cfg; }

 private:
  Config config_{};
};

}  // namespace savasan::control

#endif  // SAVASAN_CONTROL_WORLD_TARGET_ESTIMATOR_HPP_
