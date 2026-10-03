#!/usr/bin/env bash
# Jetson'a baglanirken guncel adresleri goster (IP degisse de hostname sabit kalir).
set -euo pipefail

HOST="$(hostname)"
PORT_VNC="${SAVASAN_VNC_PORT:-5900}"
PORT_SSH=22

echo "=== Jetson baglanti bilgisi ==="
echo "  Hostname: ${HOST}"
echo ""

echo "mDNS (ayni WiFi/LAN — IP degisse de calisir):"
if systemctl is-active --quiet avahi-daemon 2>/dev/null; then
  echo "  VNC:  ${HOST}.local:${PORT_VNC}"
  echo "  SSH:  ssh nvidia@${HOST}.local"
else
  echo "  (avahi kapali — sudo systemctl enable --now avahi-daemon)"
fi
echo ""

echo "Guncel IP adresleri:"
mapfile -t ips < <(hostname -I | tr ' ' '\n' | grep -v '^$' | grep -v '^127\.' | grep -v '^192\.168\.55\.' || true)
if [[ ${#ips[@]} -eq 0 ]]; then
  echo "  (aktif IP yok)"
else
  for ip in "${ips[@]}"; do
    echo "  VNC:  ${ip}:${PORT_VNC}"
    echo "  SSH:  ssh nvidia@${ip}"
    echo "  ---"
  done
fi
echo ""

echo "SSH tuneli (VNC guvenli):"
echo "  ssh -L ${PORT_VNC}:localhost:${PORT_VNC} nvidia@${HOST}.local"
echo "  TigerVNC Viewer -> localhost:${PORT_VNC}"
echo ""

if systemctl is-active --quiet tigervnc-x0.service 2>/dev/null; then
  echo "VNC servisi: AKTIF (tigervnc-x0, acilista: $(systemctl is-enabled tigervnc-x0.service 2>/dev/null || echo '?'))"
elif systemctl is-active --quiet x11vnc.service 2>/dev/null; then
  echo "VNC servisi: AKTIF (x11vnc, acilista: $(systemctl is-enabled x11vnc.service 2>/dev/null || echo '?'))"
else
  echo "VNC servisi: KAPALI — sudo bash scripts/setup_vnc.sh"
fi
