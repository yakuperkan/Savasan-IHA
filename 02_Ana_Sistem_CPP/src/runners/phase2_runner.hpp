/**
 * @file phase2_runner.hpp
 * @brief Faz-2 (tespit/çıkarım, takipçisiz) çalıştırıcı bildirimi.
 */
#ifndef SAVASAN_RUNNERS_PHASE2_RUNNER_HPP_
#define SAVASAN_RUNNERS_PHASE2_RUNNER_HPP_

#include <string>

#include "deepstream/sink_type.hpp"
#include "pipeline/ingest_config.hpp"

namespace savasan::runners {

/// @brief Faz-2 boru hattını (nvinfer tespit, takipçi yok) çalıştırır.
/// @param cfg Kamera/görüntü kaynağı yapılandırması.
/// @param pgi_config Birincil çıkarım (nvinfer) config yolu.
/// @param sink Çıkış sink türü (ekran/dosya/fakesink).
/// @param seconds Çalışma süresi (sn); <=0 ise süresiz.
/// @return Başarılıysa true.
bool RunPhase2(savasan::IngestConfig cfg, const std::string& pgi_config,
               savasan::deepstream::SinkType sink, int seconds);

}  // namespace savasan::runners

#endif  // SAVASAN_RUNNERS_PHASE2_RUNNER_HPP_
