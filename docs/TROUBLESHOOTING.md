# Troubleshooting Guide

Bu dokuman, en yaygin production sorunlari ve hizli cozumlerini listeler.

## Service Baslamiyor

Kontrol:

```bash
sudo systemctl status savasan-airlock
journalctl -u savasan-airlock -n 200 --no-pager
```

Yaygin nedenler:
- `/etc/savasan/savasan.env` gecersiz (`validate_env` FAIL — servis pipeline'a ulasmadan duser)
- `SAVASAN_TRACKER_PROFILE=stable` gibi gecersiz profil adi (izinli: `default` `calm` `aggressive` `auto`)
- binary path yanlis veya binary yok
- kamera/serial cihaz izni yok
- `StartLimitBurst` asildi (`reset-failed` gerekir)

Cozum:
```bash
/usr/local/bin/savasan-validate-env.sh /etc/savasan/savasan.env
sudo systemctl reset-failed savasan-airlock
sudo systemctl start savasan-airlock
sudo /usr/local/bin/savasan-recover.sh   # env geri alma gerekiyorsa
```

## OSD Kutu Izi / Ekranda Yigilan Kirmizi Bbox

Semptom: `uav 0` etiketli kutular yatay birikir; video siyah kalabilir.

Neden: display meta pool'dan donen `num_rects` sifirlanmiyordu (2026-06-10 oncesi binary).

Cozum: Guncel `build/savasan_iha` kullanin (`ResetDisplayMetaForAcquire` fix). Restart:

```bash
sudo systemctl restart savasan-airlock
```

Istege bagli env:
- `SAVASAN_DISPLAY_SYNC=0` — dusuk latency, iz riski artar (varsayilan sync acik)
- `SAVASAN_OSD_SHOW_ALL_DETECTIONS=1` — tum bbox'lari goster (debug)

## SEGV (status=11) — Servis Aninda Coker

2026-06-10: `nvds_remove_obj_meta` / `nvds_clear_display_meta_list` ile agresif meta silme SEGV uretti; kaldirildi.

Guncel binary ile duzelmeli. Hala SEGV varsa:

```bash
coredumpctl -1 info /home/nvidia/Savasan_IHA_Workspace/02_Ana_Sistem_CPP/build-app/savasan_iha
```

## Tracker Profil vs Preset

| Ayar | Anlam |
|------|--------|
| `SAVASAN_TRACKER_PROFILE=default` | NvDCF acik, `tracker_config.yml` |
| `SAVASAN_TRACKER_AUTO_RESTART=0` | Profil degistirme dongusu kapali (nvtracker acik kalir) |
| `SAVASAN_TRACKER_DISABLE=1` | Nvtracker tamamen kapali (yarismada onerilmez) |
| `source scripts/load_tracker_preset.sh stable` | Preset env yukler; profil adi `stable` degildir |

## Restart Loop

Semptom:
- Servis art arda dusup kalkiyor.

Kontrol:
```bash
journalctl -u savasan-airlock -f
```

Cozum:
1. Son env degisikligini geri al (`savasan-recover.sh`).
2. `SAVASAN_PHASE`, `SAVASAN_CAMERA`, `SAVASAN_V4L2_DEVICE`, `SAVASAN_ALC_DEVICE` alanlarini dogrula.
3. Donanim baglantilarini kontrol et.

## Loglar Buyuyor / Disk Doluyor

Kontrol:
```bash
du -sh /var/log/savasan
sudo logrotate -d /etc/logrotate.d/savasan-airlock
```

Cozum:
```bash
sudo logrotate -f /etc/logrotate.d/savasan-airlock
```

## Phase5 Lock/Guidance Beklenmedik Davranis

Kontrol:
- `CRITICAL`/`ERROR` loglari tara
- `SAVASAN_SEND_ONLY_ON_LOCK`, `SAVASAN_SETPOINT_TX_ENABLE` degiskenlerini kontrol et

Komut:
```bash
grep -E "CRITICAL|ERROR|WARN" /var/log/savasan/service.err.log | tail -n 100
```

## Healthcheck Fail

Manuel kos:
```bash
sudo /usr/local/bin/savasan-healthcheck.sh
echo $?
```

Fail kodlari:
- `2`: env dosyasi yok
- `3`: env validasyon hatasi
- `4`: restart basarisiz

## Crash Dump Gerekli

Son crash dump'i topla:

```bash
sudo /usr/local/bin/savasan-coredump-collect.sh
ls -lah /var/crash/savasan
```

Doğrudan coredumpctl inceleme:

```bash
sudo coredumpctl -1 info /home/nvidia/Savasan_IHA_Workspace/02_Ana_Sistem_CPP/build-app/savasan_iha
```

## Oneri

Deploy degisikliklerinde asagidaki sirayi uygula:
1. Backup
2. Validation
3. Restart
4. Healthcheck
5. 60s log izleme
