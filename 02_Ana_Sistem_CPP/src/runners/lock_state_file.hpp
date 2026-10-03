/**
 * @file lock_state_file.hpp
 * @brief Görüntü kilidi durumunu AlpaguLink / RF köprüsü için dosyaya yayınlar.
 *
 * Varsayılan yol: /tmp/savasan_lock.state (SAVASAN_LOCK_STATE_FILE ile değişir).
 * İçerik: tek satır "0" veya "1". Publish yalnızca değer değişince yazar.
 */
#ifndef SAVASAN_RUNNERS_LOCK_STATE_FILE_HPP_
#define SAVASAN_RUNNERS_LOCK_STATE_FILE_HPP_

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace savasan::runners {

inline constexpr const char* kDefaultLockStateFilePath = "/tmp/savasan_lock.state";

/// @brief Kilit durumunu verilen yola atomik yazar (her çağrıda).
/// @return Yazma + rename başarılıysa true.
inline bool WriteLockStateFile(const bool lock_valid, const char* path) {
  if (path == nullptr || path[0] == '\0') {
    return false;
  }
  const int value = lock_valid ? 1 : 0;
  const std::string tmp = std::string(path) + ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc | std::ios::out);
    if (!out) {
      return false;
    }
    out << value << '\n';
    out.flush();
    if (!out) {
      return false;
    }
  }
  if (std::rename(tmp.c_str(), path) != 0) {
    std::remove(tmp.c_str());
    return false;
  }
  return true;
}

/// @brief Ortam yoluna kilit durumunu yazar; aynı değerde no-op.
/// @return Bu çağrıda dosya güncellendiyse true.
inline bool PublishLockStateToFile(const bool lock_valid) {
  static std::atomic<int> last_published{-1};
  const int value = lock_valid ? 1 : 0;
  if (last_published.exchange(value) == value) {
    return false;
  }

  const char* path = std::getenv("SAVASAN_LOCK_STATE_FILE");
  if (path == nullptr || path[0] == '\0') {
    path = kDefaultLockStateFilePath;
  }
  if (!WriteLockStateFile(lock_valid, path)) {
    last_published.store(-1);  // sonraki tick yeniden denesin
    return false;
  }
  return true;
}

}  // namespace savasan::runners

#endif  // SAVASAN_RUNNERS_LOCK_STATE_FILE_HPP_
