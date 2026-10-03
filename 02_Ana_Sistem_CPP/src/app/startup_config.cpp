/**
 * @file startup_config.cpp
 * @brief Başlangıç yapılandırması ayrıştırma uygulaması. Komut satırı
 *        argümanlarını (faz, kamera modu, sink, config yolu) ve ilgili ortam
 *        değişkenlerini işleyip dosya erişilebilirliğini doğrular.
 */
#include "app/startup_config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <unistd.h>

#include "common/config_path.hpp"
#include "common/log.hpp"

namespace {

/// @brief Varsayılan nvinfer (PGIE) config göreceli yolu.
constexpr char kDefaultPgiConfigRel[] = "config/deepstream/config_infer_primary.txt";
/// @brief Varsayılan tracker config göreceli yolu.
constexpr char kDefaultTrackerConfigRel[] = "config/deepstream/tracker_config.yml";
/// @brief Agresif profil için varsayılan tracker config göreceli yolu.
constexpr char kDefaultTrackerConfigAggressiveRel[] =
    "config/deepstream/tracker_config_aggressive.yml";
/// @brief Sakin (calm) profil için varsayılan tracker config göreceli yolu.
constexpr char kDefaultTrackerConfigCalmRel[] = "config/deepstream/tracker_config_calm.yml";

/// @brief Varsayılan NvDCF tracker düşük seviye kütüphane (.so) yolu.
constexpr char kDefaultNvDCFLib[] =
    "/opt/nvidia/deepstream/deepstream/lib/libnvds_nvmultiobjecttracker.so";

/// @brief Geçerli tracker profil adı mı kontrol eder.
bool IsKnownTrackerProfile(const char* profile) {
  return std::strcmp(profile, "default") == 0 || std::strcmp(profile, "aggressive") == 0 ||
         std::strcmp(profile, "calm") == 0 || std::strcmp(profile, "auto") == 0;
}

/// @brief Dize başı/sonu boşluklarını kırpar.
std::string TrimCopy(std::string value) {
  const auto not_space = [](const unsigned char c) { return !std::isspace(c); };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
  return value;
}

/// @brief Dosya yolunun üst dizinini döndürür.
std::string ParentDirOf(const std::string& path) {
  const std::size_t pos = path.find_last_of('/');
  if (pos == std::string::npos) {
    return ".";
  }
  if (pos == 0) {
    return "/";
  }
  return path.substr(0, pos);
}

/// @brief Göreli yolu config dosyasına göre mutlak/çözülmüş yola dönüştürür.
std::string ResolveRelativeToConfig(const std::string& config_path, const std::string& ref) {
  if (ref.empty() || ref[0] == '/') {
    return ref;
  }
  return ParentDirOf(config_path) + "/" + ref;
}

/// @brief nvinfer config içinden model-engine-file satırını okur.
/// @return Satır yoksa boş dize.
std::string ExtractModelEngineFile(const std::string& pgi_config_path) {
  std::ifstream in(pgi_config_path);
  if (!in) {
    return {};
  }

  constexpr char kKey[] = "model-engine-file=";
  std::string line;
  while (std::getline(in, line)) {
    const std::size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos || line[start] == '#') {
      continue;
    }
    if (line.compare(start, std::strlen(kKey), kKey) != 0) {
      continue;
    }

    std::string value = TrimCopy(line.substr(start + std::strlen(kKey)));
    const std::size_t hash = value.find('#');
    if (hash != std::string::npos) {
      value = TrimCopy(value.substr(0, hash));
    }
    return ResolveRelativeToConfig(pgi_config_path, value);
  }
  return {};
}

/// @brief Ortam değişkeni doluysa onu, değilse verilen varsayılanı döndürür.
/// @param env_var Bakılacak ortam değişkeni adı.
/// @param fallback Değişken yoksa/boşsa kullanılacak varsayılan değer.
/// @return Ortam değeri veya varsayılan.
std::string EnvOrDefault(const char* env_var, const char* fallback) {
  const char* v = std::getenv(env_var);
  return (v != nullptr && v[0] != '\0') ? v : fallback;
}

/// @brief Aktif tracker profiline göre varsayılan tracker config yolunu çözer.
/// @return SAVASAN_TRACKER_PROFILE değerine uygun çözülmüş config yolu.
std::string ResolveTrackerConfigDefaultByProfile() {
  const char* profile = std::getenv("SAVASAN_TRACKER_PROFILE");
  if (profile == nullptr || profile[0] == '\0') {
    return savasan::common::ResolveConfigPath("SAVASAN_TRACKER_CONFIG_DEFAULT",
                                              kDefaultTrackerConfigRel);
  }
  if (std::strcmp(profile, "aggressive") == 0) {
    return savasan::common::ResolveConfigPath("SAVASAN_TRACKER_CONFIG_AGGRESSIVE",
                                              kDefaultTrackerConfigAggressiveRel);
  }
  if (std::strcmp(profile, "calm") == 0) {
    return savasan::common::ResolveConfigPath("SAVASAN_TRACKER_CONFIG_CALM",
                                              kDefaultTrackerConfigCalmRel);
  }
  return savasan::common::ResolveConfigPath("SAVASAN_TRACKER_CONFIG_DEFAULT",
                                            kDefaultTrackerConfigRel);
}

/// @brief Ortamdan tracker profil adını okuyup geçerli bir değere eşler.
/// @return "default", "aggressive", "calm" veya "auto" değerlerinden biri.
std::string ResolveTrackerProfile() {
  const char* profile = std::getenv("SAVASAN_TRACKER_PROFILE");
  if (profile == nullptr || profile[0] == '\0') {
    return "default";
  }
  if (IsKnownTrackerProfile(profile)) {
    return profile;
  }
  SAVASAN_LOG_F(savasan::common::LogLevel::kWarn, "startup_config",
                "Bilinmeyen SAVASAN_TRACKER_PROFILE='%s'; 'default' kullaniliyor", profile);
  return "default";
}

/// @brief Verilen yolun okunabilir olup olmadığını kontrol eder.
/// @param path Kontrol edilecek dosya yolu.
/// @return Dosya okunabiliyorsa true.
bool CheckFileReadable(const std::string& path) { return ::access(path.c_str(), R_OK) == 0; }

/// @brief SAVASAN_RUN_SECONDS ortam değişkeninden çalışma süresini okur.
/// @param fallback_seconds Değişken yoksa kullanılacak varsayılan süre.
/// @return 0 (süresiz) ya da [5, 600] aralığına sıkıştırılmış saniye değeri.
int RunSecondsFromEnv(const int fallback_seconds = 30) {
  const char* v = std::getenv("SAVASAN_RUN_SECONDS");
  if (v == nullptr || v[0] == '\0') return fallback_seconds;
  const int s = std::atoi(v);
  if (s == 0) {
    return 0;
  }
  const int clamped = std::clamp(s, 5, 600);
  if (clamped != s) {
    SAVASAN_LOG_F(savasan::common::LogLevel::kWarn, "startup_config",
                  "SAVASAN_RUN_SECONDS=%d aralik disi; %d olarak kirpildi", s, clamped);
  }
  return clamped;
}

/// @brief SAVASAN_INGEST_FPS ortam değişkeninden kamera ingest FPS değerini okur.
/// @param fallback_fps Değişken yok/geçersizse kullanılacak varsayılan FPS.
/// @return [5, 120] aralığına sıkıştırılmış FPS değeri.
unsigned int IngestFpsFromEnv(unsigned int fallback_fps) {
  const char* v = std::getenv("SAVASAN_INGEST_FPS");
  if (v == nullptr || v[0] == '\0') {
    return fallback_fps;
  }
  const int fps = std::atoi(v);
  if (fps <= 0) {
    SAVASAN_LOG_F(savasan::common::LogLevel::kWarn, "startup_config",
                  "Gecersiz SAVASAN_INGEST_FPS='%s'; varsayilan %u kullaniliyor", v,
                  fallback_fps);
    return fallback_fps;
  }
  const int clamped = std::clamp(fps, 5, 120);
  if (clamped != fps) {
    SAVASAN_LOG_F(savasan::common::LogLevel::kWarn, "startup_config",
                  "SAVASAN_INGEST_FPS=%d aralik disi; %d olarak kirpildi", fps, clamped);
  }
  return static_cast<unsigned int>(clamped);
}

}  // namespace

namespace savasan::app {

/// @copydoc savasan::app::ParseStartupConfig
bool ParseStartupConfig(int argc, char** argv, StartupConfig* out, std::string* error) {
  if (out == nullptr) {
    if (error != nullptr) *error = "internal: startup config output is null";
    return false;
  }

  StartupConfig cfg{};
  cfg.pgi_config =
      savasan::common::ResolveConfigPath("SAVASAN_PGI_CONFIG", kDefaultPgiConfigRel);
  cfg.tracker_profile = ResolveTrackerProfile();
  const std::string tracker_default = ResolveTrackerConfigDefaultByProfile();
  cfg.tracker_config = EnvOrDefault("SAVASAN_TRACKER_CONFIG", tracker_default.c_str());
  cfg.ll_lib = EnvOrDefault("SAVASAN_TRACKER_LIB", kDefaultNvDCFLib);

  int argi = 1;
  if (argc >= 2) {
    if (std::strcmp(argv[1], "phase2") == 0) {
      cfg.phase = 2;
      argi = 2;
    } else if (std::strcmp(argv[1], "phase3") == 0) {
      cfg.phase = 3;
      argi = 2;
    } else if (std::strcmp(argv[1], "phase4") == 0) {
      cfg.phase = 4;
      cfg.phase4_hybrid = true;
      argi = 2;
    } else if (std::strcmp(argv[1], "phase5") == 0) {
      cfg.phase = 5;
      cfg.phase4_hybrid = true;
      argi = 2;
    } else if (std::strcmp(argv[1], "phase1") == 0) {
      cfg.phase = 1;
      argi = 2;
    } else if (std::strncmp(argv[1], "phase", 5) == 0) {
      SAVASAN_LOG_F(savasan::common::LogLevel::kWarn, "startup_config",
                    "Bilinmeyen faz argumani='%s'; Faz 1 (kamera dogrulama) secildi", argv[1]);
      cfg.phase = 1;
      argi = 2;
    }
  }

  // Geriye uyumluluk: argv'de "usb" geçilirse atlanır (tek kamera kaynağı).
  if (argc > argi && std::strcmp(argv[argi], "usb") == 0) {
    ++argi;
  } else if (argc > argi && std::strcmp(argv[argi], "csi") == 0) {
    if (error != nullptr) {
      *error = "HATA: CSI kamera destegi kaldirildi; yalnizca USB BRIO kullanilir.";
    }
    return false;
  }

  while (argc > argi) {
    if (std::strcmp(argv[argi], "display") == 0) {
      cfg.sink = deepstream::SinkType::kDisplay;
    } else if (std::strcmp(argv[argi], "hybrid") == 0) {
      cfg.phase4_hybrid = true;
      cfg.lock_mode_from_argv = true;
    } else if (std::strcmp(argv[argi], "baseline") == 0) {
      cfg.phase4_hybrid = false;
      cfg.lock_mode_from_argv = true;
    } else if (argv[argi][0] == '/') {
      cfg.pgi_config = argv[argi];
    }
    ++argi;
  }

  if (cfg.sink == deepstream::SinkType::kFake) {
    const char* sd = std::getenv("SAVASAN_DISPLAY");
    if (sd != nullptr && std::strcmp(sd, "display") == 0) {
      cfg.sink = deepstream::SinkType::kDisplay;
    }
  }

  if ((cfg.phase == 4 || cfg.phase == 5) && !cfg.lock_mode_from_argv) {
    const char* lm = std::getenv("SAVASAN_LOCK_MODE");
    if (lm != nullptr && lm[0] != '\0') {
      if (std::strcmp(lm, "baseline") == 0) {
        cfg.phase4_hybrid = false;
      } else if (std::strcmp(lm, "hybrid") == 0) {
        cfg.phase4_hybrid = true;
      }
    }
  }

  cfg.ingest.width = 800;
  cfg.ingest.height = 600;
  // BRIO MJPEG 800x600 @ 30 (4:3, şartname). 60 fps bu çözünürlükte yok.
  cfg.ingest.fps = IngestFpsFromEnv(30);
  const char* v4l = std::getenv("SAVASAN_V4L2_DEVICE");
  if (v4l != nullptr && v4l[0] != '\0') {
    cfg.ingest.v4l2_device = v4l;
  }
  if (::access(cfg.ingest.v4l2_device.c_str(), F_OK) != 0) {
    if (error != nullptr) *error = "HATA: Kamera cihazi bulunamadi: " + cfg.ingest.v4l2_device;
    return false;
  }

  if (cfg.phase >= 2 && !CheckFileReadable(cfg.pgi_config)) {
    if (error != nullptr) *error = "HATA: nvinfer config bulunamadi veya okunamiyor: " + cfg.pgi_config;
    return false;
  }
  if (cfg.phase >= 2) {
    const std::string model_engine = ExtractModelEngineFile(cfg.pgi_config);
    if (!model_engine.empty() && !CheckFileReadable(model_engine)) {
      if (error != nullptr) {
        *error = "HATA: model-engine-file okunamiyor: " + model_engine;
      }
      return false;
    }
  }
  if (cfg.phase >= 3 && !CheckFileReadable(cfg.tracker_config)) {
    if (error != nullptr)
      *error = "HATA: nvtracker config bulunamadi veya okunamiyor: " + cfg.tracker_config;
    return false;
  }
  if (cfg.phase >= 3 && !CheckFileReadable(cfg.ll_lib)) {
    if (error != nullptr) *error = "HATA: tracker kutuphane (.so) bulunamadi veya okunamiyor: " + cfg.ll_lib;
    return false;
  }

  cfg.run_seconds = RunSecondsFromEnv(30);
  *out = cfg;
  return true;
}

}  // namespace savasan::app
