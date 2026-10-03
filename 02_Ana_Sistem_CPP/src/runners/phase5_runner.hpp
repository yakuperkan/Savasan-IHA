/**
 * @file phase5_runner.hpp
 * @brief Faz-5 (takip + güdüm + otopilot/ALC_LINK + yarışma) çalıştırıcı bildirimi.
 */
#ifndef SAVASAN_RUNNERS_PHASE5_RUNNER_HPP_
#define SAVASAN_RUNNERS_PHASE5_RUNNER_HPP_

#include <string>

#include "deepstream/sink_type.hpp"
#include "pipeline/ingest_config.hpp"
#include "tracking/lock_mode.hpp"

namespace savasan::runners {

/// @brief Faz-5 boru hattını ALC_LINK otopilot köprüsüyle çalıştırır.
///
/// Faz-3 görsel hattını kurar, üstüne kilit→seri gönderim, güdüm/PID, kaçış ve
/// yarışma HTTP entegrasyonunu ekler.
/// @param cfg Kamera/görüntü kaynağı yapılandırması.
/// @param pgi_config Birincil çıkarım (nvinfer) config yolu.
/// @param tracker_config nvtracker yapılandırma yolu.
/// @param ll_lib Düşük seviye takipçi kütüphanesi yolu.
/// @param sink Çıkış sink türü (ekran/dosya/fakesink).
/// @param run_seconds Çalışma süresi (sn); <=0 ise süresiz.
/// @param lock_mode Kilit seçim modu.
/// @return Başarılıysa true.
bool RunPhase5WithAlc(savasan::IngestConfig cfg, const std::string& pgi_config,
                      const std::string& tracker_config, const std::string& ll_lib,
                      savasan::deepstream::SinkType sink, int run_seconds,
                      savasan::tracking::LockMode lock_mode);

}  // namespace savasan::runners

#endif  // SAVASAN_RUNNERS_PHASE5_RUNNER_HPP_
