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

uint8_t ComputeXorChecksum(const std::vector<uint8_t>& data, const size_t begin,
                           const size_t end_exclusive) {
  uint8_t checksum = 0;
  for (size_t i = begin; i < end_exclusive; ++i) {
    checksum ^= data[i];
  }
  return checksum;
}

uint16_t ComputeCrc16Ccitt(const std::vector<uint8_t>& data, const size_t begin,
                           const size_t end_exclusive) {
  uint16_t crc = 0xFFFFU;
  for (size_t i = begin; i < end_exclusive; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8U;
    for (int bit = 0; bit < 8; ++bit) {
      if ((crc & 0x8000U) != 0U) {
        crc = static_cast<uint16_t>((crc << 1U) ^ 0x1021U);
      } else {
        crc = static_cast<uint16_t>(crc << 1U);
      }
    }
  }
  return crc;
}

bool ReadExactWithTimeout(const int fd, const size_t expected_size, std::vector<uint8_t>* out) {
  out->clear();
  out->reserve(expected_size);

  constexpr int kTimeoutMs = 500;
  constexpr size_t kChunkSize = 64;
  while (out->size() < expected_size) {
    struct pollfd pfd {};
    pfd.fd = fd;
    pfd.events = POLLIN;

    const int prc = ::poll(&pfd, 1, kTimeoutMs);
    if (prc <= 0) {
      return false;
    }
    if ((pfd.revents & POLLIN) == 0) {
      return false;
    }

    uint8_t buf[kChunkSize];
    const ssize_t n = ::read(fd, buf, sizeof(buf));
    if (n <= 0) {
      return false;
    }
    out->insert(out->end(), buf, buf + n);
  }

  if (out->size() > expected_size) {
    out->resize(expected_size);
  }
  return true;
}

bool ExpectNoData(const int fd) {
  struct pollfd pfd {};
  pfd.fd = fd;
  pfd.events = POLLIN;
  const int prc = ::poll(&pfd, 1, 100);
  return prc == 0;
}

void FlushAsyncTx(savasan::autopilot::AlcLinkBridge& bridge) {
  bridge.WaitTxIdle(std::chrono::milliseconds(500));
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
  cfg.normalize_coordinates = true;

  savasan::autopilot::AlcLinkBridge bridge(cfg);
  if (!bridge.Connect()) {
    ::close(master_fd);
    return 2;
  }

  // 1) Lock koordinat paketi.
  savasan::autopilot::LockCoordinates lock{};
  lock.x = 0.5f;
  lock.y = 0.25f;
  lock.valid = true;
  lock.track_id = 42U;
  if (!bridge.SendLockCoordinates(lock)) {
    bridge.Disconnect();
    ::close(master_fd);
    return 3;
  }
  FlushAsyncTx(bridge);

  std::vector<uint8_t> packet;
  if (!ReadExactWithTimeout(master_fd, 8, &packet)) {
    bridge.Disconnect();
    ::close(master_fd);
    return 4;
  }
  if (packet[0] != 0xAA || packet[1] != 0x55 || packet[2] != 0x01) {
    bridge.Disconnect();
    ::close(master_fd);
    return 5;
  }
  const uint16_t expected_x = static_cast<uint16_t>(std::lround(0.5f * 65535.0f));
  const uint16_t expected_y = static_cast<uint16_t>(std::lround(0.25f * 65535.0f));
  if (packet[3] != static_cast<uint8_t>((expected_x >> 8) & 0xFF) ||
      packet[4] != static_cast<uint8_t>(expected_x & 0xFF) ||
      packet[5] != static_cast<uint8_t>((expected_y >> 8) & 0xFF) ||
      packet[6] != static_cast<uint8_t>(expected_y & 0xFF)) {
    bridge.Disconnect();
    ::close(master_fd);
    return 6;
  }
  if (packet[7] != ComputeXorChecksum(packet, 2, 7)) {
    bridge.Disconnect();
    ::close(master_fd);
    return 7;
  }

  // 2) Invalid lock paket göndermez.
  savasan::autopilot::LockCoordinates invalid{};
  invalid.x = 0.8f;
  invalid.y = 0.2f;
  invalid.valid = false;
  if (bridge.SendLockCoordinates(invalid)) {
    bridge.Disconnect();
    ::close(master_fd);
    return 8;
  }
  if (!ExpectNoData(master_fd)) {
    bridge.Disconnect();
    ::close(master_fd);
    return 9;
  }

  // 3) No-lock paketi.
  if (!bridge.SendNoLock()) {
    bridge.Disconnect();
    ::close(master_fd);
    return 10;
  }
  FlushAsyncTx(bridge);
  if (!ReadExactWithTimeout(master_fd, 4, &packet)) {
    bridge.Disconnect();
    ::close(master_fd);
    return 11;
  }
  if (packet[0] != 0xAA || packet[1] != 0x55 || packet[2] != 0x00 ||
      packet[3] != ComputeXorChecksum(packet, 2, 3)) {
    bridge.Disconnect();
    ::close(master_fd);
    return 12;
  }

  // 4) Evasion komutu.
  constexpr uint8_t kEvasionType = 0x7A;
  if (!bridge.SendEvasionCommand(kEvasionType)) {
    bridge.Disconnect();
    ::close(master_fd);
    return 13;
  }
  FlushAsyncTx(bridge);
  if (!ReadExactWithTimeout(master_fd, 5, &packet)) {
    bridge.Disconnect();
    ::close(master_fd);
    return 14;
  }
  if (packet[0] != 0xAA || packet[1] != 0x55 || packet[2] != 0x02 || packet[3] != kEvasionType ||
      packet[4] != ComputeXorChecksum(packet, 2, 4)) {
    bridge.Disconnect();
    ::close(master_fd);
    return 15;
  }

  bridge.Disconnect();
  ::close(master_fd);
  master_fd = -1;

  // 5) No-lock legacy checksum modu (0xFF).
  if (::openpty(&master_fd, &slave_fd, slave_name, nullptr, nullptr) != 0) {
    return 21;
  }
  ::close(slave_fd);
  slave_fd = -1;

  cfg.lock_packet_version = 1;
  cfg.no_lock_checksum_mode = 1;
  savasan::autopilot::AlcLinkBridge bridge_legacy_nolock(cfg);
  if (!bridge_legacy_nolock.Connect()) {
    ::close(master_fd);
    return 22;
  }
  if (!bridge_legacy_nolock.SendNoLock()) {
    bridge_legacy_nolock.Disconnect();
    ::close(master_fd);
    return 23;
  }
  FlushAsyncTx(bridge_legacy_nolock);
  if (!ReadExactWithTimeout(master_fd, 4, &packet)) {
    bridge_legacy_nolock.Disconnect();
    ::close(master_fd);
    return 24;
  }
  if (packet[0] != 0xAA || packet[1] != 0x55 || packet[2] != 0x00 || packet[3] != 0xFF) {
    bridge_legacy_nolock.Disconnect();
    ::close(master_fd);
    return 25;
  }
  bridge_legacy_nolock.Disconnect();
  ::close(master_fd);
  master_fd = -1;

  // 7) Lock v2 paketi: track_id + sequence + timestamp + CRC16.
  if (::openpty(&master_fd, &slave_fd, slave_name, nullptr, nullptr) != 0) {
    return 26;
  }
  ::close(slave_fd);
  slave_fd = -1;

  cfg.lock_packet_version = 2;
  cfg.no_lock_checksum_mode = 0;
  savasan::autopilot::AlcLinkBridge bridge_v2(cfg);
  if (!bridge_v2.Connect()) {
    ::close(master_fd);
    return 27;
  }

  savasan::autopilot::LockCoordinates lock_v2{};
  lock_v2.x = 0.5f;
  lock_v2.y = 0.25f;
  lock_v2.valid = true;
  lock_v2.track_id = 0x11223344U;
  if (!bridge_v2.SendLockCoordinates(lock_v2)) {
    bridge_v2.Disconnect();
    ::close(master_fd);
    return 28;
  }
  FlushAsyncTx(bridge_v2);
  if (!ReadExactWithTimeout(master_fd, 20, &packet)) {
    bridge_v2.Disconnect();
    ::close(master_fd);
    return 29;
  }
  if (packet[0] != 0xAA || packet[1] != 0x55 || packet[2] != 0x11 || packet[3] != 0x01) {
    bridge_v2.Disconnect();
    ::close(master_fd);
    return 30;
  }
  if (packet[4] != static_cast<uint8_t>((expected_x >> 8) & 0xFF) ||
      packet[5] != static_cast<uint8_t>(expected_x & 0xFF) ||
      packet[6] != static_cast<uint8_t>((expected_y >> 8) & 0xFF) ||
      packet[7] != static_cast<uint8_t>(expected_y & 0xFF)) {
    bridge_v2.Disconnect();
    ::close(master_fd);
    return 31;
  }
  if (packet[8] != 0x11 || packet[9] != 0x22 || packet[10] != 0x33 || packet[11] != 0x44) {
    bridge_v2.Disconnect();
    ::close(master_fd);
    return 32;
  }
  const uint16_t seq0 =
      static_cast<uint16_t>((static_cast<uint16_t>(packet[12]) << 8U) | packet[13]);
  if (seq0 != 0U) {
    bridge_v2.Disconnect();
    ::close(master_fd);
    return 33;
  }
  const uint16_t crc0 = ComputeCrc16Ccitt(packet, 2, 18);
  const uint16_t crc0_got =
      static_cast<uint16_t>((static_cast<uint16_t>(packet[18]) << 8U) | packet[19]);
  if (crc0 != crc0_got) {
    bridge_v2.Disconnect();
    ::close(master_fd);
    return 34;
  }

  lock_v2.track_id = 0x01020304U;
  if (!bridge_v2.SendLockCoordinates(lock_v2)) {
    bridge_v2.Disconnect();
    ::close(master_fd);
    return 35;
  }
  FlushAsyncTx(bridge_v2);
  if (!ReadExactWithTimeout(master_fd, 20, &packet)) {
    bridge_v2.Disconnect();
    ::close(master_fd);
    return 36;
  }
  const uint16_t seq1 =
      static_cast<uint16_t>((static_cast<uint16_t>(packet[12]) << 8U) | packet[13]);
  if (seq1 != 1U) {
    bridge_v2.Disconnect();
    ::close(master_fd);
    return 37;
  }
  const uint16_t crc1 = ComputeCrc16Ccitt(packet, 2, 18);
  const uint16_t crc1_got =
      static_cast<uint16_t>((static_cast<uint16_t>(packet[18]) << 8U) | packet[19]);
  if (crc1 != crc1_got) {
    bridge_v2.Disconnect();
    ::close(master_fd);
    return 38;
  }

  bridge_v2.Disconnect();
  ::close(master_fd);
  master_fd = -1;

  return 0;
}
