#!/usr/bin/env bash
set -euo pipefail

FLAG_PATH="${1:-/etc/savasan/maintenance.flag}"
SERVICE_NAME="${2:-savasan-airlock.service}"
HEALTH_TIMER="${3:-savasan-airlock-healthcheck.timer}"

rm -f "${FLAG_PATH}"
echo "[flight-mode] flag removed: ${FLAG_PATH}"

systemctl enable --now "${HEALTH_TIMER}"
systemctl start "${SERVICE_NAME}"

echo "[flight-mode] ${HEALTH_TIMER} enabled+started"
echo "[flight-mode] ${SERVICE_NAME} started (AlpaguLink ayni unit icinde)"
echo "[flight-mode] system is now in FLIGHT/TEST mode"
