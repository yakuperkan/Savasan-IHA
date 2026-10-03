#!/bin/bash
# Algilama + (Faz2) sonra takip/kilit (Faz3). Masaustu terminal + DISPLAY=:0 sart.
#   ./run_demo_pipeline.sh usb

set -e
CAM="${1:-usb}"
ROOT="$(dirname "$0")"
export DISPLAY="${DISPLAY:-:0}"
export XAUTHORITY="${XAUTHORITY:-/run/user/1000/gdm/Xauthority}"

echo "========== 1/2 Faz2: nvinfer + bbox (60 sn) =========="
SAVASAN_RUN_SECONDS=60 "$ROOT/run_phase2.sh" "$CAM" 60

echo ""
echo "========== 2/2 Faz3: tracker + kilit + bbox (120 sn) =========="
SAVASAN_RUN_SECONDS=120 "$ROOT/run_phase3.sh" "$CAM" 120

echo "Demo bitti."
