#!/usr/bin/env bash
# Sabit senaryolu kisa kosu: FPS/latency/GPU CSV + awk ozeti.
# Kullanim:
#   ./scripts/telemetry_verify_run.sh [sure_sn] [cikti.csv]
# Ornek:
#   cd .../02_Ana_Sistem_CPP && ./scripts/telemetry_verify_run.sh 120 /tmp/savasan_metrics.csv

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${ROOT}/build/savasan_iha"
SECS="${1:-120}"
OUT="${2:-/tmp/savasan_telemetry_$(date +%Y%m%d_%H%M%S).csv}"

if [[ ! -x "$BIN" ]]; then
  echo "Binary yok: $BIN — once: cd build && cmake .. && cmake --build ." >&2
  exit 1
fi

export NVDS_ENABLE_LATENCY_MEASUREMENT="${NVDS_ENABLE_LATENCY_MEASUREMENT:-1}"
export SAVASAN_TELEMETRY_CSV="$OUT"
export SAVASAN_UDP_ENABLE="${SAVASAN_UDP_ENABLE:-0}"

# Opsiyonel: tam ortam /etc'den
if [[ -f /etc/savasan/savasan.env ]]; then
  set -a
  # shellcheck disable=SC1091
  source /etc/savasan/savasan.env
  set +a
fi

echo "CSV: $OUT"
echo "Sure: ${SECS}s | phase3 usb fakesink (ekran yok)"
echo "---"

set +e
timeout "${SECS}"s "$BIN" phase3 usb fakesink
RC=$?
set -e
if [[ "$RC" -ne 0 && "$RC" -ne 124 ]]; then
  echo "Uyari: cikis kodu $RC" >&2
fi

if [[ ! -f "$OUT" ]]; then
  echo "CSV olusmadi (pipeline telemetri probe calismadi veya dosya yazilamadi)." >&2
  exit 1
fi

echo ""
echo "=== Ozet (nvds_ok=1 satirlari, latency_ms) ==="
awk -F, 'NR==1{next} $4==1 && $5>=0 {n++;s+=$5; if($5<m||m==""){m=$5} if($5>M){M=$5}} END{
  if(n>0) printf "nvds_ornek=%d  avg_ms=%.2f  min_ms=%.2f  max_ms=%.2f\n", n, s/n, m, M;
  else print "nvds_ok=1 satir yok veya latency -1";
}' "$OUT"

echo ""
echo "=== Ozet (tum satirlar, latency_ms>-0.5) ==="
awk -F, 'NR==1{next} $5>=0 {n++;s+=$5; if($5<m||m==""){m=$5} if($5>M){M=$5}} END{
  if(n>0) printf "n=%d  avg_ms=%.2f  min_ms=%.2f  max_ms=%.2f\n", n, s/n, m, M;
  else print "veri yok";
}' "$OUT"

echo ""
echo "Ilk 5 veri satiri:"
head -6 "$OUT"
