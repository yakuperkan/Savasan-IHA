#!/bin/bash
export NVDS_ENABLE_LATENCY_MEASUREMENT=1
# Savasan IHA — Faz 2: nvinfer + OSD (tracker yok)
# Bu betiği GNOME Terminal'den çalıştır:
#   cd ~/Savasan_IHA_Workspace && ./run_phase2.sh
# Terminalde [TELEMETRİ] FPS / Latency icin bu betik NVDS_ENABLE_LATENCY_MEASUREMENT=1 export eder.

CAM="${1:-usb}"
SECONDS="${2:-60}"

export DISPLAY=:0
export XAUTHORITY=/run/user/1000/gdm/Xauthority
export SAVASAN_RUN_SECONDS="$SECONDS"

BIN="$(dirname "$0")/../02_Ana_Sistem_CPP/build/savasan_iha"

if [ ! -x "$BIN" ]; then
    echo "HATA: $BIN bulunamadi. Once derle:"
    echo "  cd 02_Ana_Sistem_CPP/build && cmake --build ."
    exit 1
fi

echo "============================================"
echo " Savasan IHA — Faz 2 (nvinfer + OSD)"
echo " Kamera: $CAM  |  Sure: ${SECONDS}sn"
echo " Ctrl+C ile durdur"
echo "============================================"
exec "$BIN" phase2 "$CAM" display
