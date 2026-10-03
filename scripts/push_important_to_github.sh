#!/usr/bin/env bash
# Yalnizca kod, test, config, modeller (03_Modeller), teknik .md dokumanlar.
# PDF / jpg / png / video / sartname klasoru / .cursor — .gitignore ile dislanir.
# Kullanim:
#   ./scripts/push_important_to_github.sh --dry-run
#   ./scripts/push_important_to_github.sh -m "mesaj"
#   ./scripts/push_important_to_github.sh --no-push
#   ./scripts/push_important_to_github.sh --skip-models
#   ./scripts/push_important_to_github.sh --allow-large-models
#   ./scripts/push_important_to_github.sh --include-cursor   # ozel: .cursorrules + skills (-f); genelde kullanma

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

DRY_RUN=0
NO_PUSH=0
INCLUDE_CURSOR=0
SKIP_MODELS=0
ALLOW_LARGE_MODELS=0
COMMIT_MSG=""

usage() {
  sed -n '2,15p' "$0" | sed 's/^# //'
  exit "${1:-0}"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help) usage 0 ;;
    --dry-run) DRY_RUN=1; shift ;;
    --no-push) NO_PUSH=1; shift ;;
    --include-cursor) INCLUDE_CURSOR=1; shift ;;
    --skip-models) SKIP_MODELS=1; shift ;;
    --allow-large-models) ALLOW_LARGE_MODELS=1; shift ;;
    -m|--message)
      COMMIT_MSG="${2:?}"
      shift 2
      ;;
    *) echo "Bilinmeyen arguman: $1" >&2; usage 1 ;;
  esac
done

if ! git rev-parse --git-dir >/dev/null 2>&1; then
  echo "Git deposu degil: $ROOT" >&2
  exit 1
fi

BRANCH="$(git branch --show-current 2>/dev/null || echo main)"
REMOTE="${GIT_REMOTE:-origin}"

# 00_Requirements_and_Rules: .gitignore (tam klasor). docs + 97: sadece izin verilen uzantılar (txt/md vb.)
PATHS=(
  ".github"
  ".gitignore"
  "README.md"
  "02_Ana_Sistem_CPP"
  "docs"
  "97_Documents"
)

if [[ "$SKIP_MODELS" -eq 0 ]]; then
  PATHS+=("03_Modeller")
fi

for f in REFERENCE_DEEPSTREAM_REPOS_INDEX.md DEEPSTREAM_INCLUDES_INDEX.md; do
  [[ -f "$f" ]] && PATHS+=("$f")
done

echo "=== Savasan IHA — kod + test + config + modeller (medya/PDF yok) ==="
echo "Dal: $BRANCH | Remote: $REMOTE"
echo ""

if [[ "$INCLUDE_CURSOR" -eq 1 ]]; then
  echo "UYARI: --include-cursor ile editor dosyalari zorlanir; takim politikasi genelde buna ihtiyac duymaz." >&2
fi

if [[ "$SKIP_MODELS" -eq 0 && "$ALLOW_LARGE_MODELS" -eq 0 && -d 03_Modeller ]]; then
  LARGE="$(find 03_Modeller -type f -size +85M 2>/dev/null || true)"
  if [[ -n "$LARGE" ]]; then
    echo "UYARI: 03_Modeller altinda 85MB ustu dosya var (GitHub ~100MB/dosya):" >&2
    echo "$LARGE" | while read -r line; do [[ -n "$line" ]] && du -h "$line" >&2; done
    echo "" >&2
    echo "Devam: --allow-large-models | Modeller haric: --skip-models" >&2
    exit 1
  fi
fi

ADD_CMD=(git add)
if [[ "$DRY_RUN" -eq 1 ]]; then
  ADD_CMD=(git add -n)
fi

for p in "${PATHS[@]}"; do
  if [[ -e "$p" ]]; then
    "${ADD_CMD[@]}" -- "$p" || true
  fi
done

if [[ "$INCLUDE_CURSOR" -eq 1 ]]; then
  if [[ -f .cursorrules ]]; then
    "${ADD_CMD[@]}" -f -- .cursorrules
  fi
  if [[ -d .cursor/skills ]]; then
    "${ADD_CMD[@]}" -f -- .cursor/skills
  fi
  echo "(cursor -f eklendi)"
fi

echo ""
echo "--- git status (kisaca) ---"
git status -s

if [[ "$DRY_RUN" -eq 1 ]]; then
  echo ""
  echo "Dry-run bitti. Gercek islem icin --dry-run kaldir."
  exit 0
fi

if git diff --cached --quiet 2>/dev/null && git diff --quiet 2>/dev/null; then
  echo "Stage'de degisiklik yok."
  exit 0
fi

if [[ -z "$COMMIT_MSG" ]]; then
  read -r -p "Commit mesaji (bos = iptal): " COMMIT_MSG
  if [[ -z "${COMMIT_MSG// }" ]]; then
    echo "Iptal."
    exit 1
  fi
fi

git commit -m "$COMMIT_MSG"

if [[ "$NO_PUSH" -eq 1 ]]; then
  echo "Push yok (--no-push). Sonra: git push $REMOTE $BRANCH"
  exit 0
fi

echo "Push: $REMOTE $BRANCH"
git push "$REMOTE" "$BRANCH"

echo ""
echo "Tamam. (main -> otomatik release workflow varsa Releases'e bak.)"
