#!/usr/bin/env bash
# Phase5 — HDMI/monitor (nv3dsink) + HUD + terminal telemetri.
# GNOME terminalinden veya dogrudan Jetson konsolundan calistirin (SSH'de pencere acilmaz).
#
#   ./scripts/run_phase5_display.sh
#   ./scripts/run_phase5_display.sh usb 120
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${SAVASAN_BINARY:-${ROOT}/02_Ana_Sistem_CPP/build-app/savasan_iha}"
CAM="${1:-usb}"
SECONDS="${2:-0}"

if [[ ! -x "${BIN}" ]]; then
  echo "HATA: ${BIN} yok. Derle:" >&2
  echo "  cd ${ROOT}/02_Ana_Sistem_CPP/build && cmake --build . -j\$(nproc)" >&2
  exit 1
fi

UID_NUM="$(id -u)"
export DISPLAY="${DISPLAY:-:0}"
export XAUTHORITY="${XAUTHORITY:-/run/user/${UID_NUM}/gdm/Xauthority}"
if [[ ! -f "${XAUTHORITY}" && -f "${HOME}/.Xauthority" ]]; then
  export XAUTHORITY="${HOME}/.Xauthority"
fi

export NVDS_ENABLE_LATENCY_MEASUREMENT=1
export SAVASAN_RUN_SECONDS="${SECONDS}"
export SAVASAN_DISPLAY=display
export SAVASAN_INGEST_FPS="${SAVASAN_INGEST_FPS:-60}"
export SAVASAN_UDP_ENABLE=0
export SAVASAN_TRACKER_DISABLE="${SAVASAN_TRACKER_DISABLE:-0}"

echo "============================================"
echo " Savasan Phase5 — MONITOR (nv3dsink)"
echo " Kamera: ${CAM}  |  Sure: ${SECONDS} sn (0=suresiz)"
echo " DISPLAY=${DISPLAY}  XAUTHORITY=${XAUTHORITY}"
echo " Sink: nv3dsink + HUD (bbox, AV alani, vektor)"
echo " Telemetri: bu terminal | Ctrl+C durdur"
echo "============================================"

exec "${BIN}" phase5 "${CAM}" display
