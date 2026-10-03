/**
 * @file server_clock.cpp
 * @brief @ref server_clock.hpp uygulaması (env/dosya/HTTP offset birleştirme).
 */
#include "runners/server_clock.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

namespace savasan::runners {

namespace {

std::atomic<std::int64_t> g_http_sync_extra_ms{0};        ///< HTTP senkronundan gelen ek offset (ms).
std::atomic<bool> g_competition_http_time_synced{false};  ///< En az bir HTTP senkronu yapıldı mı.
std::atomic<std::int64_t> g_last_http_sync_steady_ns{0};  ///< Son HTTP senkronunun steady_clock anı (ns).

/// @brief Offset dosyası tanımlı ve içinde boş olmayan bir satır var mı.
bool FileOffsetLineNonEmpty() {
  const char* path = std::getenv("SAVASAN_SERVER_TIME_OFFSET_FILE");
  if (path == nullptr || path[0] == '\0') {
    return false;
  }
  std::ifstream f(path);
  std::string line;
  if (!f || !std::getline(f, line)) {
    return false;
  }
  while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
    line.pop_back();
  }
  return !line.empty();
}

/// @brief Bir dizeyi int64'e gevşek (hata durumunda 0) olarak çözer.
std::int64_t ParseInt64Loose(const std::string& s) {
  if (s.empty()) return 0;
  try {
    size_t idx = 0;
    const long long v = std::stoll(s, &idx, 10);
    if (idx == 0) return 0;
    return static_cast<std::int64_t>(v);
  } catch (...) {
    return 0;
  }
}

/// @brief SAVASAN_SERVER_TIME_OFFSET_MS ortam değişkenini okur (yoksa 0).
std::int64_t ReadEnvOffsetMs() {
  const char* raw = std::getenv("SAVASAN_SERVER_TIME_OFFSET_MS");
  if (raw == nullptr || raw[0] == '\0') return 0;
  return ParseInt64Loose(std::string(raw));
}

/// @brief Temel offset (env + dosya); HTTP yarışma senkronu hariç.
/// Env offset bir kez okunur; dosya offset'i SAVASAN_SERVER_TIME_OFFSET_POLL_MS
/// periyoduyla (varsayılan 1 sn) yeniden okunur.
std::int64_t GetBaseServerTimeOffsetMs() {
  static std::atomic<std::int64_t> g_env_offset{0};
  static std::atomic<bool> g_env_init{false};
  if (!g_env_init.exchange(true)) {
    g_env_offset.store(ReadEnvOffsetMs());
  }

  const char* path = std::getenv("SAVASAN_SERVER_TIME_OFFSET_FILE");
  if (path == nullptr || path[0] == '\0') {
    return g_env_offset.load();
  }

  static std::atomic<std::int64_t> g_file_offset{0};
  static std::chrono::steady_clock::time_point g_last_poll{};
  static std::atomic<bool> g_poll_inited{false};

  int poll_ms = 1000;
  const char* poll_raw = std::getenv("SAVASAN_SERVER_TIME_OFFSET_POLL_MS");
  if (poll_raw != nullptr && poll_raw[0] != '\0') {
    const int v = std::atoi(poll_raw);
    if (v >= 100 && v <= 60000) poll_ms = v;
  }

  const auto now = std::chrono::steady_clock::now();
  if (g_poll_inited.load()) {
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - g_last_poll).count();
    if (elapsed < poll_ms) {
      return g_env_offset.load() + g_file_offset.load();
    }
  } else {
    g_poll_inited.store(true);
  }
  g_last_poll = now;

  std::ifstream f(path);
  std::string line;
  if (!f || !std::getline(f, line)) {
    return g_env_offset.load() + g_file_offset.load();
  }
  while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
    line.pop_back();
  }
  const std::int64_t parsed = ParseInt64Loose(line);
  g_file_offset.store(parsed);
  return g_env_offset.load() + g_file_offset.load();
}

}  // namespace

std::int64_t GetServerTimeOffsetMs() {
  return GetBaseServerTimeOffsetMs() + g_http_sync_extra_ms.load();
}

// Sunucu epoch'undan, yerel saat ve temel offset çıkarılarak ek HTTP offset'i
// hesaplanır; böylece ServerTimeMsNow() sunucu zamanına yaklaşır.
void ApplyCompetitionHttpTimeSync(const std::int64_t server_epoch_ms_utc) {
  if (server_epoch_ms_utc <= 0) {
    return;
  }
  using namespace std::chrono;
  const auto ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
  const std::int64_t local = static_cast<std::int64_t>(ms);
  const std::int64_t base = GetBaseServerTimeOffsetMs();
  g_http_sync_extra_ms.store(server_epoch_ms_utc - local - base);
  g_competition_http_time_synced.store(true);
  const auto ns = duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
  g_last_http_sync_steady_ns.store(ns);
}

std::int64_t GetHttpClockSyncAgeMs() {
  const std::int64_t last = g_last_http_sync_steady_ns.load();
  if (last == 0) {
    return -1;
  }
  using namespace std::chrono;
  const auto now =
      duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
  return (now - last) / 1000000;
}

// Aktif senkron kaynağını öncelik sırasına göre belirler (HTTP > env+dosya > env > dosya > yok).
const char* GetClockSyncSourceCstr() {
  if (g_competition_http_time_synced.load()) {
    return "http";
  }
  const std::int64_t env_off = ReadEnvOffsetMs();
  const bool file_ok = FileOffsetLineNonEmpty();
  if (env_off != 0 && file_ok) {
    return "env_file";
  }
  if (env_off != 0) {
    return "env";
  }
  if (file_ok) {
    return "file";
  }
  return "none";
}

// Yerel sistem saatine toplam offset eklenerek sunucu zamanı üretilir.
std::int64_t ServerTimeMsNow() {
  using namespace std::chrono;
  const auto ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
  return static_cast<std::int64_t>(ms) + GetServerTimeOffsetMs();
}

}  // namespace savasan::runners
