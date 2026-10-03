/**
 * @file icamera_source.hpp
 * @brief Kamera kaynakları için ortak soyut arayüz ve kare (frame) veri yapısı.
 *
 * Farklı kamera girişleri bu arayüzü uygulayarak tek tip bir
 * yaşam döngüsü (başlat/durdur) ve kare alma sözleşmesi sunar.
 */
#ifndef SAVASAN_CAMERA_ICAMERA_SOURCE_HPP_
#define SAVASAN_CAMERA_ICAMERA_SOURCE_HPP_

#include <chrono>
#include <cstdint>

namespace savasan::camera {

/// @brief Tek bir kameradan alınan kareyi ve üst verilerini taşıyan yapı.
struct FrameData {
  bool valid = false;                                  ///< Kare geçerli mi (başarıyla alındı mı).
  uint64_t frame_id = 0;                               ///< Artan kare sıra numarası (sayaç).
  int width = 0;                                       ///< Kare genişliği (piksel).
  int height = 0;                                      ///< Kare yüksekliği (piksel).
  std::chrono::steady_clock::time_point capture_tp{};  ///< Karenin yakalandığı zaman damgası (steady_clock).
};

/// @brief Tüm kamera kaynaklarının uygulaması gereken soyut arayüz.
class ICameraSource {
 public:
  /// @brief Sanal yıkıcı; türetilmiş sınıfların güvenli yıkımını sağlar.
  virtual ~ICameraSource() = default;
  /// @brief Kamera kaynağını başlatır (pipeline/cihaz açılışı).
  /// @return Başlatma başarılıysa true, aksi halde false.
  virtual bool Start() = 0;
  /// @brief Kamera kaynağını durdurur ve kaynakları serbest bırakır.
  virtual void Stop() = 0;
  /// @brief Kaynağın çalışır durumda olup olmadığını döner.
  /// @return Çalışıyorsa true.
  virtual bool IsRunning() const = 0;
  /// @brief En güncel kareyi döner.
  /// @return Geçerli kare yoksa valid=false olan FrameData.
  virtual FrameData GetFrame() = 0;
};

}  // namespace savasan::camera

#endif  // SAVASAN_CAMERA_ICAMERA_SOURCE_HPP_
