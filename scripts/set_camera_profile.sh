#!/usr/bin/env bash
# Kamera profili sec -> ilgili env (airlock VEYA kamikaze) guncelle.
# systemd servisi BASLATMAZ/DURDURMAZ; gerekirse kullanici manuel restart eder.
#
# AIR_LOCK ve Kamikaze ayri env dosyalari; ortak olan yalnizca camera_profiles/*.env
#
#   ./scripts/set_camera_profile.sh sunny                    # AIR_LOCK (savasan.env)
#   ./scripts/set_camera_profile.sh --kamikaze sunny         # Kamikaze (kamikaze.env)
#   ./scripts/set_camera_profile.sh --both sunny             # ikisi birden
#   ./scripts/set_camera_profile.sh status
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WS_ENV="${ROOT}/02_Ana_Sistem_CPP/config/systemd/savasan.env"
ETC_ENV="/etc/savasan/savasan.env"
KAMIKAZE_WS_ENV="/home/nvidia/Savasan_IHA_Workspace_KAMIKAZE/02_Ana_Sistem_CPP/config/systemd/kamikaze.env"
KAMIKAZE_ETC_ENV="/etc/savasan/kamikaze.env"
KAMIKAZE_SYSTEMD="/home/nvidia/Savasan_IHA_Workspace_KAMIKAZE/02_Ana_Sistem_CPP/config/systemd"
PROFILE_DIR_WS="${ROOT}/02_Ana_Sistem_CPP/config/camera_profiles"
PROFILE_DIR_ETC="/etc/savasan/camera_profiles"
TARGET="airlock"

resolve_profile() {
  case "${1,,}" in
    outdoor_sunny | sunny | sun | gunes) echo "outdoor_sunny" ;;
    outdoor_overcast | overcast | cloudy | bulut) echo "outdoor_overcast" ;;
    hangar_indoor | hangar | indoor) echo "hangar_indoor" ;;
    "") echo "" ;;
    *) echo "" ;;
  esac
}

read_profile_from_env_file() {
  local file="$1"
  [[ -f "${file}" ]] || return 1
  local line
  line="$(grep -E '^SAVASAN_CAMERA_PROFILE=' "${file}" | tail -1 || true)"
  [[ -n "${line}" ]] || return 1
  resolve_profile "${line#SAVASAN_CAMERA_PROFILE=}" || echo "${line#SAVASAN_CAMERA_PROFILE=}"
}

set_env_profile_line() {
  local file="$1"
  local profile="$2"
  [[ -f "${file}" ]] || return 0
  local tmp
  tmp="$(mktemp)"
  grep -v '^SAVASAN_CAMERA_PROFILE=' "${file}" | grep -v '^SAVASAN_CAMERA_PROFILE_DIR=' > "${tmp}" || true
  {
    cat "${tmp}"
    printf 'SAVASAN_CAMERA_PROFILE=%s\n' "${profile}"
    printf 'SAVASAN_CAMERA_PROFILE_DIR=%s\n' "${PROFILE_DIR_ETC}"
  } > "${file}"
  rm -f "${tmp}"
}

update_env_profile() {
  local file="$1"
  local profile="$2"
  local label="$3"
  [[ -f "${file}" ]] || return 0
  local etc_dir
  etc_dir="$(dirname "${file}")"
  if [[ -w "${file}" ]] && [[ -w "${etc_dir}" ]]; then
    set_env_profile_line "${file}" "${profile}"
    echo "[profil] Guncellendi: ${file}"
    return 0
  fi
  local tmp
  tmp="$(mktemp)"
  cp "${file}" "${tmp}"
  set_env_profile_line "${tmp}" "${profile}"
  if run_sudo cp "${tmp}" "${file}"; then
    echo "[profil] Guncellendi (sudo): ${file}"
  else
    echo "[profil] UYARI: ${label} yazilamadi (sudo sifre?) — diger env dosyalari guncellendi." >&2
  fi
  rm -f "${tmp}"
}

read_v4l2_device_for_target() {
  local target="$1"
  local f line
  if [[ "${target}" == "kamikaze" ]]; then
    for f in "${KAMIKAZE_ETC_ENV}" "${KAMIKAZE_WS_ENV}"; do
      [[ -f "${f}" ]] || continue
      line="$(grep -E '^SAVASAN_V4L2_DEVICE=' "${f}" | tail -1 || true)"
      if [[ -n "${line}" ]]; then
        echo "${line#SAVASAN_V4L2_DEVICE=}"
        return 0
      fi
    done
  else
    for f in "${ETC_ENV}" "${WS_ENV}"; do
      [[ -f "${f}" ]] || continue
      line="$(grep -E '^SAVASAN_V4L2_DEVICE=' "${f}" | tail -1 || true)"
      if [[ -n "${line}" ]]; then
        echo "${line#SAVASAN_V4L2_DEVICE=}"
        return 0
      fi
    done
  fi
  echo "/dev/video0"
}

run_sudo() {
  if [[ "$(id -u)" -eq 0 ]]; then
    "$@"
  elif command -v sudo >/dev/null 2>&1; then
    sudo "$@"
  else
    return 1
  fi
}

sync_etc_profiles() {
  if [[ ! -d "${PROFILE_DIR_ETC}" ]] || ! diff -q "${PROFILE_DIR_WS}/outdoor_sunny.env" "${PROFILE_DIR_ETC}/outdoor_sunny.env" >/dev/null 2>&1; then
    echo "[profil] /etc/savasan/camera_profiles senkronlaniyor..."
    if ! run_sudo mkdir -p "${PROFILE_DIR_ETC}"; then
      echo "[profil] UYARI: /etc profil dizini yazilamadi; workspace profilleri kullanilacak." >&2
      return 1
    fi
    run_sudo cp -f "${PROFILE_DIR_WS}/"*.env "${PROFILE_DIR_ETC}/"
    run_sudo chmod 644 "${PROFILE_DIR_ETC}/"*.env 2>/dev/null || true
  fi
  return 0
}

install_systemd_scripts_if_needed() {
  local run_ws="${ROOT}/02_Ana_Sistem_CPP/config/systemd/savasan-run.sh"
  local setup_ws="${ROOT}/02_Ana_Sistem_CPP/config/systemd/setup_usb_camera.sh"
  local apply_ws="${ROOT}/02_Ana_Sistem_CPP/config/systemd/apply_usb_camera_profile.sh"
  local kamikaze_ws="${ROOT}/02_Ana_Sistem_CPP/config/systemd/savasan-run-kamikaze-flight.sh"
  local run_etc="/usr/local/bin/savasan-run.sh"
  local setup_etc="/usr/local/bin/savasan-setup-usb-camera.sh"
  local apply_etc="/usr/local/bin/savasan-apply-camera-profile.sh"
  local kamikaze_etc="/usr/local/bin/savasan-run-kamikaze-flight.sh"
  if [[ ! -f "${run_etc}" ]] || ! diff -q "${run_ws}" "${run_etc}" >/dev/null 2>&1; then
    echo "[profil] savasan-run.sh guncelleniyor..."
    run_sudo install -m 0755 "${run_ws}" "${run_etc}" || echo "[profil] UYARI: savasan-run.sh kurulamadi." >&2
  fi
  if [[ ! -f "${setup_etc}" ]] || ! diff -q "${setup_ws}" "${setup_etc}" >/dev/null 2>&1; then
    echo "[profil] setup_usb_camera.sh guncelleniyor..."
    run_sudo install -m 0755 "${setup_ws}" "${setup_etc}" || echo "[profil] UYARI: setup_usb_camera kurulamadi." >&2
  fi
  if [[ ! -f "${apply_etc}" ]] || ! diff -q "${apply_ws}" "${apply_etc}" >/dev/null 2>&1; then
    echo "[profil] apply_usb_camera_profile.sh guncelleniyor..."
    run_sudo install -m 0755 "${apply_ws}" "${apply_etc}" || echo "[profil] UYARI: apply script kurulamadi." >&2
  fi
  if [[ -f "${kamikaze_ws}" ]] && { [[ ! -f "${kamikaze_etc}" ]] || ! diff -q "${kamikaze_ws}" "${kamikaze_etc}" >/dev/null 2>&1; }; then
    echo "[profil] savasan-run-kamikaze-flight.sh guncelleniyor..."
    run_sudo install -m 0755 "${kamikaze_ws}" "${kamikaze_etc}" || echo "[profil] UYARI: kamikaze flight script kurulamadi." >&2
  fi
}

apply_v4l2() {
  local profile="$1"
  local device_target="$2"
  local dir="${PROFILE_DIR_ETC}"
  local device
  device="$(read_v4l2_device_for_target "${device_target}")"
  if [[ ! -f "${dir}/${profile}.env" ]]; then
    dir="${PROFILE_DIR_WS}"
  fi
  export SAVASAN_CAMERA_PROFILE_DIR="${dir}"
  export SAVASAN_V4L2_DEVICE="${device}"
  "${ROOT}/scripts/load_camera_profile.sh" "${profile}" --apply
}

show_v4l2() {
  local device="$1"
  echo ""
  echo "=== Kamera (${device}) ==="
  v4l2-ctl --device="${device}" \
    --get-ctrl=gain,exposure_time_absolute,white_balance_temperature,brightness,contrast,saturation 2>/dev/null \
    || echo "(v4l2 okunamadi — kamera bagli mi?)"
}

show_env_profile() {
  local label="$1"
  local ws_file="$2"
  local etc_file="$3"
  local ws_val etc_val
  ws_val="$(read_profile_from_env_file "${ws_file}" 2>/dev/null || echo '<yok>')"
  etc_val="$(read_profile_from_env_file "${etc_file}" 2>/dev/null || echo '<yok>')"
  echo "  ${label}:"
  echo "    workspace: ${ws_val}  (${ws_file})"
  echo "    systemd:   ${etc_val}  (${etc_file})"
  if [[ "${ws_val}" != "${etc_val}" ]]; then
    echo "    UYARI: workspace ve /etc farkli — sudo ile ilgili camera_profile komutunu tekrar calistir" >&2
  fi
}

show_status() {
  echo "=== Kamera profil durumu (ortak dosyalar: ${PROFILE_DIR_ETC}) ==="
  show_env_profile "AIR_LOCK" "${WS_ENV}" "${ETC_ENV}"
  show_env_profile "Kamikaze" "${KAMIKAZE_WS_ENV}" "${KAMIKAZE_ETC_ENV}"
  echo ""
  echo "Run script kamikaze profil destegi:"
  if grep -q 'apply_usb_camera_profile' /usr/local/bin/savasan-run-kamikaze-flight.sh 2>/dev/null; then
    echo "  savasan-run-kamikaze-flight.sh: OK"
  else
    echo "  savasan-run-kamikaze-flight.sh: ESKI (sudo ./camera_profile_kamikaze sunny ile kur)" >&2
  fi
  show_v4l2 "$(read_v4l2_device_for_target kamikaze)"
}

service_is_active() {
  local unit="$1"
  command -v systemctl >/dev/null 2>&1 && systemctl is-active --quiet "${unit}" 2>/dev/null
}

any_target_service_active() {
  case "${TARGET}" in
    airlock) service_is_active savasan-airlock ;;
    kamikaze) service_is_active savasan-kamikaze ;;
    both) service_is_active savasan-airlock || service_is_active savasan-kamikaze ;;
    *) return 1 ;;
  esac
}

# --- arg parse ---
PROFILE_ARGS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --airlock | -a) TARGET="airlock"; shift ;;
    --kamikaze | -k) TARGET="kamikaze"; shift ;;
    --both | -b) TARGET="both"; shift ;;
    list | ls)
      echo "Kamera profilleri (${PROFILE_DIR_WS}):"
      for f in "${PROFILE_DIR_WS}"/*.env; do
        echo "  $(basename "${f}" .env)"
      done
      echo ""
      echo "AIR_LOCK:  ./camera_profile sunny"
      echo "Kamikaze:  ./camera_profile_kamikaze sunny"
      echo "Ikisi:     ./camera_profile --both sunny"
      echo "Durum:     ./camera_profile status"
      exit 0
      ;;
    status) show_status; exit 0 ;;
    *)
      PROFILE_ARGS+=("$1")
      shift
      ;;
  esac
done

PROFILE=""
if [[ ${#PROFILE_ARGS[@]} -ge 1 ]]; then
  PROFILE="$(resolve_profile "${PROFILE_ARGS[0]}")"
  [[ -n "${PROFILE}" ]] || {
    echo "HATA: Bilinmeyen profil: ${PROFILE_ARGS[0]}" >&2
    exit 1
  }
else
  case "${TARGET}" in
    kamikaze) PROFILE="$(read_profile_from_env_file "${KAMIKAZE_WS_ENV}" || true)" ;;
    both)
      PROFILE="$(read_profile_from_env_file "${WS_ENV}" || read_profile_from_env_file "${KAMIKAZE_WS_ENV}" || true)"
      ;;
    *) PROFILE="$(read_profile_from_env_file "${WS_ENV}" || true)" ;;
  esac
  [[ -n "${PROFILE}" ]] || {
    echo "Kullanim: $0 [--airlock|--kamikaze|--both] <sunny|overcast|hangar>" >&2
    exit 1
  }
fi

[[ -f "${PROFILE_DIR_WS}/${PROFILE}.env" ]] || {
  echo "HATA: Profil dosyasi yok: ${PROFILE_DIR_WS}/${PROFILE}.env" >&2
  exit 1
}

echo "[profil] Secilen: ${PROFILE} (hedef: ${TARGET})"

update_airlock_env() {
  set_env_profile_line "${WS_ENV}" "${PROFILE}"
  echo "[profil] Guncellendi: ${WS_ENV}"
  update_env_profile "${ETC_ENV}" "${PROFILE}" "/etc/savasan/savasan.env"
}

update_kamikaze_env() {
  update_env_profile "${KAMIKAZE_WS_ENV}" "${PROFILE}" "kamikaze workspace env"
  update_env_profile "${KAMIKAZE_ETC_ENV}" "${PROFILE}" "/etc/savasan/kamikaze.env"
}

case "${TARGET}" in
  airlock) update_airlock_env ;;
  kamikaze) update_kamikaze_env ;;
  both) update_airlock_env; update_kamikaze_env ;;
esac

sync_etc_profiles || true
install_systemd_scripts_if_needed || true

if any_target_service_active; then
  echo "[profil] Servis calisiyor — v4l2 atlandi (kamera mesgul). Env guncellendi; restart sonrasi uygulanir."
else
  case "${TARGET}" in
    kamikaze) apply_v4l2 "${PROFILE}" kamikaze ;;
    airlock) apply_v4l2 "${PROFILE}" airlock ;;
    both) apply_v4l2 "${PROFILE}" kamikaze ;;
  esac
fi

echo ""
echo "[profil] Tamam: ${PROFILE} (${TARGET})"
case "${TARGET}" in
  airlock) echo "  Servisi elle baslat: sudo systemctl restart savasan-airlock" ;;
  kamikaze) echo "  Servisi elle baslat: sudo systemctl restart savasan-kamikaze" ;;
  both)
    echo "  Servisleri elle baslat:"
    echo "    sudo systemctl restart savasan-airlock"
    echo "    sudo systemctl restart savasan-kamikaze"
    ;;
esac
show_v4l2 "$(read_v4l2_device_for_target "$([[ "${TARGET}" == airlock ]] && echo airlock || echo kamikaze)")"
