#!/bin/bash
# Faz 3 + ayrintili GStreamer logu (sink / Argus hatalarini yakala)
# GNOME Terminal:  ./run_phase3_debug.sh
# Cikti: /tmp/savasan_phase3_debug.log dosyasina da yazilir

cd "$(dirname "$0")" || exit 1
export DISPLAY="${DISPLAY:-:0}"
export XAUTHORITY="${XAUTHORITY:-/run/user/1000/gdm/Xauthority}"
export SAVASAN_RUN_SECONDS="${SAVASAN_RUN_SECONDS:-45}"

LOG=/tmp/savasan_phase3_debug.log
export GST_DEBUG="${GST_DEBUG:-2}"
# Sink ve Argus icin biraz daha gurultulu:
export GST_DEBUG_NO_COLOR=1

exec > >(tee -a "$LOG") 2>&1
echo "=== $(date) DISPLAY=$DISPLAY XAUTHORITY=$XAUTHORITY SINK=${SAVASAN_DISPLAY_SINK:-nvegl(default)} ==="

./run_phase3.sh "${1:-usb}" "${2:-45}"
echo "=== Log ayrica: $LOG ==="
