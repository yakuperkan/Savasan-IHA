#!/usr/bin/env bash
# Jetson GNOME masaustunu VNC ile uzaktan ac.
#
# Varsayilan: TigerVNC x0vncserver (mevcut :0 ekranini paylasir)
# Alternatif: x11vnc (--backend x11vnc)
#
# Kullanim:
#   sudo bash scripts/setup_vnc.sh
#   sudo bash scripts/setup_vnc.sh --backend tigervnc
#   VNC_PASSWORD='gizli' sudo -E bash scripts/setup_vnc.sh
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BACKEND="tigervnc"
PASSWD_FILE="/home/nvidia/.vnc/passwd"
LOG_DIR="/var/log/savasan"

usage() {
  echo "Kullanim: sudo bash $0 [--backend tigervnc|x11vnc]" >&2
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --backend)
      [[ $# -ge 2 ]] || { usage; exit 1; }
      BACKEND="$2"
      shift 2
      ;;
    -h | --help)
      usage
      exit 0
      ;;
    *)
      echo "HATA: Bilinmeyen arguman: $1" >&2
      usage
      exit 1
      ;;
  esac
done

case "${BACKEND}" in
  tigervnc | tiger) BACKEND="tigervnc" ;;
  x11vnc | x11) BACKEND="x11vnc" ;;
  *)
    echo "HATA: Gecersiz backend: ${BACKEND} (tigervnc veya x11vnc)" >&2
    exit 1
    ;;
esac

if [[ "$(id -u)" -ne 0 ]]; then
  echo "HATA: root gerekli — sudo bash $0" >&2
  exit 1
fi

apt_install() {
  # Wine i386 foreign arch yuzunden tam apt update kirilabilir; once direkt kurmayi dene.
  local -a pkgs=("$@")
  export DEBIAN_FRONTEND=noninteractive
  if apt-get install -y --no-install-recommends "${pkgs[@]}" 2>/dev/null; then
    return 0
  fi
  echo "[vnc] apt update (yalnizca arm64 — i386 wine kaynakli 404 onlenir)..."
  apt-get -o APT::Architectures=arm64 update -qq || true
  apt-get install -y --no-install-recommends "${pkgs[@]}"
}

install_backend() {
  case "${BACKEND}" in
    tigervnc)
      echo "[vnc] TigerVNC x0vncserver kuruluyor..."
      apt_install tigervnc-scraping-server tigervnc-tools
      command -v x0vncserver >/dev/null 2>&1 || {
        echo "HATA: x0vncserver kurulamadi." >&2
        exit 1
      }
      ;;
    x11vnc)
      echo "[vnc] x11vnc kuruluyor..."
      apt_install x11vnc
      command -v x11vnc >/dev/null 2>&1 || {
        echo "HATA: x11vnc kurulamadi." >&2
        exit 1
      }
      ;;
  esac
}

write_passwd() {
  local pw="$1"
  case "${BACKEND}" in
    tigervnc)
      printf '%s\n' "${pw}" | vncpasswd -f > "${PASSWD_FILE}"
      ;;
    x11vnc)
      x11vnc -storepasswd "${pw}" "${PASSWD_FILE}"
      ;;
  esac
  chown nvidia:nvidia "${PASSWD_FILE}"
  chmod 600 "${PASSWD_FILE}"
}

install_backend

mkdir -p /home/nvidia/.vnc "${LOG_DIR}"
chown nvidia:nvidia /home/nvidia/.vnc "${LOG_DIR}"

if [[ ! -f "${PASSWD_FILE}" ]]; then
  if [[ -n "${VNC_PASSWORD:-}" ]]; then
    pw="${VNC_PASSWORD}"
  else
    echo ""
    echo "VNC baglanti sifresi (TigerVNC Viewer / RealVNC, en az 6 karakter):"
    read -r -s -p "Sifre: " pw
    echo ""
    read -r -s -p "Tekrar: " pw2
    echo ""
    if [[ "${pw}" != "${pw2}" ]]; then
      echo "HATA: Sifreler eslesmiyor." >&2
      exit 1
    fi
    if [[ ${#pw} -lt 6 ]]; then
      echo "HATA: Sifre en az 6 karakter olmali." >&2
      exit 1
    fi
  fi
  write_passwd "${pw}"
  echo "[vnc] Sifre kaydedildi: ${PASSWD_FILE}"
else
  echo "[vnc] Mevcut sifre korunuyor: ${PASSWD_FILE}"
  echo "      TigerVNC: printf 'sifre\\n' | vncpasswd -f | sudo tee ${PASSWD_FILE} && sudo chown nvidia:nvidia ${PASSWD_FILE}"
  echo "      x11vnc:   sudo x11vnc -storepasswd ${PASSWD_FILE}"
fi

UNIT_DST_NAME=""
case "${BACKEND}" in
  tigervnc)
    UNIT_SRC="${ROOT}/02_Ana_Sistem_CPP/config/systemd/tigervnc-x0.service"
    UNIT_DST_NAME="tigervnc-x0.service"
  ;;
  x11vnc)
    UNIT_SRC="${ROOT}/02_Ana_Sistem_CPP/config/systemd/x11vnc.service"
    UNIT_DST_NAME="x11vnc.service"
  ;;
esac

[[ -f "${UNIT_SRC}" ]] || {
  echo "HATA: Birim dosyasi yok: ${UNIT_SRC}" >&2
  exit 1
}

# Diger backend servisini kapat (port 5900 cakismasi)
systemctl disable --now x11vnc.service 2>/dev/null || true
systemctl disable --now tigervnc-x0.service 2>/dev/null || true

install -m 0644 "${UNIT_SRC}" "/etc/systemd/system/${UNIT_DST_NAME}"
install -m 0755 "${ROOT}/02_Ana_Sistem_CPP/config/systemd/wait_for_display.sh" \
  /usr/local/bin/savasan-wait-for-display.sh 2>/dev/null || true
install -m 0755 "${ROOT}/02_Ana_Sistem_CPP/config/systemd/start_tigervnc_x0.sh" \
  /usr/local/bin/savasan-start-tigervnc-x0.sh 2>/dev/null || true
if [[ "${BACKEND}" == "tigervnc" ]]; then
  install -m 0644 "${ROOT}/02_Ana_Sistem_CPP/config/systemd/tigervnc-x0-ensure.service" \
    /etc/systemd/system/tigervnc-x0-ensure.service
  install -m 0644 "${ROOT}/02_Ana_Sistem_CPP/config/systemd/tigervnc-x0-ensure.timer" \
    /etc/systemd/system/tigervnc-x0-ensure.timer
  loginctl enable-linger nvidia 2>/dev/null || true
fi
: > "${LOG_DIR}/tigervnc-x0.log" 2>/dev/null || true
systemctl daemon-reload
systemctl enable "${UNIT_DST_NAME}"
if [[ "${BACKEND}" == "tigervnc" ]]; then
  systemctl enable tigervnc-x0-ensure.timer
  systemctl start tigervnc-x0-ensure.timer
fi
systemctl restart "${UNIT_DST_NAME}"

sleep 1
if systemctl is-active --quiet "${UNIT_DST_NAME}"; then
  echo "[vnc] Servis aktif: ${UNIT_DST_NAME}"
else
  echo "[vnc] UYARI: Servis baslamadi — log:" >&2
  journalctl -u "${UNIT_DST_NAME}" -n 25 --no-pager >&2 || true
  exit 1
fi

primary_ip="$(hostname -I | awk '{print $1}')"
host_name="$(hostname)"
echo ""
echo "=== VNC hazir (${BACKEND}) ==="
echo "  mDNS:   ${host_name}.local:5900  (ayni agda IP degisse de)"
echo "  IP:     ${primary_ip}:5900"
echo "  Laptop: TigerVNC Viewer"
echo "  IP ogren: ./scripts/jetson_connect_info.sh"
echo "  Acilista otomatik: systemctl is-enabled ${UNIT_DST_NAME}  (enabled olmali)"
echo ""
echo "Kamera trackbar (VNC masaustu terminal):"
echo "  cd ${ROOT} && ./scripts/camera_tune_vnc.sh"
echo ""
echo "Durum:  systemctl status ${UNIT_DST_NAME}"
