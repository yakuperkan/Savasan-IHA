/**
 * @file track_selector.cpp
 * @brief @ref savasan::tracking::TrackSelector uygulaması.
 *
 * DeepStream batch metadata'sını gezer, aday tespitleri toplar, saf kilit
 * politikasını (@ref lock_selection_policy.hpp) uygular ve OSD (ekran üstü)
 * çizimlerini (kutu/etiket, AV karesi, hata vektörü, durum banner'ı) üretir.
 * Kilit seçim parametreleri ortam değişkenlerinden okunur ve 2 sn önbelleklenir.
 */
#include "tracking/track_selector.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <gst/gst.h>

// DeepStream metadata API
#include "nvdsmeta.h"
#include "gstnvdsmeta.h"
#include "common/log.hpp"
#include "deepstream/ds_app.hpp"
#include "runners/phase5_runtime.hpp"
#include "tracking/lock_selection_policy.hpp"

namespace savasan::tracking {


/// @brief Takip edilmeyen nesneler için DeepStream kimliği.
static constexpr uint64_t kUntrackedId =
    static_cast<uint64_t>(UNTRACKED_OBJECT_ID);

/// @brief HUD (AV karesi ve hata vektörü) çizim parametreleri.
struct LockHudParams {
  float area_margin_norm = 0.05f;  ///< Şartname: görüntünün kenarlarından %5 kırpılmış merkez alan.
  int area_line_width = 1;         ///< AV kare çizgi kalınlığı (px).
  int vector_line_width = 1;       ///< Hata vektörü çizgi kalınlığı (px).
};

/// @brief HUD parametrelerini ortam değişkenlerinden (güvenli aralığa kırparak) okur.
LockHudParams LoadLockHudParamsFromEnv() {
  LockHudParams p{};
  const auto read_env_float = [](const char* key, const float fallback) {
    const char* raw = std::getenv(key);
    if (raw == nullptr || raw[0] == '\0') return fallback;
    char* end = nullptr;
    const float v = std::strtof(raw, &end);
    return (end == raw) ? fallback : v;
  };
  const auto read_env_int = [](const char* key, const int fallback) {
    const char* raw = std::getenv(key);
    if (raw == nullptr || raw[0] == '\0') return fallback;
    char* end = nullptr;
    const long v = std::strtol(raw, &end, 10);
    return (end == raw) ? fallback : static_cast<int>(v);
  };
  p.area_margin_norm = std::clamp(read_env_float("SAVASAN_LOCK_AREA_MARGIN_NORM", p.area_margin_norm),
                                  0.0f, 0.40f);
  p.area_line_width = std::clamp(read_env_int("SAVASAN_LOCK_AREA_LINE_WIDTH", p.area_line_width), 1, 3);
  p.vector_line_width =
      std::clamp(read_env_int("SAVASAN_LOCK_VECTOR_LINE_WIDTH", p.vector_line_width), 1, 3);
  return p;
}

/// @brief OSD çizgi parametrelerini opak beyaz bir çizgi olarak doldurur.
void SetWhiteLine(NvOSD_LineParams* line, const int x1, const int y1, const int x2, const int y2,
                  const int width) {
  line->x1 = x1;
  line->y1 = y1;
  line->x2 = x2;
  line->y2 = y2;
  line->line_width = width;
  line->line_color.red = 1.0f;
  line->line_color.green = 1.0f;
  line->line_color.blue = 1.0f;
  line->line_color.alpha = 1.0f;
}

/// @brief OSD dikdörtgen parametrelerini kırmızı kenarlıklı (dolgusuz) bir kutu olarak doldurur.
void SetRedRect(NvOSD_RectParams* rect, const float left, const float top, const float width,
                const float height, const int border_width) {
  rect->left = left;
  rect->top = top;
  rect->width = width;
  rect->height = height;
  rect->border_width = border_width;
  rect->has_bg_color = 0;
  rect->has_color_info = 1;
  rect->border_color.red = 1.0f;
  rect->border_color.green = 0.0f;
  rect->border_color.blue = 0.0f;
  rect->border_color.alpha = 1.0f;
}

/// @brief Bir nesnenin kutusunu ve etiket metnini gizler (kilit-dışı nesneleri saklamak için).
void HideObjectBbox(NvDsObjectMeta* obj) {
  if (obj == nullptr) {
    return;
  }
  obj->rect_params.border_width = 0;
  if (obj->text_params.display_text != nullptr) {
    g_free(obj->text_params.display_text);
    obj->text_params.display_text = nullptr;
  }
}

/// @brief OSD etiket metnini yalnızca değiştiyse günceller (gereksiz bellek ayırmayı önler).
void UpdateDisplayTextIfChanged(NvOSD_TextParams* text_params, const char* next_text) {
  if (text_params == nullptr || next_text == nullptr) return;
  if (text_params->display_text != nullptr &&
      std::strcmp(text_params->display_text, next_text) == 0) {
    return;
  }
  if (text_params->display_text != nullptr) {
    g_free(text_params->display_text);
    text_params->display_text = nullptr;
  }
  text_params->display_text = g_strdup(next_text);
}

/// @brief Kilit seçim parametrelerini ortam değişkenlerinden okur (eksikler varsayılan kalır).
policy::LockSelectionParams LoadSelectionParamsFromEnv() {
  policy::LockSelectionParams p{};
  const auto read_env_float = [](const char* key, const float fallback) {
    const char* raw = std::getenv(key);
    if (raw == nullptr || raw[0] == '\0') return fallback;
    char* end = nullptr;
    const float v = std::strtof(raw, &end);
    return (end == raw) ? fallback : v;
  };
  const auto read_env_int = [](const char* key, const int fallback) {
    const char* raw = std::getenv(key);
    if (raw == nullptr || raw[0] == '\0') return fallback;
    char* end = nullptr;
    const long v = std::strtol(raw, &end, 10);
    return (end == raw) ? fallback : static_cast<int>(v);
  };
  p.new_lock_min_conf =
      read_env_float("SAVASAN_LOCK_NEW_MIN_CONF", p.new_lock_min_conf);
  p.keep_lock_min_conf =
      read_env_float("SAVASAN_LOCK_KEEP_MIN_CONF", p.keep_lock_min_conf);
  p.confidence_weight =
      read_env_float("SAVASAN_LOCK_CONF_WEIGHT", p.confidence_weight);
  p.center_weight = read_env_float("SAVASAN_LOCK_CENTER_WEIGHT", p.center_weight);
  p.id_switch_grace_norm_dist = read_env_float(
      "SAVASAN_LOCK_ID_SWITCH_GRACE_NORM_DIST", p.id_switch_grace_norm_dist);
  p.id_switch_min_conf =
      read_env_float("SAVASAN_LOCK_ID_SWITCH_MIN_CONF", p.id_switch_min_conf);
  p.id_switch_min_iou =
      read_env_float("SAVASAN_LOCK_ID_SWITCH_MIN_IOU", p.id_switch_min_iou);
  p.roi_x_min = read_env_float("SAVASAN_LOCK_ROI_X_MIN", p.roi_x_min);
  p.roi_x_max = read_env_float("SAVASAN_LOCK_ROI_X_MAX", p.roi_x_max);
  p.roi_y_min = read_env_float("SAVASAN_LOCK_ROI_Y_MIN", p.roi_y_min);
  p.roi_y_max = read_env_float("SAVASAN_LOCK_ROI_Y_MAX", p.roi_y_max);
  p.av_margin_norm =
      read_env_float("SAVASAN_LOCK_AV_MARGIN_NORM", p.av_margin_norm);
  p.interior_bad_tolerance_ms =
      read_env_int("SAVASAN_LOCK_INTERIOR_BAD_MS", p.interior_bad_tolerance_ms);
  p.grace_miss_frames =
      read_env_int("SAVASAN_LOCK_GRACE_MISS_FRAMES", p.grace_miss_frames);
  p.min_track_frames =
      read_env_int("SAVASAN_LOCK_MIN_TRACK_FRAMES", p.min_track_frames);
  p.health_conf_weight =
      read_env_float("SAVASAN_LOCK_HEALTH_CONF_WEIGHT", p.health_conf_weight);
  p.health_center_weight = read_env_float("SAVASAN_LOCK_HEALTH_CENTER_WEIGHT",
                                          p.health_center_weight);
  p.health_stability_weight = read_env_float("SAVASAN_LOCK_HEALTH_STABILITY_WEIGHT",
                                             p.health_stability_weight);
  p.health_id_weight =
      read_env_float("SAVASAN_LOCK_HEALTH_ID_WEIGHT", p.health_id_weight);
  return p;
}

/// @brief Kilit seçim parametrelerini 2 sn'de bir env'den yenileyerek önbellekler.
/// Her karede env okumanın maliyetini önler; çalışma sırasında ayar değişikliğine izin verir.
policy::LockSelectionParams CachedLockSelectionParams() {
  static std::mutex mu;
  static policy::LockSelectionParams cached = LoadSelectionParamsFromEnv();
  static std::chrono::steady_clock::time_point last_reload{};
  static constexpr auto kReloadInterval = std::chrono::seconds(2);

  const auto now = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lk(mu);
  if (last_reload.time_since_epoch().count() == 0 ||
      (now - last_reload) >= kReloadInterval) {
    cached = LoadSelectionParamsFromEnv();
    last_reload = now;
  }
  return cached;
}

namespace {

/// @brief Loglanacak kilit durum geçişi türü.
enum class LockTransitionLog {
  kNone,          ///< Loglanacak geçiş yok.
  kLockConfirmed, ///< 4 sn AV kilidi doğrulandı (başarılı vuruş).
  kNewLock,       ///< Yeni bir hedefe kilitlenildi.
  kLockLost,      ///< Kilit kaybedildi.
};

}  // namespace

// ---------------------------------------------------------------------------
// ProbeCallback — GStreamer akış (streaming) iş parçacığında çağrılır.
// Buffer'ı doğrular ve bağlamdaki seçiciye iş yükünü devreder.
// ---------------------------------------------------------------------------
GstPadProbeReturn TrackSelector::ProbeCallback(GstPad* /*pad*/,
                                                GstPadProbeInfo* info,
                                                gpointer userdata) {
  if (!(info->type & GST_PAD_PROBE_TYPE_BUFFER)) {
    return GST_PAD_PROBE_OK;
  }
  GstBuffer* buf = GST_PAD_PROBE_INFO_BUFFER(info);
  if (!buf) return GST_PAD_PROBE_OK;

  auto* ctx = static_cast<TrackSelectorProbeCtx*>(userdata);
  if (ctx == nullptr || ctx->selector == nullptr) {
    return GST_PAD_PROBE_OK;
  }
  ctx->selector->UpdateFromBuffer(buf, ctx->phase5);
  return GST_PAD_PROBE_OK;
}

// ---------------------------------------------------------------------------
// UpdateFromBuffer — batch metadata'yı gezer, en iyi hedefi seçer, kilit
// durumunu günceller, geçiş loglarını basar ve OSD çizimlerini yapar.
// ---------------------------------------------------------------------------
void TrackSelector::UpdateFromBuffer(GstBuffer* buffer,
                                     savasan::runners::Phase5Runtime* phase5) {
  NvDsBatchMeta* batch_meta = gst_buffer_get_nvds_batch_meta(buffer);
  const auto now = std::chrono::steady_clock::now();

  static thread_local char txt_buf[128];
  static thread_local std::vector<policy::DetectionCandidate> candidates;
  static thread_local std::vector<NvDsObjectMeta*> frame_objects;
  static thread_local std::unordered_set<uint64_t> seen_track_ids;
  candidates.clear();
  frame_objects.clear();
  seen_track_ids.clear();

  float frame_w = last_frame_w_;
  float frame_h = last_frame_h_;

  LockState prev;
  uint64_t last_logged_id = kUntrackedId;
  uint64_t locked_track_id = kUntrackedId;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    prev = state_;
    last_logged_id = last_logged_id_;
    if (prev.IsLocked()) {
      locked_track_id = prev.target.track_id;
    }
  }

  const policy::LockSelectionParams params = CachedLockSelectionParams();
  const int min_track_frames = std::clamp(params.min_track_frames, 1, 30);

  // 1. Tek geçişte aday topla + OSD için nesne işaretçilerini sakla.
  if (batch_meta) {
    for (NvDsFrameMetaList* fl = batch_meta->frame_meta_list; fl; fl = fl->next) {
      auto* frame = static_cast<NvDsFrameMeta*>(fl->data);
      if (!frame) continue;
      if (frame->pipeline_width > 0) {
        frame_w = static_cast<float>(frame->pipeline_width);
      }
      if (frame->pipeline_height > 0) {
        frame_h = static_cast<float>(frame->pipeline_height);
      }
      for (NvDsObjectMetaList* ol = frame->obj_meta_list; ol; ol = ol->next) {
        auto* obj = static_cast<NvDsObjectMeta*>(ol->data);
        if (!obj) continue;
        frame_objects.push_back(obj);

        const float conf = (obj->confidence >= 0.0f) ? obj->confidence : obj->tracker_confidence;
        if (obj->object_id == kUntrackedId) continue;
        const float w = obj->rect_params.width;
        const float h = obj->rect_params.height;
        // Uzak UAV kutusu piksel olarak cok kucuk olabilir; track_id varsa aday kabul et.
        if (w <= 0.0f || h <= 0.0f || !std::isfinite(w) || !std::isfinite(h)) continue;

        seen_track_ids.insert(obj->object_id);
        const int consecutive_frames = track_consecutive_frames_[obj->object_id] + 1;
        if (consecutive_frames < min_track_frames && obj->object_id != locked_track_id) {
          continue;
        }

        policy::DetectionCandidate cand{};
        cand.track_id = obj->object_id;
        cand.cx = obj->rect_params.left + (w * 0.5f);
        cand.cy = obj->rect_params.top + (h * 0.5f);
        cand.w = w;
        cand.h = h;
        cand.confidence = conf;
        cand.class_id = obj->class_id;
        candidates.push_back(cand);
      }
    }
    last_frame_w_ = frame_w;
    last_frame_h_ = frame_h;
  }

  {
    std::unordered_map<uint64_t, int> next_frames;
    next_frames.reserve(seen_track_ids.size());
    for (const uint64_t track_id : seen_track_ids) {
      next_frames[track_id] = track_consecutive_frames_[track_id] + 1;
    }
    track_consecutive_frames_ = std::move(next_frames);
  }

  // 2. Saf politika ile yeni kilit durumunu hesapla (env parametreleri 2 sn önbellekli).
  // batch_meta yoksa boş aday listesiyle kayıp sayacı ilerler.
  LockState next =
      policy::UpdateLockStateFromCandidates(prev, candidates, frame_w, frame_h, mode_, now, params);

  // Durum geçişini tespit et (doğrulanan/yeni/kaybedilen kilit) — loglama için.
  LockTransitionLog transition = LockTransitionLog::kNone;
  uint64_t new_logged_id = last_logged_id;
  if (!prev.valid_lock && next.valid_lock) {
    transition = LockTransitionLog::kLockConfirmed;
  }
  if (next.target.locked && next.target.track_id != last_logged_id) {
    transition = LockTransitionLog::kNewLock;
    new_logged_id = next.target.track_id;
  }
  if (prev.IsLocked() && !next.IsLocked()) {
    transition = LockTransitionLog::kLockLost;
    new_logged_id = kUntrackedId;
  }

  {
    std::lock_guard<std::mutex> lk(mutex_);
    state_ = next;
    last_logged_id_ = new_logged_id;
  }

  switch (transition) {
    case LockTransitionLog::kLockConfirmed: {
      const float nx = next.target.cx / std::max(frame_w, 1.0f);
      const float ny = next.target.cy / std::max(frame_h, 1.0f);
      savasan::common::Log(savasan::common::LogLevel::kInfo, "TrackSelector",
                           "BASARILI VURUS (4sn AV kilit): track_id=" +
                               std::to_string(next.target.track_id) +
                               " paket={\"kilitlenmeGecerli\":true,\"hedefNormX\":" +
                               std::to_string(nx) + ",\"hedefNormY\":" + std::to_string(ny) +
                               ",\"trackKimligi\":" +
                               std::to_string(static_cast<unsigned long>(next.target.track_id)) +
                               "}");
      break;
    }
    case LockTransitionLog::kNewLock:
      savasan::common::Log(savasan::common::LogLevel::kInfo, "TrackSelector",
                           "Yeni kilit: track_id=" + std::to_string(next.target.track_id) +
                               " conf=" + std::to_string(next.target.confidence));
      break;
    case LockTransitionLog::kLockLost:
      savasan::common::Log(savasan::common::LogLevel::kWarn, "TrackSelector",
                           "Kilit kayboldu, reset.");
      break;
    case LockTransitionLog::kNone:
      break;
  }

  // 3. Kutu/etiket OSD: kilit varken yalnızca hedef iz gösterilir (ekran kalabalığını azaltır).
  if (!batch_meta) {
    return;
  }
  const bool hide_non_lock_bbox = []() {
    const char* raw = std::getenv("SAVASAN_OSD_SHOW_ALL_DETECTIONS");
    return raw == nullptr || raw[0] == '\0' || std::strcmp(raw, "0") == 0;
  }();
  for (NvDsObjectMeta* obj : frame_objects) {
    if (hide_non_lock_bbox && next.target.locked &&
        obj->object_id != next.target.track_id) {
      HideObjectBbox(obj);
      continue;
    }
    savasan::deepstream::ApplyStandardObjStyle(obj);
    const float conf = (obj->confidence >= 0.0f) ? obj->confidence : obj->tracker_confidence;
    const char* label = (obj->obj_label[0] != '\0') ? obj->obj_label : "obj";
    std::snprintf(txt_buf, sizeof(txt_buf), "%.64s id=%lu conf=%.2f", label,
                  static_cast<unsigned long>(obj->object_id), conf);
    UpdateDisplayTextIfChanged(&obj->text_params, txt_buf);
  }

  // 4. Durum banner'ı (HUD): kilit süresi yalnızca nesne algılanınca.
  static thread_local char banner[96];
  static const LockHudParams hud_params = LoadLockHudParamsFromEnv();
  static const char* team_name = []() -> const char* {
    const char* raw = std::getenv("SAVASAN_OSD_TEAM_NAME");
    return (raw != nullptr && raw[0] != '\0') ? raw : "Elektrik AR-GE İHA";
  }();
  (void)phase5;
  // Sayaç ve hata vektörü: aktif algılama (veya grace) ve AV içinde.
  int lock_sec = 0;
  int lock_cs = 0;
  const bool counting_lock =
      next.target.locked &&
      (next.frames_without_detection == 0 || next.reason == LockReason::kGraceHold) &&
      (next.av_in || next.reason == LockReason::kGraceHold) &&
      next.lock_start_time.time_since_epoch().count() != 0;
  // Kullanıcı isteği: kilit süresi yalnızca nesne gerçekten algılanınca görünsün.
  const bool show_lock_duration = counting_lock && next.frames_without_detection == 0;
  if (show_lock_duration) {
    const double elapsed_s =
        std::chrono::duration<double>(now - next.lock_start_time).count();
    const double clamped_s = std::max(elapsed_s, 0.0);
    // Sartname AV: 4 sn dolunca OSD sayaci sifirlanir (4+ sn ek gosterim yok).
    constexpr double kAvLockCycleS = 4.0;
    const double display_s = std::fmod(clamped_s, kAvLockCycleS);
    lock_sec = static_cast<int>(display_s);
    lock_cs = static_cast<int>((display_s - static_cast<double>(lock_sec)) * 100.0);
    if (lock_cs < 0) {
      lock_cs = 0;
    } else if (lock_cs > 99) {
      lock_cs = 99;
    }
    std::snprintf(banner, sizeof(banner), "Kilit suresi : %d.%02ds", lock_sec, lock_cs);
  } else {
    banner[0] = '\0';
  }

  // 5. Her kareye OSD ekle: takım adı, (varsa) kilit süresi, AV karesi, hata vektörü.
  // Saat sağ üstte gst_app_helpers OSDSinkTimestampProbe ile çizilir (şartname).
  for (NvDsFrameMetaList* fl = batch_meta->frame_meta_list; fl; fl = fl->next) {
    auto* frame = static_cast<NvDsFrameMeta*>(fl->data);
    if (!frame) continue;
    const int fw = (frame->pipeline_width > 0) ? frame->pipeline_width : static_cast<int>(frame_w);
    const int fh = (frame->pipeline_height > 0) ? frame->pipeline_height : static_cast<int>(frame_h);
    NvDsDisplayMeta* dm = nvds_acquire_display_meta_from_pool(batch_meta);
    if (!dm) continue;
    savasan::deepstream::ResetDisplayMetaForAcquire(dm);

    // Hedef vurus alani (AV): önce hesapla — kilit süresi AV altina yerlesecek.
    const int margin_x = static_cast<int>(std::lround(static_cast<float>(fw) * hud_params.area_margin_norm));
    const int margin_y = static_cast<int>(std::lround(static_cast<float>(fh) * hud_params.area_margin_norm));
    const int usable_w = std::max(2, fw - (2 * margin_x));
    const int usable_h = std::max(2, fh - (2 * margin_y));
    const int side = std::max(2, std::min(usable_w, usable_h));
    const int av_left = std::max(0, (fw - side) / 2);
    const int av_top = std::max(0, (fh - side) / 2);
    const int av_bottom = av_top + side;

    dm->num_labels = show_lock_duration ? 2 : 1;

    // Sol üst: takım adı.
    NvOSD_TextParams& team_txt = dm->text_params[0];
    static thread_local gchar* s_cached_team = nullptr;
    if (s_cached_team == nullptr) {
      s_cached_team = g_strdup(team_name);
    }
    UpdateDisplayTextIfChanged(&team_txt, s_cached_team);
    team_txt.x_offset = 8;
    team_txt.y_offset = 6;
    team_txt.font_params.font_name = const_cast<gchar*>("Serif");
    team_txt.font_params.font_size = 9;
    team_txt.font_params.font_color.red = 1.0f;
    team_txt.font_params.font_color.green = 1.0f;
    team_txt.font_params.font_color.blue = 1.0f;
    team_txt.font_params.font_color.alpha = 1.0f;
    team_txt.set_bg_clr = 1;
    team_txt.text_bg_clr.red = 0.0f;
    team_txt.text_bg_clr.green = 0.0f;
    team_txt.text_bg_clr.blue = 0.0f;
    team_txt.text_bg_clr.alpha = 0.65f;

    // AV alaninin altinda (ortalanmis): kilit suresi — sadece nesne algilaninca.
    if (show_lock_duration) {
      NvOSD_TextParams& lock_txt = dm->text_params[1];
      static thread_local char s_prev_banner[96]{};
      static thread_local gchar* s_cached_banner = nullptr;
      const bool banner_changed =
          (s_cached_banner == nullptr) ||
          (std::strncmp(s_prev_banner, banner, sizeof(s_prev_banner)) != 0);
      if (banner_changed) {
        if (s_cached_banner != nullptr) {
          g_free(s_cached_banner);
        }
        s_cached_banner = g_strdup(banner);
        std::memcpy(s_prev_banner, banner, sizeof(s_prev_banner));
        s_prev_banner[sizeof(s_prev_banner) - 1] = '\0';
      }
      UpdateDisplayTextIfChanged(&lock_txt, s_cached_banner);
      // Yaklasik metin genisligi (9pt Serif ~7px/karakter) ile AV altina ortala.
      const int approx_text_w = static_cast<int>(std::strlen(banner)) * 7;
      lock_txt.x_offset = std::max(4, av_left + (side - approx_text_w) / 2);
      lock_txt.y_offset = std::min(fh - 24, av_bottom + 4);
      lock_txt.font_params.font_name = const_cast<gchar*>("Serif");
      lock_txt.font_params.font_size = 9;
      lock_txt.font_params.font_color.red = 0.2f;
      lock_txt.font_params.font_color.green = 1.0f;
      lock_txt.font_params.font_color.blue = 0.3f;
      lock_txt.font_params.font_color.alpha = 1.0f;
      lock_txt.set_bg_clr = 1;
      lock_txt.text_bg_clr.red = 0.0f;
      lock_txt.text_bg_clr.green = 0.0f;
      lock_txt.text_bg_clr.blue = 0.0f;
      lock_txt.text_bg_clr.alpha = 0.75f;
    }

    if (dm->num_rects < MAX_ELEMENTS_IN_DISPLAY_META) {
      SetRedRect(&dm->rect_params[dm->num_rects], static_cast<float>(av_left),
                 static_cast<float>(av_top), static_cast<float>(side),
                 static_cast<float>(side), hud_params.area_line_width);
      dm->num_rects++;
    }

    // Merkezden hedef merkezine beyaz surekli hata vektoru (AV icinde aktif algilama).
    if (counting_lock && dm->num_lines < MAX_ELEMENTS_IN_DISPLAY_META) {
      const int cx = fw / 2;
      const int cy = fh / 2;
      const int tx = static_cast<int>(std::lround(next.target.cx));
      const int ty = static_cast<int>(std::lround(next.target.cy));
      SetWhiteLine(&dm->line_params[dm->num_lines], cx, cy, tx, ty, hud_params.vector_line_width);
      dm->num_lines++;
    }
    nvds_add_display_meta_to_frame(frame, dm);
  }
}

// ---------------------------------------------------------------------------
LockState TrackSelector::GetState() const {
  std::lock_guard<std::mutex> lk(mutex_);
  return state_;
}

void TrackSelector::Reset() {
  std::lock_guard<std::mutex> lk(mutex_);
  state_.Reset();
  last_logged_id_ = kUntrackedId;
  track_consecutive_frames_.clear();
}

}  // namespace savasan::tracking
