#!/usr/bin/env bash
set -euo pipefail

# Usage (repo root):
#   source ./scripts/load_tracker_preset.sh stable
#   source ./scripts/load_tracker_preset.sh aggressive_mission
#   source ./scripts/load_tracker_preset.sh noisy_tracking

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  echo "Bu script source edilmelidir:"
  echo "  source ./scripts/load_tracker_preset.sh <stable|aggressive_mission|noisy_tracking>"
  exit 1
fi

if [[ $# -ne 1 ]]; then
  echo "Kullanim: source ./scripts/load_tracker_preset.sh <stable|aggressive_mission|noisy_tracking>"
  return 1
fi

preset_name="$1"
case "${preset_name}" in
  stable|aggressive_mission|noisy_tracking) ;;
  *)
    echo "Gecersiz preset: ${preset_name}"
    echo "Gecerli secenekler: stable, aggressive_mission, noisy_tracking"
    return 1
    ;;
esac

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
preset_path="${repo_root}/02_Ana_Sistem_CPP/config/deepstream/presets/${preset_name}.env"

if [[ ! -f "${preset_path}" ]]; then
  echo "Preset dosyasi bulunamadi: ${preset_path}"
  return 1
fi

# shellcheck disable=SC1090
source "${preset_path}"

required_vars=(
  SAVASAN_TRACKER_PROFILE
  SAVASAN_TRACKER_AUTO_RESTART
  SAVASAN_AUTO_AGGRESSIVE_SPEED_MPS
  SAVASAN_AUTO_CALM_SPEED_MPS
  SAVASAN_AUTO_AGGRESSIVE_JITTER
  SAVASAN_AUTO_CALM_JITTER
  SAVASAN_AUTO_RESTART_MIN_DWELL_SEC
  SAVASAN_AUTO_RESTART_COOLDOWN_SEC
)

for v in "${required_vars[@]}"; do
  if [[ -z "${!v:-}" ]]; then
    echo "Preset dogrulama hatasi: ${v} bos"
    return 1
  fi
done

echo "[PRESET] ${preset_name} yüklendi"
echo "[PRESET] TRACKER_PROFILE=${SAVASAN_TRACKER_PROFILE} AUTO_RESTART=${SAVASAN_TRACKER_AUTO_RESTART}"
