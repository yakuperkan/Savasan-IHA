#!/usr/bin/env bash
# VNC servisini systemd olmadan elle baslat (saha debug).
# Ctrl+C ile durur.
set -euo pipefail

export DISPLAY="${DISPLAY:-:0}"
export XAUTHORITY="${XAUTHORITY:-/run/user/1000/gdm/Xauthority}"
PASSWD="${HOME}/.vnc/passwd"
PORT="${SAVASAN_VNC_PORT:-5900}"

if [[ ! -f "${PASSWD}" ]]; then
  echo "HATA: ${PASSWD} yok — once: sudo bash scripts/setup_vnc.sh" >&2
  exit 1
fi

echo "[vnc] Elle baslatiliyor — port ${PORT} (Ctrl+C durdur)"
echo "[vnc] Baglanti: $(hostname).local:${PORT} veya $(hostname -I | awk '{print $1}'):${PORT}"
exec x0vncserver -display :0 -rfbport "${PORT}" -PasswordFile "${PASSWD}" \
  -SecurityTypes VncAuth -AlwaysShared -localhost=no -fg -verbose
