#!/usr/bin/env python3
"""
INT8 Deployment - Step 2: INT8 Calibration Cache Oluşturma
Canlı izleme için: tail -f /tmp/int8_calib_build.log
"""
import subprocess
import sys
from pathlib import Path

# Konfigürasyon
MODEL_DIR = Path("/home/nvidia/Savasan_IHA_Workspace/03_Modeller/yolo26_uav22")
ONNX_MODEL = MODEL_DIR / "model_yolo26_static_b1.onnx"
CALIB_DIR = MODEL_DIR / "dataset888_1k"
CALIB_CACHE = MODEL_DIR / "int8_calibration.cache"
INT8_ENGINE = MODEL_DIR / "model_int8_calibration.engine"
WORKSPACE_GB = 8
IMG_SIZE = 1280
BATCH_SIZE = 1

def build_int8_calibration():
    if not ONNX_MODEL.exists():
        print(f"[ERROR] ONNX model yok: {ONNX_MODEL}", flush=True)
        return False

    if not CALIB_DIR.exists():
        print(f"[ERROR] Kalibrasyon dataset yok: {CALIB_DIR}", flush=True)
        return False

    # Görselleri say
    images = list(CALIB_DIR.glob("*.jpg")) + list(CALIB_DIR.glob("*.png")) + list(CALIB_DIR.glob("*.jpeg"))
    if len(images) < 100:
        print(f"[WARNING] Sadece {len(images)} kalibrasyon görseli bulundu (önerilen: 100+)", flush=True)

    print(f"[INFO] INT8 Kalibrasyon başlatılıyor...", flush=True)
    print(f"[INFO] Kalibrasyon görselleri: {len(images)}", flush=True)
    print(f"[INFO] Çıktı: /tmp/int8_calib_build.log", flush=True)

    # Kalibrasyon için kullanım komutu
    cmd = [
        "/usr/src/tensorrt/bin/trtexec",
        f"--onnx={ONNX_MODEL}",
        f"--saveEngine={INT8_ENGINE}",
        f"--memPoolSize=workspace:{WORKSPACE_GB}G",
        "--fp16",
        "--int8",
        f"--calib={CALIB_CACHE}",
        "--profilingVerbosity=detailed",
    ]

    # Önceki cache'i temizle
    if CALIB_CACHE.exists():
        CALIB_CACHE.unlink()
        print("[INFO] Eski kalibrasyon cache temizlendi", flush=True)

    # Canlı çıktı için Popen kullan
    with open("/tmp/int8_calib_build.log", "w") as log_file:
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
            print(f"[ERROR] INT8 calibration başarısız! Detaylar: tail -f /tmp/int8_calib_build.log", flush=True)
            return False

    if CALIB_CACHE.exists():
        print(f"[SUCCESS] INT8 calibration cache oluşturuldu: {CALIB_CACHE}", flush=True)
        print(f"[INFO] Cache boyutu: {CALIB_CACHE.stat().st_size / 1024:.1f} KB", flush=True)

        if INT8_ENGINE.exists():
            print(f"[SUCCESS] INT8 calibration engine oluşturuldu: {INT8_ENGINE}", flush=True)
            print(f"[INFO] Dosya boyutu: {INT8_ENGINE.stat().st_size / 1024 / 1024:.1f} MB", flush=True)
            return True
        else:
            print(f"[WARNING] INT8 engine oluşturulamadı ama cache var", flush=True)
            return True  # Cache oluşturuldu, bu yeterli
    else:
        print(f"[ERROR] Kalibrasyon cache oluşturulamadı!", flush=True)
        return False

if __name__ == "__main__":
    success = build_int8_calibration()
    sys.exit(0 if success else 1)