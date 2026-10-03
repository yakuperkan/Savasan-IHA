# Tracker Auto-Restart Presets

Bu klasor, `SAVASAN_TRACKER_PROFILE=auto` icin hazir ayar setleri icerir.

## Presetler

- `stable.env` — dengeli ve guvenli baslangic
- `aggressive_mission.env` — hizli cevap, rekabet odakli
- `noisy_tracking.env` — gurultulu sahnelerde restart toleransi yuksek

## Hizli Kullanım

Repo kokunden:

```bash
cd /home/nvidia/Savasan_IHA_Workspace
source scripts/load_tracker_preset.sh stable
SAVASAN_DISPLAY= ./02_Ana_Sistem_CPP/build/savasan_iha phase5 usb hybrid
```

Alternatif olarak tek komut yukleyici:

```bash
source "./scripts/load_tracker_preset.sh" stable
```

Aktif ayarlari gormek icin:

```bash
./scripts/show_tracker_preset.sh
```

## Notlar

- Bu dosyalar shell icin tasarlanmistir (`source` ile yuklenir).
- Presetler `SAVASAN_TRACKER_PROFILE=auto` ve `SAVASAN_TRACKER_AUTO_RESTART=1` ayarlarini da set eder.
- Parametreleri presetten sonra manuel override edebilirsiniz.
- `show_tracker_preset.sh` env set edilmemisse default degerleri gosterir ve bilinen presetlerle eslesmeyi raporlar.

## Parametreler Ne Is Yapar

- `SAVASAN_AUTO_AGGRESSIVE_SPEED_MPS`: hiz bu esigi gecerse aggressive onerisi guclenir.
- `SAVASAN_AUTO_CALM_SPEED_MPS`: hiz bu esigin altina inerse calm onerisi guclenir.
- `SAVASAN_AUTO_AGGRESSIVE_JITTER`: lock jitter yuksekse aggressive'e gecis tetiklenir.
- `SAVASAN_AUTO_CALM_JITTER`: jitter dusukse calm'e donus tetiklenir.
- `SAVASAN_AUTO_RESTART_MIN_DWELL_SEC`: yeni profile gectikten sonra tekrar degisime izin verilmeden beklenecek minimum sure.
- `SAVASAN_AUTO_RESTART_COOLDOWN_SEC`: restartlar arasi minimum bekleme; restart firtinasini engeller.
- `SAVASAN_AUTO_PRECHECK_MAX_JITTER` ve `SAVASAN_AUTO_PRECHECK_MAX_COMM_FAILS`: restart oncesi saglik kapisi.
- `SAVASAN_AUTO_POSTCHECK_TIMEOUT_SEC`: restart sonrasi saglik dogrulama penceresi.
- `SAVASAN_AUTO_MAX_RESTARTS`: toplam restart limiti.
- `SAVASAN_AUTO_MAX_CONSECUTIVE_FAILS` ve `SAVASAN_AUTO_MAX_FAILS_IN_WINDOW`: tekrarli hata durumunda auto-restart'i kapatan fail-safe.
