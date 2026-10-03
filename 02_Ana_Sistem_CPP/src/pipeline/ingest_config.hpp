/**
 * @file ingest_config.hpp
 * @brief USB kamera ingest yapılandırması: çözünürlük, FPS ve V4L2 cihaz yolu.
 */
#ifndef SAVASAN_PIPELINE_INGEST_CONFIG_HPP_
#define SAVASAN_PIPELINE_INGEST_CONFIG_HPP_

#include <string>

namespace savasan {

/// @brief USB (Logitech BRIO) kamera yakalama parametreleri.
struct IngestConfig {
  unsigned int width = 800;   ///< Yakalama görüntü genişliği (piksel).
  unsigned int height = 600;  ///< Yakalama görüntü yüksekliği (piksel).
  unsigned int fps = 30;      ///< Hedef kare hızı (saniyedeki kare sayısı).
  /// @brief USB kamera cihaz yolu.
  ///        (BRIO'da çoğu kurulumda /dev/video0 veya /dev/video2 = yakalama;
  ///        /dev/video1 sıklıkla yalnızca metadata — v4l2-ctl --device=/dev/videoN ile doğrulayın.)
  std::string v4l2_device = "/dev/video0";
};

}  // namespace savasan

#endif  // SAVASAN_PIPELINE_INGEST_CONFIG_HPP_
