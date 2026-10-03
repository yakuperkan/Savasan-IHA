/**
 * @file lock_selection_policy.hpp
 * @brief Hedef kilit seçim politikası (saf, yan etkisiz fonksiyonlar).
 *
 * DeepStream'den bağımsız, test edilebilir kilit mantığını içerir: aday
 * tespitlerden en iyi hedefi seçme, ROI/AV (Hedef Vuruş Alanı) kontrolü, kimlik
 * değişimi (id-switch) toleransı, şartname kaynaklı 4 sn kilit zamanlayıcısı ve
 * kilit sağlığı (health) hesabı. TrackSelector bu fonksiyonları DeepStream
 * metadata'sından doldurduğu girdilerle çağırır.
 */
#ifndef SAVASAN_TRACKING_LOCK_SELECTION_POLICY_HPP_
#define SAVASAN_TRACKING_LOCK_SELECTION_POLICY_HPP_

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

#include "tracking/lock_mode.hpp"
#include "tracking/lock_state.hpp"

namespace savasan::tracking::policy {

/// @brief Tek karedeki bir aday tespit (track_id + kutu + güven).
struct DetectionCandidate {
  uint64_t track_id = UINT64_MAX; ///< Takip kimliği.
  float cx = 0.0f;                ///< Kutu merkezi X (piksel).
  float cy = 0.0f;                ///< Kutu merkezi Y (piksel).
  float w = 0.0f;                 ///< Kutu genişliği (piksel).
  float h = 0.0f;                 ///< Kutu yüksekliği (piksel).
  float confidence = 0.0f;        ///< Tespit güveni.
  int class_id = -1;              ///< Sınıf kimliği.
};

/// @brief Kilit seçim politikasının ayar parametreleri (env ile override edilebilir).
struct LockSelectionParams {
  float new_lock_min_conf = 0.40f;  ///< Yeni kilit için asgari güven.
  float keep_lock_min_conf = 0.20f; ///< Mevcut kilidi korumak için asgari güven.
  float confidence_weight = 0.85f;  ///< Yeni kilit skorunda güven ağırlığı.
  float center_weight = 0.15f;      ///< Yeni kilit skorunda merkeze yakınlık ağırlığı.
  float id_switch_grace_norm_dist = 0.08f; ///< Kimlik değişiminde kabul edilen azami normalize mesafe.
  float id_switch_min_conf = 0.35f; ///< Kimlik değişimi için asgari güven.
  float id_switch_min_iou = 0.35f;  ///< Kimlik değişiminde aynı fiziksel hedef kabul IoU eşiği.
  float id_switch_min_aspect_ratio = 0.50f; ///< Kimlik değişiminde kutu en-boy oranı benzerliği eşiği.
  float baseline_min_conf = 0.10f;  ///< Baseline modda hayalet kutuları elemek için asgari güven.
  float roi_x_min = 0.0f;  ///< ROI sol sınırı (normalize [0..1]).
  float roi_x_max = 1.0f;  ///< ROI sağ sınırı (normalize [0..1]).
  float roi_y_min = 0.0f;  ///< ROI üst sınırı (normalize [0..1]).
  float roi_y_max = 1.0f;  ///< ROI alt sınırı (normalize [0..1]).
  float av_margin_norm = 0.05f;  ///< Şartname: kenarlardan %5 kırpılmış merkez AV payı.
  int interior_bad_tolerance_ms = LockState::kInteriorBadToleranceMs; ///< 4 sn seride toleranslı ara kayıp (ms).
  int grace_miss_frames = 18;  ///< HUD tahmini; gerçek koruma interior_bad_ms (şartname %5) ile yapılır.
  int min_track_frames = 2;    ///< Yeni kilit adayı için asgari ardışık görünür kare sayısı.
  float health_conf_weight = 0.35f;      ///< Sağlık skorunda güven ağırlığı.
  float health_center_weight = 0.25f;    ///< Sağlık skorunda merkeze yakınlık ağırlığı.
  float health_stability_weight = 0.25f; ///< Sağlık skorunda kararlılık (kayıpsızlık) ağırlığı.
  float health_id_weight = 0.15f;        ///< Sağlık skorunda kimlik tutarlılığı ağırlığı.
};

/// @brief Hedefin kare merkezine yakınlık skorunu [0..1] hesaplar (1 = tam merkez).
inline float ComputeCenterScore(const float cx, const float cy, const float frame_w,
                                const float frame_h) {
  const float safe_w = std::max(frame_w, 1.0f);
  const float safe_h = std::max(frame_h, 1.0f);
  const float half_w = safe_w * 0.5f;
  const float half_h = safe_h * 0.5f;
  const float dx = cx - half_w;
  const float dy = cy - half_h;
  const float denom = (half_w * half_w) + (half_h * half_h);
  if (denom <= 1e-6f) return 0.0f;
  const float dist_sq = (dx * dx) + (dy * dy);
  return 1.0f - std::min(dist_sq / denom, 1.0f);
}

/// @brief Merkez + boyut ile tanımlı dikdörtgen kutu (IoU hesabı için).
///
/// Koordinat sözleşmesi: Bu yapı ve politika katmanındaki @ref DetectionCandidate /
/// @ref LockedTarget alanları **merkez formatı** kullanır: (cx, cy, w, h).
/// DeepStream @c NvDsObjectMeta::rect_params sol-üst (left, top, width, height)
/// verir; @c TrackSelector bunu probe girişinde merkeze çevirir:
/// @code
///   cx = left + width * 0.5f;
///   cy = top  + height * 0.5f;
/// @endcode
/// IoU hesabında kenarlar her zaman @c cx ± w/2, @c cy ± h/2 ile türetilir.
struct BBox {
  float cx = 0.0f; ///< Kutu merkezi X (piksel).
  float cy = 0.0f; ///< Kutu merkezi Y (piksel).
  float w = 0.0f;  ///< Genişlik (piksel, > 0).
  float h = 0.0f;  ///< Yükseklik (piksel, > 0).
};

namespace detail {

constexpr float kIouAreaEpsilon = 1e-6f;

/// @brief Kutu boyutlarının IoU için geçerli olup olmadığını kontrol eder.
inline bool IsValidBBoxForIou(const BBox& box) noexcept {
  return std::isfinite(box.cx) && std::isfinite(box.cy) && std::isfinite(box.w) &&
         std::isfinite(box.h) && box.w > 0.0f && box.h > 0.0f;
}

/// @brief Merkez formatındaki kutunun axis-aligned kenarlarını döndürür.
inline void BBoxToEdges(const BBox& box, float* left, float* top, float* right,
                        float* bottom) noexcept {
  const float half_w = box.w * 0.5f;
  const float half_h = box.h * 0.5f;
  *left = box.cx - half_w;
  *top = box.cy - half_h;
  *right = box.cx + half_w;
  *bottom = box.cy + half_h;
}

}  // namespace detail

/// @brief DeepStream sol-üst formatından merkez @ref BBox üretir (dönüşüm referansı).
inline BBox BBoxFromTopLeft(const float x, const float y, const float w,
                            const float h) noexcept {
  return BBox{x + (w * 0.5f), y + (h * 0.5f), w, h};
}

/// @brief @ref LockedTarget kutusunu @ref BBox'a dönüştürür (merkez formatı).
inline BBox BBoxFromLockedTarget(const LockedTarget& t) noexcept {
  return BBox{t.cx, t.cy, t.w, t.h};
}

/// @brief @ref DetectionCandidate kutusunu @ref BBox'a dönüştürür (merkez formatı).
inline BBox BBoxFromCandidate(const DetectionCandidate& c) noexcept {
  return BBox{c.cx, c.cy, c.w, c.h};
}

/// @brief İki kutunun IoU (Intersection over Union) değerini [0..1] hesaplar.
///
/// Girdi kutuları merkez formatında (cx, cy, w, h) olmalıdır. Kenarlar
/// @c cx ± w/2 ve @c cy ± h/2 ile türetilir; sol-üst (x, y) doğrudan kullanılmaz.
/// Sıfır/negatif alan, birleşim sıfırı, NaN/Inf veya sıfıra bölünme durumunda 0 döner.
inline float calculate_iou(const BBox& a, const BBox& b) noexcept {
  if (!detail::IsValidBBoxForIou(a) || !detail::IsValidBBoxForIou(b)) {
    return 0.0f;
  }

  float a_left = 0.0f;
  float a_top = 0.0f;
  float a_right = 0.0f;
  float a_bottom = 0.0f;
  float b_left = 0.0f;
  float b_top = 0.0f;
  float b_right = 0.0f;
  float b_bottom = 0.0f;
  detail::BBoxToEdges(a, &a_left, &a_top, &a_right, &a_bottom);
  detail::BBoxToEdges(b, &b_left, &b_top, &b_right, &b_bottom);

  const float inter_left = std::max(a_left, b_left);
  const float inter_top = std::max(a_top, b_top);
  const float inter_right = std::min(a_right, b_right);
  const float inter_bottom = std::min(a_bottom, b_bottom);

  const float inter_w = inter_right - inter_left;
  const float inter_h = inter_bottom - inter_top;
  if (inter_w <= 0.0f || inter_h <= 0.0f) {
    return 0.0f;
  }

  const float inter_area = inter_w * inter_h;
  if (!std::isfinite(inter_area) || inter_area <= 0.0f) {
    return 0.0f;
  }

  const float area_a = a.w * a.h;
  const float area_b = b.w * b.h;
  if (!std::isfinite(area_a) || !std::isfinite(area_b) || area_a <= 0.0f || area_b <= 0.0f) {
    return 0.0f;
  }

  const float union_area = area_a + area_b - inter_area;
  if (!std::isfinite(union_area) || union_area <= detail::kIouAreaEpsilon) {
    return 0.0f;
  }

  const float iou = inter_area / union_area;
  if (!std::isfinite(iou)) {
    return 0.0f;
  }
  return std::clamp(iou, 0.0f, 1.0f);
}

/// @brief Kimlik değişimi adayının sınıf ve kutu oranı ile uyumlu olup olmadığını kontrol eder.
inline bool IsCompatibleIdSwitchTarget(const LockedTarget& locked,
                                       const DetectionCandidate& cand,
                                       const float min_aspect_ratio) noexcept {
  if (locked.class_id >= 0 && cand.class_id >= 0 && locked.class_id != cand.class_id) {
    return false;
  }
  const BBox locked_bbox = BBoxFromLockedTarget(locked);
  const BBox cand_bbox = BBoxFromCandidate(cand);
  if (!detail::IsValidBBoxForIou(locked_bbox) || !detail::IsValidBBoxForIou(cand_bbox)) {
    return false;
  }
  const float locked_aspect = locked_bbox.w / locked_bbox.h;
  const float cand_aspect = cand_bbox.w / cand_bbox.h;
  const float ratio =
      std::min(locked_aspect, cand_aspect) / std::max(locked_aspect, cand_aspect);
  return ratio >= min_aspect_ratio;
}

/// @brief İki hedef merkezi arasındaki kareye normalize öklid mesafesini hesaplar.
inline float ComputeNormalizedDistance(const LockedTarget& a, const DetectionCandidate& b,
                                       const float frame_w, const float frame_h) {
  const float safe_w = std::max(frame_w, 1.0f);
  const float safe_h = std::max(frame_h, 1.0f);
  const float dx = (a.cx - b.cx) / safe_w;
  const float dy = (a.cy - b.cy) / safe_h;
  return std::sqrt((dx * dx) + (dy * dy));
}

/// @brief Hedef merkezinin Hedef Vuruş Alanı (AV) içinde olup olmadığını döndürür.
/// AV, kenarlardan @p margin_norm oranında kırpılmış, ortalanmış kare bir bölgedir.
inline bool IsInAttackArea(const float cx, const float cy, const float frame_w,
                           const float frame_h, const float margin_norm) {
  const float safe_w = std::max(frame_w, 1.0f);
  const float safe_h = std::max(frame_h, 1.0f);
  const float margin_x = safe_w * std::clamp(margin_norm, 0.03f, 0.40f);
  const float margin_y = safe_h * std::clamp(margin_norm, 0.03f, 0.40f);
  const float usable_w = std::max(2.0f, safe_w - (2.0f * margin_x));
  const float usable_h = std::max(2.0f, safe_h - (2.0f * margin_y));
  const float side = std::max(2.0f, std::min(usable_w, usable_h));
  const float av_left = std::max(0.0f, (safe_w - side) * 0.5f);
  const float av_top = std::max(0.0f, (safe_h - side) * 0.5f);
  return cx >= av_left && cx <= (av_left + side) && cy >= av_top && cy <= (av_top + side);
}

/// @brief İki politika güncellemesi arası geçen süreyi ms olarak verir (1..250 aralığına kırpılmış).
/// İlk çağrıda (önceki damga yoksa) 33 ms (~30 fps) varsayar.
inline int FrameDeltaMs(const LockState& current_state,
                        const std::chrono::steady_clock::time_point now) {
  if (current_state.last_policy_update_tp.time_since_epoch().count() == 0) {
    return 33;
  }
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      now - current_state.last_policy_update_tp);
  return static_cast<int>(std::clamp(ms.count(), 1L, 250L));
}

/// @brief Şartname 4 sn kilit zamanlayıcısını günceller.
///
/// AV dışındayken zamanlayıcı sıfırlanır ve kilit geçersiz kılınır. AV içinde:
/// kimlik değiştiyse (koruma yoksa) sayaç sıfırlanır; aynı izde ara kayıp varsa
/// tolerans bütçesine göre korunur veya sıfırlanır; kesintisiz 4 sn dolunca ve
/// tolerans aşılmamışsa @c valid_lock true yapılır.
/// @param same_track Aday, mevcut kilitli izle aynı mı.
/// @param preserve_timer_on_switch Kimlik değişiminde 4 sn sayacını (ve geçerli kilidi) koru.
inline void UpdateSartnameLockTimer(LockState& next, const LockState& current_state,
                                    const bool same_track,
                                    const bool preserve_timer_on_switch, const bool in_av,
                                    const int interior_bad_tolerance_ms,
                                    const std::chrono::steady_clock::time_point now) {
  if (!in_av) {
    next.lock_start_time = std::chrono::steady_clock::time_point{};
    next.valid_lock = false;
    next.lock_interior_bad_ms = 0;
    if (next.reason == LockReason::kNone) {
      next.reason = LockReason::kAvOut;
    }
    return;
  }
  if (!same_track) {
    if (!preserve_timer_on_switch) {
      next.lock_start_time = now;
      next.valid_lock = false;
      next.lock_interior_bad_ms = 0;
    }
  } else if (current_state.frames_without_detection > 0) {
    if (!current_state.valid_lock &&
        (current_state.lock_interior_bad_ms > interior_bad_tolerance_ms ||
         current_state.lock_start_time.time_since_epoch().count() == 0)) {
      next.lock_start_time = now;
      next.valid_lock = false;
      next.lock_interior_bad_ms = 0;
    } else {
      next.lock_start_time = current_state.lock_start_time;
      next.valid_lock = current_state.valid_lock;
    }
  } else if (next.lock_start_time.time_since_epoch().count() == 0) {
    next.lock_start_time = now;
    next.valid_lock = false;
  } else if (!next.valid_lock &&
             (now - next.lock_start_time) >= LockState::kRequiredLockDuration &&
             next.lock_interior_bad_ms <= interior_bad_tolerance_ms) {
    next.valid_lock = true;
    next.lock_interior_bad_ms = 0;
  } else if (next.valid_lock) {
    next.lock_interior_bad_ms = 0;
  }
}

/// @brief Hedef merkezinin yapılandırılmış ilgi bölgesi (ROI) içinde olup olmadığını döndürür.
inline bool IsInRoi(const float cx, const float cy, const float frame_w, const float frame_h,
                    const LockSelectionParams& p) {
  const float safe_w = std::max(frame_w, 1.0f);
  const float safe_h = std::max(frame_h, 1.0f);
  const float nx = cx / safe_w;
  const float ny = cy / safe_h;
  return nx >= p.roi_x_min && nx <= p.roi_x_max && ny >= p.roi_y_min && ny <= p.roi_y_max;
}

/// @brief Kilit sağlığını [0..1] hesaplar: güven, merkeze yakınlık, kararlılık ve
/// kimlik tutarlılığının ağırlıklı toplamı (operatör görünürlük metriği).
inline float ComputeLockHealth(const DetectionCandidate& c, const LockState& current_state,
                               const float frame_w, const float frame_h,
                               const LockSelectionParams& p) {
  const float conf = std::clamp(c.confidence, 0.0f, 1.0f);
  const float center = std::clamp(ComputeCenterScore(c.cx, c.cy, frame_w, frame_h), 0.0f, 1.0f);
  const float stability =
      1.0f - std::clamp(static_cast<float>(current_state.frames_without_detection) /
                            static_cast<float>(std::max(1, LockState::kMaxMissFrames)),
                        0.0f, 1.0f);
  const float id_consistency =
      (current_state.IsLocked() && current_state.target.track_id == c.track_id) ? 1.0f : 0.5f;
  const float raw = (p.health_conf_weight * conf) + (p.health_center_weight * center) +
                    (p.health_stability_weight * stability) + (p.health_id_weight * id_consistency);
  return std::clamp(raw, 0.0f, 1.0f);
}

/**
 * @brief Bir karedeki adaylardan yeni kilit durumunu hesaplayan ana politika fonksiyonu.
 *
 * Akış: (1) parametreleri güvenli aralığa kırp ve sağlık ağırlıklarını normalize et,
 * (2) moda göre en iyi adayı seç (kilitliyken aynı iz tercih edilir),
 * (3) gerekirse kimlik değişimi (id-switch) toleransıyla yakın bir adayı kabul et,
 * (4) ROI/AV kontrolü ve şartname 4 sn zamanlayıcısını güncelle,
 * (5) aday bulunamazsa kayıp sayaçlarını artır, tolerans bütçesi içinde grace ile
 * kilidi koru, bütçe aşılır/azami kayıp dolarsa kilidi sıfırla.
 *
 * @return Bu kare için hesaplanmış yeni @ref LockState (girdi durumunu değiştirmez).
 */
inline LockState UpdateLockStateFromCandidates(
    const LockState& current_state, const std::vector<DetectionCandidate>& candidates,
    const float frame_w, const float frame_h, const LockMode mode,
    const std::chrono::steady_clock::time_point now, const LockSelectionParams& raw_params) {
  // Parametreleri güvenli aralıklara kırp (env'den gelebilecek hatalı değerlere karşı).
  LockSelectionParams params = raw_params;
  params.new_lock_min_conf = std::clamp(params.new_lock_min_conf, 0.0f, 1.0f);
  params.keep_lock_min_conf = std::clamp(params.keep_lock_min_conf, 0.0f, 1.0f);
  params.confidence_weight = std::clamp(params.confidence_weight, 0.0f, 1.0f);
  params.center_weight = std::clamp(params.center_weight, 0.0f, 1.0f);
  params.id_switch_grace_norm_dist = std::max(params.id_switch_grace_norm_dist, 0.0f);
  params.id_switch_min_conf = std::clamp(params.id_switch_min_conf, 0.0f, 1.0f);
  params.id_switch_min_iou = std::clamp(params.id_switch_min_iou, 0.0f, 1.0f);
  params.id_switch_min_aspect_ratio =
      std::clamp(params.id_switch_min_aspect_ratio, 0.0f, 1.0f);
  params.baseline_min_conf = std::clamp(params.baseline_min_conf, 0.0f, 1.0f);
  params.roi_x_min = std::clamp(params.roi_x_min, 0.0f, 1.0f);
  params.roi_x_max = std::clamp(params.roi_x_max, params.roi_x_min, 1.0f);
  params.roi_y_min = std::clamp(params.roi_y_min, 0.0f, 1.0f);
  params.roi_y_max = std::clamp(params.roi_y_max, params.roi_y_min, 1.0f);
  params.av_margin_norm = std::clamp(params.av_margin_norm, 0.03f, 0.40f);
  params.interior_bad_tolerance_ms =
      std::clamp(params.interior_bad_tolerance_ms, 0, LockState::kInteriorBadToleranceMs);
  params.grace_miss_frames = std::clamp(params.grace_miss_frames, 0, LockState::kMaxMissFrames);
  params.health_conf_weight = std::max(params.health_conf_weight, 0.0f);
  params.health_center_weight = std::max(params.health_center_weight, 0.0f);
  params.health_stability_weight = std::max(params.health_stability_weight, 0.0f);
  params.health_id_weight = std::max(params.health_id_weight, 0.0f);
  {
    const float sum = params.health_conf_weight + params.health_center_weight +
                      params.health_stability_weight + params.health_id_weight;
    if (sum > 1e-6f) {
      params.health_conf_weight /= sum;
      params.health_center_weight /= sum;
      params.health_stability_weight /= sum;
      params.health_id_weight /= sum;
    } else {
      params.health_conf_weight = 0.35f;
      params.health_center_weight = 0.25f;
      params.health_stability_weight = 0.25f;
      params.health_id_weight = 0.15f;
    }
  }

  LockState next = current_state;
  DetectionCandidate best{};
  bool found_any = false;
  float best_metric = -1.0f;
  bool has_candidates = !candidates.empty();
  bool has_low_conf_reject = false;
  bool id_switched = false;
  next.reason = LockReason::kNone;
  next.grace_ms_remaining = 0;

  // 1) Mod'a göre en iyi adayı seç. Kilitliyken aynı track_id öncelikli (istikrar).
  for (const auto& c : candidates) {
    if (mode == LockMode::kHybrid) {
      // Hibrit: kilitli iz yeterli güvene sahipse doğrudan korunur.
      if (current_state.IsLocked() && c.track_id == current_state.target.track_id &&
          c.confidence >= params.keep_lock_min_conf) {
        best = c;
        found_any = true;
        break;
      }
      if (current_state.IsLocked() && c.track_id == current_state.target.track_id &&
          c.confidence < params.keep_lock_min_conf) {
        has_low_conf_reject = true;
      }
      if (!current_state.IsLocked() && c.confidence >= params.new_lock_min_conf) {
        const float center_score = ComputeCenterScore(c.cx, c.cy, frame_w, frame_h);
        const float score =
            (params.confidence_weight * c.confidence) + (params.center_weight * center_score);
        if (score > best_metric) {
          best_metric = score;
          best = c;
          found_any = true;
        }
      } else if (!current_state.IsLocked() && c.confidence < params.new_lock_min_conf) {
        has_low_conf_reject = true;
      }
      continue;
    }

    if (current_state.IsLocked() && c.track_id == current_state.target.track_id) {
      if (c.confidence >= params.baseline_min_conf) {
        best = c;
        found_any = true;
        break;
      }
      has_low_conf_reject = true;
      continue;
    }
    if (!current_state.IsLocked() && c.confidence >= params.baseline_min_conf &&
        c.confidence > best.confidence) {
      best = c;
      found_any = true;
    } else if (!current_state.IsLocked() && c.confidence < params.baseline_min_conf) {
      has_low_conf_reject = true;
    }
  }

  // 2) Kilitli iz bu karede bulunamadıysa: farklı track_id için önce IoU, sonra
  //    mesafe tabanlı id-switch toleransı uygulanır (NvDCF yaklaşma/manevra ID değişimi).
  if (!found_any && current_state.IsLocked()) {
    const BBox locked_bbox = BBoxFromLockedTarget(current_state.target);
    for (const auto& c : candidates) {
      if (c.track_id == current_state.target.track_id) {
        continue;
      }
      if (c.confidence < params.id_switch_min_conf) {
        continue;
      }
      if (!IsCompatibleIdSwitchTarget(current_state.target, c,
                                      params.id_switch_min_aspect_ratio)) {
        continue;
      }
      const float iou = calculate_iou(locked_bbox, BBoxFromCandidate(c));
      if (iou > params.id_switch_min_iou) {
        best = c;
        found_any = true;
        id_switched = true;
        break;
      }
      const float norm_dist =
          ComputeNormalizedDistance(current_state.target, c, frame_w, frame_h);
      if (norm_dist <= params.id_switch_grace_norm_dist) {
        best = c;
        found_any = true;
        id_switched = true;
        break;
      }
    }
  }

  // 3) Seçilen aday ROI dışındaysa kilit kabul edilmez (roi_out nedeniyle).
  const bool roi_in_candidate =
      found_any && IsInRoi(best.cx, best.cy, frame_w, frame_h, params);
  if (found_any && !roi_in_candidate) {
    next.roi_in = false;
    next.reason = LockReason::kRoiOut;
    found_any = false;
  }

  if (found_any && roi_in_candidate) {
    const bool same_track =
        current_state.IsLocked() && (current_state.target.track_id == best.track_id);
    const float switch_iou =
        calculate_iou(BBoxFromLockedTarget(current_state.target), BBoxFromCandidate(best));
    const bool preserve_timer_on_switch =
        !same_track && current_state.IsLocked() &&
        IsCompatibleIdSwitchTarget(current_state.target, best,
                                   params.id_switch_min_aspect_ratio) &&
        (best.confidence >= params.id_switch_min_conf) &&
        ((switch_iou > params.id_switch_min_iou) ||
         (ComputeNormalizedDistance(current_state.target, best, frame_w, frame_h) <=
          params.id_switch_grace_norm_dist));

    next.target.track_id = best.track_id;
    next.target.cx = best.cx;
    next.target.cy = best.cy;
    next.target.w = best.w;
    next.target.h = best.h;
    next.target.confidence = best.confidence;
    next.target.class_id = best.class_id;
    next.target.locked = true;
    next.frames_without_detection = 0;
    next.roi_in = true;
    next.av_in = IsInAttackArea(best.cx, best.cy, frame_w, frame_h, params.av_margin_norm);
    next.lock_health = ComputeLockHealth(best, current_state, frame_w, frame_h, params);
    if (id_switched || (!same_track && current_state.IsLocked())) {
      next.reason = LockReason::kIdSwitch;
    } else {
      next.reason = LockReason::kNone;
    }

    next.last_policy_update_tp = now;
    if (!same_track && !preserve_timer_on_switch) {
      next.lock_interior_bad_ms = 0;
    } else {
      next.lock_interior_bad_ms = current_state.lock_interior_bad_ms;
    }
    UpdateSartnameLockTimer(next, current_state, same_track, preserve_timer_on_switch,
                            next.av_in, params.interior_bad_tolerance_ms, now);
    if (next.valid_lock) {
      next.lock_interior_bad_ms = 0;
    }
    return next;
  }

  // 4) Bu karede geçerli aday yok: kayıp sayaçlarını ve tolerans bütçesini güncelle.
  const int frame_dt_ms = FrameDeltaMs(current_state, now);
  next.last_policy_update_tp = now;
  next.frames_without_detection = current_state.frames_without_detection + 1;
  if (!current_state.valid_lock) {
    next.lock_interior_bad_ms = current_state.lock_interior_bad_ms + frame_dt_ms;
  } else {
    next.lock_interior_bad_ms = 0;
  }
  next.roi_in = false;
  next.lock_health = std::max(0.0f, next.lock_health - 0.03f);
  if (current_state.IsLocked()) {
    next.target = current_state.target;
    next.target.locked = true;
    if (mode == LockMode::kBaseline && has_low_conf_reject) {
      next.target.locked = false;
      next.valid_lock = false;
      next.lock_start_time = std::chrono::steady_clock::time_point{};
      next.lock_interior_bad_ms = 0;
      next.reason = LockReason::kLowConf;
      return next;
    }
    // Sartname: 4 sn streak boyunca toplam ara kayip <= interior_bad_tolerance_ms (%5 = 200 ms).
    // valid_lock dogrulandiktan sonra butce birikimi kilidi dusurmemeli.
    const bool timer_budget_ok =
        current_state.valid_lock ||
        next.lock_interior_bad_ms <= params.interior_bad_tolerance_ms;
    if (timer_budget_ok) {
      const int remaining_ms =
          params.interior_bad_tolerance_ms - next.lock_interior_bad_ms;
      next.grace_ms_remaining = std::max(0, remaining_ms);
      next.reason = LockReason::kGraceHold;
      next.av_in = current_state.av_in;
      next.lock_start_time = current_state.lock_start_time;
      next.valid_lock = current_state.valid_lock;
      if (next.frames_without_detection >= LockState::kMaxMissFrames) {
        const LockReason reset_reason = next.reason;
        next.Reset();
        next.reason = reset_reason;
      }
      return next;
    }
    // Butce asildi: 4 sn sayaci sifirlanir; interior_bad birikimi korunur (toplam kayip).
    next.lock_start_time = std::chrono::steady_clock::time_point{};
    next.valid_lock = false;
  } else {
    next.lock_start_time = std::chrono::steady_clock::time_point{};
    next.valid_lock = false;
    next.lock_interior_bad_ms = 0;
  }
  next.av_in = false;
  if (next.reason == LockReason::kNone) {
    if (has_low_conf_reject) {
      next.reason = LockReason::kLowConf;
    } else if (has_candidates) {
      next.reason = LockReason::kRoiOut;
    } else {
      next.reason = LockReason::kMissed;
    }
  }
  // Azami kayıp kare aşıldıysa kilidi tamamen sıfırla (nedeni koruyarak).
  if (next.frames_without_detection >= LockState::kMaxMissFrames) {
    const LockReason reset_reason = next.reason;
    next.Reset();
    next.reason = reset_reason;
  }
  return next;
}

}  // namespace savasan::tracking::policy

#endif  // SAVASAN_TRACKING_LOCK_SELECTION_POLICY_HPP_
