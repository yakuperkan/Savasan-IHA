# Operations Runbook

Bu dokuman production operasyonu icin minimum SOP ve acil durum adimlarini verir.

Hizli tek sayfa ozet:
- `docs/OPERATOR_CHEAT_SHEET.md`

## 1) Hizli Durum Kontrolu

```bash
sudo systemctl status savasan-airlock
sudo systemctl status savasan-airlock-healthcheck.timer
sudo systemctl status savasan-airlock-coredump-collect.timer
journalctl -u savasan-airlock -n 100 --no-pager
tail -n 100 /var/log/savasan/service.log
tail -n 100 /var/log/savasan/service.err.log
```

## 2) Health Check

```bash
sudo /usr/local/bin/savasan-healthcheck.sh
```

- Servis kapaliysa otomatik restart dener.
- Env dosyasi gecersizse restart etmez ve hata doner.

## 3) Konfigurasyon Degistirme SOP

1. Degisiklik oncesi otomatik yedek:
   ```bash
   sudo /usr/local/bin/savasan-backup-env.sh
   ```
2. `/etc/savasan/savasan.env` dosyasini guncelle.
3. Validasyon:
   ```bash
   sudo /usr/local/bin/savasan-validate-env.sh /etc/savasan/savasan.env
   ```
4. Servisi yeniden baslat:
   ```bash
   sudo systemctl restart savasan-airlock
   ```
5. 60 saniye izleme:
   ```bash
   journalctl -u savasan-airlock -f
   ```

## 4) Acil Rollback

```bash
sudo /usr/local/bin/savasan-recover.sh
```

Manuel rollback:

```bash
sudo /usr/local/bin/savasan-rollback-env.sh /etc/savasan/savasan.env /var/backups/savasan latest
sudo systemctl restart savasan-airlock
```

## 5) Log Retention / Rotation

- Konfig: `/etc/logrotate.d/savasan-airlock`
- Log yolu: `/var/log/savasan/*.log`
- Politika: gunluk rotate, 14 dosya, gzip, copytruncate.

Manuel test:

```bash
sudo logrotate -f /etc/logrotate.d/savasan-airlock
```

## 6) Escalation

Asagidaki durumda acil eskalasyon yap:

- Ardisik restart loop (`StartLimitBurst` tetiklenirse)
- Healthcheck fail kodu 3+ (env validation veya restart basarisizligi)
- `CRITICAL` / `FATAL` loglarinin tekrarli gorulmesi

## 7) Crash Dump Collection

Otomatik:
- `savasan-airlock-coredump-collect.timer` her 5 dakikada coredump tarar.

Manuel:

```bash
sudo /usr/local/bin/savasan-coredump-collect.sh
ls -lah /var/crash/savasan
```

Artifact icerigi:
- `*.core.gz` (dump payload)
- `*.txt` (coredump metadata/info)
