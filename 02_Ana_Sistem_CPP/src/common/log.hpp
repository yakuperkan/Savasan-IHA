/**
 * @file log.hpp
 * @brief Tek başlık (header-only) logger. steady_clock tabanlı
 *        [saniye.ms.us] zaman damgalı, thread-safe satır çıktısı üreten ve
 *        anahtar bazlı hız sınırlama (rate limit) sunan hafif kayıt aracı.
 *
 * Çalışma zamanı seviye filtresi: SAVASAN_LOG_LEVEL (DEBUG|INFO|WARN|ERROR|CRITICAL|FATAL).
 * Sıcak yolda @ref SAVASAN_LOG_IF / @ref SAVASAN_LOG_F makrolarını kullanın; seviye
 * yetersizse argümanlar değerlendirilmez.
 */
#ifndef SAVASAN_COMMON_LOG_HPP_
#define SAVASAN_COMMON_LOG_HPP_

#include <array>
#include <chrono>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <string_view>

namespace savasan::common {

/// @brief Log önem seviyeleri (en kritikten en ayrıntılıya doğru).
enum class LogLevel : uint8_t {
  kFatal,     ///< Ölümcül hata; süreç sonlandırılmadan önceki son mesaj.
  kCritical,  ///< Kritik hata; sistem kararlılığını tehdit eder.
  kError,     ///< Hata; işlem başarısız oldu.
  kWarn,      ///< Uyarı; beklenmeyen ama kurtarılabilir durum.
  kInfo,      ///< Bilgi; normal akış mesajı.
  kDebug      ///< Hata ayıklama; ayrıntılı geliştirme bilgisi.
};

/// @brief LogLevel değerini sabit metin etiketine çevirir.
inline const char* ToString(const LogLevel level) {
  switch (level) {
    case LogLevel::kFatal:
      return "FATAL";
    case LogLevel::kCritical:
      return "CRITICAL";
    case LogLevel::kError:
      return "ERROR";
    case LogLevel::kWarn:
      return "WARN";
    case LogLevel::kInfo:
      return "INFO";
    case LogLevel::kDebug:
      return "DEBUG";
  }
  return "INFO";
}

namespace detail {

/// @brief Ortam değişkeni adını büyük harfe çevirip LogLevel döndürür; bilinmiyorsa kInfo.
inline LogLevel ParseLogLevelFromEnvValue(const char* raw) {
  if (raw == nullptr || raw[0] == '\0') {
    return LogLevel::kInfo;
  }
  char upper[16]{};
  for (size_t i = 0; i < sizeof(upper) - 1 && raw[i] != '\0'; ++i) {
    upper[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(raw[i])));
  }
  const std::string_view name(upper);
  if (name == "DEBUG") {
    return LogLevel::kDebug;
  }
  if (name == "INFO") {
    return LogLevel::kInfo;
  }
  if (name == "WARN" || name == "WARNING") {
    return LogLevel::kWarn;
  }
  if (name == "ERROR") {
    return LogLevel::kError;
  }
  if (name == "CRITICAL") {
    return LogLevel::kCritical;
  }
  if (name == "FATAL") {
    return LogLevel::kFatal;
  }
  return LogLevel::kInfo;
}

}  // namespace detail

/// @brief SAVASAN_LOG_LEVEL ortam değişkeninden minimum log seviyesini okur (varsayılan INFO).
inline LogLevel MinLogLevel() {
  static const LogLevel kMin =
      detail::ParseLogLevelFromEnvValue(std::getenv("SAVASAN_LOG_LEVEL"));
  return kMin;
}

/// @brief Verilen seviye, çalışma zamanı minimum seviyesine göre yazılmalı mı.
/// @details Düşük enum değeri = daha kritik; min INFO iken DEBUG filtrelenir.
inline bool IsLogEnabled(const LogLevel level) {
  return static_cast<uint8_t>(level) <= static_cast<uint8_t>(MinLogLevel());
}

/// @brief steady_clock saatinden [saniye.ms.us] biçiminde zaman damgası üretir.
inline void FormatTimestamp(char* buf, size_t buf_size) {
  const auto now = std::chrono::steady_clock::now();
  const auto since_epoch = now.time_since_epoch();
  const auto secs = std::chrono::duration_cast<std::chrono::seconds>(since_epoch).count();
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(since_epoch).count() % 1000;
  const auto us = std::chrono::duration_cast<std::chrono::microseconds>(since_epoch).count() % 1000;
  std::snprintf(buf, buf_size, "%lld.%03lld.%03lld",
                static_cast<long long>(secs),
                static_cast<long long>(ms),
                static_cast<long long>(us));
}

inline std::mutex& LogIoMutex() {
  static std::mutex mu;
  return mu;
}

namespace detail {

inline void LogWriteLine(const LogLevel level, std::string_view tag, std::string_view message) {
  char ts[32];
  FormatTimestamp(ts, sizeof(ts));

  std::string line;
  line.reserve(tag.size() + message.size() + 48);
  line.push_back('[');
  line.append(ts);
  line.append("][");
  line.append(ToString(level));
  line.append("][");
  line.append(tag);
  line.append("] ");
  line.append(message);
  line.push_back('\n');

  const bool to_stderr = (level == LogLevel::kFatal || level == LogLevel::kCritical ||
                          level == LogLevel::kError);
  FILE* const out = to_stderr ? stderr : stdout;

  std::lock_guard<std::mutex> lk(LogIoMutex());
  std::fwrite(line.data(), 1, line.size(), out);
}

}  // namespace detail

/// @brief string_view tabanlı log; seviye filtresi uygulanır.
inline void Log(const LogLevel level, std::string_view tag, std::string_view message) {
  if (!IsLogEnabled(level)) {
    return;
  }
  detail::LogWriteLine(level, tag, message);
}

/// @brief printf tarzı biçimlendirilmiş log (seviye kontrolü çağırandan önce yapılmalı).
inline void LogFormat(const LogLevel level, const char* tag, const char* fmt, ...) {
  if (!IsLogEnabled(level) || tag == nullptr || fmt == nullptr) {
    return;
  }
  char msg[512];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  msg[sizeof(msg) - 1] = '\0';
  detail::LogWriteLine(level, tag, msg);
}

namespace detail {

struct RateLimitSlot {
  std::string key;
  std::chrono::steady_clock::time_point last{};
};

inline constexpr size_t kRateLimitSlots = 128;

inline bool ShouldLogEveryImpl(std::string_view key,
                               const std::chrono::milliseconds interval) {
  static std::mutex mu;
  static std::array<RateLimitSlot, kRateLimitSlots> slots{};
  static size_t round_robin = 0;

  const auto now = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lk(mu);

  for (auto& slot : slots) {
    if (slot.key == key) {
      if ((now - slot.last) >= interval) {
        slot.last = now;
        return true;
      }
      return false;
    }
  }

  slots[round_robin] = RateLimitSlot{std::string(key), now};
  round_robin = (round_robin + 1) % kRateLimitSlots;
  return true;
}

}  // namespace detail

template <typename Rep, typename Period>
inline bool ShouldLogEvery(std::string_view key,
                           const std::chrono::duration<Rep, Period>& interval) {
  return detail::ShouldLogEveryImpl(
      key, std::chrono::duration_cast<std::chrono::milliseconds>(interval));
}

}  // namespace savasan::common

/// @brief Seviye yeterliyse sabit mesaj loglar (heap birleştirme yok).
#define SAVASAN_LOG_IF(level, tag, message)                         \
  do {                                                              \
    if (::savasan::common::IsLogEnabled((level))) {                 \
      ::savasan::common::Log((level), (tag), (message));            \
    }                                                               \
  } while (0)

/// @brief Seviye yeterliyse printf tarzı log (sıcak yol için önerilir).
#define SAVASAN_LOG_F(level, tag, fmt, ...)                         \
  do {                                                              \
    if (::savasan::common::IsLogEnabled((level))) {                 \
      ::savasan::common::LogFormat((level), (tag), (fmt),           \
                                   ##__VA_ARGS__);                  \
    }                                                               \
  } while (0)

#endif  // SAVASAN_COMMON_LOG_HPP_
