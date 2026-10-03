#!/usr/bin/env bash
# Ortak USB kamera profili: SAVASAN_CAMERA_PROFILE .env source + v4l2 (setup_usb_camera.sh)
# savasan-run.sh ve savasan-run-kamikaze-flight.sh tarafindan source edilir.
# Onceden savasan.env / kamikaze.env source edilmis olmali.

apply_usb_camera_profile() {
  local tag="${1:-savasan-run}"
  local workspace_root="${SAVASAN_WORKSPACE_ROOT:-/home/nvidia/Savasan_IHA_Workspace}"

  if [[ -n "${SAVASAN_CAMERA_PROFILE:-}" ]]; then
    local profile_dir
    if [[ -n "${SAVASAN_CAMERA_PROFILE_DIR:-}" ]]; then
      profile_dir="${SAVASAN_CAMERA_PROFILE_DIR}"
    elif [[ -d /etc/savasan/camera_profiles ]]; then
      profile_dir="/etc/savasan/camera_profiles"
    else
      profile_dir="${workspace_root}/02_Ana_Sistem_CPP/config/camera_profiles"
    fi
    local profile_file="${profile_dir}/${SAVASAN_CAMERA_PROFILE}.env"
    if [[ -f "${profile_file}" ]]; then
      set -a
      # shellcheck disable=SC1090
      source "${profile_file}"
      set +a
      echo "[${tag}] Kamera profili: ${SAVASAN_CAMERA_PROFILE} (${profile_file})" >&2
    else
      echo "[${tag}] UYARI: Kamera profili bulunamadi: ${profile_file}" >&2
    fi
  fi

  if [[ "${SAVASAN_CAMERA:-usb}" == "usb" ]]; then
    local setup_usb_script="${SAVASAN_SETUP_USB_CAMERA_SCRIPT:-/usr/local/bin/savasan-setup-usb-camera.sh}"
    if [[ ! -x "${setup_usb_script}" ]]; then
      setup_usb_script="${workspace_root}/02_Ana_Sistem_CPP/config/systemd/setup_usb_camera.sh"
    fi
    if [[ -x "${setup_usb_script}" ]]; then
      "${setup_usb_script}" || echo "[${tag}] UYARI: USB kamera ayari basarisiz: ${setup_usb_script}" >&2
    else
      echo "[${tag}] UYARI: USB kamera setup scripti bulunamadi" >&2
    fi
  fi
}
