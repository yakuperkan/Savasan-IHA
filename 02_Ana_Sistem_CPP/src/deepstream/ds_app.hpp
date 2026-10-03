/**
 * @file ds_app.hpp
 * @brief DeepStream boru hattı string kuruculary, pad-probe ekleyicileri ve OSD
 *        çizim yardımcılarının bildirimleri.
 */
#ifndef SAVASAN_DEEPSTREAM_DS_APP_HPP_
#define SAVASAN_DEEPSTREAM_DS_APP_HPP_

#include <string>

#include <gst/gst.h>
#include "nvdsmeta.h"

#include "deepstream/sink_type.hpp"
#include "pipeline/ingest_config.hpp"

namespace savasan::deepstream {

/// Sessiz telemetri / healthcheck okuma yolu (~1 Hz güncellenir).
constexpr char kPipelineStatsEnvPath[] = "/tmp/savasan_pipeline_stats.env";

/// @brief Faz-2 boru hattı string'ini üretir.
///
/// Zincir: nvstreammux -> nvinfer -> nvvidconv -> nvdsosd -> [sink].
/// sink=kDisplay ise nv3dsink (ekranda bbox görseli).
std::string BuildPhase2PipelineString(const IngestConfig& ingest,
                                      const std::string& primary_config_abs,
                                      SinkType sink = SinkType::kFake);

/// @brief SAVASAN_TRACKER_DISABLE=1 ile nvtracker boru hattından çıkarıldı mı.
bool IsTrackerDisabledByEnv();

/// @brief Sorun giderme için tracker kapalı başlatma açıkça onaylandı mı
///        (SAVASAN_TRACKER_DISABLE_ACK=1).
bool IsTrackerDisableAcknowledged();

/// @brief Faz-3/5 kilit fazı için tracker zorunlu; onaysız disable -> false.
bool ValidateTrackerForLockPhase();

/// @brief Faz-3 boru hattı string'ini üretir.
///
/// Zincir: nvstreammux -> nvinfer -> nvtracker -> tee.
std::string BuildPhase3PipelineString(const IngestConfig& ingest,
                                      const std::string& primary_config_abs,
                                      const std::string& tracker_config_abs,
                                      const std::string& ll_lib_abs,
                                      SinkType sink = SinkType::kFake);

/// @brief Tüm queue elemanlarına düşük gecikme politikası uygular; nvstreammux
/// batch timeout'unu ayarlar.
void ApplyLowLatencyPipelineTuning(GstElement* pipeline);

/// @brief NVDS latency meta + nvvidconv pad-probe ile bileşen bazlı gecikme ölçer
/// (~1 Hz özet). SAVASAN_COMPONENT_LATENCY_PROFILER=0 ile kapatılır (varsayılan açık).
void AttachComponentLatencyProfiler(GstElement* pipeline);

/// @brief Boru hattı sonu telemetri pad-probe'u (FPS anlık/ortalama + DeepStream
/// gecikmesi). SAVASAN_TELEMETRY_CSV=/yol ile ~1 Hz CSV satırı yazılır.
void AttachEndTelemetryProbe(GstElement* pipeline);

/// @brief USB ingest PTS referansı (pre_mux_q / decoder); telemetri gecikme tahmini.
void AttachIngestLatencyReferenceProbe(GstElement* pipeline);

/// @brief Tek obj_meta üzerine standart kırmızı bbox + sarı etiket stili uygular.
void ApplyStandardObjStyle(NvDsObjectMeta* obj);

/// @brief Faz-2 (takipçisiz) için tüm nvinfer obj_meta'larına görünür çerçeve/etiket
/// stili uygular.
void ApplyVisibleBboxStyleToBatch(NvDsBatchMeta* batch_meta);

/// @brief Havuzdan alınan display_meta bazen eski içerikle döner; yeni çizimden önce
/// text/rect/line sayaçlarını sıfırlar.
void ResetDisplayMetaForAcquire(NvDsDisplayMeta* display_meta);

/// @brief Geriye uyumluluk: yalnızca metin slotlarını temizler.
void ClearDisplayMetaTextSlots(NvDsDisplayMeta* display_meta);

}  // namespace savasan::deepstream

#endif  // SAVASAN_DEEPSTREAM_DS_APP_HPP_
