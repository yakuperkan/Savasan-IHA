/**
 * @file main.cpp
 * @brief Uygulama giriş noktası. Terminate/exception koruyucularını kurar,
 *        DeepStream gecikme ölçüm ortam değişkenlerini ayarlar, komut satırı
 *        argümanlarını ayrıştırır ve seçilen fazı sevk eder.
 * @details Komut satırı ve ortam değişkeni belgelendirmesi aşağıdaki yorum
 *          bloğundadır.
 */
// Kullanım:
//   ./savasan_iha [phase1] [usb]               -> Faz1: sadece kamera + fakesink (AI/pencere YOK)
//   ./savasan_iha phase2 [usb] [display]       -> Faz2 nvinfer + OSD
//   ./savasan_iha phase3 [usb] [display]       -> Faz3 nvinfer + nvtracker + probe
//   ./savasan_iha phase4 [usb] [hybrid|baseline] [display]
//   ./savasan_iha phase5 [usb] [hybrid|baseline] [display]
//   Faz5 ortam: SAVASAN_ALC_DEVICE=/dev/ttyACM0 SAVASAN_ALC_BAUD=115200
//               SAVASAN_ALC_HEARTBEAT=1 (isteğe bağlı)
//               SAVASAN_EVASION_ENABLE=1 (isteğe bağlı; kaçış GLib zamanlayıcıda)
//               SAVASAN_ALC_DISABLE=1 — seri yokken phase5 pipeline'ı alc olmadan dener (Faz4 ile aynı hat)
//   display: ekran sink (varsayılan nv3dsink); yoksa fakesink
//   SAVASAN_DISPLAY=display ortamda da nv3dsink (systemd ile argv boş kalsa bile)
// Ortam:
//   DISPLAY=:0  (Jetson HDMI masaüstü; SSH ile laptop ekranında pencere açılmaz)
//   SAVASAN_DISPLAY_SINK debug log için okunur; display zinciri kalıcı olarak nv3dsink'tir.
//   SAVASAN_RUN_SECONDS=60        (phase2/phase3 süre; 5..600, yoksa 30; 0=süresiz/servis modu)
//   SAVASAN_V4L2_DEVICE=/dev/video0 (usb modu; varsayılan /dev/video0)
//   SAVASAN_INGEST_FPS=60|30       (kamera ingest FPS; varsayılan 60)
//   SAVASAN_PGI_CONFIG=...          (nvinfer config; yoksa config/deepstream/config_infer_primary.txt)
//   SAVASAN_TRACKER_CONFIG=...      (nvtracker yml; yoksa config/deepstream/tracker_config.yml)
//   SAVASAN_TRACKER_LIB=...         (tracker .so; yoksa /opt/.../libnvds_nvmultiobjecttracker.so)
//   SAVASAN_DEBUG_LOG=...           (debug log yolu; yoksa /tmp/savasan_debug.log)
//   SAVASAN_LOCK_MODE=hybrid|baseline (phase4/phase5; argv'de hybrid/baseline yoksa kullanılır)
//   SAVASAN_TRACKER_PROFILE=default|calm|aggressive|auto
//   SAVASAN_TRACKER_AUTO_RESTART=0|1 (auto profil için kontrollü yeniden başlatma)
//   SAVASAN_AUTO_AGGRESSIVE_SPEED_MPS, SAVASAN_AUTO_CALM_SPEED_MPS
//   SAVASAN_AUTO_AGGRESSIVE_JITTER, SAVASAN_AUTO_CALM_JITTER
//   SAVASAN_AUTO_RESTART_MIN_DWELL_SEC, SAVASAN_AUTO_RESTART_COOLDOWN_SEC
//   SAVASAN_AUTO_PRECHECK_MAX_JITTER, SAVASAN_AUTO_PRECHECK_MAX_COMM_FAILS
//   SAVASAN_AUTO_POSTCHECK_TIMEOUT_SEC, SAVASAN_AUTO_MAX_RESTARTS
//   SAVASAN_AUTO_MAX_CONSECUTIVE_FAILS, SAVASAN_AUTO_MAX_FAILS_IN_WINDOW
//   SAVASAN_AUTO_FAIL_WINDOW_SEC
//   SAVASAN_UDP_ENABLE=1 — OSD sonrası RTP+UDP (kapalı: 0 veya tanımlı değil)
//   SAVASAN_UDP_HOST / SAVASAN_UDP_PORT (ENABLE=1 iken gerekli)
//   SAVASAN_UDP_CODEC=h264|h265 (varsayılan h264), SAVASAN_UDP_BITRATE=6000000 (bps)
//   SAVASAN_GUIDANCE_MODE=image_world|off
//   SAVASAN_CONTROL_HZ=20
//   SAVASAN_SETPOINT_TX_ENABLE=0|1 (0=yalnız dry-run log)
//   SAVASAN_TELEMETRY_CSV=/path.csv — OSD telemetrisini CSV'ye yaz (~1 Hz; gecikme doğrulama)

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <typeinfo>

#include "app/phase_dispatch.hpp"
#include "app/startup_config.hpp"
#include "common/log.hpp"
#include "runners/phase_runners.hpp"

namespace {

/// @brief Sink türünü startup özeti için metne çevirir.
const char* SinkTypeLabel(const savasan::deepstream::SinkType sink) {
  switch (sink) {
    case savasan::deepstream::SinkType::kDisplay:
      return "display";
    case savasan::deepstream::SinkType::kFake:
    default:
      return "fakesink";
  }
}

/// @brief Hibrit bayrağını kilit modu etiketine çevirir.
const char* LockModeLabel(const bool hybrid) { return hybrid ? "hybrid" : "baseline"; }

/// @brief Uçuş sonrası inceleme için kritik başlangıç ayarlarını tek satırda loglar.
void LogStartupConfig(const savasan::app::StartupConfig& cfg) {
  const std::string run_sec =
      cfg.run_seconds == 0 ? "unlimited" : std::to_string(cfg.run_seconds);
  const std::string msg = std::string("phase=") + std::to_string(cfg.phase) + " lock_mode=" +
                          LockModeLabel(cfg.phase4_hybrid) + " sink=" + SinkTypeLabel(cfg.sink) +
                          " tracker_profile=" + cfg.tracker_profile +
                          " run_seconds=" + run_sec + " v4l2=" + cfg.ingest.v4l2_device +
                          " fps=" + std::to_string(cfg.ingest.fps) + " ingest=" +
                          std::to_string(cfg.ingest.width) + "x" +
                          std::to_string(cfg.ingest.height);
  savasan::common::Log(savasan::common::LogLevel::kInfo, "Main", msg);
}

/// @brief std::terminate yakalayıcısını kurar; aktif istisnayı yeniden fırlatıp
///        ölümcül (Fatal) log yazar ve süreci güvenli biçimde sonlandırır.
void InstallTerminateHandler() {
  std::set_terminate([]() {
    try {
      const std::exception_ptr ep = std::current_exception();
      if (ep != nullptr) {
        std::rethrow_exception(ep);
      }
      savasan::common::Log(savasan::common::LogLevel::kFatal, "Main",
                           "Unhandled terminate without active exception");
    } catch (const std::exception& e) {
      savasan::common::Log(savasan::common::LogLevel::kFatal, "Main",
                           std::string("Unhandled exception: ") + typeid(e).name() +
                               " what=" + e.what());
    } catch (...) {
      savasan::common::Log(savasan::common::LogLevel::kFatal, "Main",
                           "Unhandled non-std exception");
    }
    std::abort();
  });
}

}  // namespace

/// @brief Uygulama giriş noktası.
/// @param argc Argüman sayısı.
/// @param argv Argüman dizisi.
/// @return Çalıştırma başarılıysa 0; ayrıştırma hatasında 1, üst seviye
///         istisnalarda 2 (std) veya 3 (std dışı).
int main(int argc, char** argv) {
  InstallTerminateHandler();
  try {
  if (std::getenv("NVDS_ENABLE_LATENCY_MEASUREMENT") == nullptr) {
    ::setenv("NVDS_ENABLE_LATENCY_MEASUREMENT", "1", 1);
  }
  if (std::getenv("NVDS_ENABLE_COMPONENT_LATENCY_MEASUREMENT") == nullptr) {
    ::setenv("NVDS_ENABLE_COMPONENT_LATENCY_MEASUREMENT", "1", 1);
  }

  savasan::app::StartupConfig startup_cfg;
  std::string parse_error;
  if (!savasan::app::ParseStartupConfig(argc, argv, &startup_cfg, &parse_error)) {
    savasan::common::Log(savasan::common::LogLevel::kCritical, "Main", parse_error);
    std::cerr << parse_error << '\n';
    return 1;
  }
  LogStartupConfig(startup_cfg);

  savasan::app::PhaseDispatchRunners runners{};
  runners.run_phase1 = [](savasan::IngestConfig cfg) { return savasan::runners::RunPhase1(cfg, 5); };
  runners.run_phase2 = savasan::runners::RunPhase2;
  runners.run_phase3 = [](savasan::IngestConfig cfg, const std::string& pgi_config,
                          const std::string& tracker_config, const std::string& ll_lib,
                          savasan::deepstream::SinkType sink, int run_seconds,
                          savasan::tracking::LockMode lock_mode) {
    return savasan::runners::RunPhase3(cfg, pgi_config, tracker_config, ll_lib, sink, run_seconds,
                                       lock_mode);
  };
  runners.run_phase5 = savasan::runners::RunPhase5WithAlc;
  return savasan::app::ExecutePhase(startup_cfg, runners);
  } catch (const std::exception& e) {
    savasan::common::Log(savasan::common::LogLevel::kCritical, "Main",
                         std::string("Top-level exception: ") + e.what());
    return 2;
  } catch (...) {
    savasan::common::Log(savasan::common::LogLevel::kCritical, "Main",
                         "Top-level non-std exception");
    return 3;
  }
}
