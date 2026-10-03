#!/usr/bin/env bash
# Kamera profilini yukler ve (istege bagli) v4l2 ayarlarini uygular.
#
#   ./scripts/load_camera_profile.sh outdoor_sunny
#   ./scripts/load_camera_profile.sh outdoor_sunny --apply
#
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "Kullanim: $0 <outdoor_sunny|outdoor_overcast|hangar_indoor> [--apply]" >&2
  exit 1
fi

PROFILE="$1"
APPLY=0
if [[ "${2:-}" == "--apply" ]]; then
  APPLY=1
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIR="${SAVASAN_CAMERA_PROFILE_DIR:-${ROOT}/02_Ana_Sistem_CPP/config/camera_profiles}"
FILE="${DIR}/${PROFILE}.env"

if [[ ! -f "${FILE}" ]]; then
  echo "HATA: Profil bulunamadi: ${FILE}" >&2
  exit 1
fi

echo "# source ${FILE}"
set -a
# shellcheck disable=SC1090
source "${FILE}"
set +a

export SAVASAN_CAMERA_PROFILE="${PROFILE}"
export SAVASAN_CAMERA_PROFILE_DIR="${DIR}"
export SAVASAN_V4L2_DEVICE="${SAVASAN_V4L2_DEVICE:-/dev/video0}"

if [[ "${APPLY}" -eq 1 ]]; then
  SETUP="${ROOT}/02_Ana_Sistem_CPP/config/systemd/setup_usb_camera.sh"
  if [[ ! -x "${SETUP}" ]]; then
    chmod +x "${SETUP}" 2>/dev/null || true
  fi
  bash "${SETUP}"
fi

echo "SAVASAN_CAMERA_PROFILE=${PROFILE} yuklendi."
