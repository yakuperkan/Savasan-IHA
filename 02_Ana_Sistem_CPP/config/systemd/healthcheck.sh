#!/usr/bin/env bash
set -euo pipefail

SERVICE="${1:-savasan-airlock.service}"
ENV_FILE="${2:-/etc/savasan/savasan.env}"
VALIDATE_SCRIPT="${3:-/usr/local/bin/savasan-validate-env.sh}"
MAINTENANCE_FLAG="${4:-/etc/savasan/maintenance.flag}"
STATS_FILE="${5:-/tmp/savasan_pipeline_stats.env}"

timestamp="$(date -Iseconds)"

if [[ ! -f "${ENV_FILE}" ]]; then
  echo "[${timestamp}] [healthcheck] CRITICAL: env file missing: ${ENV_FILE}" >&2
  exit 2
fi

# Env dosyasından yalnızca atanmış anahtarları oku (source side-effect yok).
declare -A KV
while IFS='=' read -r k v; do
  [[ -z "${k}" ]] && continue
  [[ "${k}" =~ ^[[:space:]]*# ]] && continue
  k="$(echo "${k}" | tr -d '[:space:]')"
  KV["${k}"]="${v}"
done < "${ENV_FILE}"

if ! systemctl is-enabled --quiet "${SERVICE}"; then
  echo "[${timestamp}] [healthcheck] WARN: service disabled: ${SERVICE}" >&2
fi

if [[ -x "${VALIDATE_SCRIPT}" ]]; then
  if ! "${VALIDATE_SCRIPT}" "${ENV_FILE}" >/dev/null; then
    echo "[${timestamp}] [healthcheck] CRITICAL: env validation failed, skip restart" >&2
    exit 3
  fi
fi

if [[ -f "${MAINTENANCE_FLAG}" ]]; then
  echo "[${timestamp}] [healthcheck] INFO: maintenance mode active, restart skipped (${MAINTENANCE_FLAG})"
  exit 0
fi

if ! systemctl is-active --quiet "${SERVICE}"; then
  auto_restart="${KV[SAVASAN_HEALTHCHECK_AUTO_RESTART]:-0}"
  if [[ "${auto_restart}" == "1" ]]; then
    echo "[${timestamp}] [healthcheck] CRITICAL: service inactive, attempting restart: ${SERVICE}" >&2
    systemctl restart "${SERVICE}"
    sleep 2
    if ! systemctl is-active --quiet "${SERVICE}"; then
      echo "[${timestamp}] [healthcheck] FATAL: restart failed: ${SERVICE}" >&2
      exit 4
    fi
    echo "[${timestamp}] [healthcheck] INFO: restart successful: ${SERVICE}"
    exit 0
  fi
  echo "[${timestamp}] [healthcheck] WARN: service inactive (auto-restart kapali; elle systemctl start)" >&2
  exit 0
fi

# Pipeline sessiz telemetri (ds_app /tmp/savasan_pipeline_stats.env)
pipeline_warn=0
if [[ -f "${STATS_FILE}" ]]; then
  inst_fps="$(grep -m1 '^inst_fps=' "${STATS_FILE}" 2>/dev/null | cut -d= -f2- || true)"
  avg_fps="$(grep -m1 '^avg_fps=' "${STATS_FILE}" 2>/dev/null | cut -d= -f2- || true)"
  gpu_pct="$(grep -m1 '^gpu_pct=' "${STATS_FILE}" 2>/dev/null | cut -d= -f2- || true)"
  stats_mtime="$(stat -c %Y "${STATS_FILE}" 2>/dev/null || echo 0)"
  now_sec="$(date +%s)"
  stats_age_sec=$(( now_sec - stats_mtime ))
  stale_sec="${KV[SAVASAN_HEALTHCHECK_STATS_STALE_SEC]:-15}"
  min_fps="${KV[SAVASAN_HEALTHCHECK_MIN_FPS]:-8}"

  if [[ "${stats_age_sec}" -gt "${stale_sec}" ]]; then
    echo "[${timestamp}] [healthcheck] WARN: pipeline stats stale (${stats_age_sec}s > ${stale_sec}s): ${STATS_FILE}" >&2
    pipeline_warn=1
  elif [[ -n "${inst_fps}" ]] && awk -v fps="${inst_fps}" -v min="${min_fps}" 'BEGIN { exit !(fps < min) }'; then
    echo "[${timestamp}] [healthcheck] WARN: low pipeline FPS inst_fps=${inst_fps} < min=${min_fps} (avg=${avg_fps:-?} gpu=${gpu_pct:-?}%)" >&2
    pipeline_warn=1
  else
    echo "[${timestamp}] [healthcheck] PIPELINE: inst_fps=${inst_fps:-?} avg_fps=${avg_fps:-?} gpu=${gpu_pct:-?}% age=${stats_age_sec}s"
  fi
else
  stale_sec="${KV[SAVASAN_HEALTHCHECK_STATS_STALE_SEC]:-15}"
  echo "[${timestamp}] [healthcheck] WARN: pipeline stats missing: ${STATS_FILE} (servis aktif ama telemetri yok)" >&2
  pipeline_warn=1
fi

if [[ "${pipeline_warn}" -eq 1 ]]; then
  echo "[${timestamp}] [healthcheck] OK: ${SERVICE} active (pipeline uyari — servis calisiyor)" >&2
  exit 0
fi

echo "[${timestamp}] [healthcheck] OK: ${SERVICE} active"
exit 0
