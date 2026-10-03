# Savaşan İHA — Dokümantasyon Girişi

Jetson Orin NX üzerinde Faz 5 (AIR_LOCK) yarışma modu. Üretim binary: `02_Ana_Sistem_CPP/build/savasan_iha`.

---

## Üretim odaklı dizinler

| Dizin | Rol |
|-------|-----|
| `02_Ana_Sistem_CPP/src/` | C++ kaynak (app, runners, tracking, control, autopilot, competition) |
| `02_Ana_Sistem_CPP/config/deepstream/` | nvinfer, tracker, preset env |
| `02_Ana_Sistem_CPP/config/systemd/` | Uçuş servisi, healthcheck, env doğrulama |
| `03_Modeller/yolo26_uav22/` | Aktif YOLO26 ONNX + Orin FP16 engine |
| `scripts/` | `run_phase3.sh`, `run_latency_test.sh`, benchmark, preset yükleme |

Python prototip katmanı kaldırıldı (2026-06); tüm runtime C++.

---

## Çalıştırma

```bash
# Standart yarışma hattı
./02_Ana_Sistem_CPP/build/savasan_iha phase5 usb hybrid

# Latency / FPS ölçümü (headless)
SAVASAN_DISPLAY= NVDS_ENABLE_LATENCY_MEASUREMENT=1 SAVASAN_RUN_SECONDS=25 \
  bash scripts/run_latency_test.sh phase5 usb
```

Fazlar: `phase1` … `phase5`. Yarışmada **`phase5`** kullanılır.

---

## Kritik ortam değişkenleri

**Pipeline / görüntü**
- `SAVASAN_PGI_CONFIG` — nvinfer config (varsayılan: YOLO26 FP16)
- `SAVASAN_TRACKER_CONFIG` — NvDCF yml
- `SAVASAN_DISPLAY` — boş = fakesink; `display` = ekran
- `SAVASAN_AIRLOCK_NVINFER_INTERVAL` — YOLO sıklığı (`2` önerilir; A/B doğrulandı)

**Mission / lock**
- `SAVASAN_MISSION_MODE=air_lock`
- `SAVASAN_LOCK_MODE=hybrid|baseline`
- `SAVASAN_ALC_DISABLE` — bench için `1`, uçuşta `0`
- `SAVASAN_ALC_LOCK_PACKET_VERSION` — `1` veya `2`

**Yarışma**
- SIHA HTTP Jetson'da yok (yer istasyonu RF `0x01`/`0x31` ile sunucuya basar)
- `SAVASAN_COMPETITION_TAKIM_NO` — isteğe bağlı takım no
- Rakip/HSS/sınır: RF dosyaları (`/tmp/savasan_rival_pool.env`, `hss_rf.env`, `boundary_rf.env`)

**Tracker**
- `SAVASAN_TRACKER_PROFILE=default|calm|aggressive|auto` — `stable` profil adı değil (validate_env reddeder)
- `SAVASAN_TRACKER_AUTO_RESTART=0` — yarışmada önerilir (nvtracker açık, restart döngüsü kapalı)
- Preset: `source scripts/load_tracker_preset.sh stable` — ayrıntı: `presets/README_PRESETS.md`
- OSD fix notları: `CHANGELOG.md`, `TROUBLESHOOTING.md`

---

## Operasyon dokümanları (öncelik sırası)

1. [OPERATOR_CHEAT_SHEET.md](OPERATOR_CHEAT_SHEET.md) — sahada hızlı komutlar
2. [DEPLOYMENT.md](DEPLOYMENT.md) — systemd kurulum
3. [BUILD.md](BUILD.md) — derleme
4. [PERFORMANCE_BASELINE.md](PERFORMANCE_BASELINE.md) — benchmark ve kabul kriterleri
5. [TROUBLESHOOTING.md](TROUBLESHOOTING.md)

---

## Arşiv / referans (yarışma koşusunda okunması şart değil)

Planlama ve geçmiş raporlar: `00_DEEPSTREAM_PLAN.md`, `PHASE6_PLUS_BACKLOG.md`, `TRACKING_ANALYSIS_REPORT.md`, optical flow rehberleri, `97_Documents/`, yerel `98_Reference_Repos/`.

**Güdüm saha kılavuzu:** `docs/GUIDANCE_USER_MANUAL.md`  
**APF / HSS kaçış:** `docs/APF_EVASION.md`
