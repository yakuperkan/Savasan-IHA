#!/usr/bin/env bash
# USB BRIO saha kalibrasyonu — trackbar + 3 profil (outdoor_sunny / outdoor_overcast / hangar_indoor).
#
#   ./scripts/camera_tune.sh
#   ./scripts/camera_tune.sh /dev/video0
#
# Pencere: 1/2/3 profil yukle, S kaydet, Q cik
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${SAVASAN_CAMERA_TUNE_BINARY:-${ROOT}/02_Ana_Sistem_CPP/build-app/camera_tune}"
DEVICE="${1:-${SAVASAN_V4L2_DEVICE:-/dev/video0}}"

export SAVASAN_CAMERA_PROFILE_DIR="${SAVASAN_CAMERA_PROFILE_DIR:-${ROOT}/02_Ana_Sistem_CPP/config/camera_profiles}"
export DISPLAY="${DISPLAY:-:0}"

if [[ ! -x "${BIN}" ]]; then
  echo "HATA: ${BIN} yok. Derle:" >&2
  echo "  cd ${ROOT}/02_Ana_Sistem_CPP/build-app && cmake --build . -j\$(nproc) --target camera_tune" >&2
  exit 1
fi

exec "${BIN}" "${DEVICE}"
