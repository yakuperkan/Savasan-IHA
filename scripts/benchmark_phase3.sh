#!/usr/bin/env bash
set -euo pipefail

MODE="${1:-usb}"
DURATION="${2:-120}"
OUT_DIR="${3:-/tmp/savasan_benchmark}"
OUT_FILE="${OUT_DIR}/phase3_${MODE}_$(date +%Y%m%d_%H%M%S).log"

mkdir -p "${OUT_DIR}"

echo "[benchmark] mode=${MODE} duration=${DURATION}s"
echo "[benchmark] output=${OUT_FILE}"

export NVDS_ENABLE_LATENCY_MEASUREMENT=1
export GST_DEBUG="${GST_DEBUG:-1}"
export SAVASAN_RUN_SECONDS="${DURATION}"

/home/nvidia/Savasan_IHA_Workspace/scripts/run_phase3.sh "${MODE}" "${DURATION}" \
  2>&1 | tee "${OUT_FILE}"

echo
echo "[benchmark] summary"
awk '
  /FPS=/ { fps_sum += gensub(/.*FPS=([0-9.]+).*/, "\\1", 1); fps_n++ }
  /latency/ || /Latency/ { lat_n++ }
  END {
    if (fps_n > 0) {
      printf("avg_fps=%.2f samples=%d\n", fps_sum/fps_n, fps_n);
    } else {
      print("avg_fps=n/a samples=0");
    }
    printf("latency_lines=%d\n", lat_n);
  }
' "${OUT_FILE}"

echo "[benchmark] raw log kept at ${OUT_FILE}"
