#ifndef SAVASAN_TESTS_MOCK_TELEMETRY_SOURCE_HPP_
#define SAVASAN_TESTS_MOCK_TELEMETRY_SOURCE_HPP_

#include "telemetry/itelemetry_source.hpp"

namespace savasan::tests {

class MockTelemetrySource final : public telemetry::ITelemetrySource {
 public:
  savasan::autopilot::TelemetrySnapshot ReadTelemetry() const override { return snapshot_; }
  void SetSnapshot(const savasan::autopilot::TelemetrySnapshot& s) { snapshot_ = s; }

 private:
  savasan::autopilot::TelemetrySnapshot snapshot_{};
};

}  // namespace savasan::tests

#endif  // SAVASAN_TESTS_MOCK_TELEMETRY_SOURCE_HPP_
