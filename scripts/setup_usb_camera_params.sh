#!/bin/bash
# Manuel USB kamera ayari (BRIO). Systemd: setup_usb_camera.sh otomatik calisir.
set -euo pipefail

USB_DEVICE="${1:-/dev/video0}"
export SAVASAN_V4L2_DEVICE="${USB_DEVICE}"

if [[ -n "${2:-}" ]]; then
  export SAVASAN_INGEST_FPS="${2}"
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SETUP="${SCRIPT_DIR}/../02_Ana_Sistem_CPP/config/systemd/setup_usb_camera.sh"
if [[ ! -x "${SETUP}" ]]; then
  chmod +x "${SETUP}" 2>/dev/null || true
fi
exec bash "${SETUP}"
