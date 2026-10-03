#!/usr/bin/env bash
# VNC veya yerel masaustunden USB BRIO trackbar kalibrasyonu.
# Servis kamerayi kullaniyorsa durdurur; cikista yeniden baslatma secenegi sunar.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export DISPLAY="${DISPLAY:-:0}"
export SAVASAN_CAMERA_PROFILE_DIR="${SAVASAN_CAMERA_PROFILE_DIR:-${ROOT}/02_Ana_Sistem_CPP/config/camera_profiles}"

SERVICE="savasan-airlock"
WAS_ACTIVE=0

if command -v systemctl >/dev/null 2>&1 && systemctl is-active --quiet "${SERVICE}" 2>/dev/null; then
  WAS_ACTIVE=1
  echo "[camera_tune] ${SERVICE} calisiyor — kamera serbest birakiliyor..."
  sudo systemctl stop "${SERVICE}"
fi

cleanup() {
  if [[ "${WAS_ACTIVE}" -eq 1 ]]; then
    echo ""
    read -r -p "[camera_tune] ${SERVICE} yeniden baslatilsin mi? [E/h] " ans
    if [[ -z "${ans}" || "${ans,,}" == "e" || "${ans,,}" == "y" ]]; then
      sudo systemctl start "${SERVICE}"
      echo "[camera_tune] ${SERVICE} baslatildi."
    else
      echo "[camera_tune] Servis durduruk kaldi. Elle: sudo systemctl start ${SERVICE}"
    fi
  fi
}
trap cleanup EXIT

echo "[camera_tune] DISPLAY=${DISPLAY}"
echo "[camera_tune] 1/2/3 profil | S kaydet | Q cik"
"${ROOT}/scripts/camera_tune.sh" "$@"
