#!/bin/bash
export NVDS_ENABLE_LATENCY_MEASUREMENT=1
# Savasan IHA — Faz 3: nvinfer + nvtracker + kilit + ekran goruntuleme
# GNOME Terminal'de (masaustu oturumu) calistir; Cursor terminalinde pencere acilmayabilir.
#
#   ./run_phase3.sh
#   ./run_phase3.sh usb 120
# USB aygiti (varsayilan /dev/video0): SAVASAN_V4L2_DEVICE=/dev/video0 ./run_phase3.sh usb 60
#
# Sink degistirmek icin (pencere yoksa sirayla dene):
#   SAVASAN_DISPLAY_SINK=xvimagesink ./run_phase3.sh
#   SAVASAN_DISPLAY_SINK=nv3dsink ./run_phase3.sh
# Varsayilan: nvegl (nvegltransform ! nveglglessink)

CAM="${1:-usb}"
SECONDS="${2:-60}"

export DISPLAY="${DISPLAY:-:0}"
export XAUTHORITY="${XAUTHORITY:-/run/user/1000/gdm/Xauthority}"
export SAVASAN_RUN_SECONDS="$SECONDS"

BIN="$(dirname "$0")/../02_Ana_Sistem_CPP/build/savasan_iha"

if [ ! -x "$BIN" ]; then
    echo "HATA: $BIN bulunamadi. Once derle:"
    echo "  cd 02_Ana_Sistem_CPP/build && cmake --build ."
    exit 1
fi

echo "============================================"
echo " Savasan IHA — Faz 3 (NvTracker + Kilit)"
echo " Kamera: $CAM  |  Sure: ${SECONDS}sn"
echo " Ctrl+C ile durdur"
echo "============================================"
exec "$BIN" phase3 "$CAM" display
