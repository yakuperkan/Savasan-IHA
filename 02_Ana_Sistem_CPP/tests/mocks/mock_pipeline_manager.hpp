#ifndef SAVASAN_TESTS_MOCK_PIPELINE_MANAGER_HPP_
#define SAVASAN_TESTS_MOCK_PIPELINE_MANAGER_HPP_

#include "pipeline/ipipeline_manager.hpp"

namespace savasan::tests {

class MockPipelineManager final : public pipeline::IPipelineManager {
 public:
  bool build_ok = true;

  bool BuildPipeline(const pipeline::PipelineConfig& cfg) override {
    built_desc_ = cfg.pipeline_desc;
    state_ = build_ok ? pipeline::PipelineState::kReady : pipeline::PipelineState::kError;
    return build_ok;
  }

  bool SetState(pipeline::PipelineState state) override {
    if (state_ == pipeline::PipelineState::kError) return false;
    state_ = state;
    return true;
  }

  pipeline::PipelineState GetState() const override { return state_; }
  const std::string& LastBuiltDesc() const { return built_desc_; }

 private:
  std::string built_desc_;
  pipeline::PipelineState state_ = pipeline::PipelineState::kNull;
};

}  // namespace savasan::tests

#endif  // SAVASAN_TESTS_MOCK_PIPELINE_MANAGER_HPP_
