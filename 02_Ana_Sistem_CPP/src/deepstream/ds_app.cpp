/**
 * @file ds_app.cpp
 * @brief DeepStream boru hattı string kurucularının, gecikme/telemetri
 *        pad-probe'larının ve OSD çizim yardımcılarının uygulaması.
 *
 * Boru hattı kuruluşu: kamera ingest -> savasan_pre_mux_q -> nvstreammux ->
 * nvinfer (-> nvtracker) -> tee (hızlı yol fakesink + akış yolu OSD/ekran/UDP/kayıt).
 * Probe'lar düşük gecikme için hot path'te hafif tutulur; ağır işler örnekleme
 * ile seyreltilir.
 */
#include "deepstream/ds_app.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <atomic>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

#include "camera/usb_source.hpp"
#include "common/log.hpp"
#include <gst/gst.h>
#include "gstnvdsmeta.h"
#include "nvds_latency_meta.h"
#include "nvdsmeta.h"

namespace savasan::deepstream {
namespace {
/// @brief Değeri 32'nin üst katına yuvarlar (tracker boyut hizalaması için).
int AlignUp32(int v) {
  return ((v + 31) / 32) * 32;
}

/// 60 FPS kare aralığı ~16667 us; canlı kaynakta batch-size=1 için tek kare bekleme süresi.
constexpr int kDefaultMuxPushTimeoutUsec = 15000;

/// Kuyruk birikmesini engelle: en fazla 1 buffer, downstream leaky.
constexpr const char* kLeakyQueueProps =
    "max-size-buffers=1 max-size-time=0 max-size-bytes=0 leaky=2";

/// Mux öncesi: tracker odaklı profilde kısa birikim toleransı (gecikme sıçramasını önler).
constexpr const char* kPreMuxQueueProps =
    "max-size-buffers=2 max-size-time=0 max-size-bytes=0 leaky=2";

/// Jetson: SURFACE_ARRAY (4) NVMM zero-copy; override: SAVASAN_NVBUF_MEM_TYPE=0..4.
constexpr int kDefaultNvbufMemoryType = 4;

/// UDP yayın kuyruğu: ağ tıkanıklığında eski kareleri at (uçuş yolunu etkilemez).
constexpr const char* kUdpLeakyQueueProps =
    "max-size-buffers=2 max-size-time=0 max-size-bytes=0 leaky=2";

/// OSD/encode dalı: ekran yolunda tek buffer tut (iz/ghosting önleme).
constexpr const char* kStreamBranchQueueProps =
    "max-size-buffers=1 max-size-time=0 max-size-bytes=0 leaky=2";

/// @brief Donanım hızlandırmalı nvvidconv parçasını adlandırarak üretir.
std::string NvVidconvFragment(const char* element_name) {
  std::ostringstream o;
  o << "nvvidconv compute-hw=1 name=" << element_name;
  return o.str();
}

/// @brief nvstreammux batched-push-timeout değerini env'den okur (us).
int MuxPushTimeoutUsecFromEnv() {
  const char* s = std::getenv("SAVASAN_MUX_BATCH_TIMEOUT_USEC");
  if (s == nullptr || s[0] == '\0') {
    return kDefaultMuxPushTimeoutUsec;
  }
  const int v = std::atoi(s);
  if (v < 0 || v > 100000) {
    return kDefaultMuxPushTimeoutUsec;
  }
  return v;
}

/// @brief Bileşen gecikme profilcisi etkin mi (varsayılan açık; "0"/"false" kapatır).
bool ComponentLatencyProfilerEnabled() {
  const char* e = std::getenv("SAVASAN_COMPONENT_LATENCY_PROFILER");
  if (e == nullptr || e[0] == '\0') {
    return true;
  }
  return std::strcmp(e, "0") != 0 && std::strcmp(e, "false") != 0;
}

/// @brief NVDS gecikme ölçümü env ile açık mı (libnvds_meta setenv'den önce
/// yüklenebildiğinden API yerine env'e bakılır).
bool LatencyMeasurementEnabledByEnv() {
  const char* e = std::getenv("NVDS_ENABLE_LATENCY_MEASUREMENT");
  return e != nullptr && e[0] != '\0' && std::strcmp(e, "0") != 0;
}

/// Sessiz telemetri dosyası yolu ile aynı (healthcheck okur).
constexpr const char* kSilentTelemetryStatsPath = "/tmp/savasan_pipeline_stats.env";

/// @brief SAVASAN_TRACKER_DISABLE=1 kontrolü.
bool TrackerDisabledByEnv() {
  const char* e = std::getenv("SAVASAN_TRACKER_DISABLE");
  return e != nullptr && e[0] == '1';
}

/// @brief SAVASAN_TRACKER_DISABLE_ACK=1 — laboratuvar/sorun giderme onayı.
bool TrackerDisableAcknowledged() {
  const char* e = std::getenv("SAVASAN_TRACKER_DISABLE_ACK");
  return e != nullptr && e[0] == '1';
}

/// @brief Sessiz modda FPS/GPU sayaçlarını dosyaya yazar (konsol I/O yok).
void WriteSilentTelemetryStats(gint64 t_us, double inst_fps, double avg_fps, double gpu_pct) {
  FILE* fp = std::fopen(kSilentTelemetryStatsPath, "w");
  if (fp == nullptr) {
    return;
  }
  std::fprintf(fp, "t_us=%lld\ninst_fps=%.4f\navg_fps=%.4f\ngpu_pct=%.4f\n",
               static_cast<long long>(t_us), inst_fps, avg_fps, gpu_pct);
  std::fclose(fp);
}

/// @brief Telemetri pad-probe çıktısı etkin mi (varsayılan açık; "0"/"false" kapatır).
bool TelemetryProbeEnabled() {
  static const bool kEnabled = []() {
    const char* e = std::getenv("SAVASAN_TELEMETRY_PROBE");
    if (e == nullptr || e[0] == '\0') {
      return true;
    }
    return std::strcmp(e, "0") != 0 && std::strcmp(e, "false") != 0;
  }();
  return kEnabled;
}

/// @brief NVENC "Encode Latency" ölçümü etkin mi (varsayılan kapalı).
/// "1"/"true" -> aç, aksi -> kapalı.
bool EncoderLatencyMeasurementEnabled() {
  static const bool kEnabled = []() {
    const char* e = std::getenv("SAVASAN_ENCODER_LATENCY_MEASURE");
    if (e == nullptr || e[0] == '\0') {
      return false;
    }
    return std::strcmp(e, "1") == 0 || std::strcmp(e, "true") == 0;
  }();
  return kEnabled;
}

/// @brief NVMM bellek türünü env'den okur (0..4; varsayılan SURFACE_ARRAY=4).
int NvbufMemoryTypeFromEnv() {
  const char* s = std::getenv("SAVASAN_NVBUF_MEM_TYPE");
  if (s == nullptr || s[0] == '\0') {
    return kDefaultNvbufMemoryType;
  }
  const int v = std::atoi(s);
  if (v < 0 || v > 4) {
    return kDefaultNvbufMemoryType;
  }
  return v;
}

/// Jetson GPU yük yüzdesi sysfs yolu (binde-bir ölçek).
constexpr char kGpuLoadSysfs[] =
    "/sys/devices/platform/bus@0/17000000.gpu/load";

/// @brief Boru hattı sonu telemetri probe'unun durumu (FPS, gecikme, GPU, CSV).
struct TelemetryProbeState {
  guint64 total_frames = 0;     ///< Toplam işlenen kare.
  guint64 window_frames = 0;    ///< Geçerli 1 sn penceresindeki kare.
  gint64 start_us = 0;          ///< İlk kare zamanı (us).
  gint64 window_start_us = 0;   ///< Pencere başlangıcı (us).
  double avg_fps = 0.0;         ///< Toplam ortalama FPS.
  bool gpu_warned = false;      ///< GPU yük uyarısı verildi mi (histerezis).
  guint gpu_sysfs_sample_seq = 0; ///< GPU sysfs örnekleme sayacı.
  int last_gpu_permille = -1;   ///< Son okunan GPU yükü (binde-bir).
  int gpu_fd = -1;              ///< sysfs fd; açık tutulur (seek+read ile tekrar okunur).

  // SAVASAN_TELEMETRY_CSV=/path.csv — her ~1 Hz telemetri satırı eklenir (doğrulama/karşılaştırma).
  std::string csv_path;         ///< CSV çıktı yolu (boşsa kapalı).
  FILE* csv_fp = nullptr;       ///< CSV dosya tutamacı.
  bool csv_header_written = false; ///< CSV başlığı yazıldı mı.
  int csv_buffered_lines = 0;   ///< Flush'tan beri yazılan satır sayısı.

  /// @brief Jetson GPU yükünü okur (0-1000 ölçeği; -1 = okunamadı).
  int ReadGpuLoadPermille() {
    if (gpu_fd < 0) {
      gpu_fd = ::open(kGpuLoadSysfs, O_RDONLY);
      if (gpu_fd < 0) return -1;
    }
    char buf[16];
    ::lseek(gpu_fd, 0, SEEK_SET);
    const ssize_t n = ::read(gpu_fd, buf, sizeof(buf) - 1);
    if (n <= 0) return -1;
    buf[n] = '\0';
    return std::atoi(buf);
  }

  ~TelemetryProbeState() {
    if (gpu_fd >= 0) ::close(gpu_fd);
    if (csv_fp != nullptr) {
      std::fflush(csv_fp);
      std::fclose(csv_fp);
      csv_fp = nullptr;
    }
  }

  /// @brief Bir telemetri satırını CSV'ye yazar (60 satırda bir flush).
  void AppendTelemetryCsv(gint64 t_us, double inst_fps, bool nvds_ok, double nvds_lat_ms,
                          bool ingest_fb, double display_lat_ms, double gpu_pct) {
    if (csv_path.empty()) return;
    if (csv_fp == nullptr) {
      csv_fp = std::fopen(csv_path.c_str(), "a");
      if (csv_fp == nullptr) return;
    }
    if (!csv_header_written) {
      std::fprintf(csv_fp,
                   "t_us,inst_fps,avg_fps,nvds_ok,latency_ms,ingest_pts_fallback,gpu_pct\n");
      csv_header_written = true;
    }
    const double lat_out =
        nvds_ok ? nvds_lat_ms
                : ((ingest_fb && display_lat_ms >= 0.0 && display_lat_ms < 800.0) ? display_lat_ms
                                                                                    : -1.0);
    std::fprintf(csv_fp, "%lld,%.4f,%.4f,%d,%.4f,%d,%.4f\n", static_cast<long long>(t_us),
                 inst_fps, avg_fps, nvds_ok ? 1 : 0, lat_out, ingest_fb ? 1 : 0, gpu_pct);
    ++csv_buffered_lines;
    if (csv_buffered_lines >= 60) {
      std::fflush(csv_fp);
      csv_buffered_lines = 0;
    }
  }
};

// USB ingest: decoder/mux öncesi PTS -> monotonic kaydı; telemetri probe'da buf_pts ile eşlenir.
std::mutex g_ingest_pts_mu;
struct IngestPtsEntry {
  guint64 pts = GST_CLOCK_TIME_NONE;
  gint64 monotonic_us = 0;
};
std::array<IngestPtsEntry, 512> g_ingest_pts_ring{};
size_t g_ingest_pts_write_idx = 0;

void InsertIngestPts(const guint64 pts, const gint64 monotonic_us) {
  g_ingest_pts_ring[g_ingest_pts_write_idx] = IngestPtsEntry{pts, monotonic_us};
  g_ingest_pts_write_idx = (g_ingest_pts_write_idx + 1) % g_ingest_pts_ring.size();
}

bool TakeIngestPts(const guint64 pts, gint64* out_monotonic_us) {
  if (pts == GST_CLOCK_TIME_NONE) {
    return false;
  }
  for (size_t i = 0; i < g_ingest_pts_ring.size(); ++i) {
    const size_t idx =
        (g_ingest_pts_write_idx + g_ingest_pts_ring.size() - 1 - i) % g_ingest_pts_ring.size();
    if (g_ingest_pts_ring[idx].pts == pts) {
      if (out_monotonic_us != nullptr) {
        *out_monotonic_us = g_ingest_pts_ring[idx].monotonic_us;
      }
      return true;
    }
  }
  return false;
}

/// @brief Frame meta/buffer PTS/DTS anahtarlarını sırayla deneyerek ingest
/// referans zamanını (monotonic us) bulur.
bool ResolveIngestRefMonotonicUs(GstBuffer* buf, const NvDsFrameMeta* fm, gint64* out_us) {
  if (out_us == nullptr || buf == nullptr) {
    return false;
  }
  const guint64 keys[] = {
      fm != nullptr ? static_cast<guint64>(fm->buf_pts) : GST_CLOCK_TIME_NONE,
      static_cast<guint64>(GST_BUFFER_PTS(buf)),
      static_cast<guint64>(GST_BUFFER_DTS(buf)),
  };
  std::lock_guard<std::mutex> lock(g_ingest_pts_mu);
  for (guint64 key : keys) {
    if (TakeIngestPts(key, out_us)) {
      return true;
    }
  }
  return false;
}

/// @brief Ingest noktasında buffer PTS'ini yakalama anıyla halka tampona kaydeder.
GstPadProbeReturn IngestPtsProbe(GstPad* /*pad*/, GstPadProbeInfo* info, gpointer /*user_data*/) {
  if ((info->type & GST_PAD_PROBE_TYPE_BUFFER) == 0) {
    return GST_PAD_PROBE_OK;
  }
  GstBuffer* buf = GST_PAD_PROBE_INFO_BUFFER(info);
  if (!buf) {
    return GST_PAD_PROBE_OK;
  }
  const GstClockTime pts = GST_BUFFER_PTS(buf);
  if (pts == GST_CLOCK_TIME_NONE) {
    return GST_PAD_PROBE_OK;
  }
  const gint64 t_us = g_get_monotonic_time();
  std::lock_guard<std::mutex> lock(g_ingest_pts_mu);
  InsertIngestPts(static_cast<guint64>(pts), t_us);
  return GST_PAD_PROBE_OK;
}

/// @brief Bileşen bazlı gecikme ölçüm yuvaları.
enum class LatencySlot : int {
  kDecoder = 0, ///< Çözücü.
  kMuxer,       ///< nvstreammux.
  kInfer,       ///< nvinfer.
  kTracker,     ///< nvtracker.
  kVidConv,     ///< nvvidconv.
  kSlotCount    ///< Yuva sayısı.
};

/// Yuva etiketleri (rapor çıktısı için).
constexpr const char* kLatencySlotLabels[] = {
    "Decoder", "Muxer", "Infer", "Tracker", "VidConv"};

/// @brief Bileşen gecikme profilcisinin biriktirme durumu (~1 Hz raporlar).
struct ComponentLatencyProfilerState {
  std::mutex mu;  ///< Toplam/sayaç koruması.
  std::array<double, static_cast<int>(LatencySlot::kSlotCount)> sum_ms{};      ///< Yuva başına toplam ms.
  std::array<guint64, static_cast<int>(LatencySlot::kSlotCount)> sample_count{};///< Yuva başına örnek sayısı.
  gint64 window_start_us = 0;          ///< Rapor penceresi başlangıcı (us).
  std::atomic<gint64> vidconv_in_us{0};///< nvvidconv sink giriş zamanı (us).

  /// @brief Geçerli (0<ms<=800) bir ölçümü ilgili yuvaya ekler.
  void AccumulateSample(LatencySlot slot, double ms) {
    if (ms <= 0.0 || ms > 800.0) {
      return;
    }
    const int idx = static_cast<int>(slot);
    sum_ms[idx] += ms;
    ++sample_count[idx];
  }

  /// @brief NVDS latency meta'larından bileşen adına göre ölçüm toplar.
  void AccumulateFromBatchMeta(NvDsBatchMeta* batch_meta) {
    if (batch_meta == nullptr || !LatencyMeasurementEnabledByEnv()) {
      return;
    }
    for (NvDsMetaList* l = batch_meta->batch_user_meta_list; l != nullptr; l = l->next) {
      auto* user_meta = static_cast<NvDsUserMeta*>(l->data);
      if (user_meta == nullptr ||
          user_meta->base_meta.meta_type != NVDS_LATENCY_MEASUREMENT_META) {
        continue;
      }
      auto* comp = static_cast<NvDsMetaCompLatency*>(user_meta->user_meta_data);
      if (comp == nullptr) {
        continue;
      }
      const double ms = comp->out_system_timestamp - comp->in_system_timestamp;
      const char* name = comp->component_name;
      if (name == nullptr) {
        continue;
      }
      if (std::strstr(name, "nvv4l2decoder") != nullptr ||
          std::strstr(name, "savasan_decoder") != nullptr ||
          std::strstr(name, "nvdecoder") != nullptr ||
          std::strstr(name, "nvargus") != nullptr) {
        AccumulateSample(LatencySlot::kDecoder, ms);
      } else if (std::strstr(name, "nvstreammux") != nullptr) {
        AccumulateSample(LatencySlot::kMuxer, ms);
      } else if (std::strstr(name, "nvinfer") != nullptr) {
        AccumulateSample(LatencySlot::kInfer, ms);
      } else if (std::strstr(name, "nvtracker") != nullptr) {
        AccumulateSample(LatencySlot::kTracker, ms);
      }
    }
  }
};

/// @brief nvvidconv sink: giriş anını kaydeder (vidconv gecikmesi ölçümü için).
GstPadProbeReturn ComponentLatencyVidconvSinkProbe(GstPad* /*pad*/, GstPadProbeInfo* info,
                                                   gpointer user_data) {
  if ((info->type & GST_PAD_PROBE_TYPE_BUFFER) == 0) {
    return GST_PAD_PROBE_OK;
  }
  auto* st = static_cast<ComponentLatencyProfilerState*>(user_data);
  if (st == nullptr) {
    return GST_PAD_PROBE_OK;
  }
  st->vidconv_in_us.store(g_get_monotonic_time(), std::memory_order_relaxed);
  return GST_PAD_PROBE_OK;
}

/// @brief nvvidconv src: çıkış anı - giriş anı farkını VidConv yuvasına ekler.
GstPadProbeReturn ComponentLatencyVidconvSrcProbe(GstPad* /*pad*/, GstPadProbeInfo* info,
                                                  gpointer user_data) {
  if ((info->type & GST_PAD_PROBE_TYPE_BUFFER) == 0) {
    return GST_PAD_PROBE_OK;
  }
  auto* st = static_cast<ComponentLatencyProfilerState*>(user_data);
  if (st == nullptr) {
    return GST_PAD_PROBE_OK;
  }
  const gint64 in_us = st->vidconv_in_us.load(std::memory_order_relaxed);
  if (in_us <= 0) {
    return GST_PAD_PROBE_OK;
  }
  const double ms = static_cast<double>(g_get_monotonic_time() - in_us) / 1000.0;
  std::lock_guard<std::mutex> lock(st->mu);
  st->AccumulateSample(LatencySlot::kVidConv, ms);
  return GST_PAD_PROBE_OK;
}

/// @brief Pencere (~1 sn) dolunca yuva ortalamalarını tek satır olarak raporlar
/// ve sayaçları sıfırlar.
GstPadProbeReturn ComponentLatencyReportProbe(GstPad* /*pad*/, GstPadProbeInfo* info,
                                              gpointer user_data) {
  if ((info->type & GST_PAD_PROBE_TYPE_BUFFER) == 0) {
    return GST_PAD_PROBE_OK;
  }
  auto* st = static_cast<ComponentLatencyProfilerState*>(user_data);
  GstBuffer* buf = GST_PAD_PROBE_INFO_BUFFER(info);
  if (st == nullptr || buf == nullptr) {
    return GST_PAD_PROBE_OK;
  }

  const gint64 now_us = g_get_monotonic_time();
  {
    std::lock_guard<std::mutex> lock(st->mu);
    NvDsBatchMeta* batch_meta = gst_buffer_get_nvds_batch_meta(buf);
    st->AccumulateFromBatchMeta(batch_meta);

    if (st->window_start_us == 0) {
      st->window_start_us = now_us;
      return GST_PAD_PROBE_OK;
    }
    const double window_elapsed_s = static_cast<double>(now_us - st->window_start_us) / 1000000.0;
    if (window_elapsed_s < 1.0) {
      return GST_PAD_PROBE_OK;
    }

    std::cout << "[LATENCY_PROFILER] ";
    for (int i = 0; i < static_cast<int>(LatencySlot::kSlotCount); ++i) {
      if (i > 0) {
        std::cout << " | ";
      }
      std::cout << kLatencySlotLabels[i] << ": ";
      if (st->sample_count[i] > 0) {
        const double avg = st->sum_ms[i] / static_cast<double>(st->sample_count[i]);
        std::cout << std::fixed << std::setprecision(2) << avg << " ms";
      } else {
        std::cout << "--";
      }
    }
    std::cout << std::defaultfloat << '\n' << std::flush;

    st->sum_ms.fill(0.0);
    st->sample_count.fill(0);
    st->window_start_us = now_us;
  }
  return GST_PAD_PROBE_OK;
}

/// @brief Bir queue elemanına düşük gecikme (tek buffer, leaky) ayarlarını uygular.
void ApplyLowLatencyQueueProps(GstElement* queue_el) {
  if (queue_el == nullptr) {
    return;
  }
  g_object_set(G_OBJECT(queue_el), "max-size-buffers", 1, "max-size-time", G_GUINT64_CONSTANT(0),
               "max-size-bytes", 0, "leaky", 2, nullptr);
}

/// @brief Telemetri probe: kare sayar, ~1 Hz'de FPS/gecikme/GPU hesaplar, konsola
/// yazar ve (varsa) CSV'ye ekler. NVDS gecikmesi yoksa ingest PTS tahminine düşer.
GstPadProbeReturn EndTelemetryProbeCallback(GstPad* /*pad*/, GstPadProbeInfo* info,
                                            gpointer user_data) {
  if ((info->type & GST_PAD_PROBE_TYPE_BUFFER) == 0) {
    return GST_PAD_PROBE_OK;
  }

  auto* st = static_cast<TelemetryProbeState*>(user_data);
  GstBuffer* buf = GST_PAD_PROBE_INFO_BUFFER(info);
  if (!st || !buf) {
    return GST_PAD_PROBE_OK;
  }

  const gint64 now_us = g_get_monotonic_time();
  if (st->start_us == 0) {
    st->start_us = now_us;
    st->window_start_us = now_us;
  }

  st->total_frames++;
  st->window_frames++;

  const double total_elapsed_s =
      static_cast<double>(now_us - st->start_us) / 1000000.0;
  if (total_elapsed_s > 0.0) {
    st->avg_fps = static_cast<double>(st->total_frames) / total_elapsed_s;
  }

  const double window_elapsed_s =
      static_cast<double>(now_us - st->window_start_us) / 1000000.0;
  if (window_elapsed_s < 1.0) {
    return GST_PAD_PROBE_OK;
  }

  const double inst_fps = static_cast<double>(st->window_frames) / window_elapsed_s;

  if ((st->gpu_sysfs_sample_seq++ % 5u) == 0u) {
    st->last_gpu_permille = st->ReadGpuLoadPermille();
  }
  const int gpu_load = st->last_gpu_permille;
  const double gpu_pct = (gpu_load >= 0) ? (gpu_load / 10.0) : -1.0;

  // Yarış/uçuş modu: konsol/CSV I/O kapalı; sayaçlar dosyaya yazılır.
  if (!TelemetryProbeEnabled()) {
    WriteSilentTelemetryStats(now_us, inst_fps, st->avg_fps, gpu_pct);
    st->window_frames = 0;
    st->window_start_us = now_us;
    return GST_PAD_PROBE_OK;
  }

  std::array<NvDsFrameLatencyInfo, 16> latency_info{};
  const guint num_sources = nvds_measure_buffer_latency(buf, latency_info.data());
  double nvds_latency_ms = 0.0;
  bool nvds_ok = false;
  if (num_sources > 0) {
    double sum_ms = 0.0;
    const guint capped = std::min(num_sources, static_cast<guint>(latency_info.size()));
    for (guint i = 0; i < capped; ++i) {
      sum_ms += latency_info[i].latency;
    }
    nvds_latency_ms = sum_ms / static_cast<double>(capped);
    const double raw0 = latency_info[0].latency;
    nvds_ok = (raw0 > 0.0 && raw0 <= 800.0);
  }

  double display_latency_ms = nvds_latency_ms;
  bool ingest_pts_fallback = false;
  if (!nvds_ok && LatencyMeasurementEnabledByEnv()) {
    NvDsBatchMeta* batch = gst_buffer_get_nvds_batch_meta(buf);
    NvDsFrameMeta* fm = nullptr;
    if (batch != nullptr && batch->frame_meta_list != nullptr) {
      fm = static_cast<NvDsFrameMeta*>(batch->frame_meta_list->data);
    }
    gint64 ref_us = 0;
    if (ResolveIngestRefMonotonicUs(buf, fm, &ref_us)) {
      display_latency_ms = static_cast<double>(now_us - ref_us) / 1000.0;
      ingest_pts_fallback = (display_latency_ms > 0.0 && display_latency_ms < 800.0);
      if (ingest_pts_fallback) {
        nvds_latency_ms = display_latency_ms;
      }
    }
  }

  std::cout << std::fixed << std::setprecision(1)
            << "[TELEMETRİ] FPS: " << inst_fps;
  if (nvds_ok) {
    std::cout << " | Latency: " << nvds_latency_ms << " ms";
  } else if (ingest_pts_fallback) {
    std::cout << " | Latency: " << display_latency_ms << " ms (ingest PTS tahmini)";
  } else if (!LatencyMeasurementEnabledByEnv()) {
    std::cout << " | Latency: -- (NVDS_ENABLE_LATENCY_MEASUREMENT=1 export edin)";
  } else {
    std::cout << " | Latency: -- ms (PTS eslesmedi; pre_mux_q probe kontrol)";
  }
  if (gpu_pct >= 0.0) {
    std::cout << " | GPU: " << gpu_pct << "%";
  }
  std::cout << std::defaultfloat << '\n' << std::flush;

  st->AppendTelemetryCsv(now_us, inst_fps, nvds_ok || ingest_pts_fallback, nvds_latency_ms,
                         ingest_pts_fallback, display_latency_ms, gpu_pct);

  if (gpu_pct >= 90.0 && !st->gpu_warned) {
    std::cerr << "[UYARI] GPU yuku >=%90 (" << gpu_pct
              << "%); FPS dususu beklenir.\n";
    st->gpu_warned = true;
  } else if (gpu_pct < 85.0) {
    st->gpu_warned = false;
  }

  st->window_frames = 0;
  st->window_start_us = now_us;
  return GST_PAD_PROBE_OK;
}

/// @brief Ekran çıkışı için nv3dsink parçasını üretir (zero-copy).
/// Varsayılan sync=true kare birikimini/OSD izini önler; SAVASAN_DISPLAY_SYNC=0 -> sync=false.
std::string DisplaySinkGstreamerFragment() {
  const char* sync_env = std::getenv("SAVASAN_DISPLAY_SYNC");
  const bool sync_on =
      (sync_env == nullptr || sync_env[0] == '\0' || std::strcmp(sync_env, "0") != 0);
  return std::string("nv3dsink sync=") + (sync_on ? "true" : "false") + " qos=false";
}

/// @brief UDP RTP bitrate'ini env'den okur (bps); makul aralık dışında varsayılan.
int UdpBitrateFromEnv(int default_bps) {
  const char* s = std::getenv("SAVASAN_UDP_BITRATE");
  if (s == nullptr || s[0] == '\0') return default_bps;
  char* end = nullptr;
  const long v = std::strtol(s, &end, 10);
  if (end == s || v < 100000L || v > 800000000L) return default_bps;
  return static_cast<int>(v);
}

/// @brief UDP yayını env ile açıkça etkin mi (SAVASAN_UDP_ENABLE=1).
static bool UdpExplicitlyEnabledByEnv() {
  const char* e = std::getenv("SAVASAN_UDP_ENABLE");
  return (e != nullptr && e[0] != '\0' && std::strcmp(e, "1") == 0);
}

/// @brief Hangi çıkış dallarının (ekran/UDP/kayıt) istendiğini taşıyan bayraklar.
struct StreamOutputFlags {
  bool want_display = false;       ///< Ekran sink'i istendi mi.
  bool want_udp = false;           ///< UDP yayını istendi mi.
  bool want_record = false;        ///< Dosyaya kayıt istendi mi.
  bool record_ok = false;          ///< Kayıt dizini yazılabilir mi.
  const char* record_file = nullptr;///< Kayıt dosyası yolu.
  const char* udp_host = nullptr;  ///< UDP hedef host(lar)ı.
  const char* udp_port = nullptr;  ///< UDP hedef portu.
};

/// @brief Çıkış bayraklarını env ve sink türünden çözer; kayıt dizini yazılamazsa
/// kaydı devre dışı bırakır.
StreamOutputFlags ResolveStreamOutputFlags(SinkType sink) {
  StreamOutputFlags flags{};
  const char* record_file = std::getenv("SAVASAN_RECORD_FILE");
  const char* udp_host = std::getenv("SAVASAN_UDP_HOST");
  const char* udp_port = std::getenv("SAVASAN_UDP_PORT");
  flags.want_record = (record_file != nullptr && record_file[0] != '\0');
  flags.want_udp = UdpExplicitlyEnabledByEnv() && udp_host != nullptr && udp_host[0] != '\0' &&
                   udp_port != nullptr && udp_port[0] != '\0';
  flags.want_display = (sink == SinkType::kDisplay);
  flags.record_file = record_file;
  flags.udp_host = udp_host;
  flags.udp_port = udp_port;
  if (flags.want_record && record_file != nullptr) {
    std::string dir(record_file);
    const auto slash = dir.rfind('/');
    dir = (slash != std::string::npos) ? dir.substr(0, slash) : ".";
    if (::access(dir.c_str(), W_OK) == 0) {
      flags.record_ok = true;
    } else {
      std::cerr << "UYARI: Kayit dizini yazilamiyor, kayit devre disi: " << dir << '\n';
      flags.want_record = false;
    }
  }
  return flags;
}

/// @brief UDP sink elemanını üretir.
///
/// SAVASAN_UDP_HOST tek IP ("a.b.c.d") veya virgülle ayrılmış liste olabilir
/// ("ip1,ip2" ya da "ip1:port,ip2:port"). Tek hedef -> udpsink; çoklu hedef ->
/// multiudpsink (encode bir kez yapılır, paketler tüm istemcilere çoğaltılır).
/// Liste girişinde port verilmezse ortak port kullanılır.
std::string BuildUdpSinkElement(const char* host_csv, const char* default_port) {
  const std::string port = (default_port != nullptr) ? default_port : "";
  const std::string sink_props = " sync=false async=true buffer-size=1048576";

  std::vector<std::string> hosts;
  std::stringstream ss(host_csv != nullptr ? host_csv : "");
  std::string item;
  while (std::getline(ss, item, ',')) {
    const auto b = item.find_first_not_of(" \t");
    const auto e = item.find_last_not_of(" \t");
    if (b == std::string::npos) continue;
    hosts.push_back(item.substr(b, e - b + 1));
  }

  if (hosts.size() <= 1) {
    const std::string h = hosts.empty() ? std::string{} : hosts.front();
    const auto colon = h.find(':');
    const std::string ip = (colon == std::string::npos) ? h : h.substr(0, colon);
    const std::string p = (colon == std::string::npos) ? port : h.substr(colon + 1);
    return "udpsink host=" + ip + " port=" + p + sink_props;
  }

  std::string clients;
  for (size_t i = 0; i < hosts.size(); ++i) {
    if (i != 0) clients += ",";
    clients += (hosts[i].find(':') != std::string::npos) ? hosts[i] : (hosts[i] + ":" + port);
  }
  return "multiudpsink clients=" + clients + sink_props;
}

/// @brief UDP encode+yayın dalını üretir (H264/H265 env ile seçilir).
std::string BuildUdpEncodeBranch(const StreamOutputFlags& flags) {
  if (!flags.want_udp || flags.udp_host == nullptr || flags.udp_port == nullptr) {
    return "";
  }
  const int udp_bitrate = UdpBitrateFromEnv(6000000);
  const char* codec = std::getenv("SAVASAN_UDP_CODEC");
  const bool use_h265 = (codec != nullptr && std::strcmp(codec, "h265") == 0);
  const int enc_latency = EncoderLatencyMeasurementEnabled() ? 1 : 0;

  std::ostringstream o;
  o << "queue name=savasan_udp_q " << kUdpLeakyQueueProps << " ! "
    << NvVidconvFragment("savasan_udp_vidconv") << " ! "
    << "capsfilter caps=\"video/x-raw(memory:NVMM),format=NV12\" ! ";
  if (use_h265) {
    o << "nvv4l2h265enc name=savasan_udp_enc MeasureEncoderLatency=" << enc_latency
      << " insert-sps-pps=true bitrate="
      << udp_bitrate
      << " iframeinterval=15 preset-level=1 ! "
      << "h265parse ! rtph265pay config-interval=1 pt=96 mtu=1400 ! ";
  } else {
    o << "nvv4l2h264enc name=savasan_udp_enc MeasureEncoderLatency=" << enc_latency
      << " insert-sps-pps=true bitrate="
      << udp_bitrate
      << " iframeinterval=15 preset-level=1 ! "
      << "h264parse config-interval=-1 ! rtph264pay pt=96 ! ";
  }
  o << BuildUdpSinkElement(flags.udp_host, flags.udp_port);
  return o.str();
}

/// @brief Dosyaya kayıt dalını üretir (uzantıya göre mkv->matroskamux, aksi qtmux).
std::string BuildRecordBranch(const StreamOutputFlags& flags) {
  if (!flags.want_record || !flags.record_ok || flags.record_file == nullptr) {
    return "";
  }
  const int enc_latency = EncoderLatencyMeasurementEnabled() ? 1 : 0;
  const std::string rec_path(flags.record_file);
  const char* mux = (rec_path.size() >= 4 && rec_path.compare(rec_path.size() - 4, 4, ".mkv") == 0)
                        ? "matroskamux"
                        : "qtmux";
  std::ostringstream o;
  o << "queue " << kLeakyQueueProps << " ! " << NvVidconvFragment("savasan_record_vidconv") << " ! "
    << "capsfilter caps=\"video/x-raw(memory:NVMM),format=NV12\" ! "
    << "nvv4l2h264enc name=savasan_record_enc MeasureEncoderLatency=" << enc_latency
    << " insert-sps-pps=true bitrate=6000000 iframeinterval=15 preset-level=1 ! "
    << "h264parse ! " << mux << " ! filesink location=\"" << flags.record_file
    << "\" sync=false async=false";
  return o.str();
}

/// @brief nvdsosd sonrası çıkış kuyruğunu üretir: hiç dal yoksa fakesink, tek dal
/// doğrudan, birden çok dal için tee ile dağıtım.
std::string BuildPostOsdOutputTail(SinkType sink) {
  const StreamOutputFlags flags = ResolveStreamOutputFlags(sink);
  const std::string udp_branch = BuildUdpEncodeBranch(flags);
  const std::string record_branch = BuildRecordBranch(flags);

  int branch_count = 0;
  if (flags.want_display) ++branch_count;
  if (!udp_branch.empty()) ++branch_count;
  if (!record_branch.empty()) ++branch_count;

  if (branch_count == 0) {
    return std::string("queue ") + kLeakyQueueProps + " ! fakesink sync=false async=false";
  }
  if (branch_count == 1) {
    if (!udp_branch.empty()) return udp_branch;
    if (!record_branch.empty()) return record_branch;
    return std::string("queue ") + kLeakyQueueProps + " ! " + DisplaySinkGstreamerFragment();
  }

  std::ostringstream o;
  o << "tee name=savasan_osd_tee ";
  if (flags.want_display) {
    o << "savasan_osd_tee. ! queue " << kLeakyQueueProps << " ! " << DisplaySinkGstreamerFragment()
      << " ";
  }
  if (!record_branch.empty()) {
    o << "savasan_osd_tee. ! " << record_branch << " ";
  }
  if (!udp_branch.empty()) {
    o << "savasan_osd_tee. ! " << udp_branch;
  }
  return o.str();
}

/// @brief Faz-2 çıkış kuyruğu: tek yol (vidconv -> nvdsosd -> çıkış).
std::string BuildPhase2OutputTail(SinkType sink) {
  std::ostringstream o;
  o << NvVidconvFragment("savasan_post_infer_conv") << " ! nvdsosd ! " << BuildPostOsdOutputTail(sink);
  return o.str();
}

/// @brief Faz-3 tracker sonrası kuyruk: çıkış dalı gerekmiyorsa yalnızca hızlı yol
/// (fakesink); gerekiyorsa tee ile hızlı yol + OSD'li akış yolu.
std::string BuildPhase3PostTrackerTail(SinkType sink) {
  const StreamOutputFlags flags = ResolveStreamOutputFlags(sink);
  const bool need_osd_stream = flags.want_udp || flags.want_record || flags.want_display;

  if (!need_osd_stream) {
    return std::string("! queue name=savasan_fast_q ") + kLeakyQueueProps +
           " ! fakesink name=savasan_fast_sink sync=false async=false";
  }

  std::ostringstream o;
  o << "! tee name=savasan_tracker_tee "
    << "savasan_tracker_tee. ! queue name=savasan_fast_q " << kLeakyQueueProps
    << " ! fakesink name=savasan_fast_sink sync=false async=false "
    << "savasan_tracker_tee. ! queue name=savasan_stream_q " << kStreamBranchQueueProps << " ! "
    << NvVidconvFragment("savasan_stream_vidconv") << " ! nvdsosd ! " << BuildPostOsdOutputTail(sink);
  return o.str();
}

/// @brief Telemetri/gecikme probe'u için uygun pad'i seçer: önce savasan_fast_q
/// src, yoksa nvtracker0/nvinfer0/nvdsosd0 src (OSD'siz lean pipeline'lar dahil).
GstPad* AcquireTelemetryProbePad(GstElement* pipeline, const char** out_label) {
  if (pipeline == nullptr) {
    return nullptr;
  }
  static const char* kQueuePads[][2] = {{"savasan_fast_q", "src"}, {nullptr, nullptr}};
  for (int i = 0; kQueuePads[i][0] != nullptr; ++i) {
    GstElement* el = gst_bin_get_by_name(GST_BIN(pipeline), kQueuePads[i][0]);
    if (el == nullptr) {
      continue;
    }
    GstPad* pad = gst_element_get_static_pad(el, kQueuePads[i][1]);
    gst_object_unref(el);
    if (pad != nullptr) {
      if (out_label != nullptr) {
        *out_label = kQueuePads[i][0];
      }
      return pad;
    }
  }
  static const char* kElemSrc[] = {"nvtracker0", "nvinfer0", "nvdsosd0", nullptr};
  for (int i = 0; kElemSrc[i] != nullptr; ++i) {
    GstElement* el = gst_bin_get_by_name(GST_BIN(pipeline), kElemSrc[i]);
    if (el == nullptr) {
      continue;
    }
    GstPad* pad = gst_element_get_static_pad(el, "src");
    gst_object_unref(el);
    if (pad != nullptr) {
      if (out_label != nullptr) {
        *out_label = kElemSrc[i];
      }
      return pad;
    }
  }
  return nullptr;
}

}  // namespace

// Tüm queue'lara düşük gecikme ayarı uygular ve nvstreammux batch timeout'unu set eder.
void ApplyLowLatencyPipelineTuning(GstElement* pipeline) {
  if (pipeline == nullptr) {
    return;
  }

  GstIterator* it = gst_bin_iterate_elements(GST_BIN(pipeline));
  if (it == nullptr) {
    return;
  }
  GValue item = G_VALUE_INIT;
  while (gst_iterator_next(it, &item) == GST_ITERATOR_OK) {
    GstElement* el = GST_ELEMENT(g_value_get_object(&item));
    if (el != nullptr) {
      GstElementFactory* factory = gst_element_get_factory(el);
      if (factory != nullptr) {
        const gchar* factory_name = gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory));
        if (factory_name != nullptr && std::strcmp(factory_name, "queue") == 0) {
          // pre_mux_q: pipeline string'de 2 buffer (gecikme sıçraması toleransı); ezme.
          const gchar* el_name = GST_OBJECT_NAME(el);
          if (el_name != nullptr && std::strcmp(el_name, "savasan_pre_mux_q") == 0) {
            g_value_reset(&item);
            continue;
          }
          ApplyLowLatencyQueueProps(el);
        }
      }
    }
    g_value_reset(&item);
  }
  g_value_unset(&item);
  gst_iterator_free(it);

  GstElement* mux_el = gst_bin_get_by_name(GST_BIN(pipeline), "m");
  if (mux_el != nullptr) {
    const int timeout_usec = MuxPushTimeoutUsecFromEnv();
    g_object_set(G_OBJECT(mux_el), "batched-push-timeout", timeout_usec, nullptr);

    // Startup doğrulaması: muxer property'lerini bir kez oku/yazdır.
    static std::atomic<bool> s_mux_verified{false};
    bool expected = false;
    if (s_mux_verified.compare_exchange_strong(expected, true)) {
      gint width = 0;
      gint height = 0;
      gint batched_push_timeout = 0;
      gboolean sync_inputs = FALSE;
      g_object_get(G_OBJECT(mux_el), "width", &width, "height", &height, "batched-push-timeout",
                   &batched_push_timeout, "sync-inputs", &sync_inputs, nullptr);
      std::cout << "[MUX_VERIFY] nvstreammux width=" << width << " height=" << height
                << " batched-push-timeout=" << batched_push_timeout
                << " sync-inputs=" << (sync_inputs ? 1 : 0) << '\n';
    }

    gst_object_unref(mux_el);
  }

  // Startup doğrulaması: encoder MeasureEncoderLatency kapalı mı?
  // Bazı Jetson imajlarında NVENC "Encode Latency = ..." spam'ı açılabiliyor; bu,
  // journald backpressure ile gecikme sıçramalarına katkı verebiliyor.
  static std::atomic<bool> s_enc_verified{false};
  bool enc_expected = false;
  if (s_enc_verified.compare_exchange_strong(enc_expected, true)) {
    GstIterator* it2 = gst_bin_iterate_elements(GST_BIN(pipeline));
    if (it2 != nullptr) {
      GValue item2 = G_VALUE_INIT;
      while (gst_iterator_next(it2, &item2) == GST_ITERATOR_OK) {
        GstElement* el = GST_ELEMENT(g_value_get_object(&item2));
        if (el == nullptr) {
          g_value_reset(&item2);
          continue;
        }
        GstElementFactory* factory = gst_element_get_factory(el);
        const gchar* factory_name =
            (factory != nullptr) ? gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)) : nullptr;
        if (factory_name != nullptr &&
            (std::strcmp(factory_name, "nvv4l2h264enc") == 0 || std::strcmp(factory_name, "nvv4l2h265enc") == 0)) {
          gboolean measure = FALSE;
          // Property adı gst-inspect'te "MeasureEncoderLatency" (CamelCase).
          g_object_get(G_OBJECT(el), "MeasureEncoderLatency", &measure, nullptr);
          const gchar* el_name = GST_OBJECT_NAME(el);
          std::cout << "[ENC_VERIFY] el=" << (el_name ? el_name : "?")
                    << " factory=" << factory_name
                    << " MeasureEncoderLatency=" << (measure ? 1 : 0) << '\n';
          if (measure) {
            // En iyi çaba: kapatmayı dene (bazı sürümlerde yalnız READY/NULL).
            g_object_set(G_OBJECT(el), "MeasureEncoderLatency", FALSE, nullptr);
            gboolean verify = FALSE;
            g_object_get(G_OBJECT(el), "MeasureEncoderLatency", &verify, nullptr);
            std::cout << "[ENC_VERIFY]  -> set MeasureEncoderLatency=0 (now="
                      << (verify ? 1 : 0) << ")\n";
          }
        }
        g_value_reset(&item2);
      }
      g_value_unset(&item2);
      gst_iterator_free(it2);
    }
  }
}

// nvvidconv sink/src ve telemetri pad'ine probe'lar bağlayıp bileşen gecikme
// profilcisini kurar (durum nesnesi probe ömrüyle yönetilir).
void AttachComponentLatencyProfiler(GstElement* pipeline) {
  if (pipeline == nullptr || !ComponentLatencyProfilerEnabled()) {
    return;
  }
  if (!LatencyMeasurementEnabledByEnv()) {
    std::cerr << "[LATENCY_PROFILER] UYARI: NVDS_ENABLE_LATENCY_MEASUREMENT=1 gerekli.\n";
    return;
  }

  auto* st = new ComponentLatencyProfilerState();

  GstElement* vidconv_el = gst_bin_get_by_name(GST_BIN(pipeline), "savasan_stream_vidconv");
  if (vidconv_el == nullptr) {
    vidconv_el = gst_bin_get_by_name(GST_BIN(pipeline), "savasan_post_infer_conv");
  }
  if (vidconv_el != nullptr) {
    GstPad* sink_pad = gst_element_get_static_pad(vidconv_el, "sink");
    GstPad* src_pad = gst_element_get_static_pad(vidconv_el, "src");
    if (sink_pad != nullptr) {
      gst_pad_add_probe(sink_pad, GST_PAD_PROBE_TYPE_BUFFER, ComponentLatencyVidconvSinkProbe, st,
                        nullptr);
      gst_object_unref(sink_pad);
    }
    if (src_pad != nullptr) {
      gst_pad_add_probe(src_pad, GST_PAD_PROBE_TYPE_BUFFER, ComponentLatencyVidconvSrcProbe, st,
                        nullptr);
      gst_object_unref(src_pad);
    }
    gst_object_unref(vidconv_el);
  }

  const char* probe_label = nullptr;
  GstPad* telem_pad = AcquireTelemetryProbePad(pipeline, &probe_label);
  if (telem_pad == nullptr) {
    std::cerr << "[LATENCY_PROFILER] UYARI: telemetri pad bulunamadi.\n";
    delete st;
    return;
  }

  gst_pad_add_probe(telem_pad, GST_PAD_PROBE_TYPE_BUFFER, ComponentLatencyReportProbe, st,
                    [](gpointer data) { delete static_cast<ComponentLatencyProfilerState*>(data); });
  gst_object_unref(telem_pad);

  std::cout << "[LATENCY_PROFILER] Aktif (~1 Hz) pad=" << (probe_label ? probe_label : "?")
            << " | Kapat: SAVASAN_COMPONENT_LATENCY_PROFILER=0\n";
}

// Telemetri probe'unu uygun pad'e bağlar; isteğe bağlı CSV yolunu env'den alır.
void AttachEndTelemetryProbe(GstElement* pipeline) {
  if (!pipeline) {
    return;
  }

  const char* probe_label = nullptr;
  GstPad* telem_pad = AcquireTelemetryProbePad(pipeline, &probe_label);
  if (telem_pad == nullptr) {
    std::cerr << "[TELEMETRİ] UYARI: probe pad bulunamadi (savasan_fast_q / nvtracker0 / nvinfer0).\n";
    return;
  }

  auto* st = new TelemetryProbeState();
  const char* csv_env = std::getenv("SAVASAN_TELEMETRY_CSV");
  if (csv_env != nullptr && csv_env[0] != '\0') {
    st->csv_path = csv_env;
  }
  gst_pad_add_probe(telem_pad, GST_PAD_PROBE_TYPE_BUFFER, EndTelemetryProbeCallback, st,
                    [](gpointer data) { delete static_cast<TelemetryProbeState*>(data); });
  gst_object_unref(telem_pad);

  if (TelemetryProbeEnabled()) {
    std::cout << "[TELEMETRİ] Probe aktif pad=" << (probe_label ? probe_label : "?")
              << " (~1 Hz FPS/latency/GPU)\n";
  } else {
    std::cout << "[TELEMETRİ] Sessiz mod pad=" << (probe_label ? probe_label : "?")
              << " (~1 Hz -> " << kSilentTelemetryStatsPath << ")\n";
  }
}

// Faz-2 boru hattı string'ini kaynak dalı + (mux->nvinfer->OSD->çıkış) kuyruğuyla üretir.
std::string BuildPhase2PipelineString(const IngestConfig& ingest,
                                      const std::string& primary_config_abs,
                                      SinkType sink) {
   std::ostringstream tail;
   const int nvbuf_mem = NvbufMemoryTypeFromEnv();
   tail << "queue name=savasan_pre_mux_q " << kPreMuxQueueProps << " ! m.sink_0 "
        << "nvstreammux name=m batch-size=1 width=" << ingest.width << " height=" << ingest.height
        << " live-source=1 sync-inputs=1 attach-sys-ts=1 nvbuf-memory-type=" << nvbuf_mem
        << " batched-push-timeout=" << MuxPushTimeoutUsecFromEnv() << " "
        << "! nvinfer config-file-path=" << primary_config_abs << " batch-size=1 "
        << "! " << BuildPhase2OutputTail(sink);

  using savasan::camera::BuildUsbMjpegIngestBranch;
  return BuildUsbMjpegIngestBranch(ingest) + tail.str();
}

// Faz-3 boru hattı string'ini üretir: kaynak + mux->nvinfer->(nvtracker)->tee kuyruğu.
std::string BuildPhase3PipelineString(const IngestConfig& ingest,
                                      const std::string& primary_config_abs,
                                      const std::string& tracker_config_abs,
                                      const std::string& ll_lib_abs,
                                      SinkType sink) {
  const bool tracker_disable = TrackerDisabledByEnv();

   std::ostringstream tail;
   const int tracker_width = AlignUp32(ingest.width);
   const int tracker_height = AlignUp32(ingest.height);
   const int nvbuf_mem = NvbufMemoryTypeFromEnv();
   tail << "queue name=savasan_pre_mux_q " << kPreMuxQueueProps << " ! m.sink_0 "
        << "nvstreammux name=m batch-size=1 width=" << ingest.width
        << " height=" << ingest.height
        << " live-source=1 sync-inputs=1 attach-sys-ts=1 nvbuf-memory-type=" << nvbuf_mem
        << " batched-push-timeout=" << MuxPushTimeoutUsecFromEnv() << " "
        << "! nvinfer config-file-path=" << primary_config_abs << " batch-size=1 ";
   if (!tracker_disable) {
     tail << "! nvtracker"
          << " ll-lib-file=" << ll_lib_abs
          << " ll-config-file=" << tracker_config_abs
          << " tracker-width=" << tracker_width
          << " tracker-height=" << tracker_height
          << " gpu-id=0 display-tracking-id=true ";
   } else {
     common::Log(common::LogLevel::kCritical, "ds_app",
                 "Tracker DISABLED (SAVASAN_TRACKER_DISABLE=1) — track_id uretilmez, kilit mumkun "
                 "degil; onay icin SAVASAN_TRACKER_DISABLE_ACK=1");
   }
   tail << BuildPhase3PostTrackerTail(sink);

  using savasan::camera::BuildUsbMjpegIngestBranch;
  return BuildUsbMjpegIngestBranch(ingest) + tail.str();
}

// Bir nesneye standart kırmızı çerçeve + sarı/siyah etiket stili uygular.
void ApplyStandardObjStyle(NvDsObjectMeta* obj) {
  if (obj == nullptr) return;
  // Kırmızı bbox çerçeve.
  obj->rect_params.border_width = 3;
  obj->rect_params.has_color_info = 1;
  obj->rect_params.has_bg_color = 0;
  obj->rect_params.border_color.red = 1.0f;
  obj->rect_params.border_color.green = 0.0f;
  obj->rect_params.border_color.blue = 0.0f;
  obj->rect_params.border_color.alpha = 1.0f;
  // Etiket konumu.
  obj->text_params.x_offset = std::max(0, static_cast<int>(obj->rect_params.left));
  obj->text_params.y_offset = std::max(0, static_cast<int>(obj->rect_params.top) - 14);
  // Sarı metin, siyah arka plan.
  obj->text_params.font_params.font_name = const_cast<gchar*>("Serif");
  obj->text_params.font_params.font_size = 12;
  obj->text_params.font_params.font_color.red = 1.0f;
  obj->text_params.font_params.font_color.green = 1.0f;
  obj->text_params.font_params.font_color.blue = 0.0f;
  obj->text_params.font_params.font_color.alpha = 1.0f;
  obj->text_params.set_bg_clr = 1;
  obj->text_params.text_bg_clr.red = 0.0f;
  obj->text_params.text_bg_clr.green = 0.0f;
  obj->text_params.text_bg_clr.blue = 0.0f;
  obj->text_params.text_bg_clr.alpha = 0.65f;
}

// display_meta'daki tüm metin slotlarını serbest bırakır (bellek sızıntısını önler).
void ClearDisplayMetaTextSlots(NvDsDisplayMeta* display_meta) {
  if (display_meta == nullptr) {
    return;
  }
  for (guint i = 0; i < MAX_ELEMENTS_IN_DISPLAY_META; ++i) {
    NvOSD_TextParams& t = display_meta->text_params[i];
    if (t.display_text != nullptr) {
      g_free(t.display_text);
      t.display_text = nullptr;
    }
  }
}

// Havuzdan alınan display_meta'nın metinlerini ve sayaçlarını sıfırlar.
void ResetDisplayMetaForAcquire(NvDsDisplayMeta* display_meta) {
  if (display_meta == nullptr) {
    return;
  }
  ClearDisplayMetaTextSlots(display_meta);
  display_meta->num_labels = 0;
  display_meta->num_rects = 0;
  display_meta->num_lines = 0;
  display_meta->num_arrows = 0;
  display_meta->num_circles = 0;
}

// Metin değiştiyse eskisini serbest bırakıp yenisini kopyalar (gereksiz strdup'tan kaçınır).
void UpdateDisplayTextIfChanged(NvOSD_TextParams* text_params, const char* next_text) {
  if (text_params == nullptr || next_text == nullptr) return;
  if (text_params->display_text != nullptr &&
      std::strcmp(text_params->display_text, next_text) == 0) {
    return;
  }
  if (text_params->display_text != nullptr) {
    g_free(text_params->display_text);
    text_params->display_text = nullptr;
  }
  text_params->display_text = g_strdup(next_text);
}

// Faz-2: batch'teki tüm nesnelere görünür stil + "etiket conf=.." metni uygular.
void ApplyVisibleBboxStyleToBatch(NvDsBatchMeta* batch_meta) {
  if (batch_meta == nullptr) {
    return;
  }
  static thread_local char txt_buf[128];
  for (NvDsFrameMetaList* l_frame = batch_meta->frame_meta_list; l_frame != nullptr;
       l_frame = l_frame->next) {
    auto* frame = static_cast<NvDsFrameMeta*>(l_frame->data);
    if (frame == nullptr) continue;
    for (NvDsObjectMetaList* l_obj = frame->obj_meta_list; l_obj != nullptr;
         l_obj = l_obj->next) {
      auto* obj = static_cast<NvDsObjectMeta*>(l_obj->data);
      if (obj == nullptr) continue;
      ApplyStandardObjStyle(obj);
      const float conf =
          (obj->confidence >= 0.0f) ? obj->confidence : obj->tracker_confidence;
      const char* label = (obj->obj_label[0] != '\0') ? obj->obj_label : "obj";
      std::snprintf(txt_buf, sizeof(txt_buf), "%.64s conf=%.2f", label, conf);
      UpdateDisplayTextIfChanged(&obj->text_params, txt_buf);
    }
  }
}

// Ingest PTS referans probe'unu pre_mux_q ve decoder noktalarına bağlar.
void AttachIngestLatencyReferenceProbe(GstElement* pipeline) {
  if (!pipeline) {
    return;
  }
  int attached = 0;

  GstElement* pre_mux_q = gst_bin_get_by_name(GST_BIN(pipeline), "savasan_pre_mux_q");
  if (pre_mux_q != nullptr) {
    GstPad* src_pad = gst_element_get_static_pad(pre_mux_q, "src");
    if (src_pad != nullptr) {
      gst_pad_add_probe(src_pad, GST_PAD_PROBE_TYPE_BUFFER, IngestPtsProbe, nullptr, nullptr);
      gst_object_unref(src_pad);
      ++attached;
    }
    gst_object_unref(pre_mux_q);
  }

  GstElement* dec_el = gst_bin_get_by_name(GST_BIN(pipeline), "savasan_decoder");
  if (dec_el != nullptr) {
    GstPad* src_pad = gst_element_get_static_pad(dec_el, "src");
    if (src_pad != nullptr) {
      gst_pad_add_probe(src_pad, GST_PAD_PROBE_TYPE_BUFFER, IngestPtsProbe, nullptr, nullptr);
      gst_object_unref(src_pad);
      ++attached;
    }
    gst_object_unref(dec_el);
  }

  if (attached > 0) {
    std::cout << "[TELEMETRİ] Ingest PTS referans probe aktif (" << attached << " nokta)\n";
  }
}

bool IsTrackerDisabledByEnv() {
  return TrackerDisabledByEnv();
}

bool IsTrackerDisableAcknowledged() {
  return TrackerDisableAcknowledged();
}

bool ValidateTrackerForLockPhase() {
  if (!TrackerDisabledByEnv()) {
    return true;
  }
  if (TrackerDisableAcknowledged()) {
    common::Log(common::LogLevel::kWarn, "ds_app",
                "Tracker kapali ama SAVASAN_TRACKER_DISABLE_ACK=1 — kilit calismayacak");
    return true;
  }
  common::Log(common::LogLevel::kCritical, "ds_app",
              "Faz3/5 baslatma reddedildi: SAVASAN_TRACKER_DISABLE=1 (track_id yok, kilit mumkun "
              "degil). Sorun giderme icin SAVASAN_TRACKER_DISABLE_ACK=1");
  return false;
}

}  // namespace savasan::deepstream
