#!/usr/bin/env bash
# YOLO26 FP16 TensorRT engine — yalnizca hedef Jetson'da calistirin.
# Orin NX 16GB uzerinde derlenir; baska cihaza kopyalamayin.
set -euo pipefail

MODEL_DIR="$(cd "$(dirname "$0")" && pwd)"
ONNX="${MODEL_DIR}/model_yolo26_static_b1.onnx"
ENGINE="${MODEL_DIR}/model_yolo26_orin_nx_fp16_b1_gpu0.engine"
LOG="/tmp/yolo26_orin_nx_fp16_build.log"
TRTEXEC="/usr/src/tensorrt/bin/trtexec"

if [[ ! -x "$TRTEXEC" ]]; then
  echo "HATA: trtexec bulunamadi: $TRTEXEC" >&2
  exit 1
fi
if [[ ! -f "$ONNX" ]]; then
  echo "HATA: ONNX yok: $ONNX" >&2
  exit 1
fi

echo "[INFO] Derleme basliyor (yaklasik 10-15 dk)..."
echo "[INFO] Log: $LOG"
"$TRTEXEC" \
  --onnx="$ONNX" \
  --saveEngine="$ENGINE" \
  --memPoolSize=workspace:4096M \
  --fp16 \
  --profilingVerbosity=detailed \
  2>&1 | tee "$LOG"

ls -lh "$ENGINE"
echo "[OK] Engine hazir: $ENGINE"
