#!/usr/bin/env bash
# Savasan IHA systemd servisi kurulum scripti.
# Kullanim: sudo bash install.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVICE_NAME="savasan-airlock.service"
HEALTH_SERVICE="savasan-airlock-healthcheck.service"
HEALTH_TIMER="savasan-airlock-healthcheck.timer"
COREDUMP_SERVICE="savasan-airlock-coredump-collect.service"
COREDUMP_TIMER="savasan-airlock-coredump-collect.timer"
LOGROTATE_NAME="savasan-airlock"
LEGACY_SERVICE="savasan-iha.service"
LEGACY_HEALTH_SERVICE="savasan-healthcheck.service"
LEGACY_HEALTH_TIMER="savasan-healthcheck.timer"
LEGACY_COREDUMP_SERVICE="savasan-coredump-collect.service"
LEGACY_COREDUMP_TIMER="savasan-coredump-collect.timer"

# Calisan servisi durdur (varsa)
if systemctl is-active --quiet "${SERVICE_NAME}" 2>/dev/null; then
  echo "[0/5] Mevcut servis durduruluyor..."
  systemctl stop "${SERVICE_NAME}"
fi

# Legacy isimli servisler sistemde kalmissa durdur + disable + remove et.
for unit in \
  "${LEGACY_SERVICE}" \
  "${LEGACY_HEALTH_SERVICE}" \
  "${LEGACY_HEALTH_TIMER}" \
  "${LEGACY_COREDUMP_SERVICE}" \
  "${LEGACY_COREDUMP_TIMER}"; do
  systemctl stop "${unit}" 2>/dev/null || true
  systemctl disable "${unit}" 2>/dev/null || true
  rm -f "/etc/systemd/system/${unit}"
done

echo "[1/5] Environment dosyasi kopyalaniyor..."
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
mkdir -p /etc/savasan /var/log/savasan/recordings "${WORKSPACE_ROOT}/output"
chown nvidia:nvidia "${WORKSPACE_ROOT}/output" 2>/dev/null || true
cp "${SCRIPT_DIR}/stream_common.env" /etc/savasan/stream_common.env
cp -n "${SCRIPT_DIR}/savasan.env" /etc/savasan/savasan.env
# Mevcut savasan.env korunur (cp -n); sablondaki yeni anahtarlar eksikse ekle.
merge_env_missing_keys() {
  local template="$1"
  local target="$2"
  declare -A HAVE=()
  while IFS='=' read -r k _v; do
    [[ -z "${k}" || "${k}" =~ ^[[:space:]]*# ]] && continue
    k="$(echo "${k}" | tr -d '[:space:]')"
    HAVE["${k}"]=1
  done < "${target}"
  local added=0
  while IFS='=' read -r k v; do
    [[ -z "${k}" || "${k}" =~ ^[[:space:]]*# ]] && continue
    k="$(echo "${k}" | tr -d '[:space:]')"
    if [[ -z "${HAVE[$k]:-}" ]]; then
      printf '%s=%s\n' "${k}" "${v}" >> "${target}"
      added=$((added + 1))
    fi
  done < "${template}"
  if [[ "${added}" -gt 0 ]]; then
    echo "       savasan.env: ${added} eksik anahtar sablondan eklendi"
  fi
}
merge_env_missing_keys "${SCRIPT_DIR}/savasan.env" /etc/savasan/savasan.env
mkdir -p /etc/savasan/camera_profiles
cp -f "${SCRIPT_DIR}/../camera_profiles/"*.env /etc/savasan/camera_profiles/
echo "       /etc/savasan/stream_common.env"
echo "       /etc/savasan/savasan.env"
echo "       /etc/savasan/camera_profiles/*.env"

echo "[2/8] Service dosyasi kopyalaniyor..."
cp "${SCRIPT_DIR}/savasan-airlock.service" "/etc/systemd/system/${SERVICE_NAME}"
echo "       /etc/systemd/system/${SERVICE_NAME}"
# Eski ayri alpagulink unit kaldir (Artik airlock/savasan-run.sh icinde).
if [[ -f /etc/systemd/system/savasan-alpagulink.service ]]; then
  systemctl stop savasan-alpagulink.service 2>/dev/null || true
  systemctl disable savasan-alpagulink.service 2>/dev/null || true
  rm -f /etc/systemd/system/savasan-alpagulink.service
  echo "       eski savasan-alpagulink.service kaldirildi"
fi
cp "${SCRIPT_DIR}/savasan-airlock-healthcheck.service" "/etc/systemd/system/${HEALTH_SERVICE}"
cp "${SCRIPT_DIR}/savasan-airlock-healthcheck.timer" "/etc/systemd/system/${HEALTH_TIMER}"
cp "${SCRIPT_DIR}/savasan-airlock-coredump-collect.service" "/etc/systemd/system/${COREDUMP_SERVICE}"
cp "${SCRIPT_DIR}/savasan-airlock-coredump-collect.timer" "/etc/systemd/system/${COREDUMP_TIMER}"
echo "       /etc/systemd/system/${HEALTH_SERVICE}"
echo "       /etc/systemd/system/${HEALTH_TIMER}"
echo "       /etc/systemd/system/${COREDUMP_SERVICE}"
echo "       /etc/systemd/system/${COREDUMP_TIMER}"

echo "[3/8] Health/recovery scriptleri kuruluyor..."
install -m 0755 "${SCRIPT_DIR}/savasan-run.sh" /usr/local/bin/savasan-run.sh
install -m 0755 "${SCRIPT_DIR}/savasan-alpagulink.sh" /usr/local/bin/savasan-alpagulink.sh
install -m 0755 "${SCRIPT_DIR}/savasan-prepare-stream-env.sh" /usr/local/bin/savasan-prepare-stream-env.sh
install -m 0755 "${SCRIPT_DIR}/savasan-run-kamikaze-flight.sh" /usr/local/bin/savasan-run-kamikaze-flight.sh
install -m 0755 "${SCRIPT_DIR}/validate_env.sh" /usr/local/bin/savasan-validate-env.sh
install -m 0755 "${SCRIPT_DIR}/backup_env.sh" /usr/local/bin/savasan-backup-env.sh
install -m 0755 "${SCRIPT_DIR}/rollback_env.sh" /usr/local/bin/savasan-rollback-env.sh
install -m 0755 "${SCRIPT_DIR}/healthcheck.sh" /usr/local/bin/savasan-healthcheck.sh
install -m 0755 "${SCRIPT_DIR}/recover.sh" /usr/local/bin/savasan-recover.sh
install -m 0755 "${SCRIPT_DIR}/coredump_collect.sh" /usr/local/bin/savasan-coredump-collect.sh
install -m 0755 "${SCRIPT_DIR}/maintenance_mode.sh" /usr/local/bin/savasan-maintenance-mode.sh
install -m 0755 "${SCRIPT_DIR}/flight_mode.sh" /usr/local/bin/savasan-flight-mode.sh
install -m 0755 "${SCRIPT_DIR}/setup_usb_camera.sh" /usr/local/bin/savasan-setup-usb-camera.sh
install -m 0755 "${SCRIPT_DIR}/apply_usb_camera_profile.sh" /usr/local/bin/savasan-apply-camera-profile.sh
install -m 0755 "${SCRIPT_DIR}/wait_for_display.sh" /usr/local/bin/savasan-wait-for-display.sh
install -m 0755 "${SCRIPT_DIR}/start_tigervnc_x0.sh" /usr/local/bin/savasan-start-tigervnc-x0.sh
START_SCRIPT="$(cd "${SCRIPT_DIR}/../../.." && pwd)/scripts/start_savasan_service.sh"
if [[ -f "${START_SCRIPT}" ]]; then
  install -m 0755 "${START_SCRIPT}" /usr/local/bin/savasan-start.sh
  echo "       /usr/local/bin/savasan-start.sh (servis baslat + terminal telemetri)"
fi
mkdir -p /var/log/savasan /var/backups/savasan /var/crash/savasan /var/lib/savasan
chown -R nvidia:nvidia /var/log/savasan /var/backups/savasan
echo "       /usr/local/bin/savasan-* scripts + /var/log/savasan + /var/backups/savasan + /var/crash/savasan"

echo "[4/8] Logrotate kuruluyor..."
cp "${SCRIPT_DIR}/savasan-airlock.logrotate" "/etc/logrotate.d/${LOGROTATE_NAME}"
echo "       /etc/logrotate.d/${LOGROTATE_NAME}"

echo "[5/8] Binary kontrol..."
CPP_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BINARY=""
for candidate in \
  "${CPP_ROOT}/build-app/savasan_iha" \
  "${CPP_ROOT}/build/savasan_iha"; do
  if [ -x "${candidate}" ]; then
    BINARY="${candidate}"
    break
  fi
done
if [ -z "${BINARY}" ]; then
  echo "HATA: savasan_iha binary bulunamadi."
  echo "  cd ${CPP_ROOT} && mkdir -p build-app && cd build-app"
  echo "  cmake .. -DSAVASAN_BUILD_APP=ON && make -j\$(nproc) savasan_iha"
  exit 1
fi
echo "       ${BINARY} OK"

# /etc/savasan/savasan.env icinde SAVASAN_BINARY satirini guncelle (yoksa ekle).
ENV_TARGET="/etc/savasan/savasan.env"
if grep -q '^SAVASAN_BINARY=' "${ENV_TARGET}" 2>/dev/null; then
  sed -i "s|^SAVASAN_BINARY=.*|SAVASAN_BINARY=${BINARY}|" "${ENV_TARGET}"
else
  sed -i "1iSAVASAN_BINARY=${BINARY}" "${ENV_TARGET}"
fi
for kv in \
  "NVDS_ENABLE_LATENCY_MEASUREMENT=1" \
  "NVDS_ENABLE_COMPONENT_LATENCY_MEASUREMENT=1" \
  "SAVASAN_COMPONENT_LATENCY_PROFILER=1"; do
  key="${kv%%=*}"
  if grep -q "^${key}=" "${ENV_TARGET}" 2>/dev/null; then
    sed -i "s|^${key}=.*|${kv}|" "${ENV_TARGET}"
  else
    echo "${kv}" >> "${ENV_TARGET}"
  fi
done
echo "       ${ENV_TARGET} -> SAVASAN_BINARY guncellendi"

echo "[6/8] systemd daemon-reload..."
systemctl daemon-reload

if [[ -f "${SCRIPT_DIR}/tigervnc-x0.service" ]]; then
  echo "[6b/8] VNC otomatik acilis (guc kesintisi sonrasi)..."
  cp "${SCRIPT_DIR}/tigervnc-x0.service" /etc/systemd/system/tigervnc-x0.service
  systemctl disable --now x11vnc.service 2>/dev/null || true
  if [[ -f /home/nvidia/.vnc/passwd ]]; then
    systemctl enable tigervnc-x0.service
    systemctl enable tigervnc-x0-ensure.timer
    systemctl start tigervnc-x0-ensure.timer 2>/dev/null || true
    loginctl enable-linger nvidia 2>/dev/null || true
    systemctl restart tigervnc-x0.service || echo "       UYARI: tigervnc-x0 baslatilamadi — sudo bash scripts/setup_vnc.sh"
    echo "       tigervnc-x0.service + ensure.timer: enabled (acilista otomatik)"
  else
    echo "       VNC sifresi yok — bir kez: sudo bash scripts/setup_vnc.sh"
  fi
fi

DISABLE_SCRIPT="$(cd "${SCRIPT_DIR}/../../.." && pwd)/scripts/systemd_disable_autostart.sh"
echo "[7/8] Otomatik acilis: KAPALI (servis/timer disable)..."
bash "${DISABLE_SCRIPT}"

echo ""
echo "Kurulum tamam (dosyalar yuklendi). Elle baslatma:"
echo "  sudo /usr/local/bin/savasan-start.sh     # Servis baslat + telemetri bu terminalde"
echo "  sudo systemctl start savasan-airlock     # Sadece baslat (log: watch_telemetry.sh)"
echo "  sudo systemctl stop savasan-airlock      # Servisi durdur"
echo "  sudo systemctl status savasan-airlock    # Durum kontrol"
echo "  journalctl -u savasan-airlock -f         # Canli log"
echo "  sudo /usr/local/bin/savasan-healthcheck.sh      # Manuel healthcheck"
echo "  sudo /usr/local/bin/savasan-recover.sh          # Son yedekten recover"
echo "  sudo /usr/local/bin/savasan-coredump-collect.sh # Manuel crash dump toplama"
echo "  sudo /usr/local/bin/savasan-maintenance-mode.sh # Bakim modu (servis kapali)"
echo "  sudo /usr/local/bin/savasan-flight-mode.sh      # Ucus/test modu (servis acik)"
echo ""
echo "Konfigurasyon: /etc/savasan/savasan.env"
echo "  Duzenleme sonrasi: sudo systemctl restart savasan-airlock"
echo ""
echo "Acilista otomatik baslatma (istege bagli, ucus/test oncesi):"
echo "  sudo systemctl enable --now savasan-airlock-healthcheck.timer"
echo "  sudo systemctl enable --now savasan-airlock-coredump-collect.timer"
echo "  sudo systemctl enable savasan-airlock.service"
echo "  sudo systemctl start savasan-airlock"
echo "Veya: sudo /usr/local/bin/savasan-flight-mode.sh"
