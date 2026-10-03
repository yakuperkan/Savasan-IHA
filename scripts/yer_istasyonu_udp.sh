#!/usr/bin/env bash
# =============================================================================
# Savasan IHA — Yer istasyonu UDP video alici + masaustu kayit (Ubuntu laptop)
# Jetson OSD'li yayin: H265/H264 RTP port 5000 -> 192.168.1.10
#
# Kayit (varsayilan acik): ~/Desktop/flight_output_YYYYMMDD_HHMMSS.mp4
#   GCS_FLIGHT_OUTPUT veya YER_ISTASYONU_OUTPUT=/path/custom.mp4
#   GCS_RECORD_ENABLE=0 veya YER_ISTASYONU_RECORD_ENABLE=0 — kayit kapali
#
# Calistir (laptop — ONCE alici, SONRA Jetson servisi):
#   ./yer_istasyonu_udp.sh
#   ./yer_istasyonu_udp.sh 5000 h265
# =============================================================================
set -euo pipefail

PORT="${1:-${YER_ISTASYONU_UDP_PORT:-${GCS_UDP_PORT:-5000}}}"
CODEC="${2:-${YER_ISTASYONU_CODEC:-${GCS_UDP_CODEC:-h265}}}"
CODEC="$(echo "${CODEC}" | tr '[:upper:]' '[:lower:]')"
RECORD_ENABLE="${YER_ISTASYONU_RECORD_ENABLE:-${GCS_RECORD_ENABLE:-1}}"
EXPECTED_IP="${YER_ISTASYONU_EXPECTED_IP:-${GCS_EXPECTED_IP:-192.168.1.10}}"

die() {
  echo "[yer-istasyonu] HATA: $*" >&2
  exit 1
}

resolve_desktop_dir() {
  if [[ -n "${XDG_DESKTOP_DIR:-}" && -d "${XDG_DESKTOP_DIR}" ]]; then
    echo "${XDG_DESKTOP_DIR}"
    return
  fi
  if [[ -d "${HOME}/Desktop" ]]; then
    echo "${HOME}/Desktop"
    return
  fi
  if [[ -d "${HOME}/Masaüstü" ]]; then
    echo "${HOME}/Masaüstü"
    return
  fi
  echo "${HOME}"
}

need_gstreamer() {
  if command -v gst-launch-1.0 >/dev/null 2>&1; then
    return 0
  fi
  die "gst-launch-1.0 yok. Kur: sudo apt install -y gstreamer1.0-tools gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-libav"
}

warn_ip() {
  local found=0
  local ip
  while read -r ip; do
    if [[ "${ip}" == "${EXPECTED_IP}" ]]; then
      found=1
      break
    fi
  done < <(hostname -I 2>/dev/null || true)

  if [[ "${found}" -eq 0 ]]; then
    echo "[yer-istasyonu] UYARI: Bu makinenin IP'si ${EXPECTED_IP} degil (hostname -I: $(hostname -I))." >&2
    echo "[yer-istasyonu] Jetson SAVASAN_UDP_HOST=${EXPECTED_IP} ise yayin baska adrese gider." >&2
  else
    echo "[yer-istasyonu] IP OK: ${EXPECTED_IP}" >&2
  fi
}

need_gstreamer
warn_ip

RECORD_FILE=""
if [[ "${RECORD_ENABLE}" == "1" ]]; then
  DESKTOP="$(resolve_desktop_dir)"
  RECORD_FILE="${YER_ISTASYONU_OUTPUT:-${GCS_FLIGHT_OUTPUT:-${DESKTOP}/flight_output_$(date +%Y%m%d_%H%M%S).mp4}}"
fi

echo "[yer-istasyonu] Dinleniyor: UDP :${PORT} codec=${CODEC} (OSD'li yayin)" >&2
if [[ -n "${RECORD_FILE}" ]]; then
  echo "[yer-istasyonu] Kayit: ${RECORD_FILE}" >&2
  echo "[yer-istasyonu] Durdurmak icin Ctrl+C (-e ile MP4 kapatilir)" >&2
else
  echo "[yer-istasyonu] Kayit kapali (YER_ISTASYONU_RECORD_ENABLE=0)" >&2
fi
echo "[yer-istasyonu] Jetson'da servisi simdi baslatin." >&2

launch_h265() {
  if [[ -n "${RECORD_FILE}" ]]; then
    exec gst-launch-1.0 -e \
      udpsrc port="${PORT}" buffer-size=5000000 \
      caps="application/x-rtp,media=(string)video,clock-rate=(int)90000,encoding-name=(string)H265" ! \
      rtpjitterbuffer latency=0 mode=1 ! \
      rtph265depay ! h265parse ! tee name=rec_t \
      rec_t. ! queue max-size-buffers=120 leaky=downstream ! mp4mux ! \
      filesink location="${RECORD_FILE}" sync=false \
      rec_t. ! queue ! avdec_h265 max-threads=4 ! videoconvert ! \
      fpsdisplaysink video-sink="autovideosink sync=false" text-overlay=true signal-fps-measurements=true
  fi
  exec gst-launch-1.0 -v \
    udpsrc port="${PORT}" buffer-size=5000000 \
    caps="application/x-rtp,media=(string)video,clock-rate=(int)90000,encoding-name=(string)H265" ! \
    rtpjitterbuffer latency=0 mode=1 ! \
    rtph265depay ! h265parse ! \
    avdec_h265 max-threads=4 ! videoconvert ! \
    fpsdisplaysink video-sink="autovideosink sync=false" text-overlay=true signal-fps-measurements=true
}

launch_h264() {
  if [[ -n "${RECORD_FILE}" ]]; then
    exec gst-launch-1.0 -e \
      udpsrc port="${PORT}" buffer-size=5000000 \
      caps="application/x-rtp,media=(string)video,clock-rate=(int)90000,encoding-name=(string)H264" ! \
      rtpjitterbuffer latency=0 mode=1 ! \
      rtph264depay ! h264parse ! tee name=rec_t \
      rec_t. ! queue max-size-buffers=120 leaky=downstream ! mp4mux ! \
      filesink location="${RECORD_FILE}" sync=false \
      rec_t. ! queue ! avdec_h264 max-threads=4 ! videoconvert ! \
      fpsdisplaysink video-sink="autovideosink sync=false" text-overlay=true signal-fps-measurements=true
  fi
  exec gst-launch-1.0 -v \
    udpsrc port="${PORT}" buffer-size=5000000 \
    caps="application/x-rtp,media=(string)video,clock-rate=(int)90000,encoding-name=(string)H264" ! \
    rtpjitterbuffer latency=0 mode=1 ! \
    rtph264depay ! h264parse ! \
    avdec_h264 max-threads=4 ! videoconvert ! \
    fpsdisplaysink video-sink="autovideosink sync=false" text-overlay=true signal-fps-measurements=true
}

if [[ "${CODEC}" == "h265" ]]; then
  launch_h265
elif [[ "${CODEC}" == "h264" ]]; then
  launch_h264
else
  die "codec '${CODEC}' desteklenmiyor (h264 veya h265)"
fi
