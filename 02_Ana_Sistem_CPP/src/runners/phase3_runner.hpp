/**
 * @file phase3_runner.hpp
 * @brief Faz-3 (tespit + takipçi + kilit seçimi) çalıştırıcı bildirimi.
 */
#ifndef SAVASAN_RUNNERS_PHASE3_RUNNER_HPP_
#define SAVASAN_RUNNERS_PHASE3_RUNNER_HPP_

#include <string>

#include "deepstream/sink_type.hpp"
#include "pipeline/ingest_config.hpp"
#include "tracking/lock_mode.hpp"

namespace savasan::runners {

struct Phase5Runtime;

/// @brief Faz-3 boru hattını (nvinfer + nvtracker + kilit seçim politikası) çalıştırır.
/// @param cfg Kamera/görüntü kaynağı yapılandırması.
/// @param pgi_config Birincil çıkarım (nvinfer) config yolu.
/// @param tracker_config nvtracker yapılandırma yolu.
/// @param ll_lib Düşük seviye takipçi kütüphanesi yolu.
/// @param sink Çıkış sink türü (ekran/dosya/fakesink).
/// @param seconds Çalışma süresi (sn); <=0 ise süresiz.
/// @param lock_mode Kilit seçim modu (varsayılan baseline).
/// @param phase5 Opsiyonel Faz-5 çalışma zamanı (paylaşımlı durum için).
/// @return Başarılıysa true.
bool RunPhase3(savasan::IngestConfig cfg, const std::string& pgi_config,
               const std::string& tracker_config, const std::string& ll_lib,
               savasan::deepstream::SinkType sink, int seconds,
               savasan::tracking::LockMode lock_mode = savasan::tracking::LockMode::kBaseline,
               Phase5Runtime* phase5 = nullptr);

}  // namespace savasan::runners

#endif  // SAVASAN_RUNNERS_PHASE3_RUNNER_HPP_
