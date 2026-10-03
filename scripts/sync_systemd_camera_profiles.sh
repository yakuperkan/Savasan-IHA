#!/usr/bin/env bash
# Kamera profillerini /etc/savasan/camera_profiles altina kopyalar (systemd/yarisma gunu).
#   sudo ./scripts/sync_systemd_camera_profiles.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="${ROOT}/02_Ana_Sistem_CPP/config/camera_profiles"
DST="/etc/savasan/camera_profiles"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "sudo ile calistirin: sudo $0" >&2
  exit 1
fi

mkdir -p "${DST}"
cp -f "${SRC}/"*.env "${DST}/"
chmod 644 "${DST}/"*.env
echo "Kamera profilleri kopyalandi: ${DST}/"
ls -la "${DST}/"
