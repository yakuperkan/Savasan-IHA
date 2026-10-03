#include "tracking/lock_selection_policy.hpp"

#include <chrono>
#include <vector>

int main() {
  using savasan::tracking::LockMode;
  using savasan::tracking::LockState;
  using savasan::tracking::policy::DetectionCandidate;
  using savasan::tracking::policy::LockSelectionParams;
  using savasan::tracking::policy::UpdateLockStateFromCandidates;

  const auto t0 = std::chrono::steady_clock::now();
  const LockSelectionParams params{};

  // 1) Kilitsiz durumda hibrit mod, esik ustu adayla kilit acmali.
  LockState s0{};
  std::vector<DetectionCandidate> c0;
  c0.push_back({10U, 320.0f, 220.0f, 40.0f, 30.0f, 0.65f, 0});
  const LockState s1 =
      UpdateLockStateFromCandidates(s0, c0, 640.0f, 480.0f, LockMode::kHybrid, t0, params);
  if (!s1.IsLocked() || s1.target.track_id != 10U) {
    return 1;
  }
  if (s1.valid_lock) {
    return 2;
  }

  // 2) Ayni track 4 saniye surerse valid_lock true olmali.
  std::vector<DetectionCandidate> c1;
  c1.push_back({10U, 322.0f, 221.0f, 40.0f, 30.0f, 0.50f, 0});
  const LockState s2 = UpdateLockStateFromCandidates(
      s1, c1, 640.0f, 480.0f, LockMode::kHybrid, t0 + std::chrono::seconds(5), params);
  if (!s2.valid_lock) {
    return 3;
  }

  // 3) ID switch grace: yakin yeni ID geldiginde valid_lock dusmemeli.
  std::vector<DetectionCandidate> c2;
  c2.push_back({77U, 324.0f, 223.0f, 40.0f, 30.0f, 0.52f, 0});
  const LockState s3 = UpdateLockStateFromCandidates(
      s2, c2, 640.0f, 480.0f, LockMode::kHybrid, t0 + std::chrono::seconds(6), params);
  if (!s3.IsLocked() || s3.target.track_id != 77U) {
    return 4;
  }
  if (!s3.valid_lock) {
    return 5;
  }

  // 3b) IoU tabanli ID switch: mesafe esigi dar, yuksek IoU ile kilit korunmali.
  LockSelectionParams iou_params = params;
  iou_params.id_switch_grace_norm_dist = 0.01f;  // Mesafe yolu kapali (test izolasyonu).
  LockState s_iou_base{};
  s_iou_base.target.track_id = 10U;
  s_iou_base.target.cx = 320.0f;
  s_iou_base.target.cy = 240.0f;
  s_iou_base.target.w = 100.0f;
  s_iou_base.target.h = 80.0f;
  s_iou_base.target.confidence = 0.65f;
  s_iou_base.target.locked = true;
  s_iou_base.valid_lock = true;
  s_iou_base.av_in = true;
  s_iou_base.lock_start_time = t0;
  std::vector<DetectionCandidate> c_iou;
  c_iou.push_back({88U, 350.0f, 252.0f, 100.0f, 80.0f, 0.55f, 0});
  const LockState s_iou = UpdateLockStateFromCandidates(
      s_iou_base, c_iou, 640.0f, 480.0f, LockMode::kHybrid, t0 + std::chrono::seconds(6),
      iou_params);
  if (!s_iou.IsLocked() || s_iou.target.track_id != 88U) {
    return 51;
  }
  if (!s_iou.valid_lock) {
    return 52;
  }

  // 4) Aday yoksa miss sayaci artmali, timeout'ta reset olmali.
  LockState sx = s3;
  for (int i = 0; i < LockState::kMaxMissFrames; ++i) {
    sx = UpdateLockStateFromCandidates(sx, {}, 640.0f, 480.0f, LockMode::kHybrid,
                                       t0 + std::chrono::seconds(7), params);
  }
  if (sx.IsLocked() || sx.valid_lock) {
    return 6;
  }

  // 5) Algilama kesilince ilk karede sayac korunur (grace_hold + sartname butcesi).
  LockState s4{};
  std::vector<DetectionCandidate> c4;
  c4.push_back({20U, 300.0f, 240.0f, 40.0f, 30.0f, 0.70f, 0});
  LockState s5 = UpdateLockStateFromCandidates(s4, c4, 640.0f, 480.0f, LockMode::kHybrid,
                                               t0 + std::chrono::seconds(10), params);
  if (!s5.IsLocked() || s5.valid_lock) {
    return 7;
  }
  LockState s6 = UpdateLockStateFromCandidates(
      s5, c4, 640.0f, 480.0f, LockMode::kHybrid, t0 + std::chrono::seconds(13), params);
  if (!s6.IsLocked() || s6.valid_lock) {
    return 8;
  }
  LockState s7 = UpdateLockStateFromCandidates(s6, {}, 640.0f, 480.0f, LockMode::kHybrid,
                                               t0 + std::chrono::seconds(13), params);
  if (s7.valid_lock) {
    return 9;
  }
  if (s7.reason != savasan::tracking::LockReason::kGraceHold) {
    return 91;
  }
  if (s7.lock_start_time.time_since_epoch().count() == 0) {
    return 92;
  }
  const auto start_before_miss = s6.lock_start_time;
  LockState s8 = UpdateLockStateFromCandidates(
      s7, c4, 640.0f, 480.0f, LockMode::kHybrid, t0 + std::chrono::seconds(14), params);
  if (!s8.IsLocked() || s8.valid_lock) {
    return 10;
  }
  if (s8.lock_start_time != start_before_miss) {
    return 11;
  }

  // 6) Tek kare kaybi tolerans icinde sayac korunur (grace_hold).
  LockState s_gap_a{};
  std::vector<DetectionCandidate> c_gap;
  c_gap.push_back({40U, 320.0f, 240.0f, 40.0f, 30.0f, 0.70f, 0});
  LockState s_gap_b = s_gap_a;
  for (int i = 0; i < 30; ++i) {
    s_gap_b = UpdateLockStateFromCandidates(
        s_gap_b, c_gap, 640.0f, 480.0f, LockMode::kHybrid,
        t0 + std::chrono::milliseconds(20'000 + (i * 33)), params);
  }
  const auto gap_start = s_gap_b.lock_start_time;
  LockState s_gap_miss = UpdateLockStateFromCandidates(
      s_gap_b, {}, 640.0f, 480.0f, LockMode::kHybrid,
      t0 + std::chrono::milliseconds(20'000 + (30 * 33)), params);
  if (s_gap_miss.lock_start_time != gap_start) {
    return 14;
  }
  if (s_gap_miss.reason != savasan::tracking::LockReason::kGraceHold) {
    return 15;
  }

  // 7) AV disinda sayac ilerlemez, valid_lock olusmaz.
  LockState s9{};
  std::vector<DetectionCandidate> c_edge;
  c_edge.push_back({30U, 80.0f, 80.0f, 50.0f, 40.0f, 0.80f, 0});
  LockState s10 = UpdateLockStateFromCandidates(s9, c_edge, 640.0f, 480.0f, LockMode::kHybrid,
                                                t0 + std::chrono::seconds(20), params);
  if (!s10.IsLocked() || s10.av_in || s10.valid_lock) {
    return 12;
  }
  LockState s11 = UpdateLockStateFromCandidates(
      s10, c_edge, 640.0f, 480.0f, LockMode::kHybrid, t0 + std::chrono::seconds(25), params);
  if (s11.valid_lock || s11.lock_start_time.time_since_epoch().count() != 0) {
    return 13;
  }

  // 8) Tolerans asimi (200 ms) sayaci sifirlar.
  LockState s_tol = s_gap_miss;
  for (int i = 0; i < 10; ++i) {
    s_tol = UpdateLockStateFromCandidates(
        s_tol, {}, 640.0f, 480.0f, LockMode::kHybrid,
        t0 + std::chrono::milliseconds(20'000 + ((31 + i) * 33)), params);
  }
  if (s_tol.lock_start_time.time_since_epoch().count() != 0) {
    return 16;
  }

  // 9) Sartname %5: 33 ms karede ~6 ardışık kayip sayaci korur, 7. karede sifirlar.
  LockState s_streak_a{};
  std::vector<DetectionCandidate> c_streak;
  c_streak.push_back({50U, 320.0f, 240.0f, 40.0f, 30.0f, 0.70f, 0});
  LockState s_streak_b = s_streak_a;
  for (int i = 0; i < 25; ++i) {
    s_streak_b = UpdateLockStateFromCandidates(
        s_streak_b, c_streak, 640.0f, 480.0f, LockMode::kHybrid,
        t0 + std::chrono::milliseconds(30'000 + (i * 33)), params);
  }
  const auto streak_start = s_streak_b.lock_start_time;
  if (streak_start.time_since_epoch().count() == 0) {
    return 17;
  }
  const int streak_base_ms = 30'000 + (24 * 33);
  LockState s_streak_miss = s_streak_b;
  for (int miss = 1; miss <= 6; ++miss) {
    s_streak_miss = UpdateLockStateFromCandidates(
        s_streak_miss, {}, 640.0f, 480.0f, LockMode::kHybrid,
        t0 + std::chrono::milliseconds(streak_base_ms + (miss * 33)), params);
    if (s_streak_miss.lock_start_time != streak_start) {
      return 18;
    }
    if (s_streak_miss.reason != savasan::tracking::LockReason::kGraceHold) {
      return 19;
    }
  }
  LockState s_streak_over = UpdateLockStateFromCandidates(
      s_streak_miss, {}, 640.0f, 480.0f, LockMode::kHybrid,
      t0 + std::chrono::milliseconds(streak_base_ms + (7 * 33)), params);
  if (s_streak_over.lock_start_time.time_since_epoch().count() != 0) {
    return 20;
  }
  if (!s_streak_over.IsLocked()) {
    return 21;
  }

  // 10) Dagitilmis kayiplar: iki kisa glitch toplam < 200 ms sayaci korur.
  LockState s_dist_a{};
  std::vector<DetectionCandidate> c_dist;
  c_dist.push_back({60U, 320.0f, 240.0f, 40.0f, 30.0f, 0.70f, 0});
  const int dist_base_ms = 40'000;
  const int dist_lock_ms = dist_base_ms + (19 * 33);
  LockState s_dist = s_dist_a;
  for (int i = 0; i < 20; ++i) {
    s_dist = UpdateLockStateFromCandidates(
        s_dist, c_dist, 640.0f, 480.0f, LockMode::kHybrid,
        t0 + std::chrono::milliseconds(dist_base_ms + (i * 33)), params);
  }
  const auto dist_start = s_dist.lock_start_time;
  for (int i = 1; i <= 3; ++i) {
    s_dist = UpdateLockStateFromCandidates(
        s_dist, {}, 640.0f, 480.0f, LockMode::kHybrid,
        t0 + std::chrono::milliseconds(dist_lock_ms + (i * 33)), params);
  }
  const int dist_recover_ms = dist_lock_ms + (3 * 33);
  for (int i = 1; i <= 5; ++i) {
    s_dist = UpdateLockStateFromCandidates(
        s_dist, c_dist, 640.0f, 480.0f, LockMode::kHybrid,
        t0 + std::chrono::milliseconds(dist_recover_ms + (i * 33)), params);
  }
  const int dist_glitch2_ms = dist_recover_ms + (5 * 33);
  for (int i = 1; i <= 3; ++i) {
    s_dist = UpdateLockStateFromCandidates(
        s_dist, {}, 640.0f, 480.0f, LockMode::kHybrid,
        t0 + std::chrono::milliseconds(dist_glitch2_ms + (i * 33)), params);
  }
  if (s_dist.lock_start_time != dist_start) {
    return 22;
  }
  if (s_dist.lock_interior_bad_ms > params.interior_bad_tolerance_ms) {
    return 23;
  }

  // 11) Sayim sirasinda (valid_lock false) yakin id-switch sayaci sifirlamamali.
  LockSelectionParams count_sw_params = params;
  LockState s_count{};
  std::vector<DetectionCandidate> c_count_a;
  c_count_a.push_back({100U, 320.0f, 240.0f, 40.0f, 30.0f, 0.70f, 0});
  for (int i = 0; i < 60; ++i) {
    s_count = UpdateLockStateFromCandidates(
        s_count, c_count_a, 640.0f, 480.0f, LockMode::kHybrid,
        t0 + std::chrono::milliseconds(50'000 + (i * 33)), count_sw_params);
  }
  if (!s_count.IsLocked() || s_count.valid_lock) {
    return 24;
  }
  if (s_count.lock_start_time.time_since_epoch().count() == 0) {
    return 25;
  }
  const auto count_start = s_count.lock_start_time;
  std::vector<DetectionCandidate> c_count_b;
  c_count_b.push_back({101U, 324.0f, 243.0f, 40.0f, 30.0f, 0.55f, 0});
  LockState s_count_sw = UpdateLockStateFromCandidates(
      s_count, c_count_b, 640.0f, 480.0f, LockMode::kHybrid,
      t0 + std::chrono::milliseconds(52'000), count_sw_params);
  if (!s_count_sw.IsLocked() || s_count_sw.target.track_id != 101U) {
    return 26;
  }
  if (s_count_sw.valid_lock) {
    return 27;
  }
  if (s_count_sw.lock_start_time != count_start) {
    return 28;
  }

  // 12) valid_lock dogrulandiktan sonra ara kayip butcesi kilidi dusurmemeli.
  LockState s_post_val{};
  std::vector<DetectionCandidate> c_post;
  c_post.push_back({200U, 320.0f, 240.0f, 40.0f, 30.0f, 0.70f, 0});
  LockState s_post_locked = s_post_val;
  for (int i = 0; i < 130; ++i) {
    s_post_locked = UpdateLockStateFromCandidates(
        s_post_locked, c_post, 640.0f, 480.0f, LockMode::kHybrid,
        t0 + std::chrono::milliseconds(60'000 + (i * 33)), params);
  }
  if (!s_post_locked.valid_lock) {
    return 29;
  }
  LockState s_post_miss = s_post_locked;
  for (int i = 1; i <= 7; ++i) {
    s_post_miss = UpdateLockStateFromCandidates(
        s_post_miss, {}, 640.0f, 480.0f, LockMode::kHybrid,
        t0 + std::chrono::milliseconds(64'500 + (i * 33)), params);
  }
  if (!s_post_miss.valid_lock) {
    return 30;
  }
  if (s_post_miss.reason != savasan::tracking::LockReason::kGraceHold) {
    return 31;
  }

  // 13) Farkli class_id ile IoU yuksek olsa bile id-switch yapilmamali.
  LockState s_cls{};
  s_cls.target.track_id = 10U;
  s_cls.target.cx = 320.0f;
  s_cls.target.cy = 240.0f;
  s_cls.target.w = 100.0f;
  s_cls.target.h = 80.0f;
  s_cls.target.confidence = 0.65f;
  s_cls.target.class_id = 0;
  s_cls.target.locked = true;
  s_cls.valid_lock = true;
  s_cls.av_in = true;
  s_cls.lock_start_time = t0;
  std::vector<DetectionCandidate> c_cls;
  c_cls.push_back({99U, 350.0f, 252.0f, 100.0f, 80.0f, 0.55f, 1});
  const LockState s_cls_sw = UpdateLockStateFromCandidates(
      s_cls, c_cls, 640.0f, 480.0f, LockMode::kHybrid, t0 + std::chrono::seconds(70),
      params);
  if (!s_cls_sw.IsLocked() || s_cls_sw.target.track_id != 10U) {
    return 32;
  }

  // 14) Baseline modda dusuk guvenli hayalet kutu kilidi korumamali.
  LockState s_base{};
  s_base.target.track_id = 15U;
  s_base.target.cx = 320.0f;
  s_base.target.cy = 240.0f;
  s_base.target.w = 40.0f;
  s_base.target.h = 30.0f;
  s_base.target.confidence = 0.70f;
  s_base.target.locked = true;
  std::vector<DetectionCandidate> c_base;
  c_base.push_back({15U, 320.0f, 240.0f, 40.0f, 30.0f, 0.05f, 0});
  const LockState s_base_low = UpdateLockStateFromCandidates(
      s_base, c_base, 640.0f, 480.0f, LockMode::kBaseline, t0 + std::chrono::seconds(80),
      params);
  if (s_base_low.IsLocked()) {
    return 33;
  }

  // 15) grace_ms_remaining milisaniye cinsinden kalmali.
  LockState s_grace{};
  std::vector<DetectionCandidate> c_grace;
  c_grace.push_back({25U, 320.0f, 240.0f, 40.0f, 30.0f, 0.70f, 0});
  LockState s_grace_on = UpdateLockStateFromCandidates(
      s_grace, c_grace, 640.0f, 480.0f, LockMode::kHybrid,
      t0 + std::chrono::milliseconds(90'000), params);
  const LockState s_grace_miss = UpdateLockStateFromCandidates(
      s_grace_on, {}, 640.0f, 480.0f, LockMode::kHybrid,
      t0 + std::chrono::milliseconds(90'033), params);
  if (s_grace_miss.grace_ms_remaining <= 0 || s_grace_miss.grace_ms_remaining > 200) {
    return 34;
  }

  return 0;
}
