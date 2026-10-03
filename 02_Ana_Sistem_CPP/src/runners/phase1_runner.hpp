/**
 * @file phase1_runner.hpp
 * @brief Faz-1 (yalnızca görüntü alma/önizleme) çalıştırıcı bildirimi.
 */
#ifndef SAVASAN_RUNNERS_PHASE1_RUNNER_HPP_
#define SAVASAN_RUNNERS_PHASE1_RUNNER_HPP_

#include "pipeline/ingest_config.hpp"

namespace savasan::runners {

/// @brief Faz-1 boru hattını (çıkarımsız ham görüntü) çalıştırır.
/// @param cfg Kamera/görüntü kaynağı yapılandırması.
/// @param seconds Çalışma süresi (sn); <=0 ise süresiz.
/// @return Başarılıysa true.
bool RunPhase1(savasan::IngestConfig cfg, int seconds);

}  // namespace savasan::runners

#endif  // SAVASAN_RUNNERS_PHASE1_RUNNER_HPP_
