/**
 * @file gst_app_helpers.hpp
 * @brief Faz koşucuları için ortak GStreamer/uygulama yardımcıları.
 *
 * Ortam değişkeni okuma, systemd bildirimi (sd_notify), probe temizliği, düzgün
 * pipeline kapatma, GStreamer bus izleme, hata ipuçları ve OSD (saat/stil) probe
 * fonksiyonlarını içerir.
 */
#ifndef SAVASAN_RUNNERS_GST_APP_HELPERS_HPP_
#define SAVASAN_RUNNERS_GST_APP_HELPERS_HPP_

#include <atomic>
#include <glib.h>
#include <gst/gst.h>

#include <cstdint>
#include <string>
#include <vector>

#ifndef SAVASAN_DEBUG
#define SAVASAN_DEBUG 0
#endif

#include "gstnvdsmeta.h"
#include "pipeline/ingest_config.hpp"

namespace savasan::runners {

/// @brief Ortam değişkenini okur; tanımsız/boşsa @p fallback döner.
std::string EnvOrDefault(const char* env_var, const char* fallback);

#if SAVASAN_DEBUG
/// @brief Bir dizeyi JSON için güvenli (kaçışlı) hâle getirir.
std::string JsonEscape(const std::string& in);
/// @brief Yapılandırılmış (JSON satır) hata ayıklama günlüğü yazar.
void DebugLog(const char* run_id, const char* hypothesis_id, const char* location, const char* message,
              const std::string& data_json);
#endif

/// @brief systemd'ye durum bildirir (NOTIFY_SOCKET tanımlıysa).
void SdNotify(const char* state);

/// @brief Bir pad'e bağlanmış probe'un takibi (temizlik için).
struct AttachedProbe {
  GstPad* pad; ///< Probe'un bağlı olduğu pad.
  gulong id;   ///< Probe kimliği.
};

/// @brief Bağlı tüm probe'ları kaldırır ve pad referanslarını serbest bırakır.
void CleanupProbes(std::vector<AttachedProbe>& probes);

/// @brief GStreamer bus uyarı/hata sayaçları (sağlık göstergesi için).
struct GstBusHealthSnapshot {
  std::atomic<uint64_t> warning_count{0};      ///< Toplam GST_MESSAGE_WARNING sayısı.
  std::atomic<uint64_t> error_count{0};        ///< Toplam fatal GST_MESSAGE_ERROR sayısı.
  std::atomic<uint64_t> source_retry_count{0}; ///< Kaynak dalı hata yeniden deneme sayısı.
};

/// @brief Bus sağlık sayaçlarına erişim (process genelinde tek örnek).
GstBusHealthSnapshot& GetGstBusHealthSnapshot();

/// @brief BusWatch geri çağrısı için bağlam (döngü + pipeline + yeniden deneme durumu).
struct BusWatchContext {
  GMainLoop* loop = nullptr;       ///< Ana GLib döngüsü.
  GstElement* pipeline = nullptr;  ///< Yeniden deneme için pipeline (sahiplenilmez).
  guint source_error_retries = 0;    ///< Mevcut hata penceresindeki yeniden deneme sayısı.
  gint64 last_source_error_us = 0; ///< Son kaynak hatası (g_get_monotonic_time, us).
  bool usb_reconnect_enabled = false; ///< USB kopmasında gecikmeli yeniden bağlanma.
  std::string v4l2_device;         ///< V4L2 cihaz yolu (USB reconnect).
  int usb_max_attempts = 5;        ///< USB azami yeniden bağlanma denemesi.
  int usb_interval_ms = 2000;      ///< USB denemeler arası bekleme (ms).
  int usb_attempts = 0;            ///< Geçerli kopma penceresindeki USB deneme sayısı.
  gboolean usb_reconnect_pending = FALSE; ///< USB zamanlayıcı bekliyor mu.
  guint usb_reconnect_source_id = 0;    ///< g_timeout_add kimliği.
  std::atomic<bool>* pipeline_healthy = nullptr; ///< Opsiyonel Faz5 pipeline sağlığı.
  bool usb_fatal_exhausted = false;     ///< USB deneme limiti aşıldı.
};

/// @brief qtmux/matroskamux dosyalarını düzgün kapatmak için EOS gönderip bekler.
/// @return EOS başarıyla alındıysa true; zaman aşımı veya hata durumunda false.
bool GracefulPipelineShutdown(GstElement* pipeline);

/// @brief Watchdog heartbeat (systemd WATCHDOG=1) zamanlayıcı geri çağırımı.
gboolean WatchdogHeartbeat(gpointer data);
/// @brief Sinyal (örn. SIGINT) üzerine ana döngüyü sonlandırır.
gboolean OnSignalQuit(gpointer data);
/// @brief Ana döngüyü durduran zamanlayıcı geri çağırımı.
gboolean StopMainLoop(gpointer data);

/// @brief Saat bindirme (overlay) etkin mi (SAVASAN_OVERLAY_CLOCK).
bool IsClockOverlayEnabled();
/// @brief Bindirme için biçimlendirilmiş güncel sunucu saati metnini üretir.
std::string CurrentServerTimeText();

/// @brief Hata/debug mesajına göre olası neden ve kontrol ipuçlarını yazdırır.
void PrintErrorHint(const char* err_msg, const char* dbg_msg);
/// @brief GStreamer bus mesajlarını izler (hata/uyarı/EOS) ve gerektiğinde döngüyü durdurur.
/// @param data @ref BusWatchContext işaretçisi.
gboolean BusWatch(GstBus* bus, GstMessage* msg, gpointer data);

/// @brief USB kamera yeniden bağlanma etkin mi (varsayılan açık; SAVASAN_USB_RECONNECT=0 kapatır).
bool UsbCameraReconnectEnabledByEnv();
/// @brief Bekleyen USB yeniden bağlanma zamanlayıcısını iptal eder.
void CancelUsbCameraReconnect(BusWatchContext* ctx);

/// @brief gst_parse_launch ile uyumsuz NVMM caps tırnaklarını düzelterek pipeline metnini normalize eder.
std::string NormalizePipelineForParse(const std::string& pipeline);

/// @brief Faz-1 test pipeline'ını (kaynak dalı + fakesink) oluşturur.
std::string BuildPhase1TestPipeline(const savasan::IngestConfig& cfg);

/// @brief OSD sink probe çalışma modu.
enum class OsdSinkProbeMode : int {
  kPhase2StyleAndMaybeClock = 0, ///< Faz-2: kutu stilini uygula + (varsa) saat bindir.
  kPhase3ClockOnly = 1,          ///< Faz-3: yalnızca saat bindir.
};

/// @brief FPS/gecikme ölçümü için akış probe durumu (hata ayıklama).
struct StreamProbeState {
  guint64 frame_count = 0;        ///< Toplam kare sayısı.
  guint64 window_frames = 0;      ///< Pencere içi kare sayısı.
  gint64 window_start_us = 0;     ///< Pencere başlangıcı (us).
  gint64 last_frame_us = 0;       ///< Son kare anı (us).
  double ema_frame_interval_ms = 0.0; ///< Kare aralığının üstel hareketli ortalaması (ms).
};

/// @brief OSD sink pad probe'u: kutu stilini ve/veya saat bindirmesini uygular.
GstPadProbeReturn OSDSinkTimestampProbe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);

#if SAVASAN_DEBUG
GstPadProbeReturn OSDSrcProbe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);
GstPadProbeReturn SinkSinkProbe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);
bool IsTargetSinkType(const char* type_name);
#endif

}  // namespace savasan::runners

#endif  // SAVASAN_RUNNERS_GST_APP_HELPERS_HPP_
