/**
 * @file scope_profiler.hpp
 * @brief Tick/Tock tabanlı düşük maliyetli kapsam (scope) profilcisi.
 * @details Hot path yalnızca steady_clock::now() x2 + tamsayı karşılaştırması
 *          içerir. WARN logu sadece süre eşiği aşıldığında üretilir ve
 *          ShouldLogEvery ile hız sınırlamasına tabidir. Ayarlar ilk kullanımda
 *          ortam değişkenlerinden okunur.
 */
#ifndef SAVASAN_COMMON_SCOPE_PROFILER_HPP_
#define SAVASAN_COMMON_SCOPE_PROFILER_HPP_

#include <chrono>
#include <cstdlib>
#include <string>

#include "common/log.hpp"

namespace savasan::common {

/// @brief Profilci davranışını belirleyen ayarlar (ortam değişkeni kaynaklı).
struct ProfilerSettings {
  bool enabled = true;                            ///< Profilci genel açık/kapalı durumu.
  std::chrono::milliseconds warn_threshold{50};   ///< Aşıldığında uyarı üreten süre eşiği.
  std::chrono::milliseconds warn_log_interval{2000}; ///< Aynı kapsam için iki uyarı arası min. süre.
  const char* log_tag = "Perf";                   ///< Uyarı loglarında kullanılan etiket.
};

/// @brief Profilci ayarlarını ilk çağrıda ortam değişkenlerinden yükleyip döndürür.
/// @details SAVASAN_PROFILER, SAVASAN_PROFILER_WARN_MS,
///          SAVASAN_PROFILER_LOG_INTERVAL_MS ve SAVASAN_PROFILER_TAG okunur.
/// @return Süreç ömrü boyunca paylaşılan tekil ayar nesnesi.
inline const ProfilerSettings& GetProfilerSettings() {
  static const ProfilerSettings settings = []() {
    ProfilerSettings cfg;
    const char* en = std::getenv("SAVASAN_PROFILER");
    if (en != nullptr && en[0] == '0') {
      cfg.enabled = false;
      return cfg;
    }
    const char* thr = std::getenv("SAVASAN_PROFILER_WARN_MS");
    if (thr != nullptr && thr[0] != '\0') {
      const int v = std::atoi(thr);
      if (v >= 1 && v <= 60000) {
        cfg.warn_threshold = std::chrono::milliseconds(v);
      }
    }
    const char* interval = std::getenv("SAVASAN_PROFILER_LOG_INTERVAL_MS");
    if (interval != nullptr && interval[0] != '\0') {
      const int v = std::atoi(interval);
      if (v >= 100 && v <= 600000) {
        cfg.warn_log_interval = std::chrono::milliseconds(v);
      }
    }
    const char* tag = std::getenv("SAVASAN_PROFILER_TAG");
    if (tag != nullptr && tag[0] != '\0') {
      cfg.log_tag = tag;
    }
    return cfg;
  }();
  return settings;
}

/// @brief RAII kapsam profilcisi: yapıcıda Tick, yıkıcıda otomatik Tock yapar.
class ScopeProfiler {
 public:
  /// @brief Profilciyi başlatır ve (etkinse) ölçüm zamanını alır.
  /// @param scope_name Loglarda kullanılacak kapsam adı (yaşam süresi çağrana ait).
  /// @param warn_threshold_ms Uyarı eşiği; <= 0 ise global varsayılan
  ///        (SAVASAN_PROFILER_WARN_MS) kullanılır.
  explicit ScopeProfiler(const char* scope_name,
                         std::chrono::milliseconds warn_threshold_ms = std::chrono::milliseconds::zero())
      : scope_name_(scope_name), warn_threshold_ms_(warn_threshold_ms), active_(false), cancelled_(false) {
    const auto& settings = GetProfilerSettings();
    if (!settings.enabled) {
      return;
    }
    if (warn_threshold_ms_.count() <= 0) {
      warn_threshold_ms_ = settings.warn_threshold;
    }
    tick_tp_ = std::chrono::steady_clock::now();
    active_ = true;
  }

  /// @brief Kapsam sonunda ölçümü tamamlar (Tock).
  ~ScopeProfiler() { Tock(); }

  ScopeProfiler(const ScopeProfiler&) = delete;
  ScopeProfiler& operator=(const ScopeProfiler&) = delete;

  /// @brief Ölçümü iptal eder; yıkıcı/Tock artık uyarı üretmez.
  void Cancel() { cancelled_ = true; }

  /// @brief Geçen süreyi ölçer; eşik aşıldıysa hız sınırlı WARN logu yazar.
  void Tock() {
    if (!active_ || cancelled_) {
      return;
    }
    active_ = false;
    const auto& settings = GetProfilerSettings();
    const auto elapsed = std::chrono::steady_clock::now() - tick_tp_;
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
    if (elapsed_ms <= warn_threshold_ms_) {
      return;
    }
    if (!ShouldLogEvery(scope_name_, settings.warn_log_interval)) {
      return;
    }
    Log(LogLevel::kWarn, settings.log_tag,
        std::string(scope_name_) + " süre eşiği aşıldı elapsed_ms=" +
            std::to_string(elapsed_ms.count()) + " threshold_ms=" +
            std::to_string(warn_threshold_ms_.count()));
  }

 private:
  const char* scope_name_;                          ///< Loglarda kullanılan kapsam adı.
  std::chrono::steady_clock::time_point tick_tp_{}; ///< Ölçümün başladığı an.
  std::chrono::milliseconds warn_threshold_ms_{};   ///< Etkin uyarı eşiği.
  bool active_;                                     ///< Ölçüm hâlâ açık mı.
  bool cancelled_;                                  ///< Ölçüm iptal edildi mi.
};

/// @brief Manuel Tick/Tock zamanlayıcı (RAII dışı kullanım için).
class TickTockTimer {
 public:
  /// @brief Zamanlayıcıyı kurar (ölçümü henüz başlatmaz).
  /// @param scope_name Loglarda kullanılacak kapsam adı.
  /// @param warn_threshold_ms Uyarı eşiği; <= 0 ise global varsayılan kullanılır.
  explicit TickTockTimer(const char* scope_name,
                         std::chrono::milliseconds warn_threshold_ms = std::chrono::milliseconds::zero())
      : scope_name_(scope_name), warn_threshold_ms_(warn_threshold_ms), ticking_(false) {}

  /// @brief Ölçümü başlatır (etkinse başlangıç zamanını alır).
  void Tick() {
    const auto& settings = GetProfilerSettings();
    if (!settings.enabled) {
      ticking_ = false;
      return;
    }
    if (warn_threshold_ms_.count() <= 0) {
      warn_threshold_ms_ = settings.warn_threshold;
    }
    tick_tp_ = std::chrono::steady_clock::now();
    ticking_ = true;
  }

  /// @brief Ölçümü bitirir; eşik aşıldıysa hız sınırlı WARN logu yazar.
  void Tock() {
    if (!ticking_) {
      return;
    }
    ticking_ = false;
    const auto& settings = GetProfilerSettings();
    const auto elapsed = std::chrono::steady_clock::now() - tick_tp_;
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
    if (elapsed_ms <= warn_threshold_ms_) {
      return;
    }
    if (!ShouldLogEvery(scope_name_, settings.warn_log_interval)) {
      return;
    }
    Log(LogLevel::kWarn, settings.log_tag,
        std::string(scope_name_) + " süre eşiği aşıldı elapsed_ms=" +
            std::to_string(elapsed_ms.count()) + " threshold_ms=" +
            std::to_string(warn_threshold_ms_.count()));
  }

 private:
  const char* scope_name_;                          ///< Loglarda kullanılan kapsam adı.
  std::chrono::steady_clock::time_point tick_tp_{}; ///< Ölçümün başladığı an.
  std::chrono::milliseconds warn_threshold_ms_{};   ///< Etkin uyarı eşiği.
  bool ticking_;                                    ///< Ölçüm açık mı.
};

}  // namespace savasan::common

/// @brief İç token birleştirme yardımcısı (makro genişletmesinden sonra).
#define SAVASAN_CONCAT_INNER(a, b) a##b
/// @brief Token birleştirme makrosu (örn. satır numarasıyla benzersiz ad).
#define SAVASAN_CONCAT(a, b) SAVASAN_CONCAT_INNER(a, b)

/// @brief Kapsam çıkışında otomatik Tock + eşik kontrolü yapan RAII makrosu.
#define SAVASAN_PROFILE_SCOPE(scope_name) \
  ::savasan::common::ScopeProfiler SAVASAN_CONCAT(_savasan_prof_, __LINE__)(scope_name)

/// @brief Özel eşik (ms) ile kapsam profilleme makrosu.
#define SAVASAN_PROFILE_SCOPE_MS(scope_name, threshold_ms) \
  ::savasan::common::ScopeProfiler SAVASAN_CONCAT(_savasan_prof_, __LINE__)( \
      scope_name, std::chrono::milliseconds(threshold_ms))

#endif  // SAVASAN_COMMON_SCOPE_PROFILER_HPP_
