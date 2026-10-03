/**
 * @file lock_metrics.hpp
 * @brief Kilit kalite metriklerini (titreme, kayıp sayısı) toplayan yardımcı.
 */
#ifndef SAVASAN_RUNNERS_LOCK_METRICS_HPP_
#define SAVASAN_RUNNERS_LOCK_METRICS_HPP_

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace savasan::runners {

/**
 * @brief Kilit metriklerini hesaplayan sınıf.
 *
 * Son N örnek üzerinden hedef merkezinin titremesini (jitter, standart sapma),
 * kilit kayıp sayısını ve tespitsiz kare sayısını izler. Dairesel tampon kullanır.
 */
class LockMetrics {
 public:
  /// @brief Anlık metrik görüntüsü.
  struct Snapshot {
    float jitter = 0.0f;             ///< Hedef merkezi titremesi (normalize, std sapma).
    uint32_t lock_loss_count = 0;    ///< Toplam kilit kaybı sayısı.
    int frames_without_detection = 0;///< Art arda tespitsiz kare sayısı.
    bool lock_valid = false;         ///< Son güncellemede kilit geçerli miydi.
    float hedef_norm_x = 0.0f;       ///< Son örneğin normalize X konumu.
    float hedef_norm_y = 0.0f;       ///< Son örneğin normalize Y konumu.
    float hedef_bbox_w_norm = 0.0f;  ///< Son örneğin normalize kutu genişliği.
    float hedef_bbox_h_norm = 0.0f;  ///< Son örneğin normalize kutu yüksekliği.
    std::int64_t idle_delay_us = 0;  ///< Probe yakalama → idle işleme gecikmesi (us).
  };

  /// @brief Yeni bir kare örneğiyle metrikleri günceller.
  /// @param lock_valid Kilit geçerli mi (geçersizse örnek tamponu temizlenir).
  /// @param nx,ny Hedef merkezi normalize konumu.
  /// @param frames_without_detection Tespitsiz kare sayısı.
  void Update(const bool lock_valid, const float nx, const float ny,
              const int frames_without_detection, const float bbox_w_norm = 0.0f,
              const float bbox_h_norm = 0.0f) {
    if (lock_valid) {
      PushSample(nx, ny);
      last_bbox_w_norm_ = bbox_w_norm;
      last_bbox_h_norm_ = bbox_h_norm;
    } else {
      ClearSamples();
      last_bbox_w_norm_ = 0.0f;
      last_bbox_h_norm_ = 0.0f;
    }

    if (prev_lock_valid_ && !lock_valid) {
      ++lock_loss_count_;
    }
    prev_lock_valid_ = lock_valid;
    frames_without_detection_ = frames_without_detection;
    jitter_ = ComputeJitterLocked();
  }

  /// @brief Probe yakalama ile idle işleme arasındaki gecikmeyi kaydeder (us).
  void SetIdleDelayUs(const std::int64_t delay_us) { idle_delay_us_ = delay_us; }

  /// @brief Güncel metriklerin anlık görüntüsünü döndürür.
  Snapshot GetSnapshot() const {
    Snapshot s{};
    s.jitter = jitter_;
    s.lock_loss_count = lock_loss_count_;
    s.frames_without_detection = frames_without_detection_;
    s.lock_valid = prev_lock_valid_;
    if (sample_count_ > 0) {
      const Sample& last = LastSample();
      s.hedef_norm_x = last.nx;
      s.hedef_norm_y = last.ny;
      s.hedef_bbox_w_norm = last_bbox_w_norm_;
      s.hedef_bbox_h_norm = last_bbox_h_norm_;
    }
    s.idle_delay_us = idle_delay_us_;
    return s;
  }

 private:
  /// @brief Tek bir hedef merkezi örneği.
  struct Sample {
    float nx = 0.0f; ///< Normalize X.
    float ny = 0.0f; ///< Normalize Y.
  };

  static constexpr std::size_t kMaxSamples = 60; ///< Dairesel tampon kapasitesi.

  /// @brief Yeni örneği dairesel tampona ekler.
  void PushSample(const float nx, const float ny) {
    samples_[write_idx_] = Sample{nx, ny};
    write_idx_ = (write_idx_ + 1) % kMaxSamples;
    if (sample_count_ < kMaxSamples) {
      ++sample_count_;
    }
  }

  /// @brief Örnek tamponunu boşaltır (kilit kaybında çağrılır).
  void ClearSamples() {
    sample_count_ = 0;
    write_idx_ = 0;
  }

  /// @brief En son eklenen örneği döndürür.
  const Sample& LastSample() const {
    const std::size_t last_idx = (write_idx_ + kMaxSamples - 1) % kMaxSamples;
    return samples_[last_idx];
  }

  /// @brief Tampondaki en eski örneğin indeksini döndürür.
  std::size_t OldestSampleIndex() const {
    return (write_idx_ + kMaxSamples - sample_count_) % kMaxSamples;
  }

  /// @brief Titreme metriğini hesaplar: örneklerin merkezden uzaklık varyansının karekökü.
  /// 3'ten az örnekte 0 döner.
  float ComputeJitterLocked() const {
    if (sample_count_ < 3) {
      return 0.0f;
    }
    const std::size_t oldest = OldestSampleIndex();
    const float inv_n = 1.0f / static_cast<float>(sample_count_);

    float mx = 0.0f;
    float my = 0.0f;
    for (std::size_t i = 0; i < sample_count_; ++i) {
      const Sample& s = samples_[(oldest + i) % kMaxSamples];
      mx += s.nx;
      my += s.ny;
    }
    mx *= inv_n;
    my *= inv_n;

    float var = 0.0f;
    for (std::size_t i = 0; i < sample_count_; ++i) {
      const Sample& s = samples_[(oldest + i) % kMaxSamples];
      const float dx = s.nx - mx;
      const float dy = s.ny - my;
      var += (dx * dx) + (dy * dy);
    }
    var *= inv_n;
    return std::sqrt(var);
  }

  std::array<Sample, kMaxSamples> samples_{};
  std::size_t write_idx_ = 0;
  std::size_t sample_count_ = 0;
  uint32_t lock_loss_count_ = 0;
  int frames_without_detection_ = 0;
  bool prev_lock_valid_ = false;
  float jitter_ = 0.0f;
  float last_bbox_w_norm_ = 0.0f;
  float last_bbox_h_norm_ = 0.0f;
  std::int64_t idle_delay_us_ = 0;
};

}  // namespace savasan::runners

#endif  // SAVASAN_RUNNERS_LOCK_METRICS_HPP_
