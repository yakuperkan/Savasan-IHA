# Savaşan İHA — Güdüm Sistemi Kullanım Kılavuzu

**Hedef kitle:** Yarışma ve test günü saha mühendisleri  
**Platform:** Jetson Orin NX + USB BRIO kamera + Alacakart otopilot  
**Üretim modu:** Faz 5 (`phase5`), AIR_LOCK görev modu  
**Son güncelleme:** 2026-07-07 (YOLO her kare, infer/tracker spike optimizasyonu, NvDCF tuning rehberi)

---

## 1. Sistem Mimarisi Özeti

### 1.1 Temel ilke

Tüm PID hesabı **Jetson Orin NX** üzerinde yapılır. Alacakart yalnızca **uygulayıcı (effector)** rolündedir: Jetson'dan gelen nihai oran/açı komutunu servolara yansıtır; kendi içinde ayrı bir güdüm döngüsü çalıştırmaz.

### 1.2 Veri akışı

```
USB Kamera → DeepStream (YOLO @ her kare + NvDCF tracker)
         → Kilit koordinatı (0x01/0x11)  ← şartname kilidi
         → WorldTargetEstimator        ← piksel sapması → NED hata
         → PidControllerXYZYaw         ← Jetson PID
         → SetpointCommand (0x03)      ← NED oran paketi → Alacakart
```

**Algılama modu (üretim):** `config_infer_primary.txt` içinde `interval=0` → YOLO **her karede** çalışır. GPU yükü artar; tespit tazeliği ve kısa oklüzyon toleransı iyileşir. Düşük güç/latency için `interval=2` (her 3. karede YOLO) alternatif olarak `SAVASAN_PGI_CONFIG` ile denenebilir.

### 1.3 NED eksen semantiği

Sabit burun kamerası varsayımıyla görüntü merkezinden sapma NED hata sinyaline çevrilir:

| Eksen | Hata kaynağı | Anlam | Pozitif hata → PID çıkışı |
|-------|--------------|-------|---------------------------|
| **X (Kuzey)** | Bbox alanından tahmini mesafe − hedef mesafe | Kapanma hatası (m) | İleri kapanma (m/s) |
| **Y (Doğu)** | `(nx − 0.5) × PIXEL_TO_DEG` | Yatay açı hatası (deg) | Sağa yaw (deg/s) |
| **Z (Aşağı)** | `(ny − 0.5) × PIXEL_TO_DEG` | Dikey açı hatası (deg) | Aşağı pitch (deg/s); negatif = tırman |

**İşaret doğrulama (saha):**

- Hedef **sağda** → Doğu+ yaw komutu (kuyruk/yön servosu sağa).
- Hedef **yukarıda** → tırmanma yönünde pitch (negatif `vz` oranı).
- Hedef **uzakta** → kapanma (pozitif `vx`).

### 1.4 İki ayrı seri paket

| Paket | Amaç | Ne zaman gönderilir |
|-------|------|---------------------|
| **0x01 / 0x11** | Normalize kilit koordinatı | Şartname kilidi; tracker seçimi sonrası |
| **0x03** | NED oran setpoint | Güdüm döngüsü aktifken (`SAVASAN_GUIDANCE_MODE≠off`) |

Kilit paketi şartname için korunur; otonom takip davranışı **0x03** setpoint hattı ile sağlanır.

### 1.5 Oklüzyon davranışı

Güdüm döngüsünde hedef ölçümü geçersiz olduğunda (`latest_lock_valid == false` veya
`latest_target.valid == false`) Kalman tahmini **yoktur**. Davranış `SAVASAN_GUIDANCE_OCCLUSION_MODE`
ile seçilir:

| Mod | Env | Kod yolu | Davranış |
|-----|-----|----------|----------|
| **hold_last** | `hold_last` (varsayılan) | `TryOcclusionHoldLast()` | Son geçerli 0x03 komutu `SAVASAN_GUIDANCE_OCCLUSION_HOLD_MS` (vars. 400 ms) boyunca tekrarlanır; süre aşılırsa `SendZeroSetpointIfNeeded()` ile sıfırlanır. |
| **hold_zero** | `hold_zero` | `occlusion_hold_last=false` | Bekleme yok; kilit kaybı anında sıfır setpoint gönderilir, `guidance_active_prev` false olur. |

**Degrade modu:** `degraded.active=true` iken geçerli kilit olsa bile `RunGuidanceControlStep`
setpoint üretmez (erken çıkış). Donanım/iletişim hatası sonrası otonom komut kesilir.

Tüm PID hesabı Jetson'da kalır; Alacakart yalnızca 0x03 paketini uygular.

### 1.6 Telemetri tabanlı APF kaçış (özet)

Görüntü/bbox kaçışı kaldırıldı. Kaçış **RF rakip havuzu + GPS** ile 50 ms APF döngüsünde çalışır. APF aktifken PID ve lock TX kesilir; `0x03` setpoint önceliklidir.

Tam teknik kılavuz, env tablosu ve log analizi: **[APF_EVASION.md](APF_EVASION.md)**

Saha minimum env:

```bash
SAVASAN_EVASION_ENABLE=1
SAVASAN_LOG_LEVEL=INFO
SAVASAN_CONTROL_LOG_ENABLE=1
```

---

## 2. Çevre Değişkenleri Matrisi

İki katmanlı env yapısı kullanılır:

| Dosya | Yükleme sırası | İçerik |
|-------|----------------|--------|
| `/etc/savasan/stream_common.env` | Önce (systemd `EnvironmentFile`) | UDP canlı görüntü, Jetson MP4 kayıt |
| `/etc/savasan/savasan.env` | Sonra | Faz 5, güdüm, kamera, SIHA API |

Şablonlar: `02_Ana_Sistem_CPP/config/systemd/stream_common.env` ve `savasan.env`.

**Kurulum:** `sudo bash 02_Ana_Sistem_CPP/config/systemd/install.sh` — mevcut `savasan.env` korunur (`cp -n`); şablondaki **eksik anahtarlar** otomatik eklenir. `stream_common.env` her kurulumda güncellenir.

**Doğrulama:** `bash 02_Ana_Sistem_CPP/config/systemd/validate_env.sh /etc/savasan/savasan.env`  
Değişiklik sonrası: `sudo systemctl restart savasan-airlock`

**Eski env migrasyonu:** Kalman satırları kaldırıldı; oklüzyon anahtarları zorunlu. Eksikse:
`bash scripts/fix_savasan_env_occlusion.sh` (dosya yazılabilirse sudo gerekmez).

### 2.1 Güdüm anahtarları

| Değişken | Varsayılan | Aralık / Modlar | Saha Etkisi |
|----------|------------|-----------------|-------------|
| `SAVASAN_SETPOINT_TX_ENABLE` | `0` | `0` \| `1` | `0` = dry-run (0x03 seri hatta **yazılmaz**, loglanır). `1` = gerçek TX. **Uçuş öncesi `1` yapın.** |
| `SAVASAN_GUIDANCE_MODE` | `image_world` | `image_world` (veya `off` dışı herhangi bir değer) \| `off` | `off` = güdüm döngüsü kapalı. Üretim: `image_world` (görüntü → NED hata → PID). |
| `SAVASAN_GUIDANCE_PIXEL_TO_DEG` | `60` | 1.0 – 360.0 | Normalize piksel sapmasını dereceye çeviren kazanç. Yüksek = daha agresif dönüş/tırmanış. |
| `SAVASAN_GUIDANCE_OCCLUSION_MODE` | `hold_last` | `hold_last` \| `hold_zero` | Hedef kaybında son komutu koru veya anında sıfırla. Üretim: `hold_last`. |
| `SAVASAN_GUIDANCE_OCCLUSION_HOLD_MS` | `400` | 50 – 5000 (ms) | `hold_last` penceresi. Kısa YOLO kaçırmalarında silkelenmeyi önler. |
| `SAVASAN_GUIDANCE_STANDOFF_M` | `20` | ≥ 1.0 (m) | Hedef takip mesafesi. Mesafe hatası = tahmini mesafe − bu değer. |
| `SAVASAN_GUIDANCE_DISTANCE_GAIN` | `0.20` | ≥ 0.01 | Bbox alanından basit mesafe modeli: `distance ≈ gain / √(alan_norm)`. |
| `SAVASAN_CONTROL_HZ` | `20` | 1 – 200 (Hz) | Güdüm döngüsü frekansı. Düşük = daha yavaş tepki; yüksek = daha sık komut. |
| `SAVASAN_CONTROL_LOG_ENABLE` | `1` | `0` \| `1` | Güdüm/setpoint logları. Yer testinde `1` bırakın. |
| `SAVASAN_SEND_ONLY_ON_LOCK` | `1` | `0` \| `1` | `1` = yalnızca aktif kilit varken setpoint gönder. Üretim önerisi: `1`. |

### 2.2 Kaçış anahtarları — rakip APF ve yasak bölge

Ayrıntılı formül, kilit bandı ve log analizi: **[APF_EVASION.md](APF_EVASION.md)**

APF **yalnızca rakip** içindir; HSS ve saha sınırı geofence modülünde yönetilir.
Kovaladığımız hedef APF'ten muaftır — ona karşı koruma kilit bandıdır.

| Değişken | Varsayılan | Saha Etkisi |
|----------|------------|-------------|
| `SAVASAN_EVASION_ENABLE` | `0` | `1` = rakip APF kaçışı aktif |
| `SAVASAN_APF_D0` | `40` | Rakip etki yarıçapı (m) |
| `SAVASAN_APF_K_REP` | `500` | Rakip itme katsayısı |
| `SAVASAN_APF_LOCAL_MIN_EPS` | `0.05` | Kapan eşiği **oranı** (`\|F_top\| / Σ\|F_i\|`) |
| `SAVASAN_APF_LOCAL_MIN_BIAS_DEG` | `15` | Kapan kaçış sapması (°) |
| `SAVASAN_GEOFENCE_HSS_MARGIN_M` | `20` | HSS yarıçapına eklenen pay (m) |
| `SAVASAN_GEOFENCE_BOUNDARY_MARGIN_M` | `30` | Saha sınırına bırakılan pay (m) |
| `SAVASAN_GEOFENCE_BAND_MIN_M` | `60` | Önleyici kaçış bandı alt sınırı (m) |
| `SAVASAN_GEOFENCE_BAND_MULT` | `1.3` | Bant = çarpan × dönüş yarıçapı |
| `SAVASAN_SEYIR_BAND_MIN_SEP_M` | `25` | Kilit bandı: hedefe bu mesafede nişan saptırılır |
| `SAVASAN_SEYIR_BAND_BREAK_DEG` | `25` | Kilit bandı sapma açısı (°) |

### 2.3 Canlı görüntü (UDP) — `stream_common.env`

Şartname canlı görüntü yayını **GCS uygulamasından bağımsızdır**; DeepStream pipeline içinde OSD'li H.265 RTP ile gider.

| Değişken | Üretim değeri | Açıklama |
|----------|---------------|----------|
| `SAVASAN_UDP_ENABLE` | `1` | `0` = UDP kapalı (HOST yazılı olsa bile gönderilmez) |
| `SAVASAN_UDP_HOST` | `192.168.1.10,192.168.1.101` | Virgülle çoklu alıcı → `multiudpsink` |
| `SAVASAN_UDP_PORT` | `5000` | RTP portu |
| `SAVASAN_UDP_CODEC` | `h265` | `h264` veya `h265` |
| `SAVASAN_UDP_BITRATE` | `4000000` | bps |
| `SAVASAN_INGEST_FPS` | `30` | Encode yükü / kablosuz köprü için ingest FPS |

**HDMI vs UDP:** `SAVASAN_DISPLAY=display` → Jetson HDMI (`nv3dsink`). UDP ayrı tee dalıdır; laptop alıcısı gerekmez.

**Laptop alıcı (Ubuntu):** `scripts/yer_istasyonu_udp.sh` — GStreamer ile port 5000 dinler, isteğe bağlı masaüstü MP4 kaydı.

```bash
# Laptop'ta ÖNCE alıcı, SONRA Jetson servisi
./scripts/yer_istasyonu_udp.sh
# veya: ./scripts/yer_istasyonu_udp.sh 5000 h265
```

Env alias'ları: `YER_ISTASYONU_*` (yeni) veya `GCS_*` (geriye uyumlu).

**Jetson MP4 kayıt (paralel):** `SAVASAN_RECORD_ENABLE=1`, `SAVASAN_RECORD_DIR=.../output` — aynı `stream_common.env` içinde.

### 2.4 PID katsayıları ve sınırlar

Her eksen için `SAVASAN_PID_<EKsen>_KP|KI|KD` ve ayrı limit değişkenleri. Eksenler: `X` (kapanma, **m → m/s**), `Y` (yaw, **deg → deg/s**), `Z` (pitch, **deg → deg/s**), `YAW` (yedek yaw kanalı; varsayılan **kapalı**).

**Kazançlar** (`savasan.env` override'ları):

| Değişken grubu | Varsayılan (savasan.env) | Aralık | Saha Etkisi |
|----------------|--------------------------|--------|-------------|
| `SAVASAN_PID_X_KP/KI/KD` | 0.50 / 0.00 / 0.00 | Serbest float | Kapanma agresifliği. Yüksek Kp = hızlı yaklaşma. |
| `SAVASAN_PID_Y_KP/KI/KD` | 0.45 / 0.00 / 0.00 | Serbest float | Yatay (yaw) tepkisi. Aşırı salınım → Kp düşür. |
| `SAVASAN_PID_Z_KP/KI/KD` | 0.40 / 0.00 / 0.00 | Serbest float | Dikey (pitch) tepkisi. |
| `SAVASAN_PID_YAW_KP/KI/KD` | 0.20 / 0.00 / 0.00 | Serbest float | Yedek eksen; yalnızca `ENABLE_YAW_AXIS=1` iken kullanılır. |

**Çıkış / integral / slew sınırları** (eksene göre birim farklıdır; env yoksa kod varsayılanı):

| Değişken | X (m/s) | Y/Z (deg/s) | Aralık | Saha Etkisi |
|----------|---------|-------------|--------|-------------|
| `SAVASAN_PID_<EKSEN>_OUTPUT_LIMIT` | **10** | **25** | ≥ 0.1 | Komut tavanı. X için 8–12 m/s tipik; 25 m/s uçuş zarfı dışı risk. |
| `SAVASAN_PID_<EKSEN>_I_LIMIT` | **3** | **5** | ≥ 0 | İntegral birikim tavanı (anti-windup). |
| `SAVASAN_PID_<EKSEN>_SLEW_RATE` | **6** | **10** | ≥ 0 | Komut değişim hızı sınırı (birim/s). 0 = slew kapalı. |

`<EKSEN>`: `X`, `Y`, `Z`, `YAW`.

**Yaw kanalı:**

| Değişken | Varsayılan | Değerler | Saha Etkisi |
|----------|------------|----------|-------------|
| `SAVASAN_PID_ENABLE_YAW_AXIS` | `0` (kapalı) | `0` \| `1` | `1` → `eyaw` ayrı yaw PID kanalından `yaw_rate_cmd` üretir. Üretimde ana yaw **Y ekseninden** (`vy_cmd`) gelir; bu bayrak kapalı kalmalı. |

Kod varsayılanları (env yoksa): tüm eksenlerde Kp=0.5, Ki=0.01, Kd=0.0; limitler yukarıdaki tabloda. Ölü bant (deadband) kod içi 0.02.

**Post-flight diagnostik:** `SAVASAN_CONTROL_LOG_ENABLE=1` iken doygunluk veya slew kırpması olduğunda yaklaşık 1 sn'de bir log:

```text
[PID DIAG] sat X=… Y=… Z=… | slew X=… Y=… Z=…
```

`sat` = çıkış doygunluğuna kırpılan adım sayısı; `slew` = slew-rate kırpması. Komut beklenenden zayıfsa önce bu sayaçlara bakın.

### 2.5 Araç durum kestiricisi (güdüm yardımcı)

Güdüm döngüsü telemetriyi `VehicleStateEstimator` ile işler (auto tracker profil önerisi için hız kullanılır). Kalman değil; LPF + innovation gate.

| Değişken | Varsayılan | Aralık | Saha Etkisi |
|----------|------------|--------|-------------|
| `SAVASAN_STATE_TELEMETRY_WEIGHT` | 0.70 | 0.0 – 1.0 | Telemetri ağırlığı |
| `SAVASAN_STATE_VISION_WEIGHT` | 0.30 | 0.0 – 1.0 | Görüş ağırlığı |
| `SAVASAN_STATE_VELOCITY_ALPHA` | 0.35 | 0.0 – 1.0 | Hız LPF (düşük = daha yumuşak) |
| `SAVASAN_STATE_INNOVATION_GATE_MPS` | 8.0 | ≥ 0 | Ani hız sıçraması filtresi |
| `SAVASAN_STATE_YAW_ALPHA` | 0.30 | 0.0 – 1.0 | Yaw hızı LPF |
| `SAVASAN_STATE_YAW_GATE_DPS` | 45.0 | ≥ 0 | Yaw sıçrama filtresi (deg/s) |

### 2.6 0x03 paket eşlemesi (referans)

| Alan (wire) | Jetson anlamı | Birim |
|-------------|---------------|-------|
| `vx_mps` | NED Kuzey / kapanma oranı | m/s |
| `vy_mps` | NED Doğu / yaw oranı | deg/s |
| `vz_mps` | NED Aşağı / pitch oranı | deg/s (negatif = tırman) |
| `yaw_rate_dps` | Yedek | deg/s (varsayılan 0) |

### 2.7 DeepStream — YOLO (nvinfer) ve NvDCF tracker

Görüntü boru hattının algılama/takip katmanı **config dosyaları** ile ayarlanır. Kod derlemesi gerekmez; değişiklik sonrası **servis/uygulama restart** yeterlidir.

#### 2.7.1 Hangi dosyayı düzenlersin?

| Dosya | Rol | Seçim |
|-------|-----|-------|
| `02_Ana_Sistem_CPP/config/deepstream/config_infer_primary.txt` | Aktif YOLO (FP16) pgie config | `SAVASAN_PGI_CONFIG` (varsayılan bu dosya) |
| `02_Ana_Sistem_CPP/config/deepstream/config_infer_primary_yolo26s_int8_qdq.txt` | INT8 alternatif | Env ile override |
| `02_Ana_Sistem_CPP/config/deepstream/tracker_config.yml` | NvDCF default profil | `SAVASAN_TRACKER_PROFILE=default` |
| `02_Ana_Sistem_CPP/config/deepstream/tracker_config_calm.yml` | Düşük jitter / stabil | `SAVASAN_TRACKER_PROFILE=calm` |
| `02_Ana_Sistem_CPP/config/deepstream/tracker_config_aggressive.yml` | Hızlı manevra | `SAVASAN_TRACKER_PROFILE=aggressive` |
| `02_Ana_Sistem_CPP/config/deepstream/presets/*.env` | Otomatik profil eşikleri | `source presets/stable.env` vb. |

**YAML içi rehber:** `tracker_config.yml` dosyasında her parametre için aralık, artır/azalt senaryosu yorum satırı olarak mevcuttur.

#### 2.7.2 YOLO (nvinfer) — üretim değerleri

| Parametre | Üretim değeri | Aralık / not | Saha etkisi |
|-----------|---------------|--------------|-------------|
| `interval` | `0` | `0` = her kare; `1` = her 2.; `2` = her 3. | `0` = max tazelik, yüksek GPU; `2` = düşük yük, tracker-driven |
| `pre-cluster-threshold` | `0.33` | 0.25 – 0.45 (FP16); INT8 için ~0.30 | Yüksek = daha az sahte kutu, NMS yükü ve spike azalır |
| `topk` | `80` | 50 – 300 | Düşük = NMS aday sınırı; spike azaltma için 80 önerilir |
| `nms-iou-threshold` | `0.45` | 0.30 – 0.60 | Yüksek = daha fazla örtüşen kutu kalır |
| `cluster-mode` | `4` | YOLO26 custom parser | Değiştirmeyin (DeepStream-Yolo `.so` bağımlı) |

**Gökyüzü parlaması / FP patlaması:** `pre-cluster-threshold` ve `topk` birlikte sıkılaştırılır. Gerçek hedef kaybı olursa eşik 0.30'a iner; spike devam ederse 0.35–0.40 denenir.

**INT8 test:** `export SAVASAN_PGI_CONFIG=.../config_infer_primary_yolo26s_int8_qdq.txt` — skor dağılımı FP16'dan farklıdır; eşik ayrı kalibre edilir.

#### 2.7.3 NvDCF tracker — profil ve kritik parametreler

| Env | Varsayılan | Geçerli değerler |
|-----|------------|------------------|
| `SAVASAN_TRACKER_PROFILE` | `default` | `default`, `calm`, `aggressive`, `auto` |
| `SAVASAN_TRACKER_AUTO_RESTART` | `0` | `0` \| `1` — `auto` profilde kontrollü yeniden başlatma |
| `SAVASAN_TRACKER_DISABLE` | `0` | `1` = nvtracker kapalı (yarışmada önerilmez) |
| `SAVASAN_TRACKER_CONFIG` | (boş) | Elle YAML yolu; set edilirse profil override edilir |

**Default profil özeti (üretim):**

| Parametre | Değer | Kısa anlam |
|-----------|-------|------------|
| `minDetectorConfidence` | 0.40 | Track'e giren min YOLO skoru |
| `maxTargetsPerStream` | 4 | Eşzamanlı max track (1–2 gerçek hedef + FP tamponu) |
| `probationAge` | 2 | Yeni track 2 kare dayanınca onaylı |
| `maxShadowTrackingAge` | 12 | Kutu kaybında shadow ömrü (kare) |
| `tentativeDetectorConfidence` | 0.40 | Geçici track açma eşiği |
| `useColorNames` / `useHog` | 1 / 0 | GPU renk özelliği açık; HOG kapalı (CPU spike önleme) |

**Hızlı profil seçimi:**

| Senaryo | Profil | Not |
|---------|--------|-----|
| Dengeli üretim | `default` | Mevcut varsayılan |
| Kutu titremesi (jitter) | `calm` | Düşük process noise, yüksek visual ağırlık |
| Sert manevra / yüksek hız | `aggressive` | Yüksek IoU ağırlığı, yüksek vel noise |
| Otomatik geçiş | `auto` + `SAVASAN_TRACKER_AUTO_RESTART=1` | `presets/stable.env` ile birlikte |

#### 2.7.4 Tracker tuning — belirti tablosu

| Belirti | İlk müdahale (YAML) | Yön |
|---------|---------------------|-----|
| Infer/tracker spike (p95) | `pre-cluster-threshold` ↑, `topk` ↓ | YOLO config |
| Sahte hedef / çok track | `minDetectorConfidence` ↑, `maxShadowTrackingAge` ↓ | tracker yml |
| Kilit kaybı (kısa oklüzyon) | `maxShadowTrackingAge` ↑, `probationAge` ↑ | tracker yml |
| Kutu titremesi | `measurementNoiseVar4Detector` ↑, `filterLr` ↓ | tracker yml veya `calm` |
| Hızlı manevrada lag | `processNoiseVar4Vel` ↑, `matchingScoreWeight4Iou` ↑ | tracker yml veya `aggressive` |
| ID sık değişiyor | `minIouDiff4NewTarget` ↑ | tracker yml |

Bir seferde **tek parametre** değiştirin; latency profiler veya `journalctl` ile 10–15 sn gözlemleyin.

#### 2.7.5 Değişiklik sonrası: restart mı, build mi?

| Değişiklik | Build (`make`) | Restart |
|------------|----------------|---------|
| `tracker_config*.yml` | Hayır | **Evet** |
| `config_infer_primary*.txt` | Hayır | **Evet** |
| `savasan.env` / `stream_common.env` | Hayır | **Evet** (`systemctl restart savasan-airlock`) |
| Kamera profili `.env` | Hayır | Kamera script veya servis restart |
| C++ kaynak (`track_selector.cpp` vb.) | **Evet** | **Evet** |

Tracker/YOLO config dosyaları pipeline açılışında okunur; **çalışırken hot-reload yok**.

```bash
# Env + config doğrulama ve restart
bash 02_Ana_Sistem_CPP/config/systemd/validate_env.sh /etc/savasan/savasan.env
sudo systemctl restart savasan-airlock
```

### 2.8 Kilit seçimi env anahtarları

Tracker track_id üretir; **hangi hedefin kilitleneceğine** `TrackSelector` + env karar verir (`savasan.env`).

| Değişken | Varsayılan | Saha etkisi |
|----------|------------|------------|
| `SAVASAN_LOCK_MODE` | `baseline` | `hybrid` \| `baseline` — kilit seçim politikası |
| `SAVASAN_LOCK_GRACE_MISS_FRAMES` | `18` | Kısa bbox kaçırmada grace (~6 kare @30fps) |
| `SAVASAN_LOCK_INTERIOR_BAD_MS` | `200` | Şartname streak içi max ara kayıp (ms) |
| `SAVASAN_LOCK_AV_MARGIN_NORM` | `0.05` | Aspect ratio toleransı |
| `SAVASAN_LOCK_NEW_MIN_CONF` | (kod vars.) | Yeni kilit için min güven |
| `SAVASAN_LOCK_KEEP_MIN_CONF` | (kod vars.) | Mevcut kilidi koruma eşiği |
| `SAVASAN_LOCK_ROI_X_MIN/MAX` | (kod vars.) | ROI kapısı (normalize 0–1) |
| `SAVASAN_LOCK_ROI_Y_MIN/MAX` | (kod vars.) | ROI kapısı |

Kilit kaybı sorununda önce tracker `maxShadowTrackingAge`, sonra `SAVASAN_LOCK_GRACE_MISS_FRAMES` kontrol edilir.

---

## 3. Kamera Profilleri ve Saha Kalibrasyonu

### 3.1 Kamera profili seçimi

USB BRIO için hazır profiller `config/camera_profiles/` altında:

| Profil | Dosya | Ne zaman |
|--------|-------|----------|
| Güneşli açık hava | `outdoor_sunny.env` | Parlak gün, yüksek kontrast |
| Bulutlu | `outdoor_overcast.env` | Düşük kontrast |
| Hangar / kapalı alan | `hangar_indoor.env` | Yapay ışık |

**Yükleme:**

```bash
# AIR_LOCK yarışma hattı (savasan.env güncellenir)
./camera_profile sunny          # veya overcast / hangar
./camera_profile status

# Canlı ince ayar (trackbar + Y istatistik HUD)
./scripts/camera_tune.sh
# Klavye: 1/2/3 profil yükle, S kaydet
```

Profil değişimi `SAVASAN_CAMERA_PROFILE` + V4L2 exposure/gain/WB uygular. Görüntü kalitesi doğrudan YOLO ve kilit kalitesini etkiler; güdüm parametrelerinden **önce** kamera profilini doğrulayın.

### 3.2 `SAVASAN_GUIDANCE_PIXEL_TO_DEG` kalibrasyonu

**Tanım:** Normalize merkez sapması `±0.5` iken üretilen açı hatası (deg):

```
açı_hata_deg = (nx − 0.5) × PIXEL_TO_DEG
```

**FOV ile ilişki:** Kamera yatay FOV ≈ `H_FOV` derece ise, görüntü kenarında (`nx=0` veya `1`) yaklaşık:

```
PIXEL_TO_DEG ≈ H_FOV
```

Örnek: 60° yatay FOV → `PIXEL_TO_DEG=60` makul başlangıç. BRIO 1080p modunda efektif FOV ~65–78° aralığında olabilir; saha testi ile ince ayar gerekir.

**Kalibrasyon prosedürü (yerde, pervaneler sökülü):**

1. `SAVASAN_SETPOINT_TX_ENABLE=0`, `SAVASAN_CONTROL_LOG_ENABLE=1`
2. Hedefi kademeli olarak görüntü merkezinden **sağa** kaydırın → logda `vy` (yaw) pozitif olmalı
3. Hedefi **yukarı** kaydırın → logda `vz` negatif (tırmanma) olmalı
4. Sapma büyüdükçe komut oranı linear artmalı; doyuma erken vuruyorsa Kp veya gain yüksek

### 3.3 Ayar rehberi

| Belirti | Olası neden | Müdahale |
|---------|-------------|----------|
| Aşırı salınım, over-correction | Gain veya Kp yüksek | `PIXEL_TO_DEG` **düşür** (örn. 60→45); `PID_Y_KP` / `PID_Z_KP` düşür |
| Hantal, geç tepki | Gain veya Kp düşük | `PIXEL_TO_DEG` **artır** (küçük adımlarla); ilgili eksen Kp artır |
| Kapanma çok agresif | X ekseni yüksek | `PID_X_KP` düşür, `PID_X_OUTPUT_LIMIT` düşür (örn. 8) veya `STANDOFF_M` artır |
| Komut tavanında takılı (sat sayacı artıyor) | `OUTPUT_LIMIT` veya slew düşük | `[PID DIAG]` loguna bak; ilgili eksende `OUTPUT_LIMIT` / `SLEW_RATE` artır veya Kp düşür |
| Kısa hedef kaybında silkelenme | Oklüzyon | `OCCLUSION_MODE=hold_last`, `HOLD_MS` 400–600 |
| Uzun hedef kaybında drift | hold süresi uzun | `HOLD_MS` düşür (200–300) veya `hold_zero` (dikkatli) |

**Önemli:** Bir seferde yalnızca **bir** parametre değiştirin; 10–15 sn gözlemleyin.

---

## 4. Test ve Yarışma Günü Kontrol Listesi

### 4.1 Güvenlik (zorunlu)

- [ ] Yer testlerinde **pervaneler sökülü** veya uçak sabit bağlı
- [ ] Seri port ve güç bağlantıları strain relief ile sabit
- [ ] Acil durdurma prosedürü (manuel mod / güç kesme) ekip tarafından biliniyor
- [ ] İlk çalıştırmada `SAVASAN_SETPOINT_TX_ENABLE=0`

### 4.2 Ortam ve binary doğrulama

```bash
# Kurulum / güncelleme (binary + env + systemd)
sudo bash 02_Ana_Sistem_CPP/config/systemd/install.sh

# Eski savasan.env'de oklüzyon anahtarı yoksa (validate fail)
bash scripts/fix_savasan_env_occlusion.sh

# Env doğrulama
bash 02_Ana_Sistem_CPP/config/systemd/validate_env.sh /etc/savasan/savasan.env

# Servis (validate fail sonrası reset-failed gerekebilir)
sudo systemctl reset-failed savasan-airlock
sudo systemctl start savasan-airlock
journalctl -u savasan-airlock -f -o short-precise

# Binary mevcut mu
ls -l /home/nvidia/Savasan_IHA_Workspace/02_Ana_Sistem_CPP/build-app/savasan_iha

# Kamera profili
./camera_profile status

# UDP alıcı (laptop, servisten önce)
./scripts/yer_istasyonu_udp.sh
```

- [ ] `validate_env.sh` hatasız (`SAVASAN_GUIDANCE_OCCLUSION_HOLD_MS` dahil)
- [ ] `stream_common.env` UDP hedefleri doğru (`.10`, `.101`)
- [ ] `SAVASAN_ALC_DEVICE` doğru (`/dev/ttyACM0` vb.)
- [ ] Kamera profili sahaya uygun (`outdoor_sunny` / `overcast` / `hangar`)

### 4.3 Yer testi (Dry-Run)

```bash
export SAVASAN_SETPOINT_TX_ENABLE=0
export SAVASAN_CONTROL_LOG_ENABLE=1
export SAVASAN_GUIDANCE_MODE=image_world
# systemd veya doğrudan:
./02_Ana_Sistem_CPP/build-app/savasan_iha phase5 usb hybrid
```

**Doğrulanacaklar:**

- [ ] DeepStream pipeline açılıyor, FPS stabil
- [ ] HUD'da bbox + kilit göstergesi görünüyor
- [ ] Terminalde `DRY_RUN setpoint vx=... vy=... vz=...` logları geliyor
- [ ] Hedef sağa kaydırılınca `vy` pozitif; yukarı kaydırılınca `vz` negatif
- [ ] Kilit paketi (0x01) bridge logunda görülüyor
- [ ] Hedef kaybında `hold_last` davranışı: kısa süre komut korunuyor, sonra sıfırlanıyor

### 4.4 Fiziksel işaret doğrulama (pervaneler sökülü, TX açık düşük güç)

Servo hareketi gözlemlenebiliyorsa:

- [ ] Hedef **sağda** → yaw servosu **sağa** (Doğu+)
- [ ] Hedef **yukarıda** → pitch **tırmanma** yönünde
- [ ] Hedef **uzakta** → ileri kapanma yönünde itme (mekanizma tasarımına bağlı)

Bu adım `SAVASAN_SETPOINT_TX_ENABLE=1` ile yapılır; pervaneler hâlâ sökülü olmalıdır.

### 4.5 Uçuş öncesi (Otonom devreye alma)

```bash
# savasan.env içinde:
SAVASAN_SETPOINT_TX_ENABLE=1
SAVASAN_GUIDANCE_MODE=image_world
SAVASAN_GUIDANCE_OCCLUSION_MODE=hold_last
SAVASAN_GUIDANCE_OCCLUSION_HOLD_MS=400
SAVASAN_SEND_ONLY_ON_LOCK=1
```

- [ ] `validate_env.sh` tekrar çalıştırıldı
- [ ] Son kamera profili uygulandı
- [ ] Telemetri geçerli (`TelemetrySnapshot valid`)
- [ ] Manuel uçuş → otonom geçiş prosedürü uçuş lideri onaylı
- [ ] UDP canlı görüntü (192.168.1.10 / .101) ve SIHA API bağlantısı (yarışma gerekiyorsa) aktif

### 4.6 Yarışma anı hızlı referans

| Durum | Aksiyon |
|-------|---------|
| Güdümü kapat | `SAVASAN_GUIDANCE_MODE=off` + servis restart |
| Sadece log | `SAVASAN_SETPOINT_TX_ENABLE=0` |
| Bulut geçti | `./camera_profile overcast` |
| Infer/tracker spike | `pre-cluster-threshold` ↑ veya `calm` profil | Bkz. §2.7 |
| Agresif takip | `PIXEL_TO_DEG` veya `PID_Y/Z_KP` küçük artış |
| Sakin takip | `PIXEL_TO_DEG` veya Kp küçük azalış |

---

## 5. Sorun Giderme (Troubleshooting)

### 5.1 Hedef anlık kaybolduğunda uçak silkeleniyor

**Belirti:** YOLO 1–3 kare kaçırınca yaw/pitch zıplıyor.

**Çözüm:**

1. `SAVASAN_GUIDANCE_OCCLUSION_MODE=hold_last` (varsayılan) olduğunu doğrula
2. `SAVASAN_GUIDANCE_OCCLUSION_HOLD_MS` artır: 400 → 500–600 ms
3. Tracker grace: `SAVASAN_LOCK_GRACE_MISS_FRAMES` (vars. 18) yeterli mi kontrol et
4. NvDCF shadow: `maxShadowTrackingAge` (tracker yml) artırılabilir (8→12–20)
5. YOLO güven eşiği / kilit ROI env parametreleri (`SAVASAN_*`) ile uyumlu mu bak

### 5.2 Uçak hedefi ıskalıyor veya geç tepki veriyor

| Belirti | İlk müdahale | İkinci müdahale |
|---------|--------------|-----------------|
| Sürekli geride kalıyor | `PIXEL_TO_DEG` +5…10 | İlgili eksen `KP` +0.05 |
| Salınımlı takip | `PIXEL_TO_DEG` −10…15 | `PID_Y_KP` / `PID_Z_KP` −0.05 |
| Mesafe kapanmıyor | `PID_X_KP` artır | `STANDOFF_M` düşür (dikkatli) |
| Mesafe çok agresif | `PID_X_KP` düşür | `STANDOFF_M` artır |

### 5.3 Setpoint gitmiyor

- `SAVASAN_SETPOINT_TX_ENABLE=1` mi?
- `SAVASAN_GUIDANCE_MODE=off` değil mi?
- `SAVASAN_SEND_ONLY_ON_LOCK=1` iken aktif kilit var mı?
- `SAVASAN_MISSION_MODE=air_lock` ve seri TX politikası uygun mu?
- Alacakart bağlı mı? (`SAVASAN_ALC_DEVICE`, logda `AlcLinkBridge`)

### 5.4 Dry-run log var, gerçek TX yok

Beklenen davranış: `SETPOINT_TX_ENABLE=0` iken `DRY_RUN setpoint` loglanır, seri yazılmaz. Uçuş için `1` yapın.

### 5.5 Degrade mod aktif

Logda `degraded_mode=1` görülürse:

- Seri iletişim hatası (setpoint gönderimi başarısız)
- Telemetri geçersiz
- Auto tracker restart pre-check fail

**Aksiyon:** Kablo/port kontrolü, telemetri akışı, `comm_fail_count` logu; gerekirse pipeline restart.

### 5.7 `validate_env` — oklüzyon anahtarı eksik

**Belirti:** `SAVASAN_GUIDANCE_OCCLUSION_HOLD_MS is not integer: <empty>` — servis `ExecStartPre` aşamasında düşer.

**Neden:** Eski `/etc/savasan/savasan.env` (Kalman bloğu) `install.sh` tarafından üzerine yazılmadı.

**Çözüm:**

```bash
bash scripts/fix_savasan_env_occlusion.sh
bash 02_Ana_Sistem_CPP/config/systemd/validate_env.sh /etc/savasan/savasan.env
sudo systemctl reset-failed savasan-airlock
sudo systemctl start savasan-airlock
```

### 5.8 UDP görüntü gelmiyor

- Laptop'ta alıcı **servisten önce** çalışıyor mu? (`yer_istasyonu_udp.sh`)
- `grep SAVASAN_UDP /etc/savasan/stream_common.env` → `ENABLE=1`, host/port doğru
- Aynı subnet / firewall (UDP 5000)
- Journal'da encode/UDP hatası var mı: `journalctl -u savasan-airlock -f`

### 5.10 Infer veya tracker latency spike (p95/p99)

**Belirti:** Profiler'da infer ~9.5 ms normal ama ara sıra 13–14 ms; tracker 1.7 ms → 3.2 ms.

**Olası neden:** Gökyüzü parlaması → YOLO false positive → NMS yükü + NvDCF tentative/shadow track.

**Çözüm (sırayla):**

1. YOLO: `config_infer_primary.txt` → `pre-cluster-threshold=0.33`, `topk=80` (mevcut üretim)
2. Tracker: `minDetectorConfidence=0.40`, `maxShadowTrackingAge` düşük (8–12), `probationAge=2`
3. Kamera profili: parlak saha → `outdoor_sunny` / exposure ayarı
4. Hâlâ spike: `SAVASAN_TRACKER_PROFILE=calm` veya `interval=2` (YOLO her 3. kare) — latency/GPU takası

Değişiklik sonrası `sudo systemctl restart savasan-airlock` (build gerekmez).

### 5.11 Yanlış eksen işareti

Hedef sağda ama sola dönüyorsa:

1. `PIXEL_TO_DEG` işareti değil, **yaw ekseni ters bağlı** olabilir — mekanik kontrol
2. Logda `ey` pozitif mi? Değilse tracker `nx` veya ROI ters olabilir
3. Kamera montajı ters ise yazılımda eksen terslemesi gerekir (saha lideri onayı olmadan kod değiştirmeyin)

---

## Ek: Hızlı env şablonu (kopyala-yapıştır)

```bash
# Yer testi
SAVASAN_SETPOINT_TX_ENABLE=0
SAVASAN_GUIDANCE_MODE=image_world
SAVASAN_CONTROL_LOG_ENABLE=1

# Uçuş
SAVASAN_SETPOINT_TX_ENABLE=1
SAVASAN_GUIDANCE_MODE=image_world
SAVASAN_GUIDANCE_PIXEL_TO_DEG=60
SAVASAN_GUIDANCE_OCCLUSION_MODE=hold_last
SAVASAN_GUIDANCE_OCCLUSION_HOLD_MS=400
SAVASAN_SEND_ONLY_ON_LOCK=1
SAVASAN_CONTROL_HZ=20
```

---

## İlgili dosyalar

| Dosya | İçerik |
|-------|--------|
| `02_Ana_Sistem_CPP/config/systemd/savasan.env` | Güdüm, kamera, SIHA API şablonu |
| `02_Ana_Sistem_CPP/config/systemd/stream_common.env` | UDP + Jetson kayıt şablonu |
| `02_Ana_Sistem_CPP/config/systemd/validate_env.sh` | `savasan.env` doğrulama |
| `02_Ana_Sistem_CPP/config/systemd/install.sh` | Systemd kurulum + env merge |
| `scripts/fix_savasan_env_occlusion.sh` | Eski env → oklüzyon migrasyonu |
| `scripts/yer_istasyonu_udp.sh` | Laptop UDP alıcı |
| `02_Ana_Sistem_CPP/src/runners/phase5_guidance.cpp` | Güdüm döngüsü |
| `02_Ana_Sistem_CPP/src/control/world_target_estimator.cpp` | Piksel → NED hata |
| `02_Ana_Sistem_CPP/config/deepstream/config_infer_primary.txt` | YOLO nvinfer (interval, threshold, topk) |
| `02_Ana_Sistem_CPP/config/deepstream/tracker_config.yml` | NvDCF default profil + parametre yorumları |
| `02_Ana_Sistem_CPP/config/deepstream/tracker_config_calm.yml` | NvDCF calm profil |
| `02_Ana_Sistem_CPP/config/deepstream/tracker_config_aggressive.yml` | NvDCF aggressive profil |
| `02_Ana_Sistem_CPP/config/deepstream/presets/` | Auto tracker preset env dosyaları |
| `docs/APF_EVASION.md` | Telemetri APF kaçış teknik kılavuzu |
| `docs/PERFORMANCE_BASELINE.md` | FPS/latency ölçüm notları |
| `docs/GUIDANCE_USER_MANUAL.pdf` | Bu kılavuzun PDF çıktısı |
| `02_Ana_Sistem_CPP/config/camera_profiles/` | Kamera profilleri |
