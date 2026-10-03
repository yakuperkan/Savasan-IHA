#!/usr/bin/env bash
set -euo pipefail

ENV_FILE="${1:-/etc/savasan/savasan.env}"
BACKUP_ROOT="${2:-/var/backups/savasan}"
TARGET="${3:-latest}"

if [[ "${TARGET}" == "latest" ]]; then
  src="${BACKUP_ROOT}/latest.env"
else
  src="${BACKUP_ROOT}/${TARGET}"
fi

if [[ ! -e "${src}" ]]; then
  echo "[rollback_env] ERROR: backup not found: ${src}" >&2
  exit 1
fi

cp -L "${src}" "${ENV_FILE}"
echo "[rollback_env] Restored ${ENV_FILE} from ${src}"
echo "[rollback_env] Restart service with: sudo systemctl restart savasan-airlock"
