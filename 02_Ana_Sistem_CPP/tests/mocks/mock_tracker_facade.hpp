#ifndef SAVASAN_TESTS_MOCK_TRACKER_FACADE_HPP_
#define SAVASAN_TESTS_MOCK_TRACKER_FACADE_HPP_

#include "tracking/itracker_facade.hpp"

namespace savasan::tests {

class MockTrackerFacade final : public tracking::ITrackerFacade {
 public:
  void Reset() override { state_.Reset(); }
  void Update() override { ++update_count_; }
  tracking::LockState GetState() const override { return state_; }

  void SetState(const tracking::LockState& st) { state_ = st; }
  int update_count() const { return update_count_; }

 private:
  tracking::LockState state_{};
  int update_count_ = 0;
};

}  // namespace savasan::tests

#endif  // SAVASAN_TESTS_MOCK_TRACKER_FACADE_HPP_
