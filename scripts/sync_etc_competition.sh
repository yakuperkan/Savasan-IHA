#!/usr/bin/env bash
# /etc/savasan'i repo ile senkronlar (guncel validate scripti).
# Kullanim: sudo bash scripts/sync_etc_competition.sh
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_VALIDATE="${REPO_ROOT}/02_Ana_Sistem_CPP/config/systemd/validate_env.sh"
ETC_ENV="/etc/savasan/savasan.env"
ETC_VALIDATE="/usr/local/bin/savasan-validate-env.sh"

if [[ "${EUID}" -ne 0 ]]; then
  echo "HATA: sudo ile calistir: sudo bash scripts/sync_etc_competition.sh" >&2
  exit 1
fi

echo "[1/3] Deployed env yedegi aliniyor..."
backup="${ETC_ENV}.presync_$(date +%Y%m%d_%H%M%S).bak"
cp -v "${ETC_ENV}" "${backup}"

echo "[2/3] Guncel validate scripti kuruluyor..."
install -m 0755 "${REPO_VALIDATE}" "${ETC_VALIDATE}"
echo "       ${ETC_VALIDATE}"

echo "[3/3] Deployed env dogrulaniyor..."
"${ETC_VALIDATE}" "${ETC_ENV}"

echo ""
echo "Senkron tamam. SIHA HTTP Jetson'da yok; yer istasyonu basar."
echo "Yedek: ${backup}"
