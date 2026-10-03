#include "autopilot/alc_link_bridge.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <poll.h>
#include <pty.h>
#include <unistd.h>
#include <vector>

namespace {

bool WriteAll(const int fd, const uint8_t* data, const size_t len) {
  size_t off = 0;
  while (off < len) {
    const ssize_t n = ::write(fd, data + off, len - off);
    if (n <= 0) {
      return false;
    }
    off += static_cast<size_t>(n);
  }
  return true;
}

bool WaitReadable(const int fd, const int timeout_ms) {
  struct pollfd pfd {};
  pfd.fd = fd;
  pfd.events = POLLIN;
  const int rc = ::poll(&pfd, 1, timeout_ms);
  return rc > 0 && (pfd.revents & POLLIN) != 0;
}

// ALC_LINK V2.0.1 32 baytlik 0x54...0x2a telemetri paketi (little-endian).
std::array<uint8_t, 32> BuildTelemetryFrame() {
  auto put_u16le = [](std::array<uint8_t, 32>& f, size_t i, uint16_t v) {
    f[i] = static_cast<uint8_t>(v & 0xFF);
    f[i + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
  };
  auto put_s32le = [](std::array<uint8_t, 32>& f, size_t i, int32_t v) {
    const uint32_t u = static_cast<uint32_t>(v);
    f[i] = static_cast<uint8_t>(u & 0xFF);
    f[i + 1] = static_cast<uint8_t>((u >> 8) & 0xFF);
    f[i + 2] = static_cast<uint8_t>((u >> 16) & 0xFF);
    f[i + 3] = static_cast<uint8_t>((u >> 24) & 0xFF);
  };

  std::array<uint8_t, 32> frame{};
  frame[0] = 0x54;            // baslik
  frame[1] = 2;               // arac_modu = otonom
  frame[2] = 0;               // sistem_durum
  frame[3] = 1;               // arac_id
  put_s32le(frame, 4, 41508775);   // enlem = 41.508775
  put_s32le(frame, 8, 36118335);   // boylam = 36.118335
  frame[12] = 12;             // uydu sayisi
  frame[13] = 36;             // gps hiz = 36 km/h -> 10 m/s
  put_u16le(frame, 14, 210);  // pusula (yaw) = 210
  frame[16] = 0;              // kordinat sayisi
  frame[17] = 0;              // otonom gorev sirasi
  frame[18] = 1;              // roll isaret = negatif
  frame[19] = 30;             // roll deger -> -30
  frame[20] = 0;              // pitch isaret = pozitif
  frame[21] = 7;              // pitch deger -> 7
  frame[22] = 0;              // secim biti = 0 (yukseklik/voltaj/akim/harc)
  put_u16le(frame, 23, 380);  // yukseklik 38.0 m
  put_u16le(frame, 25, 1250); // voltaj 12.50 V
  put_u16le(frame, 27, 50);   // akim 5.0 A
  put_u16le(frame, 29, 100);  // harcanan akim 100 mAh
  frame[31] = 0x2a;           // bitis '*'
  return frame;
}

}  // namespace

int main() {
  int master_fd = -1;
  int slave_fd = -1;
  char slave_name[128];
  std::memset(slave_name, 0, sizeof(slave_name));

  if (::openpty(&master_fd, &slave_fd, slave_name, nullptr, nullptr) != 0) {
    return 1;
  }
  ::close(slave_fd);
  slave_fd = -1;

  savasan::autopilot::AlcLinkBridge::Config cfg{};
  cfg.device_path = slave_name;
  cfg.baud_rate = 115200;

  savasan::autopilot::AlcLinkBridge bridge(cfg);
  if (!bridge.Connect()) {
    ::close(master_fd);
    return 2;
  }

  // 1) Telemetry frame parse: RX veri master'dan yazılır, TX komutu DrainAndParseRx tetikler.
  const auto frame = BuildTelemetryFrame();
  if (!WriteAll(master_fd, frame.data(), frame.size())) {
    bridge.Disconnect();
    ::close(master_fd);
    return 3;
  }

  if (!bridge.SendNoLock()) {
    bridge.Disconnect();
    ::close(master_fd);
    return 4;
  }
  bridge.WaitTxIdle(std::chrono::milliseconds(500));

  // TX paketi master'a düşmeli (4 byte no-lock).
  if (!WaitReadable(master_fd, 500)) {
    bridge.Disconnect();
    ::close(master_fd);
    return 5;
  }
  uint8_t tx_buf[8];
  const ssize_t tx_n = ::read(master_fd, tx_buf, sizeof(tx_buf));
  if (tx_n < 4) {
    bridge.Disconnect();
    ::close(master_fd);
    return 6;
  }

  const auto snapshot = bridge.GetTelemetrySnapshot();
  if (!snapshot.valid) {
    bridge.Disconnect();
    ::close(master_fd);
    return 7;
  }
  const auto near_eq = [](double a, double b, double eps) { return std::fabs(a - b) < eps; };
  if (!near_eq(snapshot.enlem, 41.508775, 1e-4) || !near_eq(snapshot.boylam, 36.118335, 1e-4) ||
      !near_eq(snapshot.irtifa_m, 38.0, 1e-3) || !near_eq(snapshot.pitch_deg, 7.0, 1e-3) ||
      !near_eq(snapshot.roll_deg, -30.0, 1e-3) || !near_eq(snapshot.yaw_deg, 210.0, 1e-3) ||
      !near_eq(snapshot.gps_hiz_mps, 10.0, 1e-3) || snapshot.uydu_sayisi != 12 ||
      !near_eq(snapshot.voltaj_v, 12.5, 1e-3)) {
    bridge.Disconnect();
    ::close(master_fd);
    return 8;
  }

  // 2) Hat kopması sonrası write fail -> bağlantı düşmeli (TX worker kuyruğu boşalana kadar bekle).
  ::close(master_fd);
  master_fd = -1;

  (void)bridge.SendNoLock();
  bridge.WaitTxIdle(std::chrono::milliseconds(500));
  if (bridge.IsConnected()) {
    bridge.Disconnect();
    return 9;
  }

  bridge.Disconnect();
  return 0;
}
