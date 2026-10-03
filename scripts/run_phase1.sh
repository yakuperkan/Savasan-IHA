#!/bin/bash
# Faz 1: Sadece kamera -> fakesink (YAPAY ZEKA YOK, PENCERE YOK).
# Amaç: USB MJPEG hattinin acildigini dogrulamak (~5 sn).
#   ./run_phase1.sh
#   ./run_phase1.sh usb

set -e
CAM="${1:-usb}"
BIN="$(dirname "$0")/../02_Ana_Sistem_CPP/build/savasan_iha"
if [ ! -x "$BIN" ]; then
  echo "HATA: $BIN yok. cmake --build 02_Ana_Sistem_CPP/build"
  exit 1
fi
echo "=== Faz1 duman (kamera + fakesink, 5 sn) kamera=$CAM ==="
exec "$BIN" phase1 "$CAM"
