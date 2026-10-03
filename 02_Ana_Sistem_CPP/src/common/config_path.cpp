/**
 * @file config_path.cpp
 * @brief Yapılandırma yolu çözümleyicinin uygulaması. Ortam değişkenleri ve
 *        derleme zamanı kaynak dizinini kullanarak config dosya yollarını üretir.
 */
#include "common/config_path.hpp"

#include <cstdlib>
#include <string>

namespace savasan::common {

namespace {

/// @brief Taban ve göreceli yolu tek bir eğik çizgi ile birleştirir.
/// @param base Taban dizin (boşsa rel döner).
/// @param rel Göreceli yol (boşsa base döner).
/// @return Birleştirilmiş yol.
std::string JoinPath(const std::string& base, const std::string& rel) {
  if (base.empty()) {
    return rel;
  }
  if (rel.empty()) {
    return base;
  }
  if (base.back() == '/') {
    return base + rel;
  }
  return base + '/' + rel;
}

}  // namespace

/// @copydoc savasan::common::DeepstreamConfigRelPath
std::string DeepstreamConfigRelPath(const char* filename) {
  return std::string("config/deepstream/") + filename;
}

/// @copydoc savasan::common::ResolveConfigPath
std::string ResolveConfigPath(const char* env_var, const char* relative_path) {
  if (env_var != nullptr) {
    const char* env_value = std::getenv(env_var);
    if (env_value != nullptr && env_value[0] != '\0') {
      return env_value;
    }
  }

  const char* config_root = std::getenv("SAVASAN_CONFIG_ROOT");
  if (config_root != nullptr && config_root[0] != '\0') {
    return JoinPath(config_root, relative_path);
  }

#ifdef SAVASAN_SOURCE_DIR
  return JoinPath(SAVASAN_SOURCE_DIR, relative_path);
#else
  return relative_path;
#endif
}

}  // namespace savasan::common
