/**
 * @file server_clock.hpp
 * @brief Yarışma sunucu saatiyle hizalanmış zaman kaynağı.
 *
 * Yerel sistem saatine; ortam değişkeni, dosya ve (öncelikli) HTTP senkron
 * düzeltmeleri uygulayarak sunucu epoch'una yakın bir zaman üretir.
 */
#ifndef SAVASAN_RUNNERS_SERVER_CLOCK_HPP_
#define SAVASAN_RUNNERS_SERVER_CLOCK_HPP_

#include <cstdint>

namespace savasan::runners {

/// @brief Düzeltme offset'i uygulanmış güncel sunucu zamanı (ms, epoch).
std::int64_t ServerTimeMsNow();

/// @brief Toplam uygulanan zaman offset'i (env + dosya + HTTP) (ms).
std::int64_t GetServerTimeOffsetMs();

/// @brief Yarışma `GET /api/sunucusaati` epoch'u (ms, UTC) ile HTTP düzeltmesi uygular.
/// Etki: ServerTimeMsNow() ≈ sunucu epoch'u (env + dosya offset'ine ek olarak).
void ApplyCompetitionHttpTimeSync(std::int64_t server_epoch_ms_utc);

/// @brief Son başarılı HTTP senkronundan bu yana geçen süre (ms); HTTP hiç yoksa -1.
std::int64_t GetHttpClockSyncAgeMs();

/// @brief Aktif senkron kaynağı: "http" | "env_file" | "env" | "file" | "none" (öncelik HTTP).
const char* GetClockSyncSourceCstr();

}  // namespace savasan::runners

#endif  // SAVASAN_RUNNERS_SERVER_CLOCK_HPP_
