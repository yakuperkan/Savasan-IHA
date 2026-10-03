#!/usr/bin/env bash
# Savasan IHA ortam dogrulama — REQUIREMENTS.txt ile eslesir.
# Kullanim: bash scripts/check_requirements.sh [--strict]
set -euo pipefail

STRICT=0
if [[ "${1:-}" == "--strict" ]]; then
  STRICT=1
fi

WS="${SAVASAN_WORKSPACE:-/home/nvidia/Savasan_IHA_Workspace}"
CPP="${WS}/02_Ana_Sistem_CPP"
FAIL=0
WARN=0

ok()   { echo "[OK]   $*"; }
warn() { echo "[WARN] $*"; WARN=$((WARN + 1)); }
fail() { echo "[FAIL] $*"; FAIL=$((FAIL + 1)); }

check_cmd() {
  local name="$1"
  shift
  if command -v "$name" >/dev/null 2>&1; then
    ok "$name: $($("$@") 2>/dev/null | head -1)"
  else
    fail "$name bulunamadi"
  fi
}

check_file() {
  local path="$1"
  local label="${2:-$path}"
  if [[ -f "$path" ]]; then
    ok "dosya: $label"
  else
    fail "eksik dosya: $label ($path)"
  fi
}

check_gst_plugin() {
  local plugin="$1"
  if gst-inspect-1.0 "$plugin" >/dev/null 2>&1; then
    ok "GStreamer plugin: $plugin"
  else
    fail "GStreamer plugin yok: $plugin"
  fi
}

echo "=== Savasan IHA Requirements Check ==="
echo "Workspace: $WS"
echo

# --- OS / arch ---
if [[ "$(uname -m)" == "aarch64" ]]; then
  ok "mimari: aarch64"
else
  warn "mimari aarch64 degil: $(uname -m) (Jetson disi ortam?)"
fi

if [[ -f /etc/os-release ]]; then
  # shellcheck disable=SC1091
  source /etc/os-release
  if [[ "${VERSION_ID:-}" == "22.04" ]]; then
    ok "OS: ${PRETTY_NAME:-Ubuntu 22.04}"
  else
    warn "OS 22.04 bekleniyordu: ${PRETTY_NAME:-bilinmiyor}"
  fi
fi

echo
echo "--- Derleme araclari ---"
check_cmd cmake cmake --version
check_cmd g++ g++ --version
check_cmd pkg-config pkg-config --version
check_cmd git git --version

for pkg in gstreamer-1.0 libcurl; do
  if pkg-config --exists "$pkg" 2>/dev/null; then
    ok "pkg-config $pkg: $(pkg-config --modversion "$pkg")"
  else
    fail "pkg-config $pkg bulunamadi"
  fi
done

echo
echo "--- NVIDIA / DeepStream ---"
if command -v deepstream-app >/dev/null 2>&1; then
  ver="$(deepstream-app --version-all 2>/dev/null | head -1 || true)"
  ok "DeepStream: ${ver:-deepstream-app}"
else
  fail "deepstream-app bulunamadi (DeepStream kurulu mu?)"
fi

for lib in \
  /opt/nvidia/deepstream/deepstream/lib/libnvbufsurface.so \
  /opt/nvidia/deepstream/deepstream/lib/libnvds_meta.so \
  /opt/nvidia/deepstream/deepstream/lib/libnvdsgst_meta.so \
  /opt/nvidia/deepstream/deepstream/lib/libnvds_nvmultiobjecttracker.so; do
  check_file "$lib"
done

if command -v nvcc >/dev/null 2>&1; then
  ok "CUDA: $(nvcc --version 2>/dev/null | tail -1)"
else
  warn "nvcc bulunamadi (engine derleme icin gerekli)"
fi

if [[ -x /usr/src/tensorrt/bin/trtexec ]]; then
  ok "trtexec: /usr/src/tensorrt/bin/trtexec"
else
  warn "trtexec bulunamadi (TensorRT engine yeniden uretimi yapilamaz)"
fi

echo
echo "--- GStreamer pluginleri ---"
if command -v gst-inspect-1.0 >/dev/null 2>&1; then
  ok "GStreamer: $(gst-launch-1.0 --version 2>/dev/null | head -1)"
  for p in nvinfer nvtracker nvvideoconvert nvdsosd nvstreammux v4l2src; do
    check_gst_plugin "$p"
  done
else
  fail "gst-inspect-1.0 bulunamadi"
fi

echo
echo "--- Workspace dosyalari ---"
check_file "${CPP}/CMakeLists.txt" "CMakeLists.txt"
check_file "${WS}/03_Modeller/yolo26_uav22/model_yolo26_static_b1.onnx" "YOLO26 ONNX"
check_file "${WS}/03_Modeller/yolo26_uav22/model_yolo26_orin_nx_fp16_b1_gpu0.engine" "YOLO26 FP16 engine"
check_file "${CPP}/config/deepstream/labels.txt" "labels.txt"
check_file "${CPP}/config/deepstream/tracker_config.yml" "tracker_config.yml"
check_file "${CPP}/config/deepstream/config_infer_primary.txt" "config_infer_primary.txt"

YOLO_SO="${WS}/98_Reference_Repos/DeepStream-Yolo-master/nvdsinfer_custom_impl_Yolo/libnvdsinfer_custom_impl_Yolo.so"
if [[ -f "$YOLO_SO" ]]; then
  ok "DeepStream-Yolo parser: libnvdsinfer_custom_impl_Yolo.so"
else
  fail "DeepStream-Yolo .so eksik — clone + make gerekli (REQUIREMENTS.txt bolum 6)"
fi

echo
echo "--- Binary (opsiyonel) ---"
if [[ -x "${CPP}/build/savasan_iha" ]]; then
  ok "binary: ${CPP}/build/savasan_iha"
elif [[ -x "${CPP}/build-app/savasan_iha" ]]; then
  ok "binary: ${CPP}/build-app/savasan_iha"
else
  if [[ "$STRICT" -eq 1 ]]; then
    fail "savasan_iha binary yok — once cmake build yapin"
  else
    warn "savasan_iha binary henuz derlenmemis"
  fi
fi

echo
echo "--- Ozet ---"
if [[ "$FAIL" -eq 0 ]]; then
  echo "SONUC: Gecildi (${WARN} uyari)"
  exit 0
else
  echo "SONUC: ${FAIL} hata, ${WARN} uyari — REQUIREMENTS.txt dosyasina bakin"
  exit 1
fi
