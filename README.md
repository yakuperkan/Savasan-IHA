# Savaşan İHA — Faz 5

Onboard vision, lock, RF hub and guidance software for the TEKNOFEST Savaşan İHA (Fighting UAV) competition. Runs on NVIDIA Jetson Orin NX.

TEKNOFEST Savaşan İHA yarışması için Jetson Orin NX üzerinde çalışan DeepStream tabanlı görüntü işleme, hedef takip, kilitlenme ve güdüm sistemi.

**Yazar:** Yakup Erkan KAYMAZ  
**Üretim kodu:** `02_Ana_Sistem_CPP/` (C++17)  
**Çalışma dizini:** `/home/nvidia/Savasan_IHA_Workspace`

---

## Mimari (özet)

```
Kamera (USB BRIO) → decode → nvstreammux → nvinfer (YOLO26) → nvtracker (NvDCF)
    → TrackSelector / lock → AlcLinkBridge (seri) + RF hub → güdüm / kaçış
```

Varsayılan mission modu: **AIR_LOCK** (`phase5`). Üretim `nvinfer interval=0` (her karede YOLO).

---

## Dizin yapısı

```
Savasan_IHA_Workspace/
├── 02_Ana_Sistem_CPP/     # Ana C++ uygulama, config, systemd, testler
├── 03_Modeller/           # YOLO26 ONNX / TensorRT engine
├── docs/                  # Operasyon ve teknik dokümanlar
├── scripts/               # Koşu, benchmark, preset scriptleri
├── 00_Requirements_and_Rules/  # Şartname (yerel, git dışı)
├── 97_Documents/          # DeepStream referans notları
└── 98_Reference_Repos/    # Referans repolar (yerel clone, git dışı)
```

---

## Hızlı başlangıç

### Derleme

```bash
cd 02_Ana_Sistem_CPP
mkdir -p build && cd build
cmake .. -DSAVASAN_BUILD_APP=ON -DSAVASAN_BUILD_TESTS=ON
cmake --build . -j$(nproc)
```

CI/test-only (DeepStream gerekmez):

```bash
cmake .. -DSAVASAN_BUILD_APP=OFF -DSAVASAN_BUILD_TESTS=ON
make -j$(nproc) && ctest --output-on-failure
```

### Yarışma koşusu (phase5)

```bash
# Headless performans / latency ölçümü
export SAVASAN_DISPLAY=
export SAVASAN_RUN_SECONDS=0          # 0 = süresiz
export NVDS_ENABLE_LATENCY_MEASUREMENT=1

./build/savasan_iha phase5 usb hybrid

# Gerçek seri + otopilot (AlcLink)
export SAVASAN_ALC_DISABLE=0
export SAVASAN_SETPOINT_TX_ENABLE=1
./build/savasan_iha phase5 usb hybrid
```

Systemd ile uçuş: `02_Ana_Sistem_CPP/config/systemd/install.sh` — ayrıntı `docs/DEPLOYMENT.md` ve `docs/OPERATOR_CHEAT_SHEET.md`.

---

## Sık kullanılan ortam değişkenleri

| Değişken | Açıklama | Varsayılan |
|----------|----------|------------|
| `SAVASAN_V4L2_DEVICE` | USB kamera | `/dev/video0` |
| `SAVASAN_DISPLAY` | Boş = fakesink; `display` = nv3dsink | systemd'den |
| `SAVASAN_RUN_SECONDS` | Koşu süresi (sn); `0` = sınırsız | `0` |
| `SAVASAN_MISSION_MODE` | `air_lock` (LOCK workspace) | `air_lock` |
| `SAVASAN_AIRLOCK_NVINFER_INTERVAL` | YOLO atlama (`2` = her 3. kare) | `2` |
| `SAVASAN_ALC_DISABLE` | `1` = seri kapalı (bench) | `0` (uçuş) |
| `SAVASAN_ALC_LOCK_PACKET_VERSION` | Lock paketi: `1` legacy, `2` v2 | `1` |
| `SAVASAN_LOCK_MODE` | `hybrid` / `baseline` | `hybrid` |
| `SAVASAN_TRACKER_PROFILE` | `default` / `calm` / `aggressive` / `auto` | `default` |
| `SAVASAN_TRACKER_AUTO_RESTART` | `0` = profil restart kapali (nvtracker acik) | `0` |
| `SAVASAN_DISPLAY_SYNC` | Boş/`1` = nv3dsink sync; `0` = dusuk latency | sync acik |

Tam liste: `docs/README.md`, `docs/DEPLOYMENT.md`.

---

## Performans (ölçülmüş — son doğrulanmış pipeline)

Koşu: `phase5 usb`, fakesink (`SAVASAN_DISPLAY=`), mevcut üretim config, MAXN (nvpmodel 0), 25 sn.  
Kaynak: `docs/NESNE_TAKIP_TEKNIK_RAPOR.md` §4.1.

| Metrik | Ortalama | Min / Max | p95 |
|--------|----------|-----------|-----|
| FPS | **27.01** | 23.50 / 30.60 | 29.80 |
| End-to-end latency | **329.65 ms** | 286.40 / 411.50 | 379.50 ms |
| GPU | **99.04%** | — | 99.20% |
| VDD_IN | **17.48 W** | — | 19.83 W |
| TJ | **47.84 °C** | — | 49.84 °C |

GPU doygun; YOLO FP16 baskın maliyet. Eski 2026-06-10 tablosu (~58 FPS / ~61 ms) bu hattı yansıtmıyor.

A/B script: `scripts/run_latency_test.sh phase5 usb`

---

## Test

```bash
cd 02_Ana_Sistem_CPP/build-ci   # veya build-test
ctest --output-on-failure -L unit
ctest --output-on-failure -L integration
```

---

## Dokümantasyon

| Dosya | İçerik |
|-------|--------|
| [docs/README.md](docs/README.md) | Dizin ve env özeti |
| [docs/BUILD.md](docs/BUILD.md) | Derleme |
| [docs/DEPLOYMENT.md](docs/DEPLOYMENT.md) | Systemd / sahaya kurulum |
| [docs/OPERATOR_CHEAT_SHEET.md](docs/OPERATOR_CHEAT_SHEET.md) | Tek sayfa operasyon |
| [docs/PERFORMANCE_BASELINE.md](docs/PERFORMANCE_BASELINE.md) | Benchmark prosedürü |
| [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) | Sorun giderme |
| [docs/APF_EVASION.md](docs/APF_EVASION.md) | Telemetri APF + HSS kaçış, env, log analizi |
| [docs/GUIDANCE_USER_MANUAL.md](docs/GUIDANCE_USER_MANUAL.md) | Güdüm saha kılavuzu |
| [docs/CHANGELOG.md](docs/CHANGELOG.md) | Yarışma commit notları / sahada hatırlatmalar |

---

## Platform

- **Donanım:** Jetson Orin NX (ARM64)
- **Yazılım:** DeepStream 6.x, GStreamer 1.20, OpenCV 4.8, TensorRT FP16
- **Model:** YOLO26 UAV (`03_Modeller/yolo26_uav22/`)

---

## Lisans

MIT — [LICENSE](LICENSE)  
Copyright (c) 2026 Yakup Erkan KAYMAZ
