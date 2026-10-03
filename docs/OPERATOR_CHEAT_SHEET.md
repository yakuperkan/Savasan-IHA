# Operator Cheat Sheet (1-Page)

Sahada en sık kullanilan komutlarin hizli ozeti.

## 1) Install / Update

```bash
cd /home/nvidia/Savasan_IHA_Workspace/02_Ana_Sistem_CPP/config/systemd
sudo bash install.sh
```

## 2) Core Service Control

```bash
sudo systemctl start savasan-airlock
sudo systemctl stop savasan-airlock
sudo systemctl restart savasan-airlock
sudo systemctl status savasan-airlock
```

## 3) Health & Timers

```bash
sudo /usr/local/bin/savasan-healthcheck.sh
sudo systemctl status savasan-airlock-healthcheck.timer
sudo systemctl status savasan-airlock-coredump-collect.timer
```

### Ground Mode (servis kapali kalsin)
```bash
sudo /usr/local/bin/savasan-maintenance-mode.sh
```

### Test/Ucus Mode (otomatik koruma tekrar ac)
```bash
sudo /usr/local/bin/savasan-flight-mode.sh
```

## 4) Logs

```bash
journalctl -u savasan-airlock -f
tail -n 100 /var/log/savasan/service.log
tail -n 100 /var/log/savasan/service.err.log
grep -E "FATAL|CRITICAL|ERROR|WARN" /var/log/savasan/service.err.log | tail -n 100
```

## 5) Safe Config Change (SOP)

```bash
sudo /usr/local/bin/savasan-backup-env.sh
sudo /usr/local/bin/savasan-validate-env.sh /etc/savasan/savasan.env
sudo systemctl restart savasan-airlock
sudo /usr/local/bin/savasan-healthcheck.sh
```

## 6) Recovery / Rollback

```bash
# Full recover from latest backup
sudo /usr/local/bin/savasan-recover.sh

# Manual rollback
sudo /usr/local/bin/savasan-rollback-env.sh /etc/savasan/savasan.env /var/backups/savasan latest
sudo systemctl restart savasan-airlock
```

## 7) Crash Dump Collection

```bash
sudo /usr/local/bin/savasan-coredump-collect.sh
ls -lah /var/crash/savasan
sudo coredumpctl -1 info /home/nvidia/Savasan_IHA_Workspace/02_Ana_Sistem_CPP/build-app/savasan_iha
```

## 8) Log Rotation Check

```bash
sudo logrotate -d /etc/logrotate.d/savasan-airlock
sudo logrotate -f /etc/logrotate.d/savasan-airlock
```

## 9) Test Quick Check (After Critical Change)

```bash
cd /home/nvidia/Savasan_IHA_Workspace/02_Ana_Sistem_CPP
ctest --test-dir build --output-on-failure -L unit
ctest --test-dir build --output-on-failure -L integration
```

## 10) Tracker (yarışma varsayılanı)

`/etc/savasan/savasan.env`:

```bash
SAVASAN_TRACKER_PROFILE=default      # stable DEGIL — validate_env reddeder
SAVASAN_TRACKER_AUTO_RESTART=0       # nvtracker acik; profil restart dongusu kapali
SAVASAN_TRACKER_DISABLE=0
```

Preset (shell env, profil adi degil):

```bash
cd /home/nvidia/Savasan_IHA_Workspace
source ./scripts/load_tracker_preset.sh stable
./scripts/show_tracker_preset.sh
```

Servis validate FAIL:

```bash
/usr/local/bin/savasan-validate-env.sh /etc/savasan/savasan.env
sudo systemctl reset-failed savasan-airlock
sudo systemctl start savasan-airlock
```

## 11) Phase5 mission modu (AIR_LOCK — LOCK workspace)

- `/etc/savasan/savasan.env` içinde: `SAVASAN_MISSION_MODE=air_lock`; isteğe bağlı `SAVASAN_MISSION_MODE_FILE` ile dosyadan onay satırı (`SAVASAN_MISSION_MODE_POLL_MS`; satır yalnızca `air_lock` / `lock`).
- Jetson’da duman: `bash /home/nvidia/Savasan_IHA_Workspace/scripts/phase5_mission_smoke.sh unit` (ctest), ardından `air_lock` (kamera + isteğe bağlı `SAVASAN_ALC_DISABLE=0` ile gerçek seri).
- Skill: `.cursor/skills/phase5-autopilot-integration-hardening/SKILL.md`

## 12) If Something Goes Wrong (Priority Order)

1. `sudo /usr/local/bin/savasan-healthcheck.sh`
2. `sudo systemctl status savasan-airlock`
3. `tail -n 100 /var/log/savasan/service.err.log`
4. `sudo /usr/local/bin/savasan-recover.sh`
5. `sudo /usr/local/bin/savasan-coredump-collect.sh`
