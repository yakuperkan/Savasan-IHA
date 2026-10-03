#!/usr/bin/env bash
# Fake rakip telemetri + phase5 YAKLASMA + CSV + grafik (tek komut bench).
set -euo pipefail

ROOT="/home/nvidia/Savasan_IHA_Workspace"
BIN="${ROOT}/02_Ana_Sistem_CPP/build-app/savasan_iha"
DURATION="${SAVASAN_TELEM_BENCH_SECONDS:-40}"
PRED_CSV="${SAVASAN_TELEM_PRED_CSV:-/tmp/telem_pred.csv}"
TRUTH_CSV="${SAVASAN_RIVAL_TRUTH_CSV:-/tmp/savasan_rival_truth.csv}"
PLOT_OUT="${SAVASAN_TELEM_PLOT:-/tmp/telem_pred.svg}"

if [[ ! -x "${BIN}" ]]; then
  echo "[bench] HATA: binary yok: ${BIN} — once build-app" >&2
  exit 1
fi

rm -f "${PRED_CSV}" "${TRUTH_CSV}" /tmp/savasan_rival_pool.env

# shellcheck disable=SC1091
source "${ROOT}/02_Ana_Sistem_CPP/config/systemd/savasan.env" 2>/dev/null || true

export SAVASAN_RUN_SECONDS="${DURATION}"
export SAVASAN_SEYIR_TX_ENABLE=0
export SAVASAN_CONTROL_LOG_ENABLE=1
export SAVASAN_ALPAGULINK_ENABLE=0
export SAVASAN_DISPLAY=
export SAVASAN_SEND_ONLY_ON_LOCK=0
export SAVASAN_TELEM_PRED_CSV="${PRED_CSV}"
export SAVASAN_GUIDANCE_MODE=seyir

export SAVASAN_SEYIR_TELEM_ONLY=1

echo "[bench] Fake rakip baslatiliyor (${DURATION}s, path=weave)..."
python3 "${ROOT}/scripts/fake_rival_telemetry.py" \
  --path weave \
  --duration "${DURATION}" \
  --hz 10 \
  --stale-ms "${SAVASAN_FAKE_STALE_MS:-1200}" \
  --start-range-m "${SAVASAN_FAKE_START_RANGE_M:-420}" \
  --weave-amp-m "${SAVASAN_FAKE_WEAVE_AMP_M:-80}" \
  --weave-period-s "${SAVASAN_FAKE_WEAVE_PERIOD_S:-14}" \
  --speed 16 \
  --truth-csv "${TRUTH_CSV}" &
FAKE_PID=$!

sleep 1
echo "[bench] phase5 baslatiliyor (telemetri YAKLASMA CSV)..."
timeout "$((DURATION + 15))" "${BIN}" phase5 usb 2>&1 | \
  grep --line-buffered -E 'SEYIR|YAKLASMA|ARAMA|Baglandi' || true

wait "${FAKE_PID}" 2>/dev/null || true

if [[ ! -s "${PRED_CSV}" ]]; then
  echo "[bench] UYARI: pred CSV bos veya yok — kamera GORSEL onceligi veya GPS valid=0 olabilir." >&2
  echo "        Kamerayi kapatin veya own_lat hub'dan gelsin; journalctl SEYIR faz=YAKLASMA arayin." >&2
  exit 2
fi

echo "[bench] Grafik: ${PLOT_OUT}"
python3 "${ROOT}/scripts/plot_telemetry_prediction.py" --pred "${PRED_CSV}" --truth "${TRUTH_CSV}" --save "${PLOT_OUT}"
echo "[bench] Tamam. CSV: ${PRED_CSV}  PNG: ${PLOT_OUT}"
