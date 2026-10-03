#!/usr/bin/env bash
set -euo pipefail

SERVICE="${1:-savasan-airlock.service}"
ENV_FILE="${2:-/etc/savasan/savasan.env}"
BACKUP_ROOT="${3:-/var/backups/savasan}"
ROLLBACK_TARGET="${4:-latest}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "[recover] Stopping ${SERVICE}..."
systemctl stop "${SERVICE}" || true

echo "[recover] Restoring configuration (${ROLLBACK_TARGET})..."
"${SCRIPT_DIR}/rollback_env.sh" "${ENV_FILE}" "${BACKUP_ROOT}" "${ROLLBACK_TARGET}"

echo "[recover] Validating configuration..."
"${SCRIPT_DIR}/validate_env.sh" "${ENV_FILE}"

echo "[recover] Starting ${SERVICE}..."
systemctl start "${SERVICE}"

echo "[recover] Done. Check with: systemctl status ${SERVICE}"
