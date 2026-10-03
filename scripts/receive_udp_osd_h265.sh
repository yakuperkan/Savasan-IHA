#!/usr/bin/env bash
# Yer istasyonu (192.168.1.10) — Jetson OSD'li H265 RTP/UDP alici.
# Jetson: SAVASAN_UDP_ENABLE=1 SAVASAN_UDP_CODEC=h265 SAVASAN_UDP_PORT=5000
#
#   ./scripts/receive_udp_osd_h265.sh
#   ./scripts/receive_udp_osd_h265.sh 5000
#
set -euo pipefail

PORT="${1:-5000}"

echo "[receiver] UDP port=${PORT} H265 (OSD'li yayin) dinleniyor..." >&2
echo "[receiver] Jetson gonderici: SAVASAN_UDP_HOST=192.168.1.10 port=${PORT}" >&2

exec gst-launch-1.0 -v \
  udpsrc port="${PORT}" buffer-size=5000000 \
  caps="application/x-rtp,media=(string)video,clock-rate=(int)90000,encoding-name=(string)H265" ! \
  rtpjitterbuffer latency=0 mode=1 ! \
  rtph265depay ! h265parse ! \
  avdec_h265 max-threads=4 ! videoconvert ! \
  fpsdisplaysink video-sink="autovideosink sync=false" text-overlay=true signal-fps-measurements=true
