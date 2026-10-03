/**
 * @file alc_hub_file.hpp
 * @brief Alacakart telemetrisi + kilit durumunu AlpaguLink/RF için dosyaya yayınlar.
 *
 * Tek sahip C++ (AlcLinkBridge); tüketiciler dosyadan okur (ACM0 çakışması yok).
 *
 * Varsayılan yol: /tmp/savasan_alc_hub.env (SAVASAN_ALC_HUB_FILE)
 * Uplink (RF→Alacakart): /tmp/savasan_alc_uplink.bin (SAVASAN_ALC_UPLINK_FILE)
 */
#ifndef SAVASAN_RUNNERS_ALC_HUB_FILE_HPP_
#define SAVASAN_RUNNERS_ALC_HUB_FILE_HPP_

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "autopilot/alc_link_types.hpp"
#include "autopilot/ialc_link_bridge.hpp"

namespace savasan::runners {

inline constexpr const char* kDefaultAlcHubFilePath = "/tmp/savasan_alc_hub.env";
inline constexpr const char* kDefaultAlcUplinkFilePath = "/tmp/savasan_alc_uplink.bin";

struct AlcHubSnapshot {
  bool valid = false;
  double enlem = 0.0;
  double boylam = 0.0;
  float irtifa_m = 0.0f;
  float pitch_deg = 0.0f;
  float roll_deg = 0.0f;
  float yaw_deg = 0.0f;
  float gps_hiz_mps = 0.0f;
  float voltaj_v = 0.0f;
  int arac_modu = 0;
  int uydu_sayisi = 0;
  int lock = 0;
  int hedef_x = 0;  ///< Piksel (frame W ölçeği).
  int hedef_y = 0;
  int hedef_w = 0;
  int hedef_h = 0;

  /// @name Operatör nokta hedefi geri bildirimi (RF 0x33)
  /// Güdüm noktayı geofence süzgecinden geçirir; operatör kabul/red sonucunu
  /// ancak buradan öğrenebilir. AlpaguLink durum değişince tek paket basar.
  /// @{
  int goto_state = 0;   ///< @ref GotoState
  int goto_seq = 0;     ///< Hangi noktanın sonucu olduğunu eşleştirir.
  int goto_reason = 0;  ///< @ref GotoRejectReason
  /// @}
};

inline const char* AlcHubFilePath() {
  const char* path = std::getenv("SAVASAN_ALC_HUB_FILE");
  if (path == nullptr || path[0] == '\0') {
    return kDefaultAlcHubFilePath;
  }
  return path;
}

inline const char* AlcUplinkFilePath() {
  const char* path = std::getenv("SAVASAN_ALC_UPLINK_FILE");
  if (path == nullptr || path[0] == '\0') {
    return kDefaultAlcUplinkFilePath;
  }
  return path;
}

inline int HubFrameW() {
  const char* v = std::getenv("SAVASAN_COMPETITION_FRAME_W");
  if (v == nullptr || v[0] == '\0') {
    return 800;
  }
  const int w = std::atoi(v);
  return w > 0 ? w : 800;
}

inline int HubFrameH() {
  const char* v = std::getenv("SAVASAN_COMPETITION_FRAME_H");
  if (v == nullptr || v[0] == '\0') {
    return 600;
  }
  const int h = std::atoi(v);
  return h > 0 ? h : 600;
}

/// @brief Hub dosyasını atomik yazar (her çağrıda).
inline bool WriteAlcHubFile(const AlcHubSnapshot& s, const char* path) {
  if (path == nullptr || path[0] == '\0') {
    return false;
  }
  const std::string tmp = std::string(path) + ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc | std::ios::out);
    if (!out) {
      return false;
    }
    out << "valid=" << (s.valid ? 1 : 0) << '\n'
        << "enlem=" << s.enlem << '\n'
        << "boylam=" << s.boylam << '\n'
        << "irtifa_m=" << s.irtifa_m << '\n'
        << "pitch_deg=" << s.pitch_deg << '\n'
        << "roll_deg=" << s.roll_deg << '\n'
        << "yaw_deg=" << s.yaw_deg << '\n'
        << "gps_hiz_mps=" << s.gps_hiz_mps << '\n'
        << "voltaj_v=" << s.voltaj_v << '\n'
        << "arac_modu=" << s.arac_modu << '\n'
        << "uydu_sayisi=" << s.uydu_sayisi << '\n'
        << "lock=" << s.lock << '\n'
        << "hedef_x=" << s.hedef_x << '\n'
        << "hedef_y=" << s.hedef_y << '\n'
        << "hedef_w=" << s.hedef_w << '\n'
        << "hedef_h=" << s.hedef_h << '\n'
        << "goto_state=" << s.goto_state << '\n'
        << "goto_seq=" << s.goto_seq << '\n'
        << "goto_reason=" << s.goto_reason << '\n';
    out.flush();
    if (!out) {
      return false;
    }
  }
  if (std::rename(tmp.c_str(), path) != 0) {
    std::remove(tmp.c_str());
    return false;
  }
  return true;
}

/// @brief Nokta hedefi geri bildirimi (hub'a eklenir).
struct GotoFeedback {
  int state = 0;
  int seq = 0;
  int reason = 0;
};

/// @brief Telemetri + kilit hub'ını ~10 Hz ile yayınlar.
/// @param hedef_norm_x/y kilitliyken [0,1]; aksi halde yok sayılır.
inline bool PublishAlcHubToFile(const savasan::autopilot::TelemetrySnapshot& telem,
                                const bool lock_valid, const float hedef_norm_x = 0.0f,
                                const float hedef_norm_y = 0.0f,
                                const GotoFeedback& goto_fb = GotoFeedback{}) {
  static auto last_tp = std::chrono::steady_clock::time_point{};
  const auto now = std::chrono::steady_clock::now();
  if (last_tp.time_since_epoch().count() != 0 &&
      (now - last_tp) < std::chrono::milliseconds(100)) {
    return false;
  }
  last_tp = now;

  AlcHubSnapshot s{};
  s.valid = telem.valid;
  s.enlem = telem.enlem;
  s.boylam = telem.boylam;
  s.irtifa_m = telem.irtifa_m;
  s.pitch_deg = telem.pitch_deg;
  s.roll_deg = telem.roll_deg;
  s.yaw_deg = telem.yaw_deg;
  s.gps_hiz_mps = telem.gps_hiz_mps;
  s.voltaj_v = telem.voltaj_v;
  s.arac_modu = static_cast<int>(telem.arac_modu);
  s.uydu_sayisi = static_cast<int>(telem.uydu_sayisi);
  s.lock = lock_valid ? 1 : 0;
  s.goto_state = goto_fb.state;
  s.goto_seq = goto_fb.seq;
  s.goto_reason = goto_fb.reason;
  if (lock_valid) {
    const int fw = HubFrameW();
    const int fh = HubFrameH();
    s.hedef_x = static_cast<int>(std::lround(std::clamp(hedef_norm_x, 0.0f, 1.0f) * fw));
    s.hedef_y = static_cast<int>(std::lround(std::clamp(hedef_norm_y, 0.0f, 1.0f) * fh));
    // Bbox W/H henüz lock_metrics'te yok; yer tarafı 0 kabul eder.
    s.hedef_w = 0;
    s.hedef_h = 0;
  }
  return WriteAlcHubFile(s, AlcHubFilePath());
}

/// @brief RF uplink dosyası varsa köprüye ham bayt olarak iletir ve siler.
inline bool TryForwardAlcUplinkFile(savasan::autopilot::IAlcLinkBridge* bridge) {
  if (bridge == nullptr || !bridge->IsConnected()) {
    return false;
  }
  const char* path = AlcUplinkFilePath();
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }
  std::vector<uint8_t> buf((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
  in.close();
  std::remove(path);
  if (buf.empty() || buf.size() > 256) {
    return false;
  }
  return bridge->SendRawBytes(buf.data(), buf.size());
}

}  // namespace savasan::runners

#endif  // SAVASAN_RUNNERS_ALC_HUB_FILE_HPP_
