#!/usr/bin/env bash
set -euo pipefail

ENV_FILE="${1:-/etc/savasan/savasan.env}"
BACKUP_ROOT="${2:-/var/backups/savasan}"

if [[ ! -f "${ENV_FILE}" ]]; then
  echo "[backup_env] ERROR: env file not found: ${ENV_FILE}" >&2
  exit 1
fi

timestamp="$(date +%Y%m%d_%H%M%S)"
mkdir -p "${BACKUP_ROOT}"
backup_file="${BACKUP_ROOT}/savasan.env.${timestamp}"
cp "${ENV_FILE}" "${backup_file}"
ln -sfn "${backup_file}" "${BACKUP_ROOT}/latest.env"

echo "[backup_env] OK: ${backup_file}"
