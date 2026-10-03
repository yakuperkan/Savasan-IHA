#!/usr/bin/env bash
set -euo pipefail

# scripts/show_tracker_preset.sh
# Kullanım: ./scripts/show_tracker_preset.sh

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
preset_dir="${repo_root}/02_Ana_Sistem_CPP/config/deepstream/presets"

# Mevcut env değerlerini oku
read_env_var() {
  local var_name="$1"
  local default_value="$2"
  echo "${!var_name:-${default_value}}"
}

# Preset tespiti
detect_preset() {
  local detected="UNKNOWN"
  local best_match_score=0
  
  for preset_file in "${preset_dir}"/*.env; do
    if [[ -f "${preset_file}" ]]; then
      preset_name=$(basename "${preset_file}" .env)
      match_score=0
      
      while IFS='=' read -r key value; do
        if [[ "${key}" =~ ^export\ SAVASAN_AUTO_ ]]; then
          env_key="${key#export }"
          env_value="${!env_key:-}"
          
          if [[ "${value}" == "${env_value}" ]]; then
            ((match_score++))
          fi
        fi
      done < "${preset_file}"
      
      if ((match_score > best_match_score)); then
        best_match_score=${match_score}
        detected="${preset_name}"
      fi
    fi
  done
  
  echo "${detected}"
}

# Tablo çizme fonksiyonu
print_header() {
  printf "╔══════════════════════════════════════════╗\n"
  printf "║   TRACKER AUTO-PROFILE CONFIGURATION      ║\n" 
  printf "╠══════════════════════════════════════════╣\n"
}

print_row() {
  printf "║ %-40s ║\n" "$1"
}

print_separator() {
  printf "╠══════════════════════════════════════════╣\n"
}

# Ana logik
preset_name=$(detect_preset)
auto_restart="${SAVASAN_TRACKER_AUTO_RESTART:-0}"
tracker_profile="${SAVASAN_TRACKER_PROFILE:-default}"

# Tablo çiz
print_header
print_row "Preset:           ${preset_name}"
print_row "Auto-Restart:      ${auto_restart}"
print_separator

print_row "Speed Thresholds:"
print_row "  Aggressive:      ${SAVASAN_AUTO_AGGRESSIVE_SPEED_MPS:-10.0} mps"
print_row "  Calm:           ${SAVASAN_AUTO_CALM_SPEED_MPS:-6.0} mps"
print_separator

print_row "Jitter Thresholds:"
print_row "  Aggressive:      ${SAVASAN_AUTO_AGGRESSIVE_JITTER:-0.20}"
print_row "  Calm:           ${SAVASAN_AUTO_CALM_JITTER:-0.10}"
print_separator

print_row "Timing:"
print_row "  Min Dwell:       ${SAVASAN_AUTO_RESTART_MIN_DWELL_SEC:-5}s"
print_row "  Cooldown:         ${SAVASAN_AUTO_RESTART_COOLDOWN_SEC:-10}s"
print_separator

print_row "Health-Check:"
print_row "  Max Jitter:      ${SAVASAN_AUTO_PRECHECK_MAX_JITTER:-0.30}"
print_row "  Max Comm Fails:  ${SAVASAN_AUTO_MAX_COMM_FAILS:-1}"
print_separator

print_row "Fail-Safe:"
print_row "  Max Restarts:     ${SAVASAN_AUTO_MAX_RESTARTS:-5}"
print_row "  Max Consecutive: ${SAVASAN_AUTO_MAX_CONSECUTIVE_FAILS:-2}"
print_row "  Window Fails:    ${SAVASAN_AUTO_MAX_FAILS_IN_WINDOW:-3}/${SAVASAN_AUTO_FAIL_WINDOW_SEC:-60}s"
printf "╚══════════════════════════════════════════╝\n"

# Mod kontrol
if [[ "${tracker_profile}" != "auto" ]]; then
  echo "[WARN] TRACKER_PROFILE=${tracker_profile} (auto modu aktif değil)"
fi

if [[ "${auto_restart}" != "1" ]]; then
  echo "[WARN] AUTO_RESTART=${auto_restart} (restart devre dışı)"
fi
