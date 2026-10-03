#!/usr/bin/env bash
# Kamikaze ucus servisi — yalnizca kamikaze.env; savasan.env ASLA yuklenmez.
set -euo pipefail

KAMIKAZE_BINARY="/home/nvidia/Savasan_IHA_Workspace_KAMIKAZE/02_Ana_Sistem_CPP/build/savasan_iha"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MAIN_WS="/home/nvidia/Savasan_IHA_Workspace"

if [[ -f /etc/savasan/kamikaze.env ]]; then
  set -a
  # shellcheck disable=SC1091
  source /etc/savasan/kamikaze.env
  set +a
fi

# Her ucusu otomatik kaydet: SAVASAN_RECORD_ENABLE=1 ise timestamp'li
# SAVASAN_RECORD_FILE (kamikaze_<kamera>_YYYYMMDD_HHMMSS.mp4) uretilir.
PREPARE_HELPER=""
for cand in /usr/local/bin/savasan-prepare-stream-env.sh \
            "${SCRIPT_DIR}/savasan-prepare-stream-env.sh"; do
  if [[ -f "${cand}" ]]; then
    PREPARE_HELPER="${cand}"
    break
  fi
done
if [[ -n "${PREPARE_HELPER}" ]]; then
  # shellcheck disable=SC1090
  source "${PREPARE_HELPER}"
  prepare_savasan_record_file || true
fi

APPLY_HELPER="/usr/local/bin/savasan-apply-camera-profile.sh"
if [[ ! -f "${APPLY_HELPER}" ]]; then
  APPLY_HELPER="${MAIN_WS}/02_Ana_Sistem_CPP/config/systemd/apply_usb_camera_profile.sh"
fi
# shellcheck disable=SC1090
source "${APPLY_HELPER}"
apply_usb_camera_profile "savasan-run-kamikaze"

if [[ ! -x "${KAMIKAZE_BINARY}" ]]; then
  echo "[savasan-run-kamikaze] HATA: binary yok: ${KAMIKAZE_BINARY}" >&2
  exit 127
fi

export NVDS_ENABLE_LATENCY_MEASUREMENT="${NVDS_ENABLE_LATENCY_MEASUREMENT:-1}"
export NVDS_ENABLE_COMPONENT_LATENCY_MEASUREMENT="${NVDS_ENABLE_COMPONENT_LATENCY_MEASUREMENT:-1}"
export SAVASAN_RUN_SECONDS="${SAVASAN_RUN_SECONDS:-0}"

CAMERA="${SAVASAN_CAMERA:-usb}"
DISPLAY_ARG="${SAVASAN_DISPLAY:-display}"
echo "[savasan-run-kamikaze] binary=${KAMIKAZE_BINARY} camera=${CAMERA} display=${DISPLAY_ARG} udp=${SAVASAN_UDP_ENABLE:-0} run_seconds=${SAVASAN_RUN_SECONDS} profile=${SAVASAN_CAMERA_PROFILE:-<yok>}" >&2

exec "${KAMIKAZE_BINARY}" kamikaze "${CAMERA}" "${DISPLAY_ARG}"
