/**
 * @file gst_app_helpers.cpp
 * @brief @ref gst_app_helpers.hpp uygulaması (GStreamer/uygulama yardımcıları).
 */
#include "runners/gst_app_helpers.hpp"

#include <chrono>
#include <cstdio>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <ctime>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "camera/usb_source.hpp"
#include "common/log.hpp"
#include "deepstream/ds_app.hpp"
#include "pipeline/ingest_config.hpp"

#include <cstdlib>

namespace savasan::runners {

namespace {

constexpr char kDefaultDebugLogPath[] = "/tmp/savasan_debug.log";
constexpr guint kDefaultSourceErrorMaxRetries = 2;
constexpr gint64 kSourceErrorRetryWindowUs = 60 * G_USEC_PER_SEC;

GstBusHealthSnapshot& BusHealthInstance() {
  static GstBusHealthSnapshot instance;
  return instance;
}

int ReadPositiveIntEnv(const char* env_var, int fallback) {
  const char* raw = std::getenv(env_var);
  if (raw == nullptr || raw[0] == '\0') {
    return fallback;
  }
  const int value = std::atoi(raw);
  return value > 0 ? value : fallback;
}

int ReadShutdownTimeoutSec() {
  return ReadPositiveIntEnv("SAVASAN_GST_SHUTDOWN_TIMEOUT_SEC", 8);
}

guint ReadSourceErrorMaxRetries() {
  const int value = ReadPositiveIntEnv("SAVASAN_GST_SOURCE_ERROR_MAX_RETRIES",
                                       static_cast<int>(kDefaultSourceErrorMaxRetries));
  return static_cast<guint>(value);
}

bool ContainsInsensitive(const char* haystack, const char* needle) {
  return haystack != nullptr && needle != nullptr && std::strstr(haystack, needle) != nullptr;
}

// v4l2src / decoder gibi kaynak dalı hataları geçici olabilir; sınırlı yeniden deneme uygulanır.
bool IsSourceBranchError(const char* src_name, const char* err_msg, const char* dbg_msg) {
  if (src_name != nullptr) {
    if (ContainsInsensitive(src_name, "v4l2") || ContainsInsensitive(src_name, "nvv4l2") ||
        ContainsInsensitive(src_name, "nvjpeg") || ContainsInsensitive(src_name, "jpegdec") ||
        ContainsInsensitive(src_name, "source")) {
      return true;
    }
  }
  if (ContainsInsensitive(err_msg, "v4l2") || ContainsInsensitive(dbg_msg, "v4l2") ||
      ContainsInsensitive(err_msg, "No such device") || ContainsInsensitive(dbg_msg, "No such device") ||
      ContainsInsensitive(err_msg, "device") || ContainsInsensitive(dbg_msg, "device")) {
    return true;
  }
  return false;
}

bool TryRecoverSourcePipeline(BusWatchContext* ctx) {
  if (ctx == nullptr || ctx->pipeline == nullptr) {
    return false;
  }
  const guint max_retries = ReadSourceErrorMaxRetries();
  const gint64 now_us = g_get_monotonic_time();
  if (ctx->last_source_error_us > 0 &&
      (now_us - ctx->last_source_error_us) > kSourceErrorRetryWindowUs) {
    ctx->source_error_retries = 0;
  }
  ctx->last_source_error_us = now_us;
  if (ctx->source_error_retries >= max_retries) {
    return false;
  }
  ++ctx->source_error_retries;
  BusHealthInstance().source_retry_count.fetch_add(1, std::memory_order_relaxed);

  std::cerr << "[GST] Kaynak hatasi: pipeline yeniden baslatiliyor ("
            << ctx->source_error_retries << "/" << max_retries << ")\n";
  gst_element_set_state(ctx->pipeline, GST_STATE_NULL);
  if (gst_element_set_state(ctx->pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    std::cerr << "[GST] Kaynak yeniden baslatma basarisiz.\n";
    return false;
  }
  return true;
}

bool IsRecoverableCameraBusMessage(GstMessage* msg, const GError* err, const char* dbg) {
  const char* err_msg = err ? err->message : nullptr;
  if (ContainsInsensitive(err_msg, "v4l2") || ContainsInsensitive(dbg, "v4l2") ||
      ContainsInsensitive(err_msg, "No such device") || ContainsInsensitive(dbg, "No such device") ||
      ContainsInsensitive(err_msg, "Device or resource busy") ||
      ContainsInsensitive(dbg, "Device or resource busy") ||
      ContainsInsensitive(err_msg, "Failed to allocate required memory") ||
      ContainsInsensitive(dbg, "Failed to allocate required memory") ||
      ContainsInsensitive(err_msg, "Internal data stream error") ||
      ContainsInsensitive(dbg, "Internal data stream error")) {
    return true;
  }
  if (msg == nullptr || GST_MESSAGE_SRC(msg) == nullptr) {
    return false;
  }
  const char* src_name = GST_OBJECT_NAME(GST_MESSAGE_SRC(msg));
  if (src_name != nullptr &&
      (std::strcmp(src_name, "savasan_v4l2src") == 0 || std::strcmp(src_name, "savasan_decoder") == 0)) {
    return true;
  }
  GstElement* src_el = GST_ELEMENT(GST_MESSAGE_SRC(msg));
  if (src_el != nullptr) {
    GstElementFactory* factory = gst_element_get_factory(src_el);
    if (factory != nullptr) {
      const gchar* factory_name = gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory));
      if (factory_name != nullptr &&
          (std::strcmp(factory_name, "v4l2src") == 0 || std::strcmp(factory_name, "nvv4l2decoder") == 0)) {
        return true;
      }
    }
  }
  return false;
}

void MarkPipelineUnhealthy(BusWatchContext* ctx) {
  if (ctx != nullptr && ctx->pipeline_healthy != nullptr) {
    ctx->pipeline_healthy->store(false);
  }
}

gboolean UsbCameraReconnectAttempt(gpointer user_data) {
  auto* ctx = static_cast<BusWatchContext*>(user_data);
  if (ctx == nullptr || ctx->pipeline == nullptr || ctx->loop == nullptr) {
    return G_SOURCE_REMOVE;
  }
  ctx->usb_reconnect_source_id = 0;
  ctx->usb_reconnect_pending = FALSE;

  if (ctx->usb_attempts >= ctx->usb_max_attempts) {
    ctx->usb_fatal_exhausted = true;
    common::Log(common::LogLevel::kCritical, "USB",
                "Kamera yeniden baglanma limiti asildi (" + std::to_string(ctx->usb_max_attempts) +
                    " deneme)");
    g_main_loop_quit(ctx->loop);
    return G_SOURCE_REMOVE;
  }
  ++ctx->usb_attempts;

  if (::access(ctx->v4l2_device.c_str(), R_OK) != 0) {
    common::Log(common::LogLevel::kWarn, "USB",
                "V4L2 cihaz hazir degil (" + ctx->v4l2_device + "), deneme " +
                    std::to_string(ctx->usb_attempts) + "/" + std::to_string(ctx->usb_max_attempts));
    ctx->usb_reconnect_pending = TRUE;
    ctx->usb_reconnect_source_id =
        g_timeout_add(static_cast<guint>(ctx->usb_interval_ms), UsbCameraReconnectAttempt, ctx);
    return G_SOURCE_REMOVE;
  }

  gst_element_set_state(ctx->pipeline, GST_STATE_NULL);
  const GstStateChangeReturn play_ret = gst_element_set_state(ctx->pipeline, GST_STATE_PLAYING);
  if (play_ret == GST_STATE_CHANGE_FAILURE) {
    common::Log(common::LogLevel::kWarn, "USB",
                "Pipeline PLAYING basarisiz, deneme " + std::to_string(ctx->usb_attempts) + "/" +
                    std::to_string(ctx->usb_max_attempts));
    ctx->usb_reconnect_pending = TRUE;
    ctx->usb_reconnect_source_id =
        g_timeout_add(static_cast<guint>(ctx->usb_interval_ms), UsbCameraReconnectAttempt, ctx);
    return G_SOURCE_REMOVE;
  }

  ctx->usb_attempts = 0;
  if (ctx->pipeline_healthy != nullptr) {
    ctx->pipeline_healthy->store(true);
  }
  BusHealthInstance().source_retry_count.fetch_add(1, std::memory_order_relaxed);
  common::Log(common::LogLevel::kInfo, "USB", "Kamera yeniden baglandi: " + ctx->v4l2_device);
  return G_SOURCE_REMOVE;
}

void ScheduleUsbCameraReconnect(BusWatchContext* ctx) {
  if (ctx == nullptr || ctx->loop == nullptr || ctx->pipeline == nullptr) {
    return;
  }
  if (ctx->usb_reconnect_pending || ctx->usb_reconnect_source_id != 0) {
    return;
  }
  MarkPipelineUnhealthy(ctx);
  BusHealthInstance().source_retry_count.fetch_add(1, std::memory_order_relaxed);
  ctx->usb_reconnect_pending = TRUE;
  common::Log(common::LogLevel::kWarn, "USB",
              "Kamera kopmasi algilandi; " + std::to_string(ctx->usb_interval_ms) +
                  " ms sonra yeniden baglanma denenecek");
  ctx->usb_reconnect_source_id =
      g_timeout_add(static_cast<guint>(ctx->usb_interval_ms), UsbCameraReconnectAttempt, ctx);
}

}  // namespace

GstBusHealthSnapshot& GetGstBusHealthSnapshot() {
  return BusHealthInstance();
}

std::string EnvOrDefault(const char* env_var, const char* fallback) {
  const char* v = std::getenv(env_var);
  return (v != nullptr && v[0] != '\0') ? v : fallback;
}

#if SAVASAN_DEBUG
std::string JsonEscape(const std::string& in) {
  std::string out;
  out.reserve(in.size() + 16);
  for (char c : in) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out.push_back(c);
        break;
    }
  }
  return out;
}

void DebugLog(const char* run_id, const char* hypothesis_id, const char* location, const char* message,
              const std::string& data_json) {
  static const std::string log_path = EnvOrDefault("SAVASAN_DEBUG_LOG", kDefaultDebugLogPath);
  std::ofstream ofs(log_path, std::ios::app);
  if (!ofs.is_open()) return;
  const auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
  ofs << "{\"sessionId\":\"d943d8\",\"runId\":\"" << JsonEscape(run_id)
      << "\",\"hypothesisId\":\"" << JsonEscape(hypothesis_id)
      << "\",\"location\":\"" << JsonEscape(location)
      << "\",\"message\":\"" << JsonEscape(message)
      << "\",\"data\":" << data_json << ",\"timestamp\":" << ts << "}\n";
}
#endif

// systemd NOTIFY_SOCKET'e (hem yol hem soyut '@' soket adresini destekleyerek)
// durum mesajı gönderir.
void SdNotify(const char* state) {
  const char* sock_path = std::getenv("NOTIFY_SOCKET");
  if (sock_path == nullptr || sock_path[0] == '\0') return;

  const int fd = ::socket(AF_UNIX, SOCK_DGRAM, 0);
  if (fd < 0) return;

  struct sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  if (sock_path[0] == '@') {
    addr.sun_path[0] = '\0';
    std::strncpy(addr.sun_path + 1, sock_path + 1, sizeof(addr.sun_path) - 2);
  } else {
    std::strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);
  }
  const socklen_t addr_len = static_cast<socklen_t>(
      offsetof(struct sockaddr_un, sun_path) + std::strlen(sock_path));

  ::sendto(fd, state, std::strlen(state), MSG_NOSIGNAL,
           reinterpret_cast<const struct sockaddr*>(&addr), addr_len);
  ::close(fd);
}

void CleanupProbes(std::vector<AttachedProbe>& probes) {
  for (auto& p : probes) {
    gst_pad_remove_probe(p.pad, p.id);
    gst_object_unref(p.pad);
  }
  probes.clear();
}

gboolean WatchdogHeartbeat(gpointer /*data*/) {
  SdNotify("WATCHDOG=1");
  return G_SOURCE_CONTINUE;
}

gboolean OnSignalQuit(gpointer data) {
  std::cout << "[Watchdog] Sinyal alindi, temiz kapatma basliyor...\n";
  g_main_loop_quit(static_cast<GMainLoop*>(data));
  return G_SOURCE_REMOVE;
}

gboolean StopMainLoop(gpointer data) {
  g_main_loop_quit(static_cast<GMainLoop*>(data));
  return G_SOURCE_REMOVE;
}

// EOS gönderir ve bus'tan EOS/ERROR mesajını bekler; böylece muxer (qtmux/matroskamux)
// dosyayı bozulmadan kapatır. Başarısızlıkta false döner ve uyarı loglar.
bool GracefulPipelineShutdown(GstElement* pipeline) {
  if (pipeline == nullptr) {
    SAVASAN_LOG_IF(savasan::common::LogLevel::kWarn, "gst",
                   "GracefulPipelineShutdown: pipeline null");
    return false;
  }
  if (!gst_element_send_event(pipeline, gst_event_new_eos())) {
    SAVASAN_LOG_IF(savasan::common::LogLevel::kWarn, "gst", "EOS olayi gonderilemedi");
    return false;
  }
  GstBus* bus = gst_element_get_bus(pipeline);
  if (bus == nullptr) {
    SAVASAN_LOG_IF(savasan::common::LogLevel::kWarn, "gst", "Pipeline bus alinamadi");
    return false;
  }
  const int timeout_sec = ReadShutdownTimeoutSec();
  GstMessage* msg = gst_bus_timed_pop_filtered(
      bus, static_cast<GstClockTime>(timeout_sec) * GST_SECOND,
      static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
  gst_object_unref(bus);
  if (msg == nullptr) {
    std::cerr << "[GST] EOS yaniti " << timeout_sec
              << " sn icinde gelmedi; video kaydi bozuk olabilir.\n";
    SAVASAN_LOG_IF(savasan::common::LogLevel::kWarn, "gst",
                   "GracefulPipelineShutdown zaman asimi");
    return false;
  }

  const GstMessageType mtype = GST_MESSAGE_TYPE(msg);
  if (mtype == GST_MESSAGE_ERROR) {
    GError* err = nullptr;
    gchar* dbg = nullptr;
    gst_message_parse_error(msg, &err, &dbg);
    std::cerr << "[GST] Kapatma sirasinda hata: " << (err ? err->message : "?") << '\n';
    if (dbg) {
      std::cerr << "  debug: " << dbg << '\n';
      g_free(dbg);
    }
    if (err) {
      g_error_free(err);
    }
    gst_message_unref(msg);
    SAVASAN_LOG_IF(savasan::common::LogLevel::kWarn, "gst",
                   "GracefulPipelineShutdown bus ERROR");
    return false;
  }

  gst_message_unref(msg);
  return true;
}

bool IsClockOverlayEnabled() {
  const char* ev = std::getenv("SAVASAN_OVERLAY_CLOCK");
  if (ev == nullptr || ev[0] == '\0') return true;
  return std::strcmp(ev, "0") != 0;
}

// Yerel saati "SAAT YYYY-MM-DD HH:MM:SS.mmm" biçiminde döndürür (SERVER etiketi yok).
std::string CurrentServerTimeText() {
  // Varsayılan: Türkiye saati (UTC+3) beklentisi için Europe/Istanbul.
  static const bool tz_init = []() {
    const char* forced = std::getenv("SAVASAN_OVERLAY_TZ");
    const char* tz = (forced != nullptr && forced[0] != '\0') ? forced : "Europe/Istanbul";
    setenv("TZ", tz, 1);
    tzset();
    return true;
  }();
  (void)tz_init;
  const auto now = std::chrono::system_clock::now();
  const std::time_t tt = std::chrono::system_clock::to_time_t(now);
  std::tm tm_local{};
  localtime_r(&tt, &tm_local);
  char buf[64];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_local);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch())
                      .count() %
                  1000;
  char out[96];
  std::snprintf(out, sizeof(out), "SAAT %s.%03lld", buf,
                static_cast<long long>(ms));
  return std::string(out);
}

void PrintErrorHint(const char* err_msg, const char* dbg_msg) {
  auto contains = [](const char* haystack, const char* needle) -> bool {
    return haystack != nullptr && std::strstr(haystack, needle) != nullptr;
  };
  if (contains(err_msg, "nvinfer") || contains(dbg_msg, "nvinfer") ||
      contains(err_msg, "TensorRT") || contains(dbg_msg, "TensorRT")) {
    std::cerr << "  -> Olasi neden: TensorRT engine olusturulamadi veya "
                 "config-file-path / model-engine-file hatali.\n"
              << "  -> Kontrol: SAVASAN_PGI_CONFIG, onnx-file, "
                 "model-engine-file, custom-lib-path\n";
  }
  if (contains(err_msg, "v4l2") || contains(dbg_msg, "v4l2") || contains(err_msg, "device") ||
      contains(dbg_msg, "No such device")) {
    std::cerr << "  -> Olasi neden: Kamera baglantisi kesildi veya cihaz "
                 "bulunamadi.\n"
              << "  -> Kontrol: SAVASAN_V4L2_DEVICE, USB kablo baglantisi\n";
  }
}

bool UsbCameraReconnectEnabledByEnv() {
  const char* e = std::getenv("SAVASAN_USB_RECONNECT");
  if (e == nullptr || e[0] == '\0') {
    return true;
  }
  return std::strcmp(e, "0") != 0 && std::strcmp(e, "false") != 0;
}

void CancelUsbCameraReconnect(BusWatchContext* ctx) {
  if (ctx == nullptr) {
    return;
  }
  if (ctx->usb_reconnect_source_id != 0) {
    g_source_remove(ctx->usb_reconnect_source_id);
    ctx->usb_reconnect_source_id = 0;
  }
  ctx->usb_reconnect_pending = FALSE;
}

// GStreamer bus mesajlarını işler: hata/EOS'ta ana döngüyü durdurur, uyarıları
// sayar; kaynak dalı hatalarında sınırlı yeniden deneme uygular.
gboolean BusWatch(GstBus* /*bus*/, GstMessage* msg, gpointer data) {
  auto* ctx = static_cast<BusWatchContext*>(data);
  if (ctx == nullptr || ctx->loop == nullptr) {
    return TRUE;
  }
  GMainLoop* loop = ctx->loop;
  switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR: {
      GError* err = nullptr;
      gchar* dbg = nullptr;
      gst_message_parse_error(msg, &err, &dbg);
#if SAVASAN_DEBUG
      {
        std::ostringstream d;
        d << "{\"error\":\"" << JsonEscape(err ? err->message : "?")
          << "\",\"debug\":\"" << JsonEscape(dbg ? dbg : "") << "\"}";
        DebugLog("baseline", "H4", "gst_app_helpers.cpp:BusWatch", "bus_error", d.str());
      }
#endif
      const char* src_name =
          GST_MESSAGE_SRC(msg) ? GST_OBJECT_NAME(GST_MESSAGE_SRC(msg)) : "?";
      std::cerr << "GStreamer HATA [" << src_name << "]: " << (err ? err->message : "?") << '\n';
      if (dbg) {
        std::cerr << "  debug: " << dbg << '\n';
      }
      PrintErrorHint(err ? err->message : nullptr, dbg);

      const bool source_error =
          IsSourceBranchError(src_name, err ? err->message : nullptr, dbg);
      const bool camera_error =
          IsRecoverableCameraBusMessage(msg, err, dbg);
      if (dbg) {
        g_free(dbg);
      }
      if (err) {
        g_error_free(err);
      }

      if (ctx->usb_reconnect_enabled && camera_error) {
        ScheduleUsbCameraReconnect(ctx);
        return TRUE;
      }
      if (source_error && TryRecoverSourcePipeline(ctx)) {
        return TRUE;
      }

      BusHealthInstance().error_count.fetch_add(1, std::memory_order_relaxed);
      g_main_loop_quit(loop);
      break;
    }
    case GST_MESSAGE_WARNING: {
      GError* warn = nullptr;
      gchar* dbg = nullptr;
      gst_message_parse_warning(msg, &warn, &dbg);
      BusHealthInstance().warning_count.fetch_add(1, std::memory_order_relaxed);
      const char* src_name =
          GST_MESSAGE_SRC(msg) ? GST_OBJECT_NAME(GST_MESSAGE_SRC(msg)) : "?";
      std::cerr << "GStreamer UYARI [" << src_name << "]: " << (warn ? warn->message : "?")
                << '\n';
      if (dbg) {
        std::cerr << "  debug: " << dbg << '\n';
        g_free(dbg);
      }
      if (warn) {
        g_error_free(warn);
      }
      break;
    }
    case GST_MESSAGE_EOS:
      if (ctx->usb_reconnect_enabled) {
        std::cout << "EOS alindi (canli kaynak; USB yeniden baglanma deneniyor).\n";
        ScheduleUsbCameraReconnect(ctx);
      } else {
        std::cout << "EOS alindi.\n";
        g_main_loop_quit(loop);
      }
      break;
    default:
      break;
  }
  return TRUE;
}

// gst_parse_launch'ın yanlış ayrıştırdığı "'video/x-raw(memory:NVMM)" caps
// tırnaklarını kaldırarak pipeline metnini ayrıştırmaya uygun hâle getirir.
std::string NormalizePipelineForParse(const std::string& pipeline) {
  std::string out = pipeline;
  constexpr const char* kBadCapsPrefix = "'video/x-raw(memory:NVMM)";
  std::size_t pos = out.find(kBadCapsPrefix);
  while (pos != std::string::npos) {
    out.erase(pos, 1);
    std::size_t bang = out.find(" ! ", pos);
    if (bang == std::string::npos) {
      bang = out.size();
    }
    std::size_t close_quote = out.rfind('\'', bang);
    if (close_quote != std::string::npos && close_quote >= pos) {
      out.erase(close_quote, 1);
    }
    pos = out.find(kBadCapsPrefix, pos);
  }
  return out;
}

std::string BuildPhase1TestPipeline(const savasan::IngestConfig& cfg) {
  return savasan::camera::BuildUsbMjpegIngestBranch(cfg) + "fakesink sync=false async=false";
}

// OSD sink probe: moda göre kutu stilini uygular ve (etkinse) sağ üste saat
// bindirir. Saat metni saniyede bir önbelleğe alınarak güncellenir.
GstPadProbeReturn OSDSinkTimestampProbe(GstPad* /*pad*/, GstPadProbeInfo* info, gpointer user_data) {
  if ((info->type & GST_PAD_PROBE_TYPE_BUFFER) == 0) {
    return GST_PAD_PROBE_OK;
  }
  GstBuffer* buf = GST_PAD_PROBE_INFO_BUFFER(info);
  if (!buf) return GST_PAD_PROBE_OK;

  const auto mode = static_cast<OsdSinkProbeMode>(reinterpret_cast<intptr_t>(user_data));

  NvDsBatchMeta* batch_meta = gst_buffer_get_nvds_batch_meta(buf);
  if (batch_meta != nullptr && mode == OsdSinkProbeMode::kPhase2StyleAndMaybeClock) {
    savasan::deepstream::ApplyVisibleBboxStyleToBatch(batch_meta);
  }

  if (!IsClockOverlayEnabled()) {
    return GST_PAD_PROBE_OK;
  }
  if (!batch_meta) {
    return GST_PAD_PROBE_OK;
  }

  static std::string g_cached_timestamp;
  static auto g_last_update = std::chrono::steady_clock::now();
  auto now = std::chrono::steady_clock::now();
  if (g_cached_timestamp.empty() || (now - g_last_update > std::chrono::seconds(1))) {
    g_cached_timestamp = CurrentServerTimeText();
    g_last_update = now;
  }

  for (NvDsMetaList* l = batch_meta->frame_meta_list; l; l = l->next) {
    auto* frame = static_cast<NvDsFrameMeta*>(l->data);
    if (!frame) continue;

    NvDsDisplayMeta* display_meta = nvds_acquire_display_meta_from_pool(batch_meta);
    if (!display_meta) continue;
    savasan::deepstream::ResetDisplayMetaForAcquire(display_meta);
    display_meta->num_labels = 1;
    NvOSD_TextParams& txt = display_meta->text_params[0];
    txt.display_text = g_strdup(g_cached_timestamp.c_str());
    const int frame_w =
        (frame->pipeline_width > 0) ? frame->pipeline_width : 800;
    // Şartname: sağ üst, ms hassasiyetli sunucu saati — metin uzunluğuna göre sağa yapıştır.
    constexpr int kClockFont = 9;
    constexpr int kPxPerChar = 7;  // Serif ~9pt yaklaşık
    const int text_w =
        static_cast<int>(g_cached_timestamp.size()) * kPxPerChar + 12;  // +bg padding
    txt.x_offset = std::max(8, frame_w - text_w - 6);
    txt.y_offset = 6;
    txt.font_params.font_name = const_cast<gchar*>("Serif");
    txt.font_params.font_size = kClockFont;
    txt.font_params.font_color.red = 1.0f;
    txt.font_params.font_color.green = 1.0f;
    txt.font_params.font_color.blue = 1.0f;
    txt.font_params.font_color.alpha = 1.0f;
    txt.set_bg_clr = 1;
    txt.text_bg_clr.red = 0.0f;
    txt.text_bg_clr.green = 0.0f;
    txt.text_bg_clr.blue = 0.0f;
    txt.text_bg_clr.alpha = 0.6f;
    nvds_add_display_meta_to_frame(frame, display_meta);
  }
  return GST_PAD_PROBE_OK;
}

#if SAVASAN_DEBUG
GstPadProbeReturn OSDSrcProbe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data) {
  if ((info->type & GST_PAD_PROBE_TYPE_BUFFER) == 0) {
    return GST_PAD_PROBE_OK;
  }
  auto* st = static_cast<StreamProbeState*>(user_data);
  const gint64 now_us = g_get_monotonic_time();
  st->frame_count++;
  st->window_frames++;

  if (st->last_frame_us > 0) {
    const double frame_interval_ms = static_cast<double>(now_us - st->last_frame_us) / 1000.0;
    if (st->ema_frame_interval_ms <= 0.0) {
      st->ema_frame_interval_ms = frame_interval_ms;
    } else {
      st->ema_frame_interval_ms = (0.90 * st->ema_frame_interval_ms) + (0.10 * frame_interval_ms);
    }
  }
  st->last_frame_us = now_us;

  if (st->window_start_us == 0) {
    st->window_start_us = now_us;
  }

  const double elapsed_s = static_cast<double>(now_us - st->window_start_us) / 1000000.0;
  if (elapsed_s >= 1.0) {
    const double fps = static_cast<double>(st->window_frames) / elapsed_s;
    std::cout << "[Perf] FPS=" << fps << " frame_latency_ms~=" << st->ema_frame_interval_ms << '\n';
    st->window_frames = 0;
    st->window_start_us = now_us;
  }

  if (SAVASAN_DEBUG && (st->frame_count == 1 || (st->frame_count % 120) == 0)) {
    const char* caps_str_c = "";
    GstCaps* caps = gst_pad_get_current_caps(pad);
    std::string caps_str;
    if (caps) {
      gchar* tmp = gst_caps_to_string(caps);
      caps_str = tmp;
      g_free(tmp);
      gst_caps_unref(caps);
      caps_str_c = caps_str.c_str();
    }
    std::ostringstream d;
    d << "{\"frameCount\":" << st->frame_count << ",\"caps\":\"" << JsonEscape(caps_str_c) << "\"}";
    DebugLog("baseline", "H6", "gst_app_helpers.cpp:OSDSrcProbe", "osd_src_buffer_seen", d.str());
  }
  return GST_PAD_PROBE_OK;
}

GstPadProbeReturn SinkSinkProbe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data) {
  if ((info->type & GST_PAD_PROBE_TYPE_BUFFER) == 0) {
    return GST_PAD_PROBE_OK;
  }
  auto* st = static_cast<StreamProbeState*>(user_data);
  st->frame_count++;
  if (SAVASAN_DEBUG && (st->frame_count == 1 || (st->frame_count % 120) == 0)) {
    const char* caps_str_c = "";
    GstCaps* caps = gst_pad_get_current_caps(pad);
    std::string caps_str;
    if (caps) {
      gchar* tmp = gst_caps_to_string(caps);
      caps_str = tmp;
      g_free(tmp);
      gst_caps_unref(caps);
      caps_str_c = caps_str.c_str();
    }
    std::ostringstream d;
    d << "{\"frameCount\":" << st->frame_count << ",\"caps\":\"" << JsonEscape(caps_str_c) << "\"}";
    DebugLog("baseline", "H7", "gst_app_helpers.cpp:SinkSinkProbe", "sink_sink_buffer_seen", d.str());
  }
  return GST_PAD_PROBE_OK;
}

bool IsTargetSinkType(const char* type_name) {
  if (type_name == nullptr) return false;
  return std::strstr(type_name, "EglGlesSink") != nullptr ||
         std::strstr(type_name, "Nv3dSink") != nullptr || std::strstr(type_name, "XvImageSink") != nullptr;
}
#endif

}  // namespace savasan::runners
