# Faz 3/4 Compliance ve Optimizasyon Raporu (2026-04-02)

## Kisa Ozet
- Sistem CSI tarafinda stabil hale getirildi.
- USB parse/caps uyumsuzlugu giderildi.
- Latency profili `interval=2` ve `640x480@60` ile iyilesti.
- Sartnameye yonelik 3 kritik adim uygulandi:
  1) Kirmizi bbox (3px)
  2) 4 saniye time-based lock validasyonu (state tarafi)
  3) Debug/probe/log release modda kapatildi
- Faz 4 (opsiyonel) hibrit kilit modu eklendi.
- Opsiyonel H.264 MP4 kayit dali ve OSD sunucu saati overlay eklendi.

---

## Uygulanan Degisiklikler

### 1) Kamera ve ingest/pipeline
- `02_Ana_Sistem_CPP/src/pipeline/ingest_config.hpp`
  - Varsayilan profil: `640x480 @ 60 FPS`
- `02_Ana_Sistem_CPP/src/camera/csi_source.cpp`
  - FPS sabiti kaldirildi, `cfg.fps` kullanimi
- `02_Ana_Sistem_CPP/src/camera/usb_source.cpp`
  - Bare caps yerine `capsfilter caps="video/x-raw(memory:NVMM),..."`
- `02_Ana_Sistem_CPP/src/main.cpp`
  - USB override `640x480` ve `60 FPS`

### 2) Latency odakli config/pipeline
- `02_Ana_Sistem_CPP/src/deepstream/ds_app.cpp`
  - Queue: `max-size-buffers=2 leaky=2`
- `02_Ana_Sistem_CPP/config/deepstream/config_infer_primary.txt`
  - `interval=2` (stabil/latency dengesi icin)

### 3) Faz 4 opsiyonel hibrit kilit
- `02_Ana_Sistem_CPP/src/tracking/track_selector.hpp`
  - `LockMode { kBaseline, kHybrid }`
- `02_Ana_Sistem_CPP/src/tracking/track_selector.cpp`
  - Hibrit secim: hysteresis + merkez yakinlik skoru
  - Instance bazli log state (`last_logged_id_`)
- `02_Ana_Sistem_CPP/src/main.cpp`
  - Yeni kullanim: `phase4 [hybrid|baseline]`

### 4) Sartname compliance adimlari
- `02_Ana_Sistem_CPP/src/tracking/track_selector.cpp`
  - Bbox: kirmizi (`FF0000`) + `3px` border
  - `has_color_info=1`, `has_bg_color=0`, `border_color.alpha=0.0` ile DeepStream OSD uyumu
- `02_Ana_Sistem_CPP/src/tracking/lock_state.hpp`
  - `valid_lock`, `lock_start_time`, `kRequiredLockDuration=4s`
- `02_Ana_Sistem_CPP/src/tracking/track_selector.cpp`
  - Time-based lock validasyon mantigi eklendi
- `02_Ana_Sistem_CPP/CMakeLists.txt`
  - `SAVASAN_DEBUG` CMake option eklendi (varsayilan `OFF`)
- `02_Ana_Sistem_CPP/src/main.cpp`
  - Debug log/probe bloklari compile-time kosullu hale getirildi

### 5) Video kayit + saat overlay
- `02_Ana_Sistem_CPP/src/deepstream/ds_app.cpp`
  - `SAVASAN_RECORD_FILE` set edilirse tee ile H.264 MP4 kayit dali
- `02_Ana_Sistem_CPP/src/main.cpp`
  - OSD sink probe uzerinden sunucu saati overlay (`SAVASAN_OVERLAY_CLOCK`)

### 6) Skills guncellemesi
- `.cursor/skills/savasan-jetson-deepstream/SKILL.md`
  - phase4 opsiyonu, 60 FPS profil, kayit/saat env degiskenleri,
    debug derleme davranisi ve kullanim komutlari eklendi.

---

## Test Sonuclari (Ozet)

## Derleme
- `cmake --build . -j4` adimlari basarili.

## Runtime
- `./scripts/run_phase3.sh csi 10` stabil acilis: PASS
- `./scripts/run_phase3.sh usb 5` (USB branch duzeltme sonrasi): PASS
- `SAVASAN_RECORD_FILE=/tmp/savasan_phase3_record.mp4 ./scripts/run_phase3.sh csi 6`: PASS
  - Kayit dosyasi olustu: `/tmp/savasan_phase3_record.mp4`

## Latency trendi
- `interval=0`: yuksek yukte 30-40ms bandina cikma goruldu.
- `interval=2`: 60 FPS profilinde daha iyi denge (yaklasik 16-20ms bant gozlemi).
- Release modda (`SAVASAN_DEBUG=OFF`) perf probe kapali oldugu icin
  `[Perf]` satiri cikmaz; bu beklenen davranistir.

---

## Kalan Isler / Notlar
1. 4 saniye lock validasyonunun yarismadaki resmi telemetri ciktisina baglanmasi
2. ALC_link formatinda kilit bitis zamani + `cx,cy` gonderimi
3. Gerekirse release/perf benchmark icin ek metrik endpoint'i

---

## Komutlar (Hazir)

### Standart Faz 3
```bash
cd /home/nvidia/Savasan_IHA_Workspace
./scripts/run_phase3.sh csi 120
```

### Faz 4 hibrit / baseline A-B
```bash
cd /home/nvidia/Savasan_IHA_Workspace
SAVASAN_RUN_SECONDS=60 ./02_Ana_Sistem_CPP/build/savasan_iha phase4 csi hybrid display
SAVASAN_RUN_SECONDS=60 ./02_Ana_Sistem_CPP/build/savasan_iha phase4 csi baseline display
```

### Kayit + saat overlay
```bash
cd /home/nvidia/Savasan_IHA_Workspace
SAVASAN_RECORD_FILE=/tmp/savasan_phase3_record.mp4 ./scripts/run_phase3.sh csi 60
```

### Debug/probe acik derleme
```bash
cd /home/nvidia/Savasan_IHA_Workspace/02_Ana_Sistem_CPP/build
cmake -DSAVASAN_DEBUG=ON ..
cmake --build . -j4
```

### Overlay kapatmak icin
```bash
cd /home/nvidia/Savasan_IHA_Workspace
SAVASAN_OVERLAY_CLOCK=0 ./scripts/run_phase3.sh csi 60
```

