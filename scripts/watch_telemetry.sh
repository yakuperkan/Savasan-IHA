#!/usr/bin/env bash
# Savasan telemetri izleme: FPS, latency, GPU, bilesen profiler (sudo gerekmez).
#
# Systemd ile calisirken (servis zaten acik):
#   bash scripts/watch_telemetry.sh
#   sudo bash scripts/start_savasan_service.sh   # baslat + telemetri ayni terminalde
#
# Elle calistirma (stdout):
#   bash scripts/watch_telemetry.sh --follow-process
#
# Ortam:
#   SAVASAN_LOG=/var/log/savasan/service.log
#   SAVASAN_ERR_LOG=/var/log/savasan/service.err.log
#   SAVASAN_TELEMETRY_CSV=/var/log/savasan/telemetry.csv
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG="${SAVASAN_LOG:-/var/log/savasan/service.log}"
ERR_LOG="${SAVASAN_ERR_LOG:-/var/log/savasan/service.err.log}"
CSV="${SAVASAN_TELEMETRY_CSV:-}"

MODE="telemetry"
if [[ "${1:-}" == "--all" ]]; then
  MODE="all"
elif [[ "${1:-}" == "--errors" ]]; then
  MODE="errors"
elif [[ "${1:-}" == "--follow-process" ]]; then
  MODE="process"
elif [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  cat <<'EOF'
Kullanim:
  bash scripts/watch_telemetry.sh              # FPS/latency/GPU/profiler (varsayilan)
  bash scripts/watch_telemetry.sh --all        # Tum service.log satirlari
  bash scripts/watch_telemetry.sh --errors     # service.err.log
  bash scripts/watch_telemetry.sh --follow-process  # savasan_iha stdout (systemd disi)

Ortam: SAVASAN_LOG, SAVASAN_ERR_LOG, SAVASAN_TELEMETRY_CSV
EOF
  exit 0
fi

TELEMETRY_GREP='TELEMETR|LATENCY_PROFILER|UYARI.*GPU|PROFILE_METRICS|Ingest PTS|Probe aktif|GStreamer HATA'

print_header() {
  echo "=== Savasan telemetri izleme $(date '+%Y-%m-%d %H:%M:%S') ==="
  echo "log: ${LOG}"
  if [[ -f "${ERR_LOG}" ]]; then
    echo "err: ${ERR_LOG}"
  fi
  if [[ -n "${CSV}" && -f "${CSV}" ]]; then
    echo "csv: ${CSV}"
  fi
  if systemctl is-active --quiet savasan-airlock 2>/dev/null; then
    echo "servis: savasan-airlock AKTIF"
  else
    echo "servis: savasan-airlock kapali (elle calisiyorsan --follow-process)"
  fi
  echo "Ctrl+C ile cik | Filtre: FPS, Latency, GPU, LATENCY_PROFILER"
  echo "----------------------------------------"
}

tail_log() {
  local file="$1"
  local filter="${2:-}"
  if [[ ! -f "${file}" ]]; then
    echo "[watch] Dosya yok: ${file}" >&2
    echo "  Servis baslat: sudo systemctl start savasan-airlock" >&2
    return 1
  fi
  if [[ ! -r "${file}" ]]; then
    echo "[watch] Okuma izni yok: ${file} (sudo tail -f deneyin)" >&2
    return 1
  fi
  if [[ -n "${filter}" ]]; then
    tail -n 30 -f "${file}" | grep --line-buffered -E "${filter}" || true
  else
    tail -n 30 -f "${file}"
  fi
}

case "${MODE}" in
  telemetry)
    print_header
    if [[ -n "${CSV}" && -f "${CSV}" ]]; then
      echo "[csv son satir] $(tail -1 "${CSV}")"
      echo "----------------------------------------"
    fi
    tail_log "${LOG}" "${TELEMETRY_GREP}"
    ;;
  all)
    print_header
    tail_log "${LOG}" ""
    ;;
  errors)
    print_header
    tail_log "${ERR_LOG}" ""
    ;;
  process)
    echo "=== savasan_iha process stdout (pipeline loglari) ==="
    if ! pidof savasan_iha >/dev/null 2>&1; then
      echo "savasan_iha calismiyor." >&2
      exit 1
    fi
    # systemd disi calistirmada dogrudan terminal ciktisini kullanin; bu mod bilgi verir.
    echo "Process PID: $(pidof savasan_iha)"
    echo "Systemd modunda: bash scripts/watch_telemetry.sh"
    journalctl -u savasan-airlock -f --no-pager 2>/dev/null || tail_log "${LOG}" "${TELEMETRY_GREP}"
    ;;
esac
