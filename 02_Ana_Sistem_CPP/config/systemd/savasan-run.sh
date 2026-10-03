#!/usr/bin/env bash
# DeepStream latency meta icin env, process baslamadan once export edilmeli.
set -euo pipefail

# shellcheck disable=SC1091
source /usr/local/bin/savasan-prepare-stream-env.sh
if [[ -f /etc/savasan/stream_common.env ]]; then
  set -a
  source /etc/savasan/stream_common.env
  set +a
fi
prepare_savasan_record_file

export NVDS_ENABLE_LATENCY_MEASUREMENT="${NVDS_ENABLE_LATENCY_MEASUREMENT:-1}"
export NVDS_ENABLE_COMPONENT_LATENCY_MEASUREMENT="${NVDS_ENABLE_COMPONENT_LATENCY_MEASUREMENT:-1}"
# UDP varsayilan kapali (yarışma oncesi otonomi/latency testi).
export SAVASAN_UDP_ENABLE="${SAVASAN_UDP_ENABLE:-0}"

APPLY_HELPER="/usr/local/bin/savasan-apply-camera-profile.sh"
if [[ ! -f "${APPLY_HELPER}" ]]; then
  APPLY_HELPER="/home/nvidia/Savasan_IHA_Workspace/02_Ana_Sistem_CPP/config/systemd/apply_usb_camera_profile.sh"
fi
# shellcheck disable=SC1090
source "${APPLY_HELPER}"
apply_usb_camera_profile "savasan-run"

: "${SAVASAN_BINARY:?SAVASAN_BINARY is not set (see /etc/savasan/savasan.env)}"
if [[ ! -x "${SAVASAN_BINARY}" ]]; then
  echo "[savasan-run] HATA: binary calistirilamiyor: ${SAVASAN_BINARY}" >&2
  exit 127
fi

# ALC USB bagli degilse bench modu (pipeline alc olmadan calisir).
if [[ "${SAVASAN_ALC_DISABLE:-0}" != "1" ]]; then
  alc_dev="${SAVASAN_ALC_DEVICE:-/dev/ttyACM0}"
  if [[ ! -e "${alc_dev}" ]]; then
    echo "[savasan-run] UYARI: ${alc_dev} yok — SAVASAN_ALC_DISABLE=1 (bench modu)." >&2
    export SAVASAN_ALC_DISABLE=1
  fi
fi

# Mission modu dosyasi yoksa/bossa worker WARN uretir; varsayilan air_lock yaz.
MISSION_FILE="${SAVASAN_MISSION_MODE_FILE:-/tmp/savasan_mission.cmd}"
if [[ ! -s "${MISSION_FILE}" ]]; then
  printf '%s\n' "${SAVASAN_MISSION_MODE:-air_lock}" > "${MISSION_FILE}"
fi

# AlpaguLink RF köprüsü: ayni airlock cgroup'unda arka plan (ayri systemd unit YOK).
# Durdurunca systemd KillMode=control-group ile cocuk surec de ölür.
start_alpagulink_bg() {
  if [[ "${SAVASAN_ALPAGULINK_ENABLE:-1}" != "1" ]]; then
    echo "[savasan-run] AlpaguLink kapali (SAVASAN_ALPAGULINK_ENABLE!=1)"
    return 0
  fi
  local helper="/usr/local/bin/savasan-alpagulink.sh"
  if [[ ! -x "${helper}" ]]; then
    helper="/home/nvidia/Savasan_IHA_Workspace/02_Ana_Sistem_CPP/config/systemd/savasan-alpagulink.sh"
  fi
  if [[ ! -x "${helper}" ]]; then
    echo "[savasan-run] UYARI: savasan-alpagulink.sh yok — RF köprüsü baslatilmadi" >&2
    return 0
  fi
  local rfd="${SAVASAN_RFD_PORT:-/dev/ttyUSB0}"
  if [[ ! -e "${rfd}" ]]; then
    echo "[savasan-run] UYARI: ${rfd} yok — AlpaguLink atlandi (airlock devam)" >&2
    return 0
  fi
  echo "[savasan-run] AlpaguLink RF köprüsü baslatiliyor (${helper})"
  "${helper}" &
  echo "[savasan-run] AlpaguLink pid=$!"
}

start_alpagulink_bg

exec "${SAVASAN_BINARY}" "$@"
