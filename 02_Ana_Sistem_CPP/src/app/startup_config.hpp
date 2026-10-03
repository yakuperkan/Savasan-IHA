/**
 * @file startup_config.hpp
 * @brief Uygulama başlangıç yapılandırması. Komut satırı argümanlarından ve
 *        ortam değişkenlerinden çözülen faz, kaynak (sink), kamera ve config
 *        yollarını tutan veri yapısı ile ayrıştırma fonksiyonunu tanımlar.
 */
#ifndef SAVASAN_APP_STARTUP_CONFIG_HPP_
#define SAVASAN_APP_STARTUP_CONFIG_HPP_

#include <string>

#include "deepstream/sink_type.hpp"
#include "pipeline/ingest_config.hpp"

namespace savasan::app {

/// @brief Komut satırı ve ortamdan çözülen uygulama başlangıç ayarları.
struct StartupConfig {
  int phase = 1;                                          ///< Çalıştırılacak faz numarası (1..5).
  bool phase4_hybrid = false;                             ///< Faz4/5 hibrit kilit modu açık mı.
  bool lock_mode_from_argv = false;                       ///< Kilit modu argümandan mı belirlendi.
  deepstream::SinkType sink = deepstream::SinkType::kFake; ///< Çıkış sink türü (fake/display).
  std::string pgi_config;                                 ///< nvinfer (PGIE) config dosya yolu.
  std::string tracker_config;                             ///< nvtracker config dosya yolu.
  std::string tracker_profile = "default";                ///< Tracker profili (default/calm/aggressive/auto).
  std::string ll_lib;                                     ///< Tracker düşük seviye kütüphane (.so) yolu.
  IngestConfig ingest{};                                  ///< Kamera/giriş (ingest) yapılandırması.
  int run_seconds = 30;                                   ///< Çalışma süresi (sn); 0 = süresiz/servis.
};

/// @brief Komut satırı argümanlarını ve ortam değişkenlerini ayrıştırarak
///        başlangıç yapılandırmasını üretir.
/// @param argc Argüman sayısı.
/// @param argv Argüman dizisi.
/// @param out Başarılı olduğunda doldurulacak çıktı yapılandırması.
/// @param error Hata durumunda açıklayıcı mesajın yazılacağı dize (nullptr olabilir).
/// @return Ayrıştırma ve doğrulama başarılıysa true, aksi halde false.
bool ParseStartupConfig(int argc, char** argv, StartupConfig* out, std::string* error);

}  // namespace savasan::app

#endif  // SAVASAN_APP_STARTUP_CONFIG_HPP_
