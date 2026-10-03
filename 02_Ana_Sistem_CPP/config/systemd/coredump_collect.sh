#!/usr/bin/env bash
set -euo pipefail

EXE_PATH="${1:-/home/nvidia/Savasan_IHA_Workspace/02_Ana_Sistem_CPP/build-app/savasan_iha}"
OUT_DIR="${2:-/var/crash/savasan}"
STATE_FILE="${3:-/var/lib/savasan/last_core_pid}"
KEEP_COUNT="${4:-15}"

mkdir -p "${OUT_DIR}" "$(dirname "${STATE_FILE}")"

if ! command -v coredumpctl >/dev/null 2>&1; then
  echo "[coredump_collect] WARN: coredumpctl not found"
  exit 0
fi

if ! coredumpctl --no-pager --no-legend -1 info "${EXE_PATH}" >/dev/null 2>&1; then
  echo "[coredump_collect] INFO: no coredump found for ${EXE_PATH}"
  exit 0
fi

info_tmp="$(mktemp)"
coredumpctl --no-pager --no-legend -1 info "${EXE_PATH}" >"${info_tmp}"

pid="$(awk -F': *' '$1=="PID"{print $2}' "${info_tmp}" | head -n1)"
ts_human="$(awk -F': *' '$1=="Timestamp"{print $2}' "${info_tmp}" | head -n1)"
sig="$(awk -F': *' '$1=="Signal"{print $2}' "${info_tmp}" | head -n1)"

if [[ -z "${pid}" ]]; then
  echo "[coredump_collect] WARN: could not parse PID from coredump info"
  rm -f "${info_tmp}"
  exit 0
fi

last_pid=""
if [[ -f "${STATE_FILE}" ]]; then
  last_pid="$(cat "${STATE_FILE}" || true)"
fi
if [[ "${last_pid}" == "${pid}" ]]; then
  echo "[coredump_collect] INFO: latest coredump already collected (pid=${pid})"
  rm -f "${info_tmp}"
  exit 0
fi

stamp="$(date +%Y%m%d_%H%M%S)"
base="${OUT_DIR}/savasan_core_${stamp}_pid${pid}"
core_file="${base}.core"
meta_file="${base}.txt"

cp "${info_tmp}" "${meta_file}"
{
  echo ""
  echo "CollectedAt: $(date -Iseconds)"
  echo "Executable: ${EXE_PATH}"
  echo "PID: ${pid}"
  echo "Signal: ${sig:-unknown}"
  echo "Timestamp: ${ts_human:-unknown}"
} >> "${meta_file}"

if coredumpctl --no-pager --no-legend -1 dump "${EXE_PATH}" --output "${core_file}" >/dev/null 2>&1; then
  gzip -f "${core_file}"
  echo "[coredump_collect] OK: ${core_file}.gz + ${meta_file}"
  echo "${pid}" > "${STATE_FILE}"
else
  echo "[coredump_collect] WARN: failed to export dump payload (metadata saved)"
fi

rm -f "${info_tmp}"

# Keep only newest KEEP_COUNT artifacts.
count=0
for f in $(ls -1t "${OUT_DIR}"/savasan_core_* 2>/dev/null); do
  count=$((count + 1))
  if (( count > KEEP_COUNT )); then
    rm -f "${f}"
  fi
done
