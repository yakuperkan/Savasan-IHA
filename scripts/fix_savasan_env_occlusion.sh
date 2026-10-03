#!/usr/bin/env bash
# Eski /etc/savasan/savasan.env'de Kalman blogu var, okluzyon anahtarlari yoksa validate fail eder.
# Kullanim: bash scripts/fix_savasan_env_occlusion.sh  (dosya yazilabilirse sudo gerekmez)
set -euo pipefail

ENV_FILE="${1:-/etc/savasan/savasan.env}"

if [[ ! -f "${ENV_FILE}" ]]; then
  echo "HATA: ${ENV_FILE} yok" >&2
  exit 1
fi
if [[ ! -w "${ENV_FILE}" ]]; then
  echo "sudo ile calistirin: sudo bash $0" >&2
  exit 1
fi

python3 <<PY
from pathlib import Path

target = Path("${ENV_FILE}")
text = target.read_text()

lines = []
skip = False
for line in text.splitlines():
    if line.startswith("SAVASAN_KALMAN_") or line.startswith("# Innovation clipping"):
        skip = True
        continue
    if skip and line.strip() == "":
        skip = False
        continue
    if skip and not line.startswith("SAVASAN_KALMAN_"):
        skip = False
    if not skip:
        lines.append(line)

text = "\\n".join(lines)
if not text.endswith("\\n"):
    text += "\\n"

needed = {
    "SAVASAN_GUIDANCE_OCCLUSION_MODE": "hold_last",
    "SAVASAN_GUIDANCE_OCCLUSION_HOLD_MS": "400",
    "SAVASAN_GUIDANCE_PIXEL_TO_DEG": "60",
}
existing = set()
for line in text.splitlines():
    if "=" in line and not line.lstrip().startswith("#"):
        existing.add(line.split("=", 1)[0].strip())

insert = [f"{k}={v}" for k, v in needed.items() if k not in existing]
if insert:
    block = "# --- Gudum okluzyon (YOLO kacirmasinda son komutu koru) ---\\n" + "\\n".join(insert) + "\\n"
    marker = "SAVASAN_PID_YAW_KD=0.00"
    if marker in text:
        text = text.replace(marker + "\\n", marker + "\\n" + block)
    else:
        text += "\\n" + block
    target.write_text(text)
    print("[fix] Eklendi:", ", ".join(k.split("=")[0] for k in insert))
else:
    print("[fix] Okluzyon anahtarlari zaten mevcut")

PY

/usr/local/bin/savasan-validate-env.sh "${ENV_FILE}"
echo "[fix] Tamam. Sonra: sudo systemctl reset-failed savasan-airlock && sudo systemctl start savasan-airlock"
