#!/usr/bin/env bash
# Phase5: AIR_LOCK kilitlenme duman testi + birim testleri.
#
# Kullanım (repo kökünden):
#   bash scripts/phase5_mission_smoke.sh unit
#   bash scripts/phase5_mission_smoke.sh air_lock
#   bash scripts/phase5_mission_smoke.sh all        # unit + air_lock
#
# Ortam (isteğe bağlı):
#   SAVASAN_CAMERA=usb   SAVASAN_LOCK_MODE=hybrid|baseline
#   SAVASAN_RUN_SECONDS=45   SAVASAN_ENV_FILE=/path/savasan.env
#
# Güvenli masaüstü: varsayılan SAVASAN_ALC_DISABLE=1 (seri yok). Uçuş/ALC ile denemek için:
#   SAVASAN_ALC_DISABLE=0 bash scripts/phase5_mission_smoke.sh air_lock
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="${ROOT}/02_Ana_Sistem_CPP"
BUILD_DIR="${PROJECT}/build"
BIN="${BUILD_DIR}/savasan_iha"
VALIDATE="${PROJECT}/config/systemd/validate_env.sh"
ENV_FILE="${SAVASAN_ENV_FILE:-${PROJECT}/config/systemd/savasan.env}"

usage() {
  echo "Kullanım: $0 [--env PATH] {unit|air_lock|all}" >&2
  exit 1
}

[[ "${1:-}" == "--help" || "${1:-}" == "-h" ]] && usage

if [[ "${1:-}" == "--env" ]]; then
  ENV_FILE="${2:-}"
  shift 2
fi

MODE="${1:-}"
[[ -n "${MODE}" ]] || usage

if [[ ! -x "${BIN}" ]]; then
  echo "[mission-smoke] savasan_iha yok; derleniyor: ${BIN}" >&2
  cmake -S "${PROJECT}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release -DSAVASAN_BUILD_TESTS=ON
  cmake --build "${BUILD_DIR}" -j"$(nproc)" --target savasan_iha
fi

run_unit() {
  echo "=== ctest (unit + phase5 integration) ==="
  ctest --test-dir "${BUILD_DIR}" --output-on-failure \
    -R "test_(server_clock|mission_mode_utils|phase5_mission_policy|lock_selection_policy|phase5_guidance_control_loop_integration|phase5_multithread_stress_integration)"
}

run_validate() {
  if [[ -f "${VALIDATE}" && -f "${ENV_FILE}" ]]; then
    echo "=== validate_env: ${ENV_FILE} ==="
    bash "${VALIDATE}" "${ENV_FILE}"
  else
    echo "[mission-smoke] Uyarı: validate veya env yok, atlanıyor." >&2
  fi
}

# Ortak: kısa süre, seri kapalı (masaüstü), telemetry kapalı patlamasın
base_env() {
  export SAVASAN_PHASE=phase5
  export SAVASAN_RUN_SECONDS="${SAVASAN_RUN_SECONDS:-45}"
  export SAVASAN_ALC_DISABLE="${SAVASAN_ALC_DISABLE:-1}"
  export SAVASAN_SETPOINT_TX_ENABLE="${SAVASAN_SETPOINT_TX_ENABLE:-0}"
  export SAVASAN_UDP_ENABLE=0
  export NVDS_ENABLE_LATENCY_MEASUREMENT="${NVDS_ENABLE_LATENCY_MEASUREMENT:-1}"
}

run_air_lock() {
  echo "=== AIR_LOCK / kilitlenme (YOLO+tracker+lock probe; ALC TX: \$SAVASAN_ALC_DISABLE) ==="
  run_validate
  base_env
  export SAVASAN_MISSION_MODE=air_lock
  local cam="${SAVASAN_CAMERA:-usb}"
  local lm="${SAVASAN_LOCK_MODE:-hybrid}"
  echo "Kamera=${cam} lock_mode=${lm} süre=${SAVASAN_RUN_SECONDS}s — hedef gösterin veya kayıtta lock satırlarını izleyin."
  "${BIN}" phase5 "${cam}" "${lm}"
}

case "${MODE}" in
  unit)
    run_unit
    ;;
  air_lock)
    run_air_lock
    ;;
  all)
    run_unit
    run_air_lock
    ;;
  *)
    usage
    ;;
esac

echo "[mission-smoke] Bitti: ${MODE}"
