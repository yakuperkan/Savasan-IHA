#ifndef SAVASAN_TESTS_MOCK_ALC_LINK_BRIDGE_HPP_
#define SAVASAN_TESTS_MOCK_ALC_LINK_BRIDGE_HPP_

#include <string>

#include "autopilot/ialc_link_bridge.hpp"

namespace savasan::autopilot {

class MockAlcLinkBridge final : public IAlcLinkBridge {
 public:
  bool connect_ok = true;
  bool connected = true;
  int lock_count = 0;
  int no_lock_count = 0;
  int evasion_count = 0;
  int seyir_count = 0;
  SeyirModePacket last_seyir{};

  bool Connect() override {
    connected = connect_ok;
    return connect_ok;
  }

  void Disconnect() override { connected = false; }
  bool IsConnected() const override { return connected; }

  bool SendLockCoordinates(const LockCoordinates&) override {
    ++lock_count;
    return true;
  }
  bool SendNoLock() override {
    ++no_lock_count;
    return true;
  }
  bool SendEvasionCommand(uint8_t) override {
    ++evasion_count;
    return true;
  }
  bool SendSeyirModeCommand(const SeyirModePacket& pkt, bool) override {
    ++seyir_count;
    last_seyir = pkt;
    return pkt.valid;
  }

  void StartHeartbeat() override {}
  void StopHeartbeat() override {}
  TelemetrySnapshot GetTelemetrySnapshot() const override { return {}; }
  std::string DescribeEndpoint() const override { return "mock://alc"; }
};

}  // namespace savasan::autopilot

#endif  // SAVASAN_TESTS_MOCK_ALC_LINK_BRIDGE_HPP_
