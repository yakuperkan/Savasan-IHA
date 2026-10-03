#!/usr/bin/env bash
# Ortak stream env: kayit dosya yolu (AIR_LOCK + Kamikaze run wrapper).
set -euo pipefail

prepare_savasan_record_file() {
  if [[ "${SAVASAN_RECORD_ENABLE:-0}" != "1" ]]; then
    return 0
  fi
  if [[ -n "${SAVASAN_RECORD_FILE:-}" ]]; then
    echo "[savasan-stream] Kayit: ${SAVASAN_RECORD_FILE}" >&2
    return 0
  fi
  local dir="${SAVASAN_RECORD_DIR:-/home/nvidia/Savasan_IHA_Workspace/output}"
  local ext="${SAVASAN_RECORD_EXT:-mp4}"
  mkdir -p "${dir}" 2>/dev/null || true
  export SAVASAN_RECORD_FILE="${dir}/flight_output_$(date +%Y%m%d_%H%M%S).${ext}"
  echo "[savasan-stream] Kayit: ${SAVASAN_RECORD_FILE}" >&2
}

source_stream_env_files() {
  local f
  for f in /etc/savasan/stream_common.env /etc/savasan/savasan.env /etc/savasan/kamikaze.env; do
    if [[ -f "${f}" ]]; then
      set -a
      # shellcheck disable=SC1090
      source "${f}"
      set +a
    fi
  done
}
