#!/usr/bin/env bash
set -euo pipefail

FLAG_PATH="${1:-/etc/savasan/maintenance.flag}"
SERVICE_NAME="${2:-savasan-airlock.service}"
HEALTH_TIMER="${3:-savasan-airlock-healthcheck.timer}"

mkdir -p "$(dirname "${FLAG_PATH}")"
touch "${FLAG_PATH}"
echo "[maintenance-mode] flag created: ${FLAG_PATH}"

systemctl stop "${HEALTH_TIMER}" || true
systemctl disable "${HEALTH_TIMER}" 2>/dev/null || true
systemctl stop "${SERVICE_NAME}" || true

echo "[maintenance-mode] ${SERVICE_NAME} stopped"
echo "[maintenance-mode] ${HEALTH_TIMER} stopped + disabled"
echo "[maintenance-mode] system is now in MAINTENANCE mode"
