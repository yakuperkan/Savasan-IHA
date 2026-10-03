#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 ]]; then
  echo "Usage: $0 <baseline_log> <candidate_log> [max_regression_pct]"
  exit 2
fi

BASELINE_LOG="$1"
CANDIDATE_LOG="$2"
MAX_REGRESSION_PCT="${3:-5}"

extract_avg_fps() {
  local file="$1"
  local from_summary
  from_summary="$(awk -F'=' '/avg_fps=/{split($2,a," "); print a[1]}' "$file" | tail -n 1)"
  if [[ -n "${from_summary}" && "${from_summary}" != "n/a" ]]; then
    echo "${from_summary}"
    return 0
  fi
  awk '
    /FPS=/ {
      if (match($0, /FPS=[0-9.]+/)) {
        v=substr($0, RSTART+4, RLENGTH-4);
        sum += v;
        n++;
      }
    }
    END {
      if (n>0) printf "%.4f", sum/n;
    }
  ' "$file"
}

BASELINE_FPS="$(extract_avg_fps "${BASELINE_LOG}")"
CANDIDATE_FPS="$(extract_avg_fps "${CANDIDATE_LOG}")"

if [[ -z "${BASELINE_FPS}" || -z "${CANDIDATE_FPS}" ]]; then
  echo "[perf-regression] Could not compute avg_fps from logs."
  exit 3
fi

DELTA_PCT="$(awk -v b="${BASELINE_FPS}" -v c="${CANDIDATE_FPS}" 'BEGIN { printf "%.2f", ((c-b)/b)*100.0 }')"
ALLOW_MIN="$(awk -v b="${BASELINE_FPS}" -v r="${MAX_REGRESSION_PCT}" 'BEGIN { printf "%.4f", b*(1.0-r/100.0) }')"

echo "[perf-regression] baseline_avg_fps=${BASELINE_FPS}"
echo "[perf-regression] candidate_avg_fps=${CANDIDATE_FPS}"
echo "[perf-regression] delta_pct=${DELTA_PCT}%"
echo "[perf-regression] allowed_min_fps=${ALLOW_MIN} (max_regression=${MAX_REGRESSION_PCT}%)"

awk -v c="${CANDIDATE_FPS}" -v m="${ALLOW_MIN}" 'BEGIN { exit (c+1e-6 < m) ? 1 : 0 }'
echo "[perf-regression] PASS"
