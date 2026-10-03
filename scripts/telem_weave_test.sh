#!/usr/bin/env bash
# Fake weave telemetri + phase5 + grafik — tek terminal, yorum satiri yok.
set -euo pipefail

ROOT="/home/nvidia/Savasan_IHA_Workspace"
BIN="${ROOT}/02_Ana_Sistem_CPP/build-app/savasan_iha"
DUR="${1:-50}"
RIVAL_PATH="${2:-headon}"

if systemctl is-active --quiet savasan-airlock 2>/dev/null; then
  echo "[telem-test] savasan-airlock durduruluyor (ACM0 cakismasi)..."
  sudo systemctl stop savasan-airlock || true
fi

if [[ ! -x "${BIN}" ]]; then
  echo "HATA: ${BIN} yok — once build-app" >&2
  exit 1
fi

rm -f /tmp/telem_pred.csv /tmp/savasan_rival_truth.csv /tmp/savasan_rival_pool.env /tmp/savasan_bench_own.env

export SAVASAN_RUN_SECONDS="${DUR}"
export SAVASAN_SEYIR_TELEM_ONLY=1
export SAVASAN_SEYIR_TX_ENABLE=0
export SAVASAN_ALPAGULINK_ENABLE=0
export SAVASAN_DISPLAY=
export SAVASAN_SEND_ONLY_ON_LOCK=0
export SAVASAN_TELEM_PRED_CSV=/tmp/telem_pred.csv
export SAVASAN_GUIDANCE_MODE=seyir
export SAVASAN_BENCH_OWN_MOTION=1
export SAVASAN_BENCH_OWN_FILE=/tmp/savasan_bench_own.env
export SAVASAN_SEYIR_LEAD_MAX_S=90
export SAVASAN_SEYIR_CRUISE_MPS=25

echo "[telem-test] Fake rakip path=${RIVAL_PATH} (${DUR}s)..."
python3 "${ROOT}/scripts/fake_rival_telemetry.py" \
  --path "${RIVAL_PATH}" \
  --duration "${DUR}" \
  --hz 10 \
  --stale-ms 1200 \
  --weave-amp-m 80 \
  --weave-period-s 14 \
  --speed 14 &
FAKE_PID=$!

sleep 1
echo "[telem-test] Sanal IHA hareketi basliyor (lead test)..."
python3 "${ROOT}/scripts/fake_own_motion.py" \
  --duration "${DUR}" \
  --hz 20 \
  --speed 25 &
OWN_PID=$!

sleep 1
echo "[telem-test] phase5 basliyor (${DUR}s) — TensorRT ilk satirlar gecikebilir, donmedi!"
echo "[telem-test] Log: grep YAKLASMA veya Ctrl+C"

timeout "$((DUR + 20))" "${BIN}" phase5 usb 2>&1 | \
  grep --line-buffered -E 'SEYIR|YAKLASMA|ARAMA|Baglandi|Faz5 calisiyor|lead=' || true

wait "${FAKE_PID}" 2>/dev/null || true
wait "${OWN_PID}" 2>/dev/null || true

if [[ -s /tmp/telem_pred.csv ]]; then
  python3 "${ROOT}/scripts/plot_telemetry_prediction.py" --save /tmp/telem_pred.svg
  echo "[telem-test] Bitti. Grafik: /tmp/telem_pred.svg"
  echo "[telem-test] CSV: /tmp/telem_pred.csv"
  lead_pct=$(awk -F, 'NR>1 && $13>=0.5 {c++} END {if(NR>1) printf "%.0f", 100*c/(NR-1); else print 0}' /tmp/telem_pred.csv)
  echo "[telem-test] lead kullanim: ${lead_pct}%"
else
  echo "[telem-test] UYARI: telem_pred.csv bos — phase5 erken cikmis olabilir" >&2
  exit 2
fi
