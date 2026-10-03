#!/usr/bin/env python3
"""
INT8 Deployment - Step 1: FP16 GPU Baseline Engine
Canlı izleme için: tail -f /tmp/fp16_build.log
"""
import subprocess
import sys
from pathlib import Path

# Konfigürasyon
MODEL_DIR = Path("/home/nvidia/Savasan_IHA_Workspace/03_Modeller/yolo26_uav22")
ONNX_MODEL = MODEL_DIR / "model_yolo26_static_b1.onnx"
FP16_ENGINE = MODEL_DIR / "model_fp16_baseline.engine"
WORKSPACE_GB = 8

def run_trtexec():
    if not ONNX_MODEL.exists():
        print(f"[ERROR] ONNX model yok: {ONNX_MODEL}", flush=True)
        return False

    cmd = [
        "/usr/src/tensorrt/bin/trtexec",
        f"--onnx={ONNX_MODEL}",
        f"--saveEngine={FP16_ENGINE}",
        f"--memPoolSize=workspace:{WORKSPACE_GB}G",
        "--fp16",
        "--profilingVerbosity=detailed",
    ]

    print(f"[INFO] FP16 GPU Baseline engine oluşturuluyor...", flush=True)
    print(f"[INFO] Çıktı: /tmp/fp16_build.log", flush=True)

    # Canlı çıktı için Popen kullan
    with open("/tmp/fp16_build.log", "w") as log_file:
        process = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            universal_newlines=True
        )

        # Canlı çıktı al
        for line in process.stdout:
            log_file.write(line)
            log_file.flush()
            print(line, end='', flush=True)

        process.wait()

        if process.returncode != 0:
            print(f"[ERROR] Build başarısız! Detaylar: tail -f /tmp/fp16_build.log", flush=True)
            return False

    if FP16_ENGINE.exists():
        print(f"[SUCCESS] FP16 engine oluşturuldu: {FP16_ENGINE}", flush=True)
        print(f"[INFO] Dosya boyutu: {FP16_ENGINE.stat().st_size / 1024 / 1024:.1f} MB", flush=True)
        return True
    else:
        print(f"[ERROR] Engine dosyası oluşturulamadı!", flush=True)
        return False

if __name__ == "__main__":
    success = run_trtexec()
    sys.exit(0 if success else 1)