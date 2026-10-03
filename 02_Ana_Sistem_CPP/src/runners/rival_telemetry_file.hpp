/**
 * @file rival_telemetry_file.hpp
 * @brief RF (AlpaguLink) kaynaklı rakip / HSS / sınır / nokta hedefi IPC dosyaları.
 *
 * Varsayılanlar:
 *   SAVASAN_RIVAL_POOL_FILE=/tmp/savasan_rival_pool.env
 *   SAVASAN_HSS_RF_FILE=/tmp/savasan_hss_rf.env
 *   SAVASAN_BOUNDARY_RF_FILE=/tmp/savasan_boundary_rf.env
 *   SAVASAN_GOTO_FILE=/tmp/savasan_goto.env
 *
 * Yazım: AlpaguLink (atomik .tmp + rename). Okuma: C++ GuidanceControlTick.
 */
#ifndef SAVASAN_RUNNERS_RIVAL_TELEMETRY_FILE_HPP_
#define SAVASAN_RUNNERS_RIVAL_TELEMETRY_FILE_HPP_

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "competition/siha_types.hpp"

namespace savasan::runners {

inline constexpr const char* kDefaultRivalPoolFilePath = "/tmp/savasan_rival_pool.env";
inline constexpr const char* kDefaultHssRfFilePath = "/tmp/savasan_hss_rf.env";
inline constexpr const char* kDefaultBoundaryRfFilePath = "/tmp/savasan_boundary_rf.env";
inline constexpr const char* kDefaultGotoFilePath = "/tmp/savasan_goto.env";

/// @brief Operatörün yer istasyonundan gönderdiği varış noktası (RF 0x2A).
///
/// Şartname §6.1.7 bu komutu açıkça otonomiyi bozmayan komutlar arasında sayar
/// ("varış noktası tanımlama"), yani manuel moda geçiş olarak işlenmez. Nokta
/// karta yazılmaz; Jetson güdümü kendi uçurur, böylece Alacakart görev listesi
/// protokolüne (dolayısıyla ACM0'a) hiç dokunulmaz.
struct GotoTargetSnapshot {
  bool valid = false;
  int seq = 0;  ///< Operatör her yeni noktada artırır; ACK eşleştirmesi için.
  double lat_deg = 0.0;
  double lon_deg = 0.0;
  bool has_alt = false;  ///< 10 baytlık paket geldiyse irtifa da istendi.
  float alt_m = 0.0f;
};

/// @brief Nokta hedefinin güdümdeki durumu; hub üzerinden operatöre döner.
enum class GotoState : uint8_t {
  kIdle = 0,      ///< Aktif nokta yok.
  kActive = 1,    ///< Kabul edildi, uçuluyor.
  kArrived = 2,   ///< Varış yarıçapına girildi, nokta düştü.
  kRejected = 3,  ///< Süzgeçten geçmedi; sebep @ref GotoRejectReason.
};

/// @brief Nokta neden reddedildi? Operatör arayüzünde gösterilir.
enum class GotoRejectReason : uint8_t {
  kNone = 0,
  kOutsideBoundary = 1,  ///< Saha sınırının dışında ya da kenar payı içinde.
  kInsideHss = 2,        ///< HSS dairesi + emniyet payı içinde.
  kRouteBlocked = 3,     ///< Nokta güvenli ama oraya giden düz yol kesiyor.
  kNoBoundaryData = 4,   ///< Sınır poligonu henüz gelmedi, doğrulanamıyor.
  kNoGpsFix = 5,         ///< Kendi konumumuz yok.
};

/// @brief Tek rakip anlık görüntüsü (RF 0x20 alanları).
struct RivalTargetSnapshot {
  bool valid = false;
  int target_id = 0;
  double lat_deg = 0.0;
  double lon_deg = 0.0;
  float alt_m = 0.0f;
  float spd_mps = 0.0f;
  float pitch_deg = 0.0f;
  float roll_deg = 0.0f;
  float hdg_deg = 0.0f;
  int time_diff_ms = 0;
  std::chrono::steady_clock::time_point recv_tp{};
};

struct RivalPoolSnapshot {
  std::vector<RivalTargetSnapshot> targets;
  std::chrono::steady_clock::time_point last_recv_tp{};
};

struct BoundaryCorner {
  double lat_deg = 0.0;
  double lon_deg = 0.0;
};

struct BoundarySnapshot {
  std::vector<BoundaryCorner> corners;
  std::chrono::steady_clock::time_point last_recv_tp{};
};

inline const char* RivalPoolFilePath() {
  const char* p = std::getenv("SAVASAN_RIVAL_POOL_FILE");
  return (p != nullptr && p[0] != '\0') ? p : kDefaultRivalPoolFilePath;
}

inline const char* HssRfFilePath() {
  const char* p = std::getenv("SAVASAN_HSS_RF_FILE");
  return (p != nullptr && p[0] != '\0') ? p : kDefaultHssRfFilePath;
}

inline const char* BoundaryRfFilePath() {
  const char* p = std::getenv("SAVASAN_BOUNDARY_RF_FILE");
  return (p != nullptr && p[0] != '\0') ? p : kDefaultBoundaryRfFilePath;
}

inline const char* GotoFilePath() {
  const char* p = std::getenv("SAVASAN_GOTO_FILE");
  return (p != nullptr && p[0] != '\0') ? p : kDefaultGotoFilePath;
}

namespace detail {

inline std::unordered_map<std::string, std::string> ParseEnvFile(const char* path) {
  std::unordered_map<std::string, std::string> map;
  if (path == nullptr || path[0] == '\0') {
    return map;
  }
  std::ifstream in(path);
  if (!in) {
    return map;
  }
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    const auto eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    map[line.substr(0, eq)] = line.substr(eq + 1);
  }
  return map;
}

inline double GetD(const std::unordered_map<std::string, std::string>& m, const std::string& k,
                   const double fallback = 0.0) {
  const auto it = m.find(k);
  if (it == m.end() || it->second.empty()) {
    return fallback;
  }
  return std::strtod(it->second.c_str(), nullptr);
}

inline int GetI(const std::unordered_map<std::string, std::string>& m, const std::string& k,
                const int fallback = 0) {
  const auto it = m.find(k);
  if (it == m.end() || it->second.empty()) {
    return fallback;
  }
  return std::atoi(it->second.c_str());
}

inline bool AtomicWriteText(const char* path, const std::string& body) {
  if (path == nullptr || path[0] == '\0') {
    return false;
  }
  const std::string tmp = std::string(path) + ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    if (!out) {
      return false;
    }
    out << body;
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

}  // namespace detail

/// @brief Rakip havuzunu dosyadan okur (yoksa boş).
inline RivalPoolSnapshot ReadRivalPoolFile(const char* path = nullptr) {
  RivalPoolSnapshot out{};
  const char* p = (path != nullptr) ? path : RivalPoolFilePath();
  const auto m = detail::ParseEnvFile(p);
  if (m.empty()) {
    return out;
  }
  const int count = detail::GetI(m, "count", 0);
  const auto now = std::chrono::steady_clock::now();
  out.last_recv_tp = now;
  for (int i = 0; i < count && i < 32; ++i) {
    const std::string idx = std::to_string(i);
    RivalTargetSnapshot t{};
    t.valid = detail::GetI(m, "valid" + idx, 0) != 0;
    if (!t.valid) {
      continue;
    }
    t.target_id = detail::GetI(m, "id" + idx, 0);
    t.lat_deg = detail::GetD(m, "lat" + idx);
    t.lon_deg = detail::GetD(m, "lon" + idx);
    t.alt_m = static_cast<float>(detail::GetD(m, "alt" + idx));
    t.spd_mps = static_cast<float>(detail::GetD(m, "spd" + idx));
    t.pitch_deg = static_cast<float>(detail::GetD(m, "pitch" + idx));
    t.roll_deg = static_cast<float>(detail::GetD(m, "roll" + idx));
    t.hdg_deg = static_cast<float>(detail::GetD(m, "hdg" + idx));
    t.time_diff_ms = detail::GetI(m, "tdiff" + idx, 0);
    t.recv_tp = now;
    out.targets.push_back(t);
  }
  return out;
}

/// @brief HSS listesini RF dosyasından okur (HTTP HssCache ile aynı tip).
inline std::vector<savasan::competition::HssKoordinatBilgisi> ReadHssRfFile(
    const char* path = nullptr) {
  std::vector<savasan::competition::HssKoordinatBilgisi> out;
  const char* p = (path != nullptr) ? path : HssRfFilePath();
  const auto m = detail::ParseEnvFile(p);
  const int count = detail::GetI(m, "count", 0);
  for (int i = 0; i < count && i < 64; ++i) {
    const std::string idx = std::to_string(i);
    savasan::competition::HssKoordinatBilgisi z{};
    z.id = detail::GetI(m, "id" + idx, -1);
    z.hss_enlem = detail::GetD(m, "lat" + idx);
    z.hss_boylam = detail::GetD(m, "lon" + idx);
    z.hss_yaricap = static_cast<float>(detail::GetD(m, "rad" + idx));
    if (z.id >= 0 && z.hss_yaricap > 0.0f) {
      out.push_back(z);
    }
  }
  return out;
}

inline BoundarySnapshot ReadBoundaryRfFile(const char* path = nullptr) {
  BoundarySnapshot out{};
  const char* p = (path != nullptr) ? path : BoundaryRfFilePath();
  const auto m = detail::ParseEnvFile(p);
  const int count = detail::GetI(m, "count", 0);
  out.last_recv_tp = std::chrono::steady_clock::now();
  for (int i = 0; i < count && i < 64; ++i) {
    const std::string idx = std::to_string(i);
    BoundaryCorner c{};
    c.lat_deg = detail::GetD(m, "lat" + idx);
    c.lon_deg = detail::GetD(m, "lon" + idx);
    out.corners.push_back(c);
  }
  return out;
}

/// @brief Operatör nokta hedefini dosyadan okur (yoksa geçersiz).
inline GotoTargetSnapshot ReadGotoFile(const char* path = nullptr) {
  GotoTargetSnapshot out{};
  const char* p = (path != nullptr) ? path : GotoFilePath();
  const auto m = detail::ParseEnvFile(p);
  if (m.empty() || detail::GetI(m, "valid", 0) == 0) {
    return out;
  }
  out.valid = true;
  out.seq = detail::GetI(m, "seq", 0);
  out.lat_deg = detail::GetD(m, "lat");
  out.lon_deg = detail::GetD(m, "lon");
  out.has_alt = detail::GetI(m, "has_alt", 0) != 0;
  out.alt_m = static_cast<float>(detail::GetD(m, "alt_m"));
  return out;
}

/// @brief Test / yazıcı yardımcı (AlpaguLink Python tarafı kendi yazar).
inline bool WriteGotoFile(const GotoTargetSnapshot& g, const char* path = nullptr) {
  const char* p = (path != nullptr) ? path : GotoFilePath();
  std::ostringstream oss;
  oss << "valid=" << (g.valid ? 1 : 0) << '\n'
      << "seq=" << g.seq << '\n'
      << "lat=" << std::setprecision(12) << g.lat_deg << '\n'
      << "lon=" << std::setprecision(12) << g.lon_deg << '\n'
      << "has_alt=" << (g.has_alt ? 1 : 0) << '\n'
      << "alt_m=" << g.alt_m << '\n';
  return detail::AtomicWriteText(p, oss.str());
}

/// @brief Test / yazıcı yardımcı (AlpaguLink Python tarafı kendi yazar).
inline bool WriteRivalPoolFile(const RivalPoolSnapshot& pool, const char* path = nullptr) {
  const char* p = (path != nullptr) ? path : RivalPoolFilePath();
  std::ostringstream oss;
  oss << "count=" << pool.targets.size() << '\n';
  for (size_t i = 0; i < pool.targets.size(); ++i) {
    const auto& t = pool.targets[i];
    oss << "valid" << i << '=' << (t.valid ? 1 : 0) << '\n'
        << "id" << i << '=' << t.target_id << '\n'
        << "lat" << i << '=' << t.lat_deg << '\n'
        << "lon" << i << '=' << t.lon_deg << '\n'
        << "alt" << i << '=' << t.alt_m << '\n'
        << "spd" << i << '=' << t.spd_mps << '\n'
        << "pitch" << i << '=' << t.pitch_deg << '\n'
        << "roll" << i << '=' << t.roll_deg << '\n'
        << "hdg" << i << '=' << t.hdg_deg << '\n'
        << "tdiff" << i << '=' << t.time_diff_ms << '\n';
  }
  return detail::AtomicWriteText(p, oss.str());
}

}  // namespace savasan::runners

#endif  // SAVASAN_RUNNERS_RIVAL_TELEMETRY_FILE_HPP_
