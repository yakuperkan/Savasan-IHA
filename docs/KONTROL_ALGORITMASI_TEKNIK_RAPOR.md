# KONTROL ALGORİTMASI TEKNİK ANALİZ VE TASARIM RAPORU

**Proje:** Teknofest Savaşan İHA — Faz 5 (İHA takip + kaçış + güdüm)  
**Platform:** NVIDIA Jetson Orin NX (ARM64), C++17  
**Kod tabanı:** `02_Ana_Sistem_CPP/`  
**Rapor tarihi:** 11 Temmuz 2026  
**Sürüm:** 2.1 — PID blok diyagramı + parametre rehberi

---

## 1. GİRİŞ VE SİSTEM MİMARİSİ

### 1.1 Sistem Tanımı

Savaşan İHA kontrol algoritması, yarışma senaryosunda bir İHA'nın hedef rakip İHA'yı görüntü tabanlı olarak takip etmesini, mesafe ve açı hatalarını kapalı döngü ile düzeltmesini ve sunucu telemetrisi ile bildirilen rakip konumlarına ve HSS yasak bölgelerine karşı otonom kaçış manevrası üretmesini amaçlamaktadır.

Kontrol hesabının tamamı **Jetson Orin NX** üzerinde gerçekleştirilmektedir. **Alacakart otopilot** yalnızca uygulayıcı katmanıdır; `0x03` setpoint paketini servolara yansıtır.

| Mod | Tetikleyici | Çıkış | Öncelik |
|-----|-------------|-------|---------|
| Görüntü güdümü (PID) | Tracker kilit + bbox | `SetpointCommand` (0x03) | Normal takip |
| APF kaçış | Rakip/HSS tehdit yarıçapı | `SetpointCommand` (0x03) | PID'i devre dışı bırakır |
| Oklüzyon tutma | Hedef ölçümü kaybı | Son komut veya sıfır | PID/APF sonrası |

### 1.2 Kontrol Şeması

```
REFERANS: d_des, görüntü merkezi (0.5,0.5), GPS, HTTP API
    │
    ├─► WorldTargetEstimator ──► e_x, e_y, e_z ──┐
    ├─► VehicleStateEstimator (füzyon)            │
    └─► UpdateThreatPredictions ──► APF ────────┤
                                                 ▼
                              RunGuidanceControlStep (ARBITRAJ)
                              degrade? → çık
                              APF aktif? → kaçış setpoint
                              ölçüm yok? → hold_last / hold_zero
                              aksi → PID → 0x03
                                                 │
                                                 ▼
                              Alacakart Otopilot → İHA → Geri besleme
```

**Geri besleme döngüsü:** Görüntü ~30 FPS; kontrol döngüsü varsayılan $f_s = 20\,\text{Hz}$, $T_s = 50\,\text{ms}$.

---

## 2. MATEMATİKSEL TEMELLER VE KONTROL TEORİSİ

### 2.1 Teorik Altyapı

| Yaklaşım | Modül | Teorik sınıf |
|----------|-------|--------------|
| Çok eksenli ayrık PID | `PidControllerXYZYaw` | Klasik PID, hız komut modu |
| Innovation-kapılı LPF | `VehicleStateEstimator` | Birinci derece filtre + aykırı değer kapısı |
| Repulsive APF | `ComputeApfSetpoint` | Yapay potansiyel alan |

LQR, MPC ve Kalman filtresi **kullanılmamaktadır**.

### 2.2 Matematiksel Formülasyonlar

#### 2.2.1 Hedef Geometrik Kestirimi

Pinhole menzil:
$$d = \frac{f_{px} \cdot w_{gerçek}}{w \cdot W_{img}}$$

Alan tabanlı model (varsayılan):
$$d = \frac{k_{dist}}{\sqrt{w \cdot h}}$$

Hata sinyalleri:
$$e_x = d - d_{standoff}$$
$$e_y = (n_x - 0.5) \cdot g_{px \to deg}$$
$$e_z = (n_y - 0.5) \cdot g_{px \to deg}$$

#### 2.2.2 Ayrık PID

Sürekli zaman:
$$u_i(t) = K_{p,i} e_i + K_{i,i}\int e_i\,dt + K_{d,i}\frac{de_i}{dt}$$

Ayrık türev (geriye fark):
$$\dot{e}_{i,k} = \frac{e_{i,k} - e_{i,k-1}}{\Delta t_k}$$

Ayrık integral (Euler):
$$I_{i,k}^{cand} = \text{clamp}\!\left(I_{i,k-1} + e_{i,k}\Delta t_k,\; -I_{lim},\; I_{lim}\right)$$

Anti-windup çıkışı:
$$u_{i,k} = \text{clamp}\!\left(K_p e_{i,k} + K_i I_{i,k} + K_d \dot{e}_{i,k},\; -u_{lim},\; u_{lim}\right)$$

Slew-rate:
$$u_{i,k} \leftarrow \text{clamp}\!\left(u_{i,k},\; u_{i,k-1} - \dot{u}_{max}\Delta t_k,\; u_{i,k-1} + \dot{u}_{max}\Delta t_k\right)$$

#### 2.2.3 Innovation-Kapılı LPF

$$\nu_k = \text{clamp}\!\left(\tilde{v}_k - \hat{v}_{k-1},\; -g,\; g\right)$$
$$\hat{v}_k = \hat{v}_{k-1} + \alpha \cdot \nu_k$$

#### 2.2.4 Repulsive APF

$$|F| = k \cdot \left(\frac{1}{d} - \frac{1}{d_0}\right) \cdot \frac{1}{d^2}$$
$$\vec{F}_{total} = \vec{F}_{rakip} + \vec{F}_{hss}$$
$$d_{0,hss} = r_{HSS} + b_{buffer}$$

Setpoint eşlemesi:
$$e_{heading} = \text{wrap}_{[-180,180]}\!\left(\text{atan2}(F_E, F_N) - \psi_{own}\right)$$
$$v_y = \text{clamp}(k_{yaw} \cdot e_{heading},\; \pm\dot{\psi}_{max})$$
$$v_x = \text{clamp}\!\left(v_{max} \cdot \max\!\left(\frac{|F|}{k_{scale}},\; 0.25\right),\; 0,\; v_{max}\right)$$

#### 2.2.5 Haversine ve Dead Reckoning

$$d = 2 R_E \arctan2\!\left(\sqrt{a},\; \sqrt{1-a}\right), \quad R_E = 6\,371\,000\,\text{m}$$
$$\Delta\phi = \frac{v \cos\psi \cdot \Delta t}{R_E}, \quad \Delta\lambda = \frac{v \sin\psi \cdot \Delta t}{R_E \cos\phi}$$

### 2.3 Ayrıklaştırma

$$\Delta t_k = t_k - t_{k-1}, \quad 0 < \Delta t_k \leq 0.5\,\text{s}$$

İlk adımda yalnızca zaman damgası kurulur; geçersiz $\Delta t$ adımı atlanır.

### 2.4 PID Kontrol Blok Diyagramı ve Detaylı Analiz

Bu bölümde `PidControllerXYZYaw` sınıfının kapalı döngü yapısı, tek eksen iç mimarisi ve dört eksenli paralel topoloji adım adım çözümlenmektedir.

#### 2.4.1 Kapalı Döngü Üst Seviye Şeması

Sistemde PID, görüntü tabanlı hata üreten dış bir kestirici ile birlikte çalışır. Geri besleme doğrudan PID içine değil, `WorldTargetEstimator` çıkışına girer:

```
  ┌─────────────────────────────────────────────────────────────────────────────┐
  │                        DIŞ KAPALI DÖNGÜ (20 Hz)                            │
  └─────────────────────────────────────────────────────────────────────────────┘

  REFERANS                          ÖLÇÜM (feedback)                    PLANT
  ────────                          ───────────────                    ─────

  d_standoff ─────┐                 ┌── bbox (nx,ny,w,h)              ┌──────────┐
  (20 m)          │                 │   DeepStream Tracker            │ Alacakart│
                  ▼                 │                                 │ Otopilot │
           ┌──────────────┐   y    │   ┌──────────────────┐         │  (0x03)  │
           │   Σ  (hata)  │◄───────┼───│ WorldTargetEstimator│         └────┬─────┘
           │  e = r - y   │        │   │  Estimate()       │              │
           └──────┬───────┘        │   └──────────────────┘              │ u
                  │ e_x,e_y,e_z    │                                       ▼
                  ▼                └──────────────────────────────────► ┌──────────┐
           ┌──────────────────────────────────┐                           │   İHA    │
           │   PidControllerXYZYaw::Update()  │──────────────────────────►│ dinamiği │
           │   vx_cmd, vy_cmd, vz_cmd         │   SetpointCommand         └────┬─────┘
           └──────────────────────────────────┘                              │
                                                                              │
                  kamera görüntüsü ◄──────────────────────────────────────────┘
                  (piksel konumu → y)
```

**Referans vektörü** $\mathbf{r}_k$:

$$\mathbf{r}_k = \begin{bmatrix} d_{standoff} \\ 0 \\ 0 \end{bmatrix}$$

(Görüntü merkezi $(0.5,\; 0.5)$ referansı $e_y$, $e_z$ hesabına gömülüdür.)

**Ölçüm vektörü** $\mathbf{y}_k$:

$$\mathbf{y}_k = \begin{bmatrix} d_k \\ (n_x - 0.5)\, g_{px \to deg} \\ (n_y - 0.5)\, g_{px \to deg} \end{bmatrix}$$

**Hata vektörü** (PID girişi):

$$\mathbf{e}_k = \mathbf{r}_k - \mathbf{y}_k = \begin{bmatrix} e_x \\ e_y \\ e_z \end{bmatrix}$$

**Kontrol vektörü** (PID çıkışı → 0x03):

$$\mathbf{u}_k = \begin{bmatrix} v_x \\ v_y \\ v_z \end{bmatrix}$$

#### 2.4.2 Tek Eksen İç Blok Diyagramı (`UpdateAxis`)

Her eksen bağımsız bir PID kanalıdır. Aşağıdaki diyagram `UpdateAxis(cfg, st, error, dt)` fonksiyonunun tam iç yapısını gösterir:

```
                         e[k] (ham hata girişi)
                              │
                              ▼
                    ┌─────────────────────┐
                    │     DEADBAND        │     |e| < δ  →  e := 0
                    │   (ölü bant)        │     gürültü/titreme bastırma
                    └──────────┬──────────┘
                               │ e[k] (ölü bant sonrası)
              ┌────────────────┼────────────────┐
              │                │                │
              ▼                ▼                ▼
        ┌──────────┐    ┌──────────────┐  ┌──────────────────┐
        │    Kp    │    │  İNTEGRAL    │  │     TÜREV        │
        │  (oransal)│    │   KOLU       │  │  (geriye fark)   │
        │          │    │              │  │                  │
        │  Kp·e[k] │    │  z⁻¹ (delay) │  │ e[k]-e[k-1]      │
        └────┬─────┘    │      │       │  │ ───────────      │
             │           │      ▼       │  │     Δt           │
             │           │  ┌───────┐   │  │  = ė[k]          │
             │           │  │  ∫·dt │   │  └────────┬─────────┘
             │           │  │Euler  │   │           │
             │           │  └───┬───┘   │           ▼
             │           │      │       │      ┌──────────┐
             │           │      ▼       │      │    Kd    │
             │           │  ┌────────┐  │      │ Kd·ė[k]  │
             │           │  │ CLAMP  │  │      └────┬─────┘
             │           │  │±I_lim  │  │           │
             │           │  └───┬────┘  │           │
             │           │      │       │           │
             │           │ I_cand[k]    │           │
             │           │      │       │           │
             │           │      ▼       │           │
             │           │ ┌──────────┐ │           │
             │           │ │ANTI-WINDUP│ │           │
             │           │ │ koşullu  │ │           │
             │           │ │ I update │ │           │
             │           │ └────┬─────┘ │           │
             │           │      │ I[k]  │           │
             │           │      ▼       │           │
             │           │  ┌────────┐  │           │
             │           │  │   Ki   │  │           │
             │           │  │Ki·I[k] │  │           │
             │           │  └───┬────┘  │           │
             │           │      │       │           │
             └───────────┴──────┼───────┴───────────┘
                                ▼
                         ┌─────────────┐
                         │     Σ       │   u_unsat = Kp·e + Ki·I + Kd·ė
                         │   (topla)   │
                         └──────┬──────┘
                                │
                                ▼
                    ┌───────────────────────┐
                    │  OUTPUT SATURATION    │   u = clamp(u_unsat, -u_lim, +u_lim)
                    │  (çıkış doygunluğu)   │
                    └───────────┬───────────┘
                                │
                                ▼
                    ┌───────────────────────┐
                    │    SLEW-RATE LIMIT    │   |u[k]-u[k-1]| ≤ u̇_max·Δt
                    │  (değişim hızı sınırı)│
                    └───────────┬───────────┘
                                │
                                ▼
                           u[k] (çıkış)
                                │
                    ┌───────────┴───────────┐
                    ▼                       ▼
              e[k] → z⁻¹ → e[k-1]    u[k] → z⁻¹ → u[k-1]
              (prev_error)            (prev_output)
```

#### 2.4.3 Anti-Windup Alt Blok Diyagramı

Anti-windup, integral kolunun doygunluk altında nasıl kesildiğini ayrıntılı gösterir:

```
  I_cand[k] = clamp(I[k-1] + e[k]·Δt, -I_lim, +I_lim)
                    │
                    ▼
  u_unsat = Kp·e[k] + Ki·I_cand + Kd·ė[k]
                    │
         ┌──────────┴──────────┐
         ▼                     ▼
  pushing_high ?          pushing_low ?
  (u_unsat > u_lim        (u_unsat < -u_lim
   VE e > 0)               VE e < 0)
         │                     │
         └──────────┬──────────┘
                    ▼
            ┌───────────────┐
            │  pushing ?    │
            └───┬───────┬───┘
           HAYIR│       │EVET
                ▼       ▼
          I[k]=I_cand  I[k]=I[k-1]   ← integral dondurulur
                │       │
                └───────┘
                    │
                    ▼
              Ki·I[k] → toplama bloğuna
```

**Mantık tablosu:**

| $u^{unsat}$ | $e$ | pushing | Integral güncelleme |
|-------------|-----|---------|---------------------|
| $> u_{lim}$ | $> 0$ | HIGH | Dondur ($I_k = I_{k-1}$) |
| $< -u_{lim}$ | $< 0$ | LOW | Dondur |
| Doygun, $e$ ters yönde | — | YOK | Güncelle ($I_k = I_k^{cand}$) |
| Doygun değil | — | YOK | Güncelle |

#### 2.4.4 Dört Eksenli Paralel Topoloji

`PidControllerXYZYaw::Update()` dört bağımsız `UpdateAxis` örneğini paralel çalıştırır:

```
  ErrorInput                    PidControllerXYZYaw::Update()
  ──────────                    ─────────────────────────────

  err.valid ──────────────────► [valid?]──HAYIR──► Reset() → valid=false
                                    │
                                   EVET
                                    │
  now_tp ──────► Δt = now - last_tp ──► [0 < Δt ≤ 0.5?]──HAYIR──► skip
                                    │
                                   EVET
                                    │
         ┌──────────────────────────┼──────────────────────────┐
         │                          │                          │
         ▼                          ▼                          ▼
  ┌─────────────┐           ┌─────────────┐           ┌─────────────┐
  │  EKSEN X    │           │  EKSEN Y    │           │  EKSEN Z    │
  │ UpdateAxis  │           │ UpdateAxis  │           │ UpdateAxis  │
  │             │           │             │           │             │
  │ e = err.ex  │           │ e = err.ey  │           │ e = err.ez  │
  │ cfg.x       │           │ cfg.y       │           │ cfg.z       │
  │ state: x_   │           │ state: y_   │           │ state: z_   │
  └──────┬──────┘           └──────┬──────┘           └──────┬──────┘
         │ vx_cmd                  │ vy_cmd                  │ vz_cmd
         │ (m/s)                   │ (deg/s yaw)             │ (deg/s pitch)
         └──────────────────────────┼──────────────────────────┘
                                    │
                                    ▼
                           Output{valid=true,
                                  vx_cmd, vy_cmd, vz_cmd,
                                  yaw_rate_cmd=0}
                                    │
                                    ▼
                         BuildSetpointFromPid()
                                    │
                                    ▼
                         SetpointCommand → 0x03 TX
```

**Önemli:** Üretim yolunda `err.eyaw = 0` sabitlenir; yaw kontrolü Y ekseni üzerinden yapılır.

#### 2.4.5 Z-Domen Transfer Fonksiyonu Görünümü

Tek eksen için (anti-windup ve doyum hariç ideal durum):

$$U(z) = K_p E(z) + K_i \frac{T_s}{1 - z^{-1}} E(z) + K_d \frac{1 - z^{-1}}{T_s} E(z)$$

Burada $T_s \approx \Delta t_k$ (ölçülen örnekleme periyodu).

**Türev bloğu** (geriye fark):
$$\dot{E}(z) = \frac{1 - z^{-1}}{T_s} E(z)$$

**İntegral bloğu** (Euler):
$$I(z) = \frac{T_s}{1 - z^{-1}} E(z)$$

**Slew-rate bloğu** (doğrusal olmayan):
$$u_k = \text{clamp}\!\left(u_k^{sat},\; u_{k-1} - \dot{u}_{max} T_s,\; u_{k-1} + \dot{u}_{max} T_s\right)$$

#### 2.4.6 Sinyal Akış Tablosu (Tek Eksen)

| Aşama | Sinyal adı | Kod değişkeni | Birim (X/Y/Z) | Matematik |
|-------|-----------|---------------|---------------|-----------|
| Giriş | Ham hata | `error` | m / deg / deg | $e_k$ |
| 1 | Ölü bant çıkışı | `error` (üzerine yazılır) | aynı | $e_k'$ |
| 2 | Türev | `derivative` | birim/s | $\dot{e}_k$ |
| 3 | İntegral aday | `integral_candidate` | birim·s | $I_k^{cand}$ |
| 4 | Doymamış çıkış | `unsat_candidate` | birim/s | $u_k^{unsat}$ |
| 5 | Anti-windup kararı | `pushing_high/low` | bool | — |
| 6 | Güncel integral | `st->integral` | birim·s | $I_k$ |
| 7 | PID çıkışı | `output` (sat. öncesi) | birim/s | $K_p e + K_i I + K_d \dot{e}$ |
| 8 | Doymuş çıkış | `output` (sat. sonrası) | birim/s | $u_k^{sat}$ |
| 9 | Slew çıkışı | `output` (nihai) | birim/s | $u_k$ |
| Durum | Önceki hata | `st->prev_error` | — | $e_{k-1}$ |
| Durum | Önceki çıkış | `st->prev_output` | — | $u_{k-1}$ |

#### 2.4.7 `Update()` Zamanlama ve Mutex Akışı

```
  now_tp giriş
       │
       ▼
  ┌─────────────────────────────────────────┐
  │ MUTEX LOCK: cfg, x_,y_,z_,yaw_, last_tp │  ← kısa kritik bölge
  │ kopyala → yerel değişkenler             │
  └────────────────┬────────────────────────┘
                   │ MUTEX UNLOCK
                   ▼
  valid=false? ──EVET──► MUTEX LOCK → ResetLocked() → return
                   │
                  HAYIR
                   ▼
  ilk adım? ──EVET──► last_tp kur → return (valid=false)
                   │
                  HAYIR
                   ▼
  Δt hesapla → geçersiz? ──EVET──► return (valid=false)
                   │
                  HAYIR
                   ▼
  UpdateAxis × 4  (kilit DIŞINDA — ağır hesap)
                   │
                   ▼
  ┌─────────────────────────────────────────┐
  │ MUTEX LOCK: durum + last_tp yaz         │
  └────────────────┬────────────────────────┘
                   ▼
              return Output
```

Bu desen, GStreamer probe thread'i ile güdüm timer'ının aynı PID nesnesine erişmesinde blokaj süresini minimize eder.

#### 2.4.8 Eksen–Fizik Eşlemesi ve Kontrol Anlamı

| Eksen | Hata $e$ kaynağı | Çıkış $u$ | Fiziksel etki | Pozitif hata → pozitif çıkış |
|-------|-----------------|-----------|---------------|------------------------------|
| **X** | $d - d_{standoff}$ (m) | $v_x$ (m/s) | İleri/geri kapanma | Hedef uzak → ileri git |
| **Y** | $(n_x - 0.5) \cdot g$ (deg) | $v_y$ (deg/s) | Yaw oranı | Hedef sağda → sağa dön |
| **Z** | $(n_y - 0.5) \cdot g$ (deg) | $v_z$ (deg/s) | Pitch oranı | Hedef altta → aşağı pitch |
| **Yaw** | `err.eyaw` (deg) | `yaw_rate_cmd` | Yedek yaw | Üretimde kullanılmaz |

#### 2.4.9 Kod–Blok Eşleme Özeti

```cpp
// ── DEADBAND bloğu ──
if (std::abs(error) < cfg.deadband) { error = 0.0f; }

// ── TÜREV bloğu ──
const float derivative = (error - st->prev_error) / dt;

// ── İNTEGRAL + CLAMP bloğu ──
const float integral_candidate =
    std::clamp(st->integral + error * dt, -cfg.i_limit, cfg.i_limit);

// ── ANTI-WINDUP bloğu ──
const float unsat_candidate = cfg.kp*error + cfg.ki*integral_candidate + cfg.kd*derivative;
if (!(pushing_high || pushing_low)) { st->integral = integral_candidate; }

// ── TOPLAMA + OUTPUT SATURATION bloğu ──
float output = cfg.kp*error + cfg.ki*st->integral + cfg.kd*derivative;
output = std::clamp(output, -cfg.output_limit, cfg.output_limit);

// ── SLEW-RATE bloğu ──
output = std::clamp(output, st->prev_output - slew_rate*dt, st->prev_output + slew_rate*dt);

// ── DURUM GÜNCELLEME (z⁻¹) ──
st->prev_error = error;
st->prev_output = output;
```

#### 2.4.10 Kararlılık ve Pratik Tasarım Notları

1. **Ayrık zaman gecikmesi:** $\Delta t$ her adımda ölçülür; sabit $T_s$ varsayımı yapılmaz. Bu, GMainLoop jitter'ına karşı dayanıklılık sağlar.

2. **Türev gürültü amplifikasyonu:** $K_d > 0$ iken bbox jitter'ı $\dot{e}_k$ üzerinden yükseltilir. Üretimde $K_d = 0$; gerekirse önce $\delta$ artırılır.

3. **Integral windup:** Hem $I_{lim}$ ön kırpması hem koşullu güncelleme uygulanır — çift katmanlı koruma.

4. **Slew-rate:** Çıkış doygunluğundan sonra uygulanır; ani komut sıçramalarını (jerk) sınırlar. $\dot{u}_{max} = 10$ birim/s, $\Delta t = 50$ ms iken adım başına max değişim $0.5$ birim.

5. **Eksen bağımsızlığı:** X/Y/Z kanalları birbirine bağlı değildir (MIMO değil, bağımsız SISO bankası). Çapraz etkileşim yalnızca plant (İHA dinamiği) tarafında oluşur.

---

## 3. PARAMETRE REHBERİ: AMAÇ, ETKİ VE AYARLAMA

Bu bölümde her parametrenin **ne işe yaradığı**, **artırıldığında** ve **azaltıldığında** oluşan davranış değişikliği ve **saha koşulunda ne zaman ayarlanması gerektiği** açıklanmaktadır.

### 3.1 PID Parametreleri (`PidControllerXYZYaw::AxisConfig`)

Kaynak: `src/control/pid_controller.hpp`, env override: `SAVASAN_PID_{X,Y,Z,YAW}_{KP,KI,KD}`

| Parametre | Sembol | Varsayılan | Birim | Ne işe yarar? | Artırılırsa | Azaltılırsa | Ne zaman ayarlanmalı? |
|-----------|--------|------------|-------|---------------|-------------|-------------|----------------------|
| Oransal kazanç | $K_p$ | 0.5 | — | Anlık hataya orantılı tepki; sistemin "sertliği" | Daha hızlı tepki; salınım/titreme riski artar | Daha yumuşak; yavaş takip, büyük kalıcı hata | Hedef merkezde titriyorsa **azalt**; takip çok yavaşsa **artır** (küçük adımlarla, örn. 0.1) |
| İntegral kazanç | $K_i$ | 0.01 | — | Kalıcı (steady-state) hatayı sıfırlar | Statik hata daha hızlı kapanır; overshoot ve windup riski | Kalıcı mesafe/açı hatası kalır | Uzun süre hedefe yaklaşamıyorsa **artır**; salınım/overshoot varsa **azalt** |
| Türev kazanç | $K_d$ | 0.0 | — | Hata değişim hızına tepki; salınım sönümleme | Salınım azalır; gürültüye duyarlılık artar | Daha az sönüm; ani hareketlerde overshoot | Üretimde 0; bbox jitter yüksekse $K_d > 0$ dene (0.01–0.05), gürültü artarsa geri al |
| İntegral sınırı | $I_{lim}$ | 5.0 | — | Anti-windup: integral birikim tavanı | Büyük geçici komutlar üretilebilir | Integral etkisi erken doyuma girer | Uzun oklüzyon sonrası ani sıçrama varsa **azalt** |
| Çıkış sınırı | $u_{lim}$ | 25.0 | m/s veya deg/s | Komut büyüklük tavanı | Daha agresif manevra | Daha güvenli, yavaş tepki | Saha hız limiti veya servo saturasyonu varsa **azalt** |
| Ölü bant | $\delta$ | 0.02 | hata birimi | Küçük hataları sıfırlar; titremeyi keser | Daha az mikro-düzeltme; merkezde ölü bölge büyür | Daha hassas; servo titremesi artabilir | Merkezde "buzz" varsa **artır**; hedef kaybında geç tepki varsa **azalt** |
| Slew-rate | $\dot{u}_{max}$ | 10.0 | birim/s | Komut değişim hızı sınırı | Daha ani komut geçişleri | Daha yumuşak geçişler | Komutlarda sıçrama/jerk varsa **azalt**; çok yavaş dönüş varsa **artır** |

**Eksen bazlı semantik:**

| Eksen | Hata birimi | Çıkış birimi | Fiziksel anlam |
|-------|-------------|--------------|----------------|
| X | m (mesafe hatası) | m/s (kapanma) | Pozitif $e_x$ → hedef uzak → ileri kapan |
| Y | deg (yatay açı) | deg/s (yaw) | Pozitif $e_y$ → hedef sağda → sağa dön |
| Z | deg (dikey açı) | deg/s (pitch) | Pozitif $e_z$ → hedef altta → aşağı pitch; negatif = tırman |

**Önerilen ayar prosedürü:**

1. $K_i = 0$, $K_d = 0$ ile yalnızca $K_p$ artırılarak salınım eşiği bulunur.
2. Kalıcı hata kalıyorsa $K_i$ küçük adımlarla artırılır.
3. Titreme devam ediyorsa $\delta$ veya slew-rate artırılır.
4. Her değişiklikten sonra en az 10–20 s gözlem yapılır.

### 3.2 Hedef Kestirimi Parametreleri (`WorldTargetEstimator::Config`)

Kaynak: `src/control/world_target_estimator.hpp`, env: `SAVASAN_GUIDANCE_*`

| Parametre | Sembol | Varsayılan | Birim | Ne işe yarar? | Artırılırsa | Azaltılırsa | Ne zaman ayarlanmalı? |
|-----------|--------|------------|-------|---------------|-------------|-------------|----------------------|
| Hedef mesafe | $d_{standoff}$ | 20.0 | m | İdeal takip mesafesi referansı | Daha uzaktan takip; $e_x$ pozitif bias | Daha yakın takip; çarpışma riski | Rakibe çok yaklaşılıyorsa **artır**; mesafe çok açıksa **azalt** |
| Mesafe kazancı | $k_{dist}$ | 0.20 | m | Bbox alanından mesafe: $d = k_{dist}/\sqrt{wh}$ | Aynı bbox'ta daha büyük $d$ tahmini | Daha küçük $d$ tahmini | Mesafe sürekli fazla tahmin ediliyorsa **azalt**; az tahmin **artır** (saha kalibrasyonu) |
| Piksel→derece | $g_{px \to deg}$ | 60.0 | deg | Piksel sapmasını açı hatasına çevirir | Daha agresif yaw/pitch komutu | Daha yumuşak açısal tepki | Hedef merkeze yavaş geliyorsa **artır**; salınım varsa **azalt** |
| Min/max mesafe | $d_{min}, d_{max}$ | 3, 120 | m | Mesafe kestirimi kırpma | — | — | Çok yakın/uzak bbox outlier'larında ayarlanır |
| Focal length | $f_{px}$ | 0 (kapalı) | px | Pinhole mesafe modeli | Pinhole model aktif olur | Alan modeli kullanılır | Kamera intrinsik kalibrasyonu yapıldıysa etkinleştir |

### 3.3 Araç Durum Kestirimi Parametreleri (`VehicleStateEstimator::Config`)

Kaynak: `src/control/vehicle_state_estimator.hpp`, env: `SAVASAN_STATE_*`

| Parametre | Sembol | Varsayılan | Birim | Ne işe yarar? | Artırılırsa | Azaltılırsa | Ne zaman ayarlanmalı? |
|-----------|--------|------------|-------|---------------|-------------|-------------|----------------------|
| Telemetri ağırlığı | $w_{tele}$ | 0.7 | — | GPS/IMU türevli hızın füzyon payı | Telemetriye daha çok güven | Görüntüye daha çok güven | GPS güvenilirse **artır**; telemetri gecikmeli/noisy ise **azalt** |
| Görüntü ağırlığı | $w_{vis}$ | 0.3 | — | Bbox hız türevinin füzyon payı | Görsel hareket daha baskın | Telemetri baskın | Tracker stabil, telemetri zayıfsa **artır** |
| Hız LPF $\alpha$ | $\alpha_{vel}$ | 0.4 | — | Hız yumuşatma (1.0 = filtre kapalı) | Daha hızlı ölçüme uyum; gürültü artar | Daha yumuşak hız; gecikme artar | Hız tahmini sıçrıyorsa **azalt**; tepki yavaşsa **artır** |
| Innovation kapısı | $g_{vel}$ | 5.0 | m/s | Tek adımda kabul edilen max hız değişimi | Büyük sıçramalar filtrelenir | Daha fazla aykırı değer geçer | GPS spike'ları varsa **azalt**; gerçek hızlı manevra kesiliyorsa **artır** |
| Yaw LPF $\alpha$ | $\alpha_{yaw}$ | 0.4 | — | Yaw hızı yumuşatma | Daha hızlı yaw takibi | Daha yumuşak yaw | Pusula gürültülüyse **azalt** |
| Yaw kapısı | $g_{yaw}$ | 50.0 | deg/s | Yaw innovation kırpma | Daha sıkı aykırı red | Daha gevşek | Ani dönüşlerde yaw kesiliyorsa **artır** |
| Max hız | $v_{max}$ | 25.0 | m/s | Kestirilen hız tavanı | — | — | Fiziksel limit aşımında **azalt** |

**Not:** VSE çıkışı şu an PID'e doğrudan beslenmez; hız büyüklüğü tracker profil önerisi için kullanılır.

### 3.4 APF Kaçış Parametreleri (`PotentialFieldConfig`)

Kaynak: `src/evasion/potential_field_evasion.hpp`, env: `SAVASAN_APF_*`

| Parametre | Sembol | Varsayılan | Birim | Ne işe yarar? | Artırılırsa | Azaltılırsa | Ne zaman ayarlanmalı? |
|-----------|--------|------------|-------|---------------|-------------|-------------|----------------------|
| Rakip itme katsayısı | $k_{rep}$ | 500 | — | Rakip repulsive kuvvet büyüklüğü | Daha sert kaçış | Daha yumuşak kaçış | Rakibe çok geç tepki veriliyorsa **artır**; aşırı panik/manuel override varsa **azalt** |
| Tehdit yarıçapı | $d_0$ | 100 | m | APF'nin devreye girdiği mesafe | Daha erken kaçış başlar | Daha geç kaçış | Erken kaçış isteniyorsa **artır**; gereksiz kaçış varsa **azalt** |
| HSS itme katsayısı | $k_{hss}$ | 800 | — | HSS yasak bölge itme gücü | HSS'den daha sert uzaklaşma | Daha zayıf HSS tepkisi | HSS ihlali riski varsa **artır** |
| HSS buffer | $b_{buffer}$ | 25 | m | HSS yarıçapına eklenen güvenlik halkası | Daha erken HSS kaçışı | Daha geç HSS kaçışı | İhlal puanı alınıyorsa **artır** |
| Max kaçış hızı | $v_{max}^{APF}$ | 8 | m/s | İleri kaçış hız tavanı | Daha hızlı kaçış | Daha yavaş kaçış | Güvenli hız aşılıyorsa **azalt** |
| Max yaw hızı | $\dot{\psi}_{max}$ | 25 | deg/s | Kaçış dönüş hızı tavanı | Daha hızlı yön değişimi | Daha yavaş dönüş | Dönüşte stall/instabilite varsa **azalt** |
| Yaw kazancı | $k_{yaw}$ | 0.5 | — | Heading hatası → yaw komutu | Daha agresif yön düzeltme | Daha yavaş yön düzeltme | Kaçış yönüne geç yöneliyorsa **artır** |
| Stale eşiği | $t_{stale}$ | 2000 | ms | Eski rakip konumunu reddetme | Daha eski veri kabul | Daha taze veri zorunlu | Sunucu gecikmesi yüksekse **artır** |
| Local min eşiği | $\varepsilon_{min}$ | 0.05 | — | Yerel minimum algılama | Daha erken bias kaçışı | Daha geç bias kaçışı | İki tehdit arasında takılı kalıyorsa **artır** |
| Local min bias | $\Delta\theta_{bias}$ | 15 | deg | Ölü noktadan kaçış sapması | Daha büyük yön değişimi | Daha küçük sapma | Aynı noktada döngüsel takılma varsa **artır** |

### 3.5 Çalışma Zamanı (Runtime) Parametreleri

| Env değişkeni | Varsayılan | Ne işe yarar? | Artır | Azalt |
|---------------|------------|---------------|-------|-------|
| `SAVASAN_CONTROL_HZ` | 20 | Güdüm döngü frekansı | Daha sık komut; CPU yükü artar | Daha seyrek komut; gecikme artar |
| `SAVASAN_GUIDANCE_OCCLUSION_HOLD_MS` | 400 | Oklüzyonda son komut tutma süresi | Kısa kayıplara daha toleranslı | Daha hızlı sıfırlama |
| `SAVASAN_GUIDANCE_OCCLUSION_MODE` | hold_last | Oklüzyon davranışı | hold_last = süreklilik | hold_zero = anında dur |
| `SAVASAN_SETPOINT_TX_ENABLE` | 0 | Gerçek seri TX | 1 = uçuş | 0 = dry-run test |
| `SAVASAN_EVASION_ENABLE` | 0 | APF kaçış etkinliği | 1 = yarışma kaçışı | 0 = yalnızca takip |

---

## 4. KOD ÖRNEKLERİ İLE FONKSİYONEL ANALİZ

### 4.1 PID: Tek Eksen Hesabı ve Anti-Windup

Aşağıdaki kod, LaTeX'te verilen ayrık PID denklemlerinin birebir uygulamasıdır:

```cpp
// pid_controller.cpp — UpdateAxis()
if (std::abs(error) < cfg.deadband) {
  error = 0.0f;  // ölü bant: |e| < δ → e = 0
}

const float derivative = (dt > 1e-6f) ? ((error - st->prev_error) / dt) : 0.0f;
const float integral_candidate =
    std::clamp(st->integral + error * dt, -cfg.i_limit, cfg.i_limit);

const float unsat_candidate =
    cfg.kp * error + cfg.ki * integral_candidate + cfg.kd * derivative;
const bool pushing_high = (unsat_candidate > cfg.output_limit) && (error > 0.0f);
const bool pushing_low = (unsat_candidate < -cfg.output_limit) && (error < 0.0f);
if (!(pushing_high || pushing_low)) {
  st->integral = integral_candidate;  // koşullu anti-windup
}

float output = cfg.kp * error + cfg.ki * st->integral + cfg.kd * derivative;
output = std::clamp(output, -cfg.output_limit, cfg.output_limit);

if (cfg.slew_rate > 0.0f && dt > 0.0f) {
  const float max_delta = cfg.slew_rate * dt;
  output = std::clamp(output, st->prev_output - max_delta, st->prev_output + max_delta);
}
```

**Kod–formül eşlemesi:**

| Kod satırı | LaTeX karşılığı |
|------------|-----------------|
| `error * dt` | $e_k \cdot \Delta t_k$ |
| `(error - prev_error) / dt` | $\dot{e}_k = (e_k - e_{k-1})/\Delta t_k$ |
| `pushing_high / pushing_low` | Doygunlukta integral dondurma |
| `slew_rate * dt` | $\dot{u}_{max} \cdot \Delta t_k$ |

### 4.2 PID: Dört Eksenli Kontrol Adımı

```cpp
// pid_controller.cpp — Update()
const float dt = std::chrono::duration<float>(now_tp - last_tp).count();
if (dt <= 0.0f || dt > 0.5f) {
  last_tp_ = now_tp;
  return out;  // valid=false: geçersiz Δt koruması
}

out.valid = true;
out.vx_cmd = UpdateAxis(cfg.x, &x, err.ex, dt);   // mesafe → m/s
out.vy_cmd = UpdateAxis(cfg.y, &y, err.ey, dt);   // açı → deg/s yaw
out.vz_cmd = UpdateAxis(cfg.z, &z, err.ez, dt);   // açı → deg/s pitch
```

$\Delta t > 0.5\,\text{s}$ koşulu, GMainLoop jitter veya pipeline duraklamasında integral patlamasını engeller.

### 4.3 Hedef Kestirimi: Piksel → NED Hata

```cpp
// world_target_estimator.cpp — Estimate()
distance = config_.distance_gain / std::sqrt(area);  // d = k_dist / √(wh)
distance = std::clamp(distance, config_.min_distance_m, config_.max_distance_m);

const float x_prime = obs.nx - 0.5f;
const float y_prime = obs.ny - 0.5f;

out.error_x_m = distance - config_.desired_standoff_m;  // e_x = d - d_standoff
out.error_y_m = x_prime * gain;   // e_y = (nx-0.5) · g
out.error_z_m = y_prime * gain;   // e_z = (ny-0.5) · g
```

### 4.4 Innovation-Kapılı LPF

```cpp
// vehicle_state_estimator.cpp — FilterWithInnovationGate()
float innovation = measurement - prev;
if (gate_abs > 0.0f) {
  innovation = std::clamp(innovation, -gate_abs, gate_abs);  // ν ← clamp(ν, ±g)
}
return prev + (alpha * innovation);  // v̂_k = v̂_{k-1} + α·ν
```

Füzyon adımında ağırlıklı ortalama:
```cpp
fused_vx = ntw * tvx + nvw * vvx;  // ṽ = w_t·v_tele + w_v·v_vis
next.vel_x_mps = FilterWithInnovationGate(next.vel_x_mps, fused_vx, alpha, vel_gate);
```

### 4.5 APF: Repulsive Kuvvet ve Setpoint

```cpp
// potential_field_evasion.cpp — RepulsiveMagnitude()
// |F| = k · (1/d - 1/d₀) · (1/d²)
return k * (inv_d - inv_d0) * (inv_d * inv_d);

// ComputeApfSetpoint() — setpoint eşlemesi
const float heading_err = WrapAngleDeg180(desired_heading - own_yaw_deg);
result.cmd.vy_mps = std::clamp(cfg.k_yaw * heading_err, -cfg.max_yaw_rate_dps, cfg.max_yaw_rate_dps);
result.cmd.vx_mps = std::clamp(cfg.max_speed_mps * std::max(speed_scale, 0.25f), 0.0f, cfg.max_speed_mps);
```

`speed_scale = min(|F|/k_scale, 1)` ile kuvvet büyüklüğü ileri hıza orantılanır; minimum %25 hız garantisi (`0.25f`) yerel minimumda dahi kaçış devamlılığını sağlar.

### 4.6 Ana Kontrol Döngüsü: Arbitraj

```cpp
// phase5_guidance.cpp — RunGuidanceControlStep()
if (degraded) {
  rt->guidance.apf_active.store(false);
  return false;  // degrade: setpoint üretilmez
}

// APF öncelikli
if (apf.active) {
  SendSetpointOrDegrade(rt, apf.cmd, dry_run);
  return true;  // PID atlanır
}

// Oklüzyon
if (!measurement_valid) {
  if (TryOcclusionHoldLast(rt, now, dry_run)) return true;
  SendZeroSetpointIfNeeded(rt, dry_run);
  return false;
}

// PID takip
err.ex = tgt.error_x_m;
err.ey = tgt.error_y_m;
err.ez = tgt.error_z_m;
const auto out = rt->guidance.pid_controller->Update(err, now);
const auto cmd = BuildSetpointFromPid(out);
SendSetpointOrDegrade(rt, cmd, dry_run);
```

### 4.7 Kare Hızında Kestirim ve Görüntü Hızı

```cpp
// phase5_callbacks.cpp — ProcessLockPipelineFrame()
estimate = rt->guidance.world_estimator->Estimate(obs);

// Görüntü hız türevi (Y/Z eksenleri)
vis.vel_y_mps = (-(nx - prev_nx) / dt_s) * vision_gain;
vis.vel_z_mps = (-(ny - prev_ny) / dt_s) * vision_gain;
rt->guidance.state_estimator->UpdateVision(vis);

rt->guidance.latest_target = estimate;
rt->guidance.latest_lock_valid = obs.valid;
```

### 4.8 Parametre Yükleme (Ortam Değişkenleri)

```cpp
// phase5_runtime_options.cpp — ConfigurePhase5Controllers()
load_axis("SAVASAN_PID_X_KP", "SAVASAN_PID_X_KI", "SAVASAN_PID_X_KD",
          &cfg.x, 0.5f, 0.01f, 0.0f);

c.desired_standoff_m = std::max(1.0f, atof(standoff));  // SAVASAN_GUIDANCE_STANDOFF_M
c.pixel_to_deg_gain = std::max(1.0f, atof(pixel_deg));  // SAVASAN_GUIDANCE_PIXEL_TO_DEG
c.velocity_alpha = clamp(atof(vel_alpha), 0.0f, 1.0f);  // SAVASAN_STATE_VELOCITY_ALPHA
```

### 4.9 Zamanlama: 20 Hz Güdüm Timer'ı

```cpp
// phase3_runner.cpp
int control_hz = 20;  // SAVASAN_CONTROL_HZ override: [5, 100]
guidance_source_id = g_timeout_add(1000 / control_hz, GuidanceControlTick, phase5);

// phase5_callbacks.cpp
gboolean GuidanceControlTick(gpointer user_data) {
  RunGuidanceControlStep(rt, std::chrono::steady_clock::now());
  return G_SOURCE_CONTINUE;
}
```

---

## 5. DOYUM, FİLTRELEME VE GÜVENLİK MEKANİZMALARI

### 5.1 Anti-Windup Mantığı

Doygunlukta integral birikimi şu koşulla durdurulur:
$$\text{pushing}_{high} = (u^{unsat} > u_{lim}) \land (e > 0)$$
$$\text{pushing}_{low} = (u^{unsat} < -u_{lim}) \land (e < 0)$$

Bu, klasik **conditional integration** anti-windup yöntemidir.

### 5.2 Çok Katmanlı Doyum Tablosu

| Katman | Parametre | Etki |
|--------|-----------|------|
| PID integral | $I_{lim}$ | Integral birikim tavanı |
| PID çıkış | $u_{lim}$ | Komut genliği tavanı |
| PID slew | $\dot{u}_{max}$ | Komut değişim hızı |
| VSE hız | $v_{max}$ | Kestirilen hız tavanı |
| APF hız | $v_{max}^{APF}$ | Kaçış hız tavanı |
| Mesafe | $d_{min}, d_{max}$ | Geometrik kestirim kırpma |

### 5.3 Oklüzyon ve Degrade

| Durum | Koşul | Davranış |
|-------|-------|----------|
| hold_last | `occlusion_hold_last=true` | Son 0x03 komutu `occlusion_hold_ms` boyunca tekrarlanır |
| hold_zero | `occlusion_hold_last=false` | Anında sıfır setpoint |
| degrade | `degraded.active=true` | Tüm setpoint üretimi durur |

---

## 6. PERFORMANS VE REAL-TIME ANALİZ

| Döngü | Frekans | Mekanizma |
|-------|---------|-----------|
| Güdüm | 20 Hz | `g_timeout_add` |
| Görüntü kestirimi | ~30 Hz | GStreamer probe |
| APF | 20 Hz | Güdüm ile birleşik |

**Optimizasyon desenleri:** Kısa mutex tutma (kopyala–hesapla–yaz), durumsuz WTE (heap yok), $\Delta t$ geçerlilik kapısı, TX worker kuyruğu.

---

## 7. SAHA AYAR KARAR AĞACI

```
Hedef merkezde titriyor mu?
  ├─ EVET → Kp azalt VEYA deadband artır VEYA pixel_to_deg azalt
  └─ HAYIR → Hedefe yavaş yaklaşıyor mu?
       ├─ EVET → Kp artır VEYA Ki artır
       └─ HAYIR → Oklüzyonda sıçrama var mı?
            ├─ EVET → occlusion_hold_ms artır VEYA I_lim azalt
            └─ HAYIR → Rakip yaklaşınca geç kaçıyor mu?
                 ├─ EVET → k_rep artır VEYA d0 artır
                 └─ HAYIR → HSS ihlali mi?
                      ├─ EVET → k_hss artır VEYA hss_buffer artır
                      └─ HAYIR → Mevcut parametreler uygun
```

---

## EK A: Tüm Varsayılanlar

| Grup | Parametre | Değer |
|------|-----------|-------|
| PID | $K_p, K_i, K_d$ | 0.5, 0.01, 0.0 |
| PID | $I_{lim}, u_{lim}, \delta, \dot{u}_{max}$ | 5, 25, 0.02, 10 |
| WTE | $d_{standoff}, k_{dist}, g_{px \to deg}$ | 20 m, 0.20, 60 deg |
| VSE | $w_{tele}/w_{vis}, \alpha, g$ | 0.7/0.3, 0.4, 5 m/s |
| APF | $k_{rep}, d_0, k_{hss}, b_{buffer}$ | 500, 100 m, 800, 25 m |
| APF | $v_{max}, \dot{\psi}_{max}, k_{yaw}$ | 8 m/s, 25 deg/s, 0.5 |
| Runtime | $f_s^{control}$, hold_ms | 20 Hz, 400 ms |

---

## SONUÇ

Kontrol algoritması, görüntü tabanlı geometrik kestirim + ayrık PID takip ve telemetri tabanlı repulsive APF kaçışın Jetson üzerinde arbitraj edildiği hibrit bir mimaridir. Tüm parametreler runtime env ile sahada ayarlanabilir; bu rapordaki tablolar her parametrenin fiziksel anlamını, artırma/azaltma etkisini ve tipik ayar senaryolarını tanımlamaktadır. Kod örnekleri, LaTeX formülasyonlarının `pid_controller.cpp`, `world_target_estimator.cpp`, `vehicle_state_estimator.cpp`, `potential_field_evasion.cpp` ve `phase5_guidance.cpp` dosyalarındaki birebir karşılıklarını göstermektedir.

---

*Kaynak dosyalar: `02_Ana_Sistem_CPP/src/control/`, `src/evasion/`, `src/runners/phase5_guidance.*`, `phase5_callbacks.cpp`, `phase5_runtime_options.cpp`*
