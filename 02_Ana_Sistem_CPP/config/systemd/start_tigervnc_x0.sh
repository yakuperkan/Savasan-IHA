#!/usr/bin/env bash
# TigerVNC x0vncserver systemd baslatici — cift oturum / restart dongusunu onler.
set -euo pipefail

export DISPLAY="${DISPLAY:-:0}"
for auth in /run/user/1000/gdm/Xauthority /home/nvidia/.Xauthority; do
  if [[ -f "${auth}" ]]; then
    export XAUTHORITY="${auth}"
    break
  fi
done

PASSWD="${SAVASAN_VNC_PASSWD_FILE:-/home/nvidia/.vnc/passwd}"
PORT="${SAVASAN_VNC_PORT:-5900}"

if [[ ! -f "${PASSWD}" ]]; then
  echo "[tigervnc-x0] HATA: sifre dosyasi yok: ${PASSWD}" >&2
  exit 1
fi

if x0vncserver -display :0 -list 2>/dev/null | grep -qE "[[:space:]]${PORT}[[:space:]]"; then
  echo "[tigervnc-x0] Mevcut :0 VNC kapatiliyor (port ${PORT})..."
  x0vncserver -display :0 -kill -rfbport "${PORT}" 2>/dev/null || true
  sleep 1
fi

pkill -u "$(id -un)" -f 'x0vncserver -display :0' 2>/dev/null || true
sleep 1

echo "[tigervnc-x0] Baslatiliyor — port ${PORT} XAUTHORITY=${XAUTHORITY:-<yok>}"
exec x0vncserver -display :0 -rfbport "${PORT}" -PasswordFile "${PASSWD}" \
  -SecurityTypes VncAuth -AlwaysShared -localhost=no -fg
