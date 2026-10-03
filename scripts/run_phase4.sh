#!/bin/bash
export NVDS_ENABLE_LATENCY_MEASUREMENT=1
# Savasan IHA - Faz 4: nvinfer + nvtracker + hibrit kilit modu
#
#   ./run_phase4.sh
#   ./run_phase4.sh usb 120 hybrid
#   ./run_phase4.sh usb 120 baseline
#
# USB aygiti: SAVASAN_V4L2_DEVICE=/dev/video0 ./run_phase4.sh usb 60 hybrid

CAM="${1:-usb}"
SECONDS="${2:-60}"
LOCK_MODE="${3:-hybrid}"

export DISPLAY="${DISPLAY:-:0}"
export XAUTHORITY="${XAUTHORITY:-/run/user/1000/gdm/Xauthority}"
export SAVASAN_RUN_SECONDS="$SECONDS"

BIN="$(dirname "$0")/../02_Ana_Sistem_CPP/build/savasan_iha"

if [ ! -x "$BIN" ]; then
    echo "HATA: $BIN bulunamadi. Once derle:"
    echo "  cmake --build /home/nvidia/Savasan_IHA_Workspace/02_Ana_Sistem_CPP/build -j4"
    exit 1
fi

if [ "$LOCK_MODE" != "hybrid" ] && [ "$LOCK_MODE" != "baseline" ]; then
    echo "HATA: lock mode yalnizca hybrid veya baseline olabilir."
    exit 1
fi

echo "============================================"
echo " Savasan IHA - Faz 4 ($LOCK_MODE)"
echo " Kamera: $CAM  |  Sure: ${SECONDS}sn"
echo " Ctrl+C ile durdur"
echo "============================================"
exec "$BIN" phase4 "$CAM" "$LOCK_MODE" display

