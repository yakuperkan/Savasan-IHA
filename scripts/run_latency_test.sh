#!/usr/bin/env bash
# Jetson GStreamer pipeline latency olcumu (nvstreammux ↔ nvinfer arasi NVDS latency).
#
# Kullanim (repo kokunden):
#   bash scripts/run_latency_test.sh
#   bash scripts/run_latency_test.sh phase3 usb
#   bash scripts/run_latency_test.sh phase5 usb hybrid
#
# Ortam (istege bagli):
#   SAVASAN_RUN_SECONDS=25
#   SAVASAN_LATENCY_LOG=/path/latency_test_results.log
#   SAVASAN_NVBUF_MEM_TYPE=4
#   SAVASAN_CAMERA=usb   SAVASAN_LOCK_MODE=hybrid|baseline
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="${ROOT}/02_Ana_Sistem_CPP"
BIN="${PROJECT}/build/savasan_iha"
LOG_FILE="${SAVASAN_LATENCY_LOG:-${ROOT}/latency_test_results.log}"

PHASE="${1:-${SAVASAN_PHASE:-phase5}}"
CAM="${2:-${SAVASAN_CAMERA:-usb}}"
LOCK_MODE="${3:-${SAVASAN_LOCK_MODE:-hybrid}}"

if [[ ! -x "${BIN}" ]]; then
  echo "[latency-test] HATA: binary yok: ${BIN}" >&2
  echo "  cd ${PROJECT}/build && cmake .. -DSAVASAN_BUILD_APP=ON && cmake --build . -j\$(nproc)" >&2
  exit 1
fi

# 1) Headless (fakesink)
export SAVASAN_DISPLAY=""

# 2) NVMM zero-copy (SURFACE_ARRAY)
export SAVASAN_NVBUF_MEM_TYPE="${SAVASAN_NVBUF_MEM_TYPE:-4}"

# 3) NVIDIA DeepStream latency olcumu
export NVDS_ENABLE_LATENCY_MEASUREMENT=1

# 4) Sabit kosu suresi
export SAVASAN_RUN_SECONDS="${SAVASAN_RUN_SECONDS:-25}"

# Masaustu guvenli varsayilanlar (seri/UDP kapali)
export SAVASAN_ALC_DISABLE="${SAVASAN_ALC_DISABLE:-1}"
export SAVASAN_SETPOINT_TX_ENABLE="${SAVASAN_SETPOINT_TX_ENABLE:-0}"
export SAVASAN_UDP_ENABLE="${SAVASAN_UDP_ENABLE:-0}"

FULL_LOG="$(mktemp)"
trap 'rm -f "${FULL_LOG}"' EXIT

{
  echo "=== Savasan latency test $(date -Iseconds) ==="
  echo "binary=${BIN}"
  echo "phase=${PHASE} camera=${CAM} lock_mode=${LOCK_MODE}"
  echo "SAVASAN_DISPLAY=<bos> SAVASAN_NVBUF_MEM_TYPE=${SAVASAN_NVBUF_MEM_TYPE}"
  echo "NVDS_ENABLE_LATENCY_MEASUREMENT=1 SAVASAN_RUN_SECONDS=${SAVASAN_RUN_SECONDS}"
  echo "--- WARN / perf / telemetri satirlari ---"
} > "${LOG_FILE}"

echo "[latency-test] phase=${PHASE} camera=${CAM} sure=${SAVASAN_RUN_SECONDS}s"
echo "[latency-test] log=${LOG_FILE}"

run_args=("${PHASE}" "${CAM}")
if [[ "${PHASE}" == "phase4" || "${PHASE}" == "phase5" ]]; then
  run_args+=("${LOCK_MODE}")
fi

set +e
"${BIN}" "${run_args[@]}" 2>&1 | tee "${FULL_LOG}"
APP_RC=${PIPESTATUS[0]}
set -e

grep -E -i '\[WARN\]|TELEMETR[Iİ]|Latency:|GStreamer-WARNING|: WARN:|\[Perf\]' "${FULL_LOG}" \
  >> "${LOG_FILE}" || true

{
  echo "---"
  echo "exit_code=${APP_RC}"
  echo "latency_telemetry_samples=$(
    grep -cE 'TELEMETR[Iİ].*Latency:' "${FULL_LOG}" 2>/dev/null || echo 0
  )"
} >> "${LOG_FILE}"

echo
echo "[latency-test] WARN/perf satirlari: ${LOG_FILE}"
if [[ "${APP_RC}" -ne 0 ]]; then
  echo "[latency-test] Uyari: uygulama cikis kodu ${APP_RC}" >&2
  exit "${APP_RC}"
fi

echo "[latency-test] Bitti."
