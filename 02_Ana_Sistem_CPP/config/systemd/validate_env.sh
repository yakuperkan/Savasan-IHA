#!/usr/bin/env bash
set -euo pipefail

ENV_FILE="${1:-/etc/savasan/savasan.env}"

if [[ ! -f "${ENV_FILE}" ]]; then
  echo "[validate_env] ERROR: env file not found: ${ENV_FILE}" >&2
  exit 1
fi

declare -A KV
while IFS='=' read -r k v; do
  [[ -z "${k}" ]] && continue
  [[ "${k}" =~ ^[[:space:]]*# ]] && continue
  k="$(echo "${k}" | tr -d '[:space:]')"
  KV["${k}"]="${v}"
done < "${ENV_FILE}"

fail() {
  echo "[validate_env] ERROR: $*" >&2
  exit 1
}

expect_non_empty() {
  local key="$1"
  [[ -n "${KV[$key]:-}" ]] || fail "missing ${key}"
}

expect_int_range() {
  local key="$1"
  local min="$2"
  local max="$3"
  local v="${KV[$key]:-}"
  [[ "${v}" =~ ^-?[0-9]+$ ]] || fail "${key} is not integer: ${v:-<empty>}"
  (( v >= min && v <= max )) || fail "${key} out of range [${min},${max}]: ${v}"
}

expect_float_range() {
  local key="$1"
  local min="$2"
  local max="$3"
  local v="${KV[$key]:-}"
  [[ "${v}" =~ ^-?[0-9]+([.][0-9]+)?$ ]] || fail "${key} is not float: ${v:-<empty>}"
  awk -v val="${v}" -v lo="${min}" -v hi="${max}" \
    'BEGIN { if (val < lo || val > hi) exit 1; }' || fail "${key} out of range [${min},${max}]: ${v}"
}

expect_optional_int_range() {
  local key="$1"
  local min="$2"
  local max="$3"
  local v="${KV[$key]:-}"
  [[ -z "${v}" ]] && return 0
  expect_int_range "${key}" "${min}" "${max}"
}

expect_optional_float_range() {
  local key="$1"
  local min="$2"
  local max="$3"
  local v="${KV[$key]:-}"
  [[ -z "${v}" ]] && return 0
  expect_float_range "${key}" "${min}" "${max}"
}

expect_choice() {
  local key="$1"
  shift
  local v="${KV[$key]:-}"
  for opt in "$@"; do
    if [[ "${v}" == "${opt}" ]]; then
      return 0
    fi
  done
  fail "${key} invalid value: ${v:-<empty>} (allowed: $*)"
}

expect_non_empty "SAVASAN_PHASE"
expect_non_empty "SAVASAN_CAMERA"
expect_choice "SAVASAN_PHASE" "phase1" "phase2" "phase3" "phase4" "phase5"
# AIR_LOCK workspace: yarışma görevi yalnızca phase5 ile çalışır.
if [[ "${KV[SAVASAN_PHASE]}" != "phase5" ]]; then
  fail "AIR_LOCK requires SAVASAN_PHASE=phase5 (got: ${KV[SAVASAN_PHASE]})"
fi
expect_choice "SAVASAN_CAMERA" "usb"
camera_profile="${KV[SAVASAN_CAMERA_PROFILE]:-}"
if [[ -n "${camera_profile}" ]]; then
  expect_choice "SAVASAN_CAMERA_PROFILE" "outdoor_sunny" "outdoor_overcast" "hangar_indoor"
  profile_dir="${KV[SAVASAN_CAMERA_PROFILE_DIR]:-/etc/savasan/camera_profiles}"
  profile_file="${profile_dir}/${camera_profile}.env"
  [[ -f "${profile_file}" ]] || fail "camera profile file not found: ${profile_file}"
fi
expect_choice "SAVASAN_LOCK_MODE" "hybrid" "baseline"
expect_int_range "SAVASAN_ALC_BAUD" 9600 2000000
expect_optional_int_range "SAVASAN_ALC_LOCK_PACKET_VERSION" 1 2
nolock_checksum_mode="${KV[SAVASAN_ALC_NOLOCK_CHECKSUM_MODE]:-}"
if [[ -n "${nolock_checksum_mode}" && "${nolock_checksum_mode}" != "xor" && "${nolock_checksum_mode}" != "legacy_ff" ]]; then
  fail "SAVASAN_ALC_NOLOCK_CHECKSUM_MODE invalid value: ${nolock_checksum_mode} (allowed: xor legacy_ff)"
fi
expect_int_range "SAVASAN_CONTROL_HZ" 1 200
expect_int_range "SAVASAN_ALC_MIN_INTERVAL_MS" 1 10000
expect_int_range "SAVASAN_ALC_RECONNECT_INTERVAL_MS" 10 60000
expect_optional_int_range "SAVASAN_USB_RECONNECT_MAX_ATTEMPTS" 1 100
expect_optional_int_range "SAVASAN_USB_RECONNECT_INTERVAL_MS" 200 60000
usb_reconnect="${KV[SAVASAN_USB_RECONNECT]:-1}"
if [[ -n "${usb_reconnect}" && "${usb_reconnect}" != "0" && "${usb_reconnect}" != "1" ]]; then
  fail "SAVASAN_USB_RECONNECT must be 0 or 1 (got ${usb_reconnect})"
fi
expect_optional_float_range "SAVASAN_HEALTHCHECK_MIN_FPS" 1.0 120.0
expect_optional_int_range "SAVASAN_HEALTHCHECK_STATS_STALE_SEC" 3 300
expect_optional_float_range "SAVASAN_GUIDANCE_STANDOFF_M" 1.0 500.0
expect_optional_float_range "SAVASAN_GUIDANCE_DISTANCE_GAIN" 0.01 10.0
expect_optional_float_range "SAVASAN_STATE_TELEMETRY_WEIGHT" 0.0 1.0
expect_optional_float_range "SAVASAN_STATE_VISION_WEIGHT" 0.0 1.0
expect_optional_float_range "SAVASAN_STATE_VELOCITY_ALPHA" 0.0 1.0
expect_optional_float_range "SAVASAN_STATE_INNOVATION_GATE_MPS" 0.0 200.0
expect_optional_float_range "SAVASAN_STATE_YAW_ALPHA" 0.0 1.0
expect_optional_float_range "SAVASAN_STATE_YAW_GATE_DPS" 0.0 720.0
expect_optional_float_range "SAVASAN_GUIDANCE_PIXEL_TO_DEG" 1.0 360.0
expect_int_range "SAVASAN_GUIDANCE_OCCLUSION_HOLD_MS" 50 5000
expect_optional_float_range "SAVASAN_VISION_NORM_RATE_TO_MPS" 0.0 100.0

occ_mode="${KV[SAVASAN_GUIDANCE_OCCLUSION_MODE]:-hold_last}"
if [[ "${occ_mode}" != "hold_last" && "${occ_mode}" != "hold_zero" ]]; then
  fail "SAVASAN_GUIDANCE_OCCLUSION_MODE invalid value: ${occ_mode} (allowed: hold_last hold_zero)"
fi

for bool_key in SAVASAN_ALC_HEARTBEAT SAVASAN_SEND_ONLY_ON_LOCK SAVASAN_EVASION_ENABLE SAVASAN_SETPOINT_TX_ENABLE SAVASAN_CONTROL_LOG_ENABLE SAVASAN_UDP_ENABLE; do
  v="${KV[$bool_key]:-0}"
  [[ "${v}" == "0" || "${v}" == "1" ]] || fail "${bool_key} must be 0 or 1 (got ${v})"
done

tracker_profile="${KV[SAVASAN_TRACKER_PROFILE]:-default}"
if [[ "${tracker_profile}" != "default" && "${tracker_profile}" != "calm" &&
      "${tracker_profile}" != "aggressive" && "${tracker_profile}" != "auto" ]]; then
  fail "SAVASAN_TRACKER_PROFILE invalid value: ${tracker_profile} (allowed: default calm aggressive auto)"
fi

tracker_auto_restart="${KV[SAVASAN_TRACKER_AUTO_RESTART]:-0}"
[[ "${tracker_auto_restart}" == "0" || "${tracker_auto_restart}" == "1" ]] || \
  fail "SAVASAN_TRACKER_AUTO_RESTART must be 0 or 1 (got ${tracker_auto_restart})"

if [[ "${tracker_profile}" == "auto" ]]; then
  expect_optional_float_range "SAVASAN_AUTO_AGGRESSIVE_SPEED_MPS" 0.1 100.0
  expect_optional_float_range "SAVASAN_AUTO_CALM_SPEED_MPS" 0.0 100.0
  expect_optional_float_range "SAVASAN_AUTO_AGGRESSIVE_JITTER" 0.0 5.0
  expect_optional_float_range "SAVASAN_AUTO_CALM_JITTER" 0.0 5.0
  expect_optional_int_range "SAVASAN_AUTO_RECOMMEND_COOLDOWN_SEC" 1 120
  expect_optional_int_range "SAVASAN_AUTO_WATCH_LOG_INTERVAL_SEC" 1 120
  expect_optional_int_range "SAVASAN_AUTO_RESTART_MIN_DWELL_SEC" 1 300
  expect_optional_int_range "SAVASAN_AUTO_RESTART_COOLDOWN_SEC" 1 300
  expect_optional_float_range "SAVASAN_AUTO_PRECHECK_MAX_JITTER" 0.01 5.0
  expect_optional_int_range "SAVASAN_AUTO_PRECHECK_MAX_COMM_FAILS" 0 1000
  expect_optional_int_range "SAVASAN_AUTO_POSTCHECK_TIMEOUT_SEC" 1 120
  expect_optional_int_range "SAVASAN_AUTO_MAX_RESTARTS" 1 100
  expect_optional_int_range "SAVASAN_AUTO_MAX_CONSECUTIVE_FAILS" 1 20
  expect_optional_int_range "SAVASAN_AUTO_MAX_FAILS_IN_WINDOW" 1 50
  expect_optional_int_range "SAVASAN_AUTO_FAIL_WINDOW_SEC" 5 600
fi

if [[ "${KV[SAVASAN_UDP_ENABLE]:-0}" == "1" ]]; then
  expect_non_empty "SAVASAN_UDP_HOST"
  expect_int_range "SAVASAN_UDP_PORT" 1 65535
  expect_choice "SAVASAN_UDP_CODEC" "h264" "h265"
fi

if [[ -n "${KV[SAVASAN_RECORD_ENABLE]:-}" ]]; then
  v="${KV[SAVASAN_RECORD_ENABLE]}"
  [[ "${v}" == "0" || "${v}" == "1" ]] || fail "SAVASAN_RECORD_ENABLE must be 0 or 1 (got ${v})"
fi

mission_mode="${KV[SAVASAN_MISSION_MODE]:-air_lock}"
if [[ "${mission_mode}" != "air_lock" ]]; then
  fail "SAVASAN_MISSION_MODE invalid value: ${mission_mode} (LOCK workspace: only air_lock)"
fi
expect_optional_int_range "SAVASAN_MISSION_MODE_POLL_MS" 50 5000
expect_optional_int_range "SAVASAN_SERVER_TIME_OFFSET_MS" -86400000 86400000
expect_optional_int_range "SAVASAN_SERVER_TIME_OFFSET_POLL_MS" 100 60000

expect_optional_int_range "SAVASAN_COMPETITION_TAKIM_NO" 0 9999
expect_optional_int_range "SAVASAN_COMPETITION_FRAME_W" 1 8192
expect_optional_int_range "SAVASAN_COMPETITION_FRAME_H" 1 8192

echo "[validate_env] OK: ${ENV_FILE}"
