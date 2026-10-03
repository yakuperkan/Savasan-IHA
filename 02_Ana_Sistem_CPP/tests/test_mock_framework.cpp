#include "mocks/mock_camera_source.hpp"
#include "mocks/mock_pipeline_manager.hpp"
#include "mocks/mock_telemetry_source.hpp"
#include "mocks/mock_tracker_facade.hpp"

int main() {
  // Camera mock
  savasan::tests::MockCameraSource cam;
  if (!cam.Start() || !cam.IsRunning()) return 1;
  const auto frame = cam.GetFrame();
  if (!frame.valid || frame.width != 640 || frame.height != 480) return 2;
  cam.Stop();
  if (cam.IsRunning()) return 3;

  // Pipeline mock
  savasan::tests::MockPipelineManager pipe;
  savasan::pipeline::PipelineConfig cfg{};
  cfg.pipeline_desc = "mock-pipeline";
  if (!pipe.BuildPipeline(cfg)) return 4;
  if (pipe.GetState() != savasan::pipeline::PipelineState::kReady) return 5;
  if (!pipe.SetState(savasan::pipeline::PipelineState::kPlaying)) return 6;
  if (pipe.GetState() != savasan::pipeline::PipelineState::kPlaying) return 7;

  // Tracker mock
  savasan::tests::MockTrackerFacade tracker;
  savasan::tracking::LockState st{};
  st.valid_lock = true;
  st.target.locked = true;
  st.target.track_id = 99;
  tracker.SetState(st);
  tracker.Update();
  const auto observed = tracker.GetState();
  if (!observed.valid_lock || observed.target.track_id != 99) return 8;
  if (tracker.update_count() != 1) return 9;

  // Telemetry mock
  savasan::tests::MockTelemetrySource telem;
  savasan::autopilot::TelemetrySnapshot snap{};
  snap.valid = true;
  snap.vel_x_mps = 1.25f;
  telem.SetSnapshot(snap);
  const auto out = telem.ReadTelemetry();
  if (!out.valid || out.vel_x_mps < 1.24f || out.vel_x_mps > 1.26f) return 10;

  return 0;
}
