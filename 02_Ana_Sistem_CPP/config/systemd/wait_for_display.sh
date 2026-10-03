#!/usr/bin/env bash
# GNOME :0 ve Xauthority hazir olana kadar bekle (systemd ExecStartPre).
set -euo pipefail

DISPLAY_NAME="${2:-:0}"
MAX_SEC="${3:-300}"
export DISPLAY="${DISPLAY_NAME}"

try_xauth() {
  local path="$1"
  [[ -f "${path}" ]] || return 1
  export XAUTHORITY="${path}"
  xdpyinfo -display "${DISPLAY_NAME}" >/dev/null 2>&1
}

for ((i = 0; i < MAX_SEC; i++)); do
  for auth in \
    /run/user/1000/gdm/Xauthority \
    /home/nvidia/.Xauthority \
    "${XAUTHORITY:-}"; do
    [[ -n "${auth}" ]] || continue
    if try_xauth "${auth}"; then
      exit 0
    fi
  done
  sleep 1
done

echo "[wait_for_display] timeout ${MAX_SEC}s (DISPLAY=${DISPLAY_NAME})" >&2
exit 1
