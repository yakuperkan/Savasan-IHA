#!/usr/bin/env bash
# Phase5 — yalnizca terminal, fakesink (GUI/Cursor yok), telemetri stdout.
#
#   ./scripts/run_phase5_terminal.sh              # usb, 60 sn
#   ./scripts/run_phase5_terminal.sh usb 120
#   SAVASAN_JETSON_CLOCKS=1 ./scripts/run_phase5_terminal.sh usb 300
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${SAVASAN_BINARY:-${ROOT}/02_Ana_Sistem_CPP/build-app/savasan_iha}"
CAM="${1:-usb}"
SECONDS="${2:-60}"

if [[ ! -x "${BIN}" ]]; then
  echo "HATA: ${BIN} yok. Derle:" >&2
  echo "  cd ${ROOT}/02_Ana_Sistem_CPP/build && cmake --build . -j\$(nproc)" >&2
  exit 1
fi

if [[ "${SAVASAN_JETSON_CLOCKS:-0}" == "1" ]] && command -v jetson_clocks >/dev/null 2>&1; then
  echo "[terminal] jetson_clocks uygulaniyor..."
  sudo jetson_clocks 2>/dev/null || jetson_clocks 2>/dev/null || true
fi

export NVDS_ENABLE_LATENCY_MEASUREMENT=1
export NVDS_ENABLE_COMPONENT_LATENCY_MEASUREMENT="${NVDS_ENABLE_COMPONENT_LATENCY_MEASUREMENT:-0}"
export SAVASAN_COMPONENT_LATENCY_PROFILER="${SAVASAN_COMPONENT_LATENCY_PROFILER:-0}"
export SAVASAN_RUN_SECONDS="${SECONDS}"
export SAVASAN_DISPLAY=
export SAVASAN_INGEST_FPS="${SAVASAN_INGEST_FPS:-60}"
export SAVASAN_UDP_ENABLE=0
export SAVASAN_TRACKER_DISABLE="${SAVASAN_TRACKER_DISABLE:-0}"
unset DISPLAY XAUTHORITY 2>/dev/null || true

echo "============================================"
echo " Savasan Phase5 — terminal / headless"
echo " Kamera: ${CAM}  |  Sure: ${SECONDS} sn"
echo " Sink: fakesink  |  Log: bu terminal"
echo " Ctrl+C ile durdur"
echo "============================================"

exec "${BIN}" phase5 "${CAM}"
