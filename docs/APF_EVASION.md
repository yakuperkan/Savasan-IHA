# Rakip Kaçışı (APF) ve Yasak Bölge (Geofence)

**Hedef kitle:** Saha mühendisleri, Jetson operatörleri  
**Platform:** Jetson Orin NX + Alacakart otopilot  
**Son güncelleme:** 2026-08-23

> **Bu sürümde ne değişti:** HSS kaçışı APF'ten çıkarıldı. APF artık **yalnızca rakip
> uçaklar** içindir. HSS ve saha sınırı `evasion/geofence` modülünde yönetilir. Ayrıca
> kovaladığımız hedef APF'ten muaftır.

---

## 1. İki ayrı sistem, iki ayrı iş

| | APF | Geofence |
|---|---|---|
| Neye karşı | Rakip uçaklar | HSS daireleri + saha sınırı |
| Nasıl çalışır | İtici kuvvet alanı, sürekli | Süzgeç + eşik aşılınca kaçış hedefi |
| Ne üretir | Kaçış yönü/hızı | Gidilecek güvenli nokta |
| Önceliği | Takibin üstünde | **Her şeyin üstünde** |

Güdüm zinciri (`ComputeSeyirGuidance`, 50 ms):

```
geofence kaçışı  →  hedef seçimi  →  APF  →  kestirici  →  takip güdümü
                                                              ↓
                                              geofence nişan süzgeci
                                                              ↓
                                                  kilit bandı koruması
                                                              ↓
                                                       görsel trim
```

Her adım bir öncekini veto edebilir. Geofence kaçışı gerekiyorsa fonksiyon oracıkta döner:
takip da APF de çalışmaz.

Kaynaklar: `src/evasion/potential_field_evasion.*`, `src/evasion/geofence.*`,
`src/evasion/geo_kinematics.*`, `src/runners/seyir_guidance.hpp`.

---

## 2. Rakip APF

Her rakip için itici kuvvet:

\[
|F| = k_{rep} \cdot \left(\frac{1}{d} - \frac{1}{d_0}\right) \cdot \frac{1}{d^2}
\]

Yön: rakipten **uzağa**. Toplam \(\vec{F}_{total} = \sum \vec{F}_{rakip}\).

**Kovalanan hedef bu toplama girmez.** Girseydi takip güdümü hedefe yaklaşmaya, APF de aynı
hedeften uzaklaşmaya çalışırdı; uçak ikisinin arasında salınırdı. Kovaladığımız uçaktan
korunmayı kilit bandı üstlenir (bkz. §4).

Diğer rakipler için etki yarıçapı `d0 = 40 m`. Bu mesafe kilit penceresinin (~57 m) altında
seçildi: kaçış, kilit kurmaya çalıştığımız mesafeye karışmasın diye.

Rakip konumları RF üzerinden gelir (`0x20` → `/tmp/savasan_rival_pool.env`) ve tik anına
dead reckoning ile taşınır:

\[
\text{Tahmini\_Konum} = \text{Son\_Konum} + \text{Hız} \times \text{Geçen\_Zaman}
\]

`SAVASAN_APF_MAX_STALE_MS`'den eski konum yok sayılır.

---

## 3. Yerel minimum (kapan) koruması

İki rakibin tam ortasında kuvvetler birbirini götürür: bileşenler büyük, toplam sıfır.
Uçak "tehdit yok" sanıp düz uçmaya devam eder.

**Tespit ORANSALDIR:**

\[
|\vec{F}_{total}| < \varepsilon \cdot \sum_i |\vec{F}_i|
\]

Mutlak eşik **kullanılmaz**. Kuvvet büyüklüğü \(1/d^2\) ile ölçeklendiği için 40 m'deki bir
rakip ile 5 m'deki rakip arasında binlerce kat fark olur; sabit bir sayıyla kıyaslamak
uzaktaki tek bir rakibi bile "kapan" saymak demektir. (Bu tam olarak önceki sürümdeki
hataydı: sistem neredeyse her zaman yapay bias yönüne kaçıyor, gerçek itme yönünü
kullanmıyordu.)

Kapan tespit edilince:

1. Temel yön: mevcut küçük vektör; sıfırsa `own_yaw`
2. Yapay sapma **+15°** (`SAVASAN_APF_LOCAL_MIN_BIAS_DEG`)
3. Yapay büyüklük \(\sum_i |\vec{F}_i|\) — bileşenlerle aynı ölçekte, log okunabilir kalır

---

## 4. Kilit bandı — kovaladığımız hedefe çarpmamak

APF kovalanan hedefe uygulanmadığı için yakınlık koruması ayrı bir mekanizmadadır:
takip nişanı hedefe `25 m`'den yakınsa nişan noktası yana saptırılır
(`band_break_angle_deg`, vars. 25°).

25 m şu iki sınırın arasındadır:

- **Üstte** nominal takip mesafesi (28 m) — band bunun üstünde olsaydı normal takipte
  sürekli tetiklenir, hiç yaklaşamazdık
- **Altta** takip güdümünün kendi KOPMA mesafesi — band bunun altında kalsaydı iş işten
  geçtikten sonra devreye girerdi

Kilit penceresi ~57 m'de açılır, yani band devreye girdiğinde kilit çoktan kurulmuştur;
band kilidi bozmaz, sadece çarpışmayı önler.

---

## 5. Geofence — HSS ve saha sınırı

İki iş yapar:

**a) Nişan süzgeci.** Takip güdümünün ürettiği her nişan noktası ve oraya giden rota
`PointSafe`/`RouteSafe` kontrolünden geçer. Rakip bizi HSS'in içine veya sahanın dışına
çekiyorsa o tik komut üretilmez.

**b) Önleyici kaçış.** Sınıra veya HSS'e **önleyici bant** kadar yaklaşılınca kaçış hedefi
üretilir. Bant dönüş yarıçapına bağlıdır — uçak yay çizerek döndüğü için sınıra değince
dönmeye başlamak geç kalmaktır.

\[
\text{bant} = \max(\text{band\_min\_m},\ \text{band\_multiplier} \times R_{dönüş})
\]

Kaçış hedefi burnumuza göre 8 yönde aday üretip rotası HSS/sınır kesenleri eler, kalanları
"varılan yerin ferahlığı − dönüş maliyeti" ile puanlar. Aday kalmazsa saha merkezine düşer;
kaçış asla hedefsiz kalmaz.

HSS ve sınır verisi RF'ten gelir: `0x24` → `/tmp/savasan_hss_rf.env`,
`0x25` → `/tmp/savasan_boundary_rf.env`. HSS ihlali saniye başına **−5 puan** (şartname).

---

## 6. Arbitraj kuralları (değişmez)

| Durum | Davranış |
|---|---|
| Geofence kaçışı gerekli | Kaçış komutu gider; takip ve APF **çalışmaz** |
| `apf_active == true` | APF komutu gider; takip komutu ve lock/no-lock TX **kesilir** |
| Nişan geofence süzgecine takıldı | O tik **komut üretilmez** |
| Hiçbiri | Normal takip komutu |

Tracker metrikleri her durumda devam eder. SIHA telemetri/kilit puanı yer istasyonundadır (RF `0x01` / `0x31`).

**Gaz komutu hiçbir durumda gitmez** — hız otopilotun kendi işidir. Takip güdümü gaz yüzdesi
hesaplar ama bu değer yalnızca log/CSV'ye yazılır.

---

## 7. Konfigürasyon

### Rakip APF

| Ortam değişkeni | Açıklama | Varsayılan |
|-----------------|----------|------------|
| `SAVASAN_EVASION_ENABLE` | APF kaçış ana anahtarı (`1` = açık) | `0` |
| `SAVASAN_APF_D0` | Rakip etki yarıçapı (m) | `40` |
| `SAVASAN_APF_K_REP` | Rakip repulsive katsayısı | `500` |
| `SAVASAN_APF_LOCAL_MIN_EPS` | Kapan eşiği **oranı** (`\|F_top\| / Σ\|F_i\|`) | `0.05` |
| `SAVASAN_APF_LOCAL_MIN_BIAS_DEG` | Kapan kaçış sapması (°) | `15` |
| `SAVASAN_APF_MAX_SPEED_MPS` | APF ileri hız üst sınırı (m/s) | `8` |
| `SAVASAN_APF_MAX_YAW_DPS` | APF yaw rate clamp (deg/s) | `25` |
| `SAVASAN_APF_K_YAW` | Heading error → yaw rate kazancı | `0.5` |
| `SAVASAN_APF_MAX_STALE_MS` | Rakip konum eskime eşiği (ms) | `2000` |

### Geofence

| Ortam değişkeni | Açıklama | Varsayılan |
|-----------------|----------|------------|
| `SAVASAN_GEOFENCE_HSS_MARGIN_M` | HSS yarıçapına eklenen pay (m) | `20` |
| `SAVASAN_GEOFENCE_BOUNDARY_MARGIN_M` | Sınır kenarına bırakılan pay (m) | `30` |
| `SAVASAN_GEOFENCE_BAND_MIN_M` | Önleyici bant alt sınırı (m) | `60` |
| `SAVASAN_GEOFENCE_BAND_MULT` | Bant = çarpan × dönüş yarıçapı | `1.3` |

### Kilit bandı

| Ortam değişkeni | Açıklama | Varsayılan |
|-----------------|----------|------------|
| `SAVASAN_SEYIR_BAND_MIN_SEP_M` | Bandın devreye girdiği mesafe (m) | `25` |
| `SAVASAN_SEYIR_BAND_BREAK_DEG` | Nişan sapma açısı (°) | `25` |

**Örnek saha profili:**

```bash
export SAVASAN_EVASION_ENABLE=1
export SAVASAN_COMPETITION_TAKIM_NO=42
export SAVASAN_APF_D0=40
export SAVASAN_APF_K_REP=500
export SAVASAN_GEOFENCE_HSS_MARGIN_M=20
export SAVASAN_GEOFENCE_BOUNDARY_MARGIN_M=30
export SAVASAN_LOG_LEVEL=INFO
export SAVASAN_CONTROL_LOG_ENABLE=1
```

---

## 8. Log analizi

APF aktifken **100 ms** throttle ile:

```
[3180.551.824][INFO][APF] [APF KACIS] F_toplam: 12.450 | F_rakip: 12.450
```

`| LOCAL_MIN` soneki: kuvvetler birbirini götürdüğü kapanda +15° bias devreye girdi. Uçak
simetrik tuzağa düştü ama otomatik sıyrıldı.

Geofence kaçışı ayrı basılır; `geofence_reason` alanı `kOutsideBoundary`, `kInsideHss`,
`kNearBoundary`, `kNearHss` değerlerinden birini alır, `geofence_hss_id` kaynağı gösterir.

```bash
journalctl -u savasan-airlock -f | grep -E "APF KACIS|GEOFENCE"
```

---

## 9. İlgili testler

```bash
cd 02_Ana_Sistem_CPP/build-ci
ctest --output-on-failure -R "potential_field|geofence|seyir_guidance|guidance_control_loop"
```

- `test_potential_field_evasion` — Haversine, rakip repulsive, kapan tespiti
- `test_geofence` — nokta/rota süzgeci, ihlal tespiti, kaçış hedefi
- `test_seyir_guidance_safety` — kilit mesafesi, APF muafiyeti, kilit bandı eşikleri
- `test_phase5_guidance_control_loop_integration` — arbitraj sırası
