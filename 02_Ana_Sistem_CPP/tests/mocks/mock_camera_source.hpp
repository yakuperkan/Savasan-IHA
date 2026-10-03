#ifndef SAVASAN_TESTS_MOCK_CAMERA_SOURCE_HPP_
#define SAVASAN_TESTS_MOCK_CAMERA_SOURCE_HPP_

#include "camera/icamera_source.hpp"

namespace savasan::tests {

class MockCameraSource final : public camera::ICameraSource {
 public:
  bool start_ok = true;

  bool Start() override {
    running_ = start_ok;
    return start_ok;
  }

  void Stop() override { running_ = false; }
  bool IsRunning() const override { return running_; }

  camera::FrameData GetFrame() override {
    camera::FrameData out{};
    out.valid = running_;
    out.frame_id = ++frame_id_;
    out.width = 640;
    out.height = 480;
    out.capture_tp = std::chrono::steady_clock::now();
    return out;
  }

 private:
  bool running_ = false;
  uint64_t frame_id_ = 0;
};

}  // namespace savasan::tests

#endif  // SAVASAN_TESTS_MOCK_CAMERA_SOURCE_HPP_
