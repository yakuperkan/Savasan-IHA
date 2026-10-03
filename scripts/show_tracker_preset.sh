#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
preset_dir="${repo_root}/02_Ana_Sistem_CPP/config/deepstream/presets"

keys=(
  SAVASAN_AUTO_AGGRESSIVE_SPEED_MPS
  SAVASAN_AUTO_CALM_SPEED_MPS
  SAVASAN_AUTO_AGGRESSIVE_JITTER
  SAVASAN_AUTO_CALM_JITTER
  SAVASAN_AUTO_RECOMMEND_COOLDOWN_SEC
  SAVASAN_AUTO_WATCH_LOG_INTERVAL_SEC
  SAVASAN_AUTO_RESTART_MIN_DWELL_SEC
  SAVASAN_AUTO_RESTART_COOLDOWN_SEC
  SAVASAN_AUTO_PRECHECK_MAX_JITTER
  SAVASAN_AUTO_PRECHECK_MAX_COMM_FAILS
  SAVASAN_AUTO_POSTCHECK_TIMEOUT_SEC
  SAVASAN_AUTO_MAX_RESTARTS
  SAVASAN_AUTO_MAX_CONSECUTIVE_FAILS
  SAVASAN_AUTO_MAX_FAILS_IN_WINDOW
  SAVASAN_AUTO_FAIL_WINDOW_SEC
)

default_of() {
  case "$1" in
    SAVASAN_AUTO_AGGRESSIVE_SPEED_MPS) echo "10.0" ;;
    SAVASAN_AUTO_CALM_SPEED_MPS) echo "6.0" ;;
    SAVASAN_AUTO_AGGRESSIVE_JITTER) echo "0.20" ;;
    SAVASAN_AUTO_CALM_JITTER) echo "0.10" ;;
    SAVASAN_AUTO_RECOMMEND_COOLDOWN_SEC) echo "8" ;;
    SAVASAN_AUTO_WATCH_LOG_INTERVAL_SEC) echo "2" ;;
    SAVASAN_AUTO_RESTART_MIN_DWELL_SEC) echo "5" ;;
    SAVASAN_AUTO_RESTART_COOLDOWN_SEC) echo "10" ;;
    SAVASAN_AUTO_PRECHECK_MAX_JITTER) echo "0.30" ;;
    SAVASAN_AUTO_PRECHECK_MAX_COMM_FAILS) echo "1" ;;
    SAVASAN_AUTO_POSTCHECK_TIMEOUT_SEC) echo "5" ;;
    SAVASAN_AUTO_MAX_RESTARTS) echo "5" ;;
    SAVASAN_AUTO_MAX_CONSECUTIVE_FAILS) echo "2" ;;
    SAVASAN_AUTO_MAX_FAILS_IN_WINDOW) echo "3" ;;
    SAVASAN_AUTO_FAIL_WINDOW_SEC) echo "60" ;;
    *) echo "" ;;
  esac
}

effective_value() {
  local k="$1"
  local d
  d="$(default_of "${k}")"
  printf "%s" "${!k:-${d}}"
}

detect_preset() {
  local best_name="UNKNOWN"
  local best_match=0
  local best_total=0

  shopt -s nullglob
  local preset_file
  for preset_file in "${preset_dir}"/*.env; do
    [[ -f "${preset_file}" ]] || continue
    local total=0
    local match=0
    while IFS= read -r line; do
      [[ "${line}" =~ ^export[[:space:]]+SAVASAN_ ]] || continue
      local kv
      kv="${line#export }"
      local key="${kv%%=*}"
      local val="${kv#*=}"
      val="${val%\"}"
      val="${val#\"}"
      total=$((total + 1))
      if [[ "${!key:-}" == "${val}" ]]; then
        match=$((match + 1))
      fi
    done < "${preset_file}"

    if (( match > best_match )); then
      best_match="${match}"
      best_total="${total}"
      best_name="$(basename "${preset_file}" .env)"
    fi
  done
  shopt -u nullglob

  if (( best_total == 0 || best_match == 0 )); then
    printf "UNKNOWN|0|0"
    return
  fi
  printf "%s|%d|%d" "${best_name}" "${best_match}" "${best_total}"
}

line() {
  printf "+----------------------------------------------------------------------------+\n"
}

row() {
  printf "| %-74s |\n" "$1"
}

status_of() {
  local k="$1"
  local eff def
  eff="$(effective_value "${k}")"
  def="$(default_of "${k}")"
  if [[ "${eff}" == "${def}" ]]; then
    printf "default"
  elif [[ -n "${!k:-}" ]]; then
    printf "env"
  else
    printf "default(implicit)"
  fi
}

det="$(detect_preset)"
preset_name="${det%%|*}"
rest="${det#*|}"
match_count="${rest%%|*}"
total_count="${rest##*|}"
match_pct=0
if [[ "${total_count}" -gt 0 ]]; then
  match_pct=$(( (100 * match_count) / total_count ))
fi

tracker_profile="${SAVASAN_TRACKER_PROFILE:-default}"
auto_restart="${SAVASAN_TRACKER_AUTO_RESTART:-0}"
auto_mode_status="NOT READY"
restart_safety_status="NOT READY"
if [[ "${tracker_profile}" == "auto" ]]; then
  auto_mode_status="READY"
fi
if [[ "${tracker_profile}" == "auto" && "${auto_restart}" == "1" ]]; then
  restart_safety_status="READY"
fi
overall_status="CRITICAL"
if [[ "${auto_mode_status}" == "READY" && "${restart_safety_status}" == "READY" && "${match_pct}" -eq 100 ]]; then
  overall_status="OK"
elif [[ "${auto_mode_status}" == "READY" && "${match_pct}" -ge 70 ]]; then
  overall_status="WARN"
fi

line
row "TRACKER AUTO-PROFILE CONFIGURATION"
line
row "Preset Detection: ${preset_name} (match ${match_count}/${total_count})"
row "Tracker Profile:  ${tracker_profile}"
row "Auto-Restart:     ${auto_restart}"
line
row "SPEED THRESHOLDS"
row "  Aggressive: ${SAVASAN_AUTO_AGGRESSIVE_SPEED_MPS:-$(default_of SAVASAN_AUTO_AGGRESSIVE_SPEED_MPS)} mps"
row "  Calm:       ${SAVASAN_AUTO_CALM_SPEED_MPS:-$(default_of SAVASAN_AUTO_CALM_SPEED_MPS)} mps"
line
row "JITTER THRESHOLDS"
row "  Aggressive: ${SAVASAN_AUTO_AGGRESSIVE_JITTER:-$(default_of SAVASAN_AUTO_AGGRESSIVE_JITTER)}"
row "  Calm:       ${SAVASAN_AUTO_CALM_JITTER:-$(default_of SAVASAN_AUTO_CALM_JITTER)}"
line
row "ALL PARAMETERS (effective | default | source)"
line

for k in "${keys[@]}"; do
  eff="$(effective_value "${k}")"
  def="$(default_of "${k}")"
  src="$(status_of "${k}")"
  row "${k} = ${eff} | ${def} | ${src}"
done

line

if [[ "${tracker_profile}" != "auto" ]]; then
  echo "[WARN] TRACKER_PROFILE=${tracker_profile} (auto mode disabled)"
fi
if [[ "${auto_restart}" != "1" ]]; then
  echo "[WARN] TRACKER_AUTO_RESTART=${auto_restart} (restart disabled)"
fi
if [[ "${preset_name}" == "UNKNOWN" ]]; then
  echo "[WARN] Active env does not exactly match a known preset file."
fi

line
row "HEALTH SUMMARY"
line
row "AUTO MODE: ${auto_mode_status}"
row "RESTART SAFETY: ${restart_safety_status}"
row "PRESET MATCH CONFIDENCE: ${match_pct}%"
row "STATUS: ${overall_status}"
line
