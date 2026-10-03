#!/usr/bin/env bash
# Eski install.sh ile acilista otomatik baslatilmissa: servisi ve timer'lari durdur + disable.
# Kullanim: sudo bash /home/nvidia/Savasan_IHA_Workspace/scripts/systemd_disable_autostart.sh
set -euo pipefail

if [[ "${EUID}" -ne 0 ]]; then
  echo "sudo ile calistirin."
  exit 1
fi

for unit in \
  savasan-airlock.service \
  savasan-alpagulink.service \
  savasan-airlock-gcs.service \
  savasan-airlock-healthcheck.timer \
  savasan-airlock-coredump-collect.timer \
  savasan-kamikaze.service \
  savasan-kamikaze-gcs.service \
  savasan_kamikaze.service \
  savasan_lock.service \
  savasan_qr_prototype.service \
  savasan-iha.service \
  savasan-healthcheck.timer \
  savasan-coredump-collect.timer; do
  systemctl stop "${unit}" 2>/dev/null || true
  systemctl disable "${unit}" 2>/dev/null || true
done

systemctl daemon-reload
echo "[ok] savasan servis/timer'lar durduruldu; acilista otomatik baslama kapali."
echo "     Terminalden: bash /home/nvidia/Savasan_IHA_Workspace/scripts/run_phase5_terminal.sh usb 60"
echo "     Systemd elle: sudo systemctl start savasan-airlock"
echo "     Ucus modu (timer+servis): sudo /usr/local/bin/savasan-flight-mode.sh"
