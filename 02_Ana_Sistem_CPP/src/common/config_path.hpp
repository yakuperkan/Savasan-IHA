/**
 * @file config_path.hpp
 * @brief Yapılandırma dosyası yol çözümleyici arayüzü. Ortam değişkeni,
 *        SAVASAN_CONFIG_ROOT ve derleme zamanı kaynak dizini öncelik sırasıyla
 *        config dosyalarının mutlak/göreceli yolunu belirler.
 */
#ifndef SAVASAN_COMMON_CONFIG_PATH_HPP_
#define SAVASAN_COMMON_CONFIG_PATH_HPP_

#include <string>

namespace savasan::common {

/// @brief config/deepstream/ altındaki bir dosya için göreceli yol üretir.
/// @param filename config/deepstream/ dizinindeki dosya adı.
/// @return "config/deepstream/<filename>" biçiminde göreceli yol.
std::string DeepstreamConfigRelPath(const char* filename);

/// @brief Bir config dosyasının yolunu öncelik sırasına göre çözer.
/// @details Öncelik: env_var -> SAVASAN_CONFIG_ROOT + relative_path ->
///          SAVASAN_SOURCE_DIR + relative_path -> relative_path.
/// @param env_var Önce bakılacak ortam değişkeni adı (nullptr olabilir).
/// @param relative_path Kök dizinlere eklenecek göreceli yol.
/// @return Çözülmüş yapılandırma dosyası yolu.
std::string ResolveConfigPath(const char* env_var, const char* relative_path);

}  // namespace savasan::common

#endif  // SAVASAN_COMMON_CONFIG_PATH_HPP_
