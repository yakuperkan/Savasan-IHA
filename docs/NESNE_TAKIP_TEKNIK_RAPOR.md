# NESNE TAKİP (OBJECT TRACKING) ALGORİTMASI TEKNİK ANALİZ VE TASARIM RAPORU

**Proje:** Teknofest Savaşan İHA — Faz 5 (İHA takip + kaçış + güdüm)  
**Platform:** NVIDIA Jetson Orin NX (ARM64), DeepStream SDK 7.x, C++17  
**Kod tabanı:** `02_Ana_Sistem_CPP/`  
**Rapor tarihi:** 11 Temmuz 2026

---

## 1. GİRİŞ VE SİSTEM MİMARİSİ

### 1.1 Sistem Tanımı

Savaşan İHA workspace'inde nesne takibi **iki katmanlı** bir mimari ile gerçekleştirilmiştir. Birinci katman, NVIDIA DeepStream `nvtracker` eklentisi ve `libnvds_nvmultiobjecttracker.so` kütüphanesi üzerinde çalışan **NvDCF (Discriminative Correlation Filter)** tabanlı çoklu nesne takibidir (MOT). İkinci katman, MOT çıktısından yarışma şartnamesine uygun **tek hedef kilit seçimi** yapan `TrackSelector` ve `lock_selection_policy` modülleridir.

SORT, DeepSORT veya ByteTrack türevi özel bir C++ MOT implementasyonu **bulunmamaktadır**. Kalman filtresi, IoU hesaplaması ve veri ilişkilendirme SDK içinde kapalı kutu olarak yürütülmektedir. Uygulama kodu yalnızca `object_id` (track_id) tüketir ve kilit kararı verir.

| Katman | Bileşen | Sorumluluk |
|--------|---------|------------|
| Algılama | `nvinfer` + YOLO26 | Bbox + confidence üretimi |
| MOT | `nvtracker` + NvDCF | Kareler arası track_id atama |
| Kilit seçimi | `TrackSelector` | Tek hedef, 4 sn AV kilidi |
| Güdüm beslemesi | `WorldTargetEstimator` | Bbox → NED hata (MOT değil) |

### 1.2 Dosya Yapısı

```
02_Ana_Sistem_CPP/
├── src/tracking/
│   ├── track_selector.cpp/hpp      # GStreamer probe + OSD
│   ├── lock_selection_policy.hpp   # Saf kilit politikası
│   ├── lock_state.hpp              # LockState, LockedTarget
│   └── lock_mode.hpp               # kBaseline / kHybrid
├── src/deepstream/ds_app.cpp       # Pipeline: nvinfer → nvtracker
├── src/runners/
│   ├── phase3_runner.cpp           # Probe bağlantısı
│   ├── phase5_callbacks.cpp        # Seri kilit TX
│   └── lock_metrics.hpp            # Jitter metriği
└── config/deepstream/
    ├── tracker_config.yml          # NvDCF varsayılan profil
    ├── tracker_config_calm.yml
    ├── tracker_config_aggressive.yml
    └── config_infer_primary.txt    # YOLO + NMS IoU
```

**Runtime bağımlılığı (repo dışı):**

- GStreamer: `libnvdsgst_tracker.so`
- Düşük seviye: `/opt/nvidia/deepstream/deepstream/lib/libnvds_nvmultiobjecttracker.so`

### 1.3 Takip Akış Şeması

```
[USB Kamera] → [nvstreammux] → [nvinfer YOLO26] → [nvtracker NvDCF]
                                                      │
                    ┌─────────────────────────────────┼──────────────────────┐
                    ▼                                 ▼                      ▼
         TrackSelector::ProbeCallback      AlcLockSerialProbe          tee → OSD/UDP
                    │                                 │
                    ▼                                 ▼
              LockState güncelleme            PendingLockFrame → idle
                    │                                 │
                    └────────────┬────────────────────┘
                                 ▼
                    ProcessLockPipelineFrame
                    (LockMetrics, WorldTargetEstimator, seri TX)
```

**Kare döngüsü:**

1. YOLO her karede (veya AIR_LOCK modunda her 3. karede) bbox üretir.
2. NvDCF mevcut track'leri tahmin eder, YOLO kutularıyla eşleştirir, `object_id` yazar.
3. `TrackSelector` metadata'yı tarar, tek hedef seçer.
4. `AlcLockSerialProbe` normalize koordinatı seri protokole iletir.

---

## 2. MATEMATİKSEL TEMELLER VE TEORİK ALTYAPI

### 2.1 Mimari Ayrım

Workspace C++ kaynak kodunda `predict()`, `update()`, `associate_detections()` adlı fonksiyonlar **tanımlanmamıştır**. Bu işlemler `libnvds_nvmultiobjecttracker.so` içinde yürütülür. Matematiksel temeller iki bölümde ele alınmıştır:

- **Bölüm 2.2–2.4:** NvDCF MOT (SDK, `tracker_config.yml` yapılandırması)
- **Bölüm 2.5:** Uygulama katmanı kilit seçim matematiği

### 2.2 Durum Tahmini (State Estimation)

`tracker_config.yml` içinde `stateEstimatorType: 1` ile etkinleştirilen Kalman filtresi, normalize görüntü koordinatlarında çalışır. Tipik durum vektörü:

$$
\mathbf{x}_k = \begin{bmatrix} x_k \\ y_k \\ w_k \\ h_k \\ \dot{x}_k \\ \dot{y}_k \\ \dot{w}_k \\ \dot{h}_k \end{bmatrix}
$$

Burada $(x_k, y_k)$ bbox merkezi, $(w_k, h_k)$ genişlik ve yükseklik; değerler normalize $[0,1]$ ölçeğindedir.

**Yapılandırma dosyasından okunan gürültü parametreleri:**

```yaml
# tracker_config.yml — StateEstimator bloğu
StateEstimator:
  stateEstimatorType: 1
  processNoiseVar4Loc: 2.5      # Konum süreç gürültüsü σ²_xy
  processNoiseVar4Size: 1.2     # Boyut süreç gürültüsü σ²_wh
  processNoiseVar4Vel: 2.0      # Hız süreç gürültüsü σ²_v
  measurementNoiseVar4Detector: 4.0   # YOLO ölçüm gürültüsü R_det
  measurementNoiseVar4Tracker: 7.0    # NvDCF görsel ölçüm gürültüsü R_trk
```

### 2.3 Kalman Filtresi Denklemleri

**Zaman güncelleme (predict):**

$$
\hat{\mathbf{x}}_{k|k-1} = \mathbf{F}\,\hat{\mathbf{x}}_{k-1|k-1}, \qquad
\mathbf{P}_{k|k-1} = \mathbf{F}\,\mathbf{P}_{k-1|k-1}\,\mathbf{F}^{\mathsf{T}} + \mathbf{Q}
$$

Sabit hız modelinde:

$$
\mathbf{F} = \begin{bmatrix} \mathbf{I}_4 & \Delta t \cdot \mathbf{I}_4 \\ \mathbf{0} & \mathbf{I}_4 \end{bmatrix}
$$

**Ölçüm güncelleme (update):**

$$
\mathbf{K}_k = \mathbf{P}_{k|k-1}\,\mathbf{H}^{\mathsf{T}}\left(\mathbf{H}\,\mathbf{P}_{k|k-1}\,\mathbf{H}^{\mathsf{T}} + \mathbf{R}\right)^{-1}
$$

$$
\hat{\mathbf{x}}_{k|k} = \hat{\mathbf{x}}_{k|k-1} + \mathbf{K}_k\left(\mathbf{z}_k - \mathbf{H}\,\hat{\mathbf{x}}_{k|k-1}\right)
$$

Ölçüm $\mathbf{z}_k$ ya YOLO detector kutusundan ($\mathbf{R} = R_{\text{det}}$) ya da NvDCF görsel çıktısından ($\mathbf{R} = R_{\text{trk}}$) gelir.

### 2.4 Veri İlişkilendirme (Data Association)

#### IoU (Intersection over Union)

$$
\text{IoU}(A,B) = \frac{|A \cap B|}{|A \cup B|}
$$

YOLO NMS eşiği (`config_infer_primary.txt`):

```ini
[class-attrs-all]
nms-iou-threshold=0.45
pre-cluster-threshold=0.33
topk=80
```

Tracker hedef yönetimi (`tracker_config.yml`):

```yaml
TargetManagement:
  minIouDiff4NewTarget: 0.50    # Yeni track için min IoU farkı
  maxShadowTrackingAge: 12      # Oklüzyon altında ID koruma (kare)
  probationAge: 2               # Onay gecikmesi (kare)
```

#### Birleşik Eşleştirme Skoru

`associationMatcherType: 0` (GREEDY) — Hungarian algoritması **kullanılmamaktadır**.

$$
S_{\text{overall}} = w_v \cdot S_{\text{visual}} + w_s \cdot S_{\text{size}} + w_i \cdot S_{\text{iou}}
$$

Varsayılan ağırlıklar:

```yaml
DataAssociator:
  associationMatcherType: 0          # GREEDY (Hungarian değil)
  minMatchingScore4Overall: 0.10
  minMatchingScore4VisualSimilarity: 0.45
  minMatchingScore4SizeSimilarity: 0.55
  matchingScoreWeight4VisualSimilarity: 0.45
  matchingScoreWeight4SizeSimilarity: 0.25
  matchingScoreWeight4Iou: 0.30
```

#### NvDCF Görsel Özellik (DCF)

```yaml
VisualTracker:
  visualTrackerType: 1     # NvDCF
  useColorNames: 1         # GPU renk özellikleri
  useHog: 0                # CPU HOG kapalı (performans)
  featureImgSizeLevel: 1   # 12×12 (en hafif)
  filterLr: 0.11           # DCF öğrenme oranı α
  gaussianSigma: 0.78      # Arama penceresi genişliği
```

DCF güncelleme:

$$
\mathbf{w}_k = (1 - \alpha)\,\mathbf{w}_{k-1} + \alpha\,\mathbf{w}_{\text{new}}, \quad \alpha = 0{,}11
$$

### 2.5 Uygulama Katmanı Kilit Seçim Matematiği

C++ kodunda MOT eşleştirmesi yapılmaz; mevcut `track_id` listesinden tek hedef seçilir.

**Merkez skoru** (`lock_selection_policy.hpp`):

$$
s_{\text{center}} = 1 - \min\!\left(\frac{(c_x - W/2)^2 + (c_y - H/2)^2}{(W/2)^2 + (H/2)^2},\; 1\right)
$$

```cpp
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
```

**Hibrit kilit skoru** (yeni kilit, `LockMode::kHybrid`):

$$
s_{\text{hybrid}} = 0{,}85 \cdot \text{conf} + 0{,}15 \cdot s_{\text{center}}
$$

**ID switch toleransı** (IoU değil, normalize Öklid mesafesi):

$$
d_{\text{norm}} = \sqrt{\left(\frac{c_x^a - c_x^b}{W}\right)^2 + \left(\frac{c_y^a - c_y^b}{H}\right)^2}
$$

Kabul koşulu: $d_{\text{norm}} \leq 0{,}08$ ve $\text{conf} \geq 0{,}35$

```cpp
inline float ComputeNormalizedDistance(const LockedTarget& a, const DetectionCandidate& b,
                                       const float frame_w, const float frame_h) {
  const float safe_w = std::max(frame_w, 1.0f);
  const float safe_h = std::max(frame_h, 1.0f);
  const float dx = (a.cx - b.cx) / safe_w;
  const float dy = (a.cy - b.cy) / safe_h;
  return std::sqrt((dx * dx) + (dy * dy));
}
```

---

## 3. YAZILIM MİMARİSİ VE FONKSİYONEL ANALİZ

### 3.1 Modül ve Nesne Yapısı

#### LockedTarget ve LockState

Kilitlenen hedefin bilgisi `lock_state.hpp` içinde modellenmiştir:

```cpp
struct LockedTarget {
  uint64_t track_id = UINT64_MAX;   // NVDS_UNTRACKED_OBJECT_ID = 0xFFFFFFFFFFFFFFFF
  float    cx = 0.0f;               // Kutu merkezi X (piksel)
  float    cy = 0.0f;
  float    w  = 0.0f;
  float    h  = 0.0f;
  float    confidence = 0.0f;
  int      class_id  = -1;
  bool     locked    = false;
};

struct LockState {
  LockedTarget target;
  int  frames_without_detection = 0;
  bool valid_lock = false;          // Şartname 4 sn AV kilidi doğrulandı mı
  bool roi_in = false;
  bool av_in = false;
  float lock_health = 0.0f;
  LockReason reason = LockReason::kNone;
  static constexpr int kMaxMissFrames = 45;           // ~0,75 sn @ 60 fps
  static constexpr std::chrono::seconds kRequiredLockDuration{4};
  static constexpr int kInteriorBadToleranceMs = 200; // %5 tolerans
};
```

#### DetectionCandidate

Politika girdisi (`lock_selection_policy.hpp`):

```cpp
struct DetectionCandidate {
  uint64_t track_id = UINT64_MAX;
  float cx = 0.0f, cy = 0.0f, w = 0.0f, h = 0.0f;
  float confidence = 0.0f;
  int class_id = -1;
};
```

### 3.2 DeepStream Pipeline Entegrasyonu

`ds_app.cpp` içinde tracker, `nvinfer` sonrasına eklenir:

```cpp
std::string BuildPhase3PipelineString(...) {
  const bool tracker_disable =
      (std::getenv("SAVASAN_TRACKER_DISABLE") != nullptr &&
       std::getenv("SAVASAN_TRACKER_DISABLE")[0] == '1');
  // ...
  tail << "! nvinfer config-file-path=" << primary_config_abs << " batch-size=1 ";
  if (!tracker_disable) {
    tail << "! nvtracker"
         << " ll-lib-file=" << ll_lib_abs
         << " ll-config-file=" << tracker_config_abs
         << " tracker-width=" << tracker_width
         << " tracker-height=" << tracker_height
         << " gpu-id=0 display-tracking-id=true ";
  }
  tail << BuildPhase3PostTrackerTail(sink);
  // ...
}
```

Tracker yolu `startup_config.cpp` içinde çözümlenir:

```cpp
constexpr char kDefaultNvDCFLib[] =
    "/opt/nvidia/deepstream/deepstream/lib/libnvds_nvmultiobjecttracker.so";
constexpr char kDefaultTrackerConfigRel[] = "config/deepstream/tracker_config.yml";
```

### 3.3 Probe Bağlantısı (phase3_runner.cpp)

Pad probe LIFO sırasıyla çalışır: `TrackSelector` önce, `AlcLockSerialProbe` sonra eklenir; böylece aynı karede güncel bbox okunur.

```cpp
// Tracker varsa nvtracker0/src, yoksa nvinfer0/src
GstElement* tracker_el = gst_bin_get_by_name(GST_BIN(pipeline), "nvtracker0");
if (tracker_el) {
  probe_target_pad = gst_element_get_static_pad(tracker_el, "src");
}
// AlcLock probe (sonra eklendi → sonra çalışır)
gst_pad_add_probe(probe_target_pad, GST_PAD_PROBE_TYPE_BUFFER,
                  AlcLockSerialProbe, &alc_ctx, nullptr);
// TrackSelector probe (son eklendi → önce çalışır)
gst_pad_add_probe(probe_target_pad, GST_PAD_PROBE_TYPE_BUFFER,
                  savasan::tracking::TrackSelector::ProbeCallback,
                  &ts_probe_ctx, nullptr);
```

### 3.4 TrackSelector::ProbeCallback()

**Amaç:** GStreamer buffer probe giriş noktası.

```cpp
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
```

### 3.5 TrackSelector::UpdateFromBuffer()

**Amaç:** Metadata tarama, politika uygulama, OSD çizimi.

**Adım 1 — Aday toplama:**

```cpp
for (NvDsObjectMetaList* ol = frame->obj_meta_list; ol; ol = ol->next) {
  auto* obj = static_cast<NvDsObjectMeta*>(ol->data);
  const float conf = (obj->confidence >= 0.0f) ? obj->confidence : obj->tracker_confidence;
  if (obj->object_id == kUntrackedId) continue;           // Takipsiz nesne atla
  if (w < 8.0f || h < 8.0f) continue;                     // Çok küçük bbox
  if (w_ratio < 0.05f && h_ratio < 0.05f) continue;       // Gürültü filtresi

  policy::DetectionCandidate cand{};
  cand.track_id = obj->object_id;
  cand.cx = obj->rect_params.left + (w * 0.5f);
  cand.cy = obj->rect_params.top + (h * 0.5f);
  cand.confidence = conf;
  candidates.push_back(cand);
}
```

**Adım 2 — Politika çağrısı:**

```cpp
LockState next = policy::UpdateLockStateFromCandidates(
    prev, candidates, frame_w, frame_h, mode_, now, params);
```

**Adım 3 — Thread-safe durum güncelleme:**

```cpp
{
  std::lock_guard<std::mutex> lk(mutex_);
  state_ = next;
}
```

### 3.6 UpdateLockStateFromCandidates()

**Amaç:** Saf, yan etkisiz kilit durumu makinesi. Girdi `LockState` değiştirilmez; yeni durum döndürülür.

**Hibrit mod — kilitli iz koruma:**

```cpp
if (current_state.IsLocked() && c.track_id == current_state.target.track_id &&
    c.confidence >= params.keep_lock_min_conf) {
  best = c;
  found_any = true;
  break;  // Aynı track_id öncelikli
}
```

**ID switch toleransı:**

```cpp
if (!found_any && current_state.IsLocked()) {
  for (const auto& c : candidates) {
    if (c.track_id == current_state.target.track_id) continue;
    if (c.confidence < params.id_switch_min_conf) continue;
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
```

**Şartname 4 sn zamanlayıcısı:**

```cpp
if (!next.valid_lock &&
    (now - next.lock_start_time) >= LockState::kRequiredLockDuration &&
    next.lock_interior_bad_ms <= interior_bad_tolerance_ms) {
  next.valid_lock = true;
}
```

**Kayıp kare reset:**

```cpp
if (next.frames_without_detection >= LockState::kMaxMissFrames) {
  const LockReason reset_reason = next.reason;
  next.Reset();
  next.reason = reset_reason;
}
```

### 3.7 AlcLockSerialProbe ve Seri TX

Probe hafif yolda `GetState()` okur, ağır işlem idle kuyruğuna aktarılır:

```cpp
const auto state = ctx->selector->GetState();
const float nx = state.target.cx / static_cast<float>(fw);
const float ny = state.target.cy / static_cast<float>(fh);

PendingLockFrame frame{};
frame.nx = nx;
frame.ny = ny;
frame.valid_lock = allow_air_lock && state.valid_lock && state.IsLocked();
frame.track_id = state.target.track_id;
// ...
ScheduleLockPipelineProcess(ctx);  // idle'da ProcessLockPipelineFrame
```

Seri kilit gönderimi (`ProcessLockPipelineFrame`):

```cpp
if (!apf_active && frame.state_is_locked) {
  savasan::autopilot::LockCoordinates c{};
  c.x = std::clamp(nx, 0.0f, 1.0f);
  c.y = std::clamp(ny, 0.0f, 1.0f);
  c.valid = true;
  c.track_id = ToProtocolTrackId(frame.track_id);
  lock_ok = TryEnqueueLockNonBlocking(ctx->bridge, c);
}
```

### 3.8 LockMetrics — Jitter Hesabı

Son 60 örneğin normalize merkez konumları üzerinde standart sapma:

```cpp
float ComputeJitterLocked() const {
  if (sample_count_ < 3) return 0.0f;
  // Ortalama (mx, my) hesapla
  float var = 0.0f;
  for (std::size_t i = 0; i < sample_count_; ++i) {
    const float dx = s.nx - mx;
    const float dy = s.ny - my;
    var += (dx * dx) + (dy * dy);
  }
  var *= inv_n;
  return std::sqrt(var);
}
```

Otomatik tracker profil önerisi (`phase5_guidance.cpp`):

```cpp
TrackerProfileRecommendation DecideAutoRecommendation(
    const float speed_mps, const float lock_jitter, ...) {
  if (speed_mps >= cfg.aggressive_speed_mps || lock_jitter >= cfg.aggressive_jitter) {
    return TrackerProfileRecommendation::kAggressive;
  }
  if (speed_mps <= cfg.calm_speed_mps && lock_jitter <= cfg.calm_jitter) {
    return TrackerProfileRecommendation::kCalm;
  }
  return previous;
}
```

### 3.9 Eşik Değerleri Özeti

| Parametre | Değer | Konum |
|-----------|-------|-------|
| `maxTargetsPerStream` | 4 | tracker_config.yml |
| `maxShadowTrackingAge` | 12 kare | tracker_config.yml |
| `probationAge` | 2 kare | tracker_config.yml |
| `nms-iou-threshold` | 0.45 | config_infer_primary.txt |
| `kMaxMissFrames` | 45 | lock_state.hpp |
| `kRequiredLockDuration` | 4 sn | lock_state.hpp |
| `id_switch_grace_norm_dist` | 0.08 | lock_selection_policy.hpp |
| `new_lock_min_conf` | 0.40 | lock_selection_policy.hpp |

**Profil karşılaştırması:**

| Parametre | default | calm | aggressive |
|-----------|---------|------|------------|
| $w_{\text{visual}}$ | 0.45 | 0.55 | 0.40 |
| $w_{\text{iou}}$ | 0.30 | 0.40 | 0.55 |
| `processNoiseVar4Vel` | 2.0 | 0.75 | 1.45 |

---

## 4. PERFORMANS, LATENCY VE DONANIM OPTİMİZASYONU

### 4.1 Baseline Ölçüm Sonuçları

Değişikliksiz config, MAXN güç modu, `SAVASAN_DISPLAY=` (fakesink), 25 sn koşu:

| Metrik | Ortalama | p95 |
|--------|----------|-----|
| FPS | 27.01 | 29.80 |
| End-to-end latency | 329.65 ms | 379.50 ms |
| GPU kullanımı | 99.04% | 99.20% |
| VDD_IN güç | 17.48 W | 19.83 W |
| TJ sıcaklık | 47.84 °C | 49.84 °C |

### 4.2 Gerçek Zamanlı Çalışabilirlik

**Optimizasyonlar:**

- `useHog: 0`, `useColorNames: 1` — CPU HOG kapatıldı, GPU-native özellik
- `featureImgSizeLevel: 1` — en hafif DCF çözünürlüğü (12×12)
- Probe sıcak yolu hafif: ağır işlemler GMainLoop idle kuyruğuna aktarılır
- `thread_local std::vector<DetectionCandidate>` — kare başına heap tahsisi önlenir
- Env parametreleri 2 sn önbelleklenir (`CachedLockSelectionParams`)

**Darboğazlar:**

- GPU %99 doygunlukta; YOLO FP16 inference baskın maliyet
- End-to-end latency ~330 ms

### 4.3 YOLO Inference Aralığı

Üretim config (`interval=0` — her karede YOLO):

```ini
[property]
interval=0
```

AIR_LOCK görev modu (`phase5_callbacks.cpp`):

```cpp
guint MissionNvinferIntervalFromEnv() {
  constexpr guint kDefaultInterval = 2u;  // Her 3. karede YOLO
  const char* s = std::getenv("SAVASAN_AIRLOCK_NVINFER_INTERVAL");
  // ...
}
```

NvDCF ara kareleri görsel tahminle taşır; GPU yükü azalır, tracker bağımlılığı artar.

### 4.4 Bellek Yönetimi

| Mekanizma | Açıklama |
|-----------|----------|
| `maxTargetsPerStream: 4` | SDK track havuzu üst sınırı |
| `candidates.clear()` | Kapasite korunarak yeniden kullanım |
| `UpdateDisplayTextIfChanged()` | Değişmeyen OSD etiketlerinde bellek ayırma atlanır |
| `kMaxSamples = 60` | LockMetrics dairesel tampon, sabit boyut |

### 4.5 Bileşen Gecikme Profili

`ds_app.cpp` içinde `LatencySlot::kTracker` ile `nvtracker` ayrı ölçülür:

```cpp
} else if (std::strstr(name, "nvtracker") != nullptr) {
  AccumulateSample(LatencySlot::kTracker, ms);
}
```

---

## 5. KAVRAMSAL EŞLEME TABLOSU

| Literatür / SORT | Savaşan İHA Karşılığı |
|------------------|----------------------|
| Detection | `nvinfer` YOLO26 |
| Track state $(x,y,w,h,v_x,v_y)$ | NvDCF `StateEstimator` (SDK) |
| `predict()` / `update()` | SDK Kalman + DCF (kapalı kutu) |
| IoU matching | SDK `DataAssociator` ($w_i=0.30$) |
| Hungarian assignment | **Kullanılmıyor** (GREEDY) |
| `max_age` | `maxShadowTrackingAge: 12` |
| `min_hits` | `probationAge: 2` |
| Re-ID embedding | **Kullanılmıyor** |
| Single-target lock | `TrackSelector` + `lock_selection_policy` |
| Güdüm kestirimi | `WorldTargetEstimator` (durumsuz, Kalman yok) |

---

## 6. SONUÇ

Savaşan İHA workspace'inde nesne takibi, **NVIDIA NvDCF tabanlı kapalı kutu MOT** ile **özelleştirilmiş tek hedef kilit politikası**nın birleşiminden oluşmaktadır. Kalman filtresi, IoU hesaplaması ve veri ilişkilendirme C++ kaynak kodunda implemente edilmemiş; `tracker_config.yml` üzerinden yapılandırılan SDK modüllerinde yürütülmektedir.

Uygulama katmanı (`src/tracking/`) MOT çıktısını tüketerek:

- Yarışma şartnamesine uygun **4 saniyelik AV kilidi** sağlar,
- **ID switch toleransı** ile NvDCF kimlik değişimlerine adaptasyon gösterir,
- **Seri protokol** ve **güdüm** katmanına normalize bbox beslemesi yapar.

Kod örnekleri bu raporda doğrudan üretim kaynaklarından alınmıştır; satır numaraları `02_Ana_Sistem_CPP/` altındaki güncel dosyalara karşılık gelmektedir.

---

*Rapor: `docs/NESNE_TAKIP_TEKNIK_RAPOR.md` — PDF: `docs/NESNE_TAKIP_TEKNIK_RAPOR.pdf`*
