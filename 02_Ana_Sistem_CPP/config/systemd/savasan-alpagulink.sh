#!/usr/bin/env bash
# AlpaguLink RF köprüsü — ACM0 açmaz; C++ hub dosyasından okur.
set -euo pipefail

WORKSPACE="${SAVASAN_WORKSPACE:-/home/nvidia/Savasan_IHA_Workspace}"
PY="${SAVASAN_ALPAGULINK_PY:-${WORKSPACE}/AlpaguLink.py}"

export SAVASAN_ALC_HUB_FILE="${SAVASAN_ALC_HUB_FILE:-/tmp/savasan_alc_hub.env}"
export SAVASAN_LOCK_STATE_FILE="${SAVASAN_LOCK_STATE_FILE:-/tmp/savasan_lock.state}"
export SAVASAN_ALC_UPLINK_FILE="${SAVASAN_ALC_UPLINK_FILE:-/tmp/savasan_alc_uplink.bin}"
export SAVASAN_RIVAL_POOL_FILE="${SAVASAN_RIVAL_POOL_FILE:-/tmp/savasan_rival_pool.env}"
export SAVASAN_HSS_RF_FILE="${SAVASAN_HSS_RF_FILE:-/tmp/savasan_hss_rf.env}"
export SAVASAN_BOUNDARY_RF_FILE="${SAVASAN_BOUNDARY_RF_FILE:-/tmp/savasan_boundary_rf.env}"
export SAVASAN_GOTO_FILE="${SAVASAN_GOTO_FILE:-/tmp/savasan_goto.env}"
export SAVASAN_RFD_PORT="${SAVASAN_RFD_PORT:-/dev/ttyUSB0}"
export SAVASAN_RFD_BAUD="${SAVASAN_RFD_BAUD:-57600}"

if [[ ! -f "${PY}" ]]; then
  echo "[alpagulink] HATA: ${PY} yok" >&2
  exit 1
fi

# RFD yoksa kisa bekle; yoksa cikis (cagiran savasan-run airlock'u durdurmaz — orada kontrol edilir)
for _ in $(seq 1 5); do
  if [[ -e "${SAVASAN_RFD_PORT}" ]]; then
    break
  fi
  echo "[alpagulink] ${SAVASAN_RFD_PORT} bekleniyor..."
  sleep 1
done
if [[ ! -e "${SAVASAN_RFD_PORT}" ]]; then
  echo "[alpagulink] HATA: ${SAVASAN_RFD_PORT} yok" >&2
  exit 1
fi

exec /usr/bin/python3 -u "${PY}"
