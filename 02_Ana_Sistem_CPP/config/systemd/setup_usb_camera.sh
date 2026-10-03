#!/usr/bin/env bash
# BRIO / UVC USB kamera v4l2 ayarlari — pipeline acilmadan once (systemd ExecStartPre).
set -euo pipefail

DEVICE="${SAVASAN_V4L2_DEVICE:-/dev/video0}"
FPS="${SAVASAN_INGEST_FPS:-60}"
GAIN="${SAVASAN_USB_GAIN:-8}"
WB_TEMP="${SAVASAN_USB_WB_TEMPERATURE:-5000}"
BRIGHTNESS="${SAVASAN_USB_BRIGHTNESS:-128}"
CONTRAST="${SAVASAN_USB_CONTRAST:-128}"
SATURATION="${SAVASAN_USB_SATURATION:-128}"
EXPOSURE_ABS="${SAVASAN_USB_EXPOSURE_ABSOLUTE:-}"

if [[ ! -e "${DEVICE}" ]]; then
  echo "[usb-camera] UYARI: cihaz yok, atlaniyor: ${DEVICE}" >&2
  exit 0
fi

if ! command -v v4l2-ctl >/dev/null 2>&1; then
  echo "[usb-camera] HATA: v4l2-ctl bulunamadi" >&2
  exit 1
fi

if [[ -z "${EXPOSURE_ABS}" ]]; then
  if ! [[ "${FPS}" =~ ^[0-9]+$ ]] || [[ "${FPS}" -le 0 ]]; then
    FPS=60
  fi
  # Gunes profili: frame suresinin ~%25'i (birim: 100 us).
  frame_us=$((1000000 / FPS))
  exposure_us=$((frame_us * 25 / 100))
  EXPOSURE_ABS=$((exposure_us / 100))
  if [[ "${EXPOSURE_ABS}" -lt 3 ]]; then
    EXPOSURE_ABS=3
  fi
  if [[ "${EXPOSURE_ABS}" -gt 500 ]]; then
    EXPOSURE_ABS=500
  fi
fi

set_ctrl() {
  local dev="$1"
  local spec="$2"
  if v4l2-ctl --device="${dev}" --set-ctrl="${spec}" >/dev/null 2>&1; then
    echo "[usb-camera] ${dev}: OK ${spec}"
    return 0
  fi
  return 1
}

# BRIO (Logitech) ve eski UVC isimlerini dene.
set_ctrl_pair() {
  local dev="$1"
  local primary="$2"
  local legacy="$3"
  if set_ctrl "${dev}" "${primary}"; then
    return 0
  fi
  if [[ -n "${legacy}" ]]; then
    set_ctrl "${dev}" "${legacy}" || true
  fi
}

apply_profile() {
  local dev="$1"
  if ! v4l2-ctl --device="${dev}" --all >/dev/null 2>&1; then
    return 0
  fi
  if ! v4l2-ctl --device="${dev}" --all 2>/dev/null | grep -qE 'auto_exposure|exposure_auto'; then
    return 0
  fi

  echo "[usb-camera] ${dev} profil uygulaniyor (fps=${FPS}, exposure_abs=${EXPOSURE_ABS}, gain=${GAIN}, wb=${WB_TEMP})"
  set_ctrl_pair "${dev}" "auto_exposure=1" "exposure_auto=1"
  set_ctrl "${dev}" "exposure_dynamic_framerate=0" || true
  set_ctrl_pair "${dev}" "exposure_time_absolute=${EXPOSURE_ABS}" "exposure_absolute=${EXPOSURE_ABS}"
  set_ctrl "${dev}" "gain=${GAIN}" || true
  set_ctrl "${dev}" "brightness=${BRIGHTNESS}" || true
  set_ctrl "${dev}" "contrast=${CONTRAST}" || true
  set_ctrl "${dev}" "saturation=${SATURATION}" || true
  set_ctrl "${dev}" "power_line_frequency=1" || true
  set_ctrl_pair "${dev}" "white_balance_automatic=0" "white_balance_temperature_auto=0"
  set_ctrl "${dev}" "white_balance_temperature=${WB_TEMP}" || true
}

apply_profile "${DEVICE}"

# BRIO'da video0/video2 ayni fiziksel cihazin capture nodlari olabilir.
for extra in /dev/video2 /dev/video0; do
  if [[ "${extra}" != "${DEVICE}" ]] && [[ -e "${extra}" ]]; then
    apply_profile "${extra}"
  fi
done

echo "[usb-camera] Son durum (${DEVICE}):"
v4l2-ctl --device="${DEVICE}" --get-ctrl=auto_exposure,exposure_time_absolute,gain,exposure_dynamic_framerate 2>/dev/null \
  | sed 's/^/[usb-camera]   /' || true
