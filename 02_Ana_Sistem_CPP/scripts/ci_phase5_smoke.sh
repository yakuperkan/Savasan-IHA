#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PROJECT_ROOT="${REPO_ROOT}/02_Ana_Sistem_CPP"
BIN_PATH="${PROJECT_ROOT}/build-smoke/savasan_iha"
LOG_DIR="${REPO_ROOT}/ci-logs"

mkdir -p "${LOG_DIR}"

if [[ ! -x "${BIN_PATH}" ]]; then
  echo "[phase5-smoke] ERROR: binary not found: ${BIN_PATH}" >&2
  exit 1
fi

CAMERA="${SAVASAN_SMOKE_CAMERA:-csi}"
RUN_SECONDS="${SAVASAN_SMOKE_RUN_SECONDS:-12}"
LOCK_MODE="${SAVASAN_SMOKE_LOCK_MODE:-hybrid}"
LOG_FILE="${LOG_DIR}/phase5-smoke.log"

if [[ "${CAMERA}" != "csi" && "${CAMERA}" != "usb" ]]; then
  echo "[phase5-smoke] ERROR: SAVASAN_SMOKE_CAMERA must be csi or usb (got ${CAMERA})" >&2
  exit 1
fi

if [[ "${CAMERA}" == "usb" ]]; then
  V4L2_DEVICE="${SAVASAN_V4L2_DEVICE:-/dev/video0}"
  if [[ ! -e "${V4L2_DEVICE}" ]]; then
    echo "[phase5-smoke] ERROR: usb camera device missing: ${V4L2_DEVICE}" >&2
    exit 1
  fi
fi

echo "[phase5-smoke] camera=${CAMERA} lock_mode=${LOCK_MODE} run_seconds=${RUN_SECONDS}" | tee "${LOG_FILE}"
echo "[phase5-smoke] binary=${BIN_PATH}" | tee -a "${LOG_FILE}"

(
  export SAVASAN_RUN_SECONDS="${RUN_SECONDS}"
  export SAVASAN_ALC_DISABLE=1
  export SAVASAN_DISPLAY=""
  export SAVASAN_ALC_HEARTBEAT=0
  export SAVASAN_SETPOINT_TX_ENABLE=0
  export SAVASAN_UDP_ENABLE=0
  export NVDS_ENABLE_LATENCY_MEASUREMENT=1

  "${BIN_PATH}" phase5 "${CAMERA}" "${LOCK_MODE}"
) 2>&1 | tee -a "${LOG_FILE}"

echo "[phase5-smoke] SUCCESS" | tee -a "${LOG_FILE}"
