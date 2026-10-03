# DEPLOYMENT

Bu dokuman, sistemin `systemd` servisi olarak kurulumu ve calistirilmasini aciklar.

## 1) Binary hazirla
Once binary derlenmis olmali:
```bash
cd /home/nvidia/Savasan_IHA_Workspace/02_Ana_Sistem_CPP/build
cmake --build . -j4
```

## 2) Service kurulumu
`install.sh` scripti service ve env dosyasini ilgili yerlere kopyalar. **Varsayilan: otomatik baslatma yok** — `savasan-airlock` ve timer’lar `enable`/`start` edilmez; acilista arka planda calismaz.

Eski kurulumda hala acilista basliyorsa bir kez:
```bash
sudo bash /home/nvidia/Savasan_IHA_Workspace/scripts/systemd_disable_autostart.sh
```

```bash
cd /home/nvidia/Savasan_IHA_Workspace/02_Ana_Sistem_CPP/config/systemd
sudo bash install.sh
```

Scriptin kullandigi dosyalar:
- `savasan-airlock.service` -> `/etc/systemd/system/savasan-airlock.service`
- `savasan.env` -> `/etc/savasan/savasan.env`
- `savasan-airlock-healthcheck.service` -> `/etc/systemd/system/savasan-airlock-healthcheck.service`
- `savasan-airlock-healthcheck.timer` -> `/etc/systemd/system/savasan-airlock-healthcheck.timer`
- `savasan-airlock-coredump-collect.service` -> `/etc/systemd/system/savasan-airlock-coredump-collect.service`
- `savasan-airlock-coredump-collect.timer` -> `/etc/systemd/system/savasan-airlock-coredump-collect.timer`
- `savasan-airlock.logrotate` -> `/etc/logrotate.d/savasan-airlock`
- `validate_env.sh` -> `/usr/local/bin/savasan-validate-env.sh`
- `backup_env.sh` -> `/usr/local/bin/savasan-backup-env.sh`
- `rollback_env.sh` -> `/usr/local/bin/savasan-rollback-env.sh`
- `healthcheck.sh` -> `/usr/local/bin/savasan-healthcheck.sh`
- `recover.sh` -> `/usr/local/bin/savasan-recover.sh`
- `coredump_collect.sh` -> `/usr/local/bin/savasan-coredump-collect.sh`
- `maintenance_mode.sh` -> `/usr/local/bin/savasan-maintenance-mode.sh`
- `flight_mode.sh` -> `/usr/local/bin/savasan-flight-mode.sh`

## 3) Servis kontrol (elle baslatma)
```bash
sudo systemctl start savasan-airlock
sudo systemctl status savasan-airlock
journalctl -u savasan-airlock -f
sudo systemctl status savasan-airlock-healthcheck.timer
sudo systemctl status savasan-airlock-coredump-collect.timer
```

### Ground / Ucus-oncesi notu (onemli)

`savasan-airlock-healthcheck.timer` aktifken, `savasan-airlock` elle durdurulsa bile bir sonraki healthcheck dongusunda tekrar baslatilir.

Havada degilsen ve servisin kapali kalmasini istiyorsan:
```bash
sudo systemctl stop savasan-airlock-healthcheck.timer
sudo systemctl stop savasan-airlock
```

Yarismaya veya teste cikarken tekrar ac:
```bash
sudo systemctl enable --now savasan-airlock-healthcheck.timer
sudo systemctl start savasan-airlock
```

### Bakim/Ucus mod scriptleri (onerilen)

Bakim modu:
```bash
sudo /usr/local/bin/savasan-maintenance-mode.sh
```

Ucus/Test modu:
```bash
sudo /usr/local/bin/savasan-flight-mode.sh
```

Not: bakim modunda `/etc/savasan/maintenance.flag` olusturulur; healthcheck bu flag varken restart denemesi yapmaz.

## 4) Konfigurasyon
Calisma parametrelerini bu dosyadan yonet:
- `/etc/savasan/savasan.env`

Ornek alanlar:
- `SAVASAN_PHASE=phase3` veya `phase4`
- `SAVASAN_LOCK_MODE=hybrid` veya `baseline` (yalnizca `phase4`; systemd'de argv'de mod yoksa kullanilir)
- `SAVASAN_CAMERA=usb`
- `SAVASAN_DISPLAY=display` — Jetson HDMI’de onizleme (`nv3dsink`); bos veya yok = fakesink. Systemd’de bu satir + `DISPLAY=:0` / `XAUTHORITY` gerekir (masaustu oturumu acik olmali).
- `SAVASAN_RUN_SECONDS=0`
- USB kamera: `SAVASAN_USB_EXPOSURE_ABSOLUTE`, `SAVASAN_USB_GAIN`, `SAVASAN_USB_WB_TEMPERATURE`

### Yarışma HUD entegrasyon notu

Lock policy esikleri saha oncesi tune edilmelidir (`SAVASAN_LOCK_*`).

Hizli test (20 sn, USB):

```bash
SAVASAN_RUN_SECONDS=20 \
/home/nvidia/Savasan_IHA_Workspace/02_Ana_Sistem_CPP/build-app/savasan_iha phase5 usb display
```

### Tracker auto-profile preset kullanimi

Hazir preset dosyalari:
- `02_Ana_Sistem_CPP/config/deepstream/presets/stable.env`
- `02_Ana_Sistem_CPP/config/deepstream/presets/aggressive_mission.env`
- `02_Ana_Sistem_CPP/config/deepstream/presets/noisy_tracking.env`

Yukleyici script (onerilen):
```bash
cd /home/nvidia/Savasan_IHA_Workspace
source ./scripts/load_tracker_preset.sh stable
./scripts/show_tracker_preset.sh
```

Dogrudan preset source etmek de mumkundur:
```bash
source "/home/nvidia/Savasan_IHA_Workspace/02_Ana_Sistem_CPP/config/deepstream/presets/stable.env"
```

Bu presetler asagidaki auto-profile davranisini ayarlar:
- `SAVASAN_TRACKER_PROFILE=auto`
- `SAVASAN_TRACKER_AUTO_RESTART=1`
- speed/jitter threshold, restart cooldown/dwell, pre/post health-check limitleri

Calistirma ornegi:
```bash
cd /home/nvidia/Savasan_IHA_Workspace/02_Ana_Sistem_CPP
./build/savasan_iha phase5 csi display
```

**Faz 5 durum tahmini (telemetri + goruntu):** `VehicleStateEstimator` goruntu hizini kilit koordinatlarinin zamana gore degisiminden turetir; birim telemetri ile ayni alanda birlestirmek icin olcek gerekir.
- `SAVASAN_VISION_NORM_RATE_TO_MPS` — varsayilan `1`; gercek m/s ile hizalamak icin sahada kalibre edin. `0` verilirse goruntu hizi katkisi kapatilir (yalniz telemetri).
- `SAVASAN_STATE_TELEMETRY_WEIGHT` / `SAVASAN_STATE_VISION_WEIGHT` — agirliklar (0–1); ikisi de gecerliyken karisim, yalniz biri gecerliyken o kaynak tam kullanilir (`phase5_runtime_options.cpp`).
- `SAVASAN_STATE_VELOCITY_ALPHA` ve `SAVASAN_STATE_INNOVATION_GATE_MPS` — hiz kestiriminde alpha filtresi ve innovation clip; ani ziplamalari sinirlamak icin kullanilir.
- `SAVASAN_STATE_YAW_ALPHA` ve `SAVASAN_STATE_YAW_GATE_DPS` — yaw-rate filtresi ve innovation gate.
- `SAVASAN_ALC_LOCK_PACKET_VERSION` — lock koordinat TX protokolu: `1` (legacy, x/y + XOR) veya `2` (v2, x/y + `track_id` + `sequence` + `timestamp_ms` + CRC16). Geriye uyumluluk icin varsayilan `1`; otopilot tarafi v2 destekliyorsa `2` secin.
- `SAVASAN_ALC_NOLOCK_CHECKSUM_MODE` — `xor` (varsayilan) veya `legacy_ff`; eski firmware no-lock checksum icin `0xFF` bekliyorsa `legacy_ff` kullanin.

**Mission modu (AIR_LOCK, `phase5`, LOCK workspace):** `SAVASAN_MISSION_MODE=air_lock`, isteğe bağlı `SAVASAN_MISSION_MODE_FILE` + `SAVASAN_MISSION_MODE_POLL_MS` ile dosyadan aynı modun teyidi (satır: `air_lock` veya `lock`). Sunucu zamanı için `SAVASAN_SERVER_TIME_OFFSET_MS` ve isteğe bağlı `SAVASAN_SERVER_TIME_OFFSET_FILE` / `SAVASAN_SERVER_TIME_OFFSET_POLL_MS`. SIHA HTTP Jetson'da yok; telemetri ve kilitlenme puanı yer istasyonu RF'den alıp sunucuya basar.

### OSD sonrasi RTP / UDP yayin (bbox + skor dahil)
**Acma:** `SAVASAN_UDP_ENABLE=1` ve birlikte `SAVASAN_UDP_HOST` + `SAVASAN_UDP_PORT`. `ENABLE` yok veya `0` iken UDP **hic gonderilmez** (HOST yazili kalsa bile).

- `SAVASAN_UDP_ENABLE` — `1` = yayin acik; `0` veya bos = kapali (varsayilan kapali)
- `SAVASAN_UDP_HOST` — ornek: `192.168.1.20` (yer istasyonu)
- `SAVASAN_UDP_PORT` — ornek: `5000`
- `SAVASAN_UDP_CODEC` — `h264` (varsayilan) veya `h265`
- `SAVASAN_UDP_BITRATE` — bps, varsayilan `6000000`

`SAVASAN_RECORD_FILE` ile birlikte kullanilabilir (tee: ekran/fake + dosya + UDP).

**Yer tarafi ornek (H.264, yazilim decode):**
```bash
gst-launch-1.0 udpsrc port=5000 caps="application/x-rtp,media=(string)video,clock-rate=(int)90000,encoding-name=(string)H264" ! \
  rtpjitterbuffer latency=100 ! rtph264depay ! h264parse ! avdec_h264 ! autovideosink sync=false
```

**Yer tarafi ornek (H.265 / HEVC, `SAVASAN_UDP_CODEC=h265`):**
```bash
gst-launch-1.0 udpsrc port=5000 address=0.0.0.0 \
  caps="application/x-rtp,media=(string)video,clock-rate=(int)90000,encoding-name=(string)H265" ! \
  rtpjitterbuffer latency=120 ! rtph265depay ! h265parse ! avdec_h265 ! autovideosink sync=false
```
(Yer PC IP’si Jetson ile ayni subnet’te olmali; gerekirse `sudo sysctl -w net.core.rmem_max=...` ile UDP tampon artirin.)

Degisiklik sonrasi:
```bash
sudo systemctl restart savasan-airlock
```

Guvenli degisiklik akisi:
```bash
sudo /usr/local/bin/savasan-backup-env.sh
sudo /usr/local/bin/savasan-validate-env.sh /etc/savasan/savasan.env
sudo systemctl restart savasan-airlock
sudo /usr/local/bin/savasan-healthcheck.sh
```

Rollback:
```bash
sudo /usr/local/bin/savasan-recover.sh
```

Crash dump toplama (manuel):
```bash
sudo /usr/local/bin/savasan-coredump-collect.sh
ls -lah /var/crash/savasan
```

## 5) Onemli notlar
- Servis `Type=notify` ve `WatchdogSec=30` ile calisir.
- Baslatma oncesi env backup + env validasyon adimlari `ExecStartPre` ile calisir.
- Loglar `/var/log/savasan/*.log` altina da yazilir ve `logrotate` ile dondurulur.
- Crash dump artefactlari `/var/crash/savasan/` altina toplanir (5 dakikada bir timer).
- HDMI/X11 gereksinimi olan sink kullaniminda `DISPLAY=:0` ortami dogru olmali.
- **Ekran gelmiyorsa:** GDM ile oturum acilmis olmali; `XAUTHORITY=/run/user/1000/gdm/Xauthority` bazi kurulumlarda farkli olabilir. Grafik oturumundayken `echo $XAUTHORITY` ile yolu alip `savasan-airlock.service` icindeki `Environment=XAUTHORITY=...` satirini guncelleyin. Wayland-only oturumda `nv3dsink` sorun cikarsa oturumu **Ubuntu on Xorg** ile acmayi deneyin.
