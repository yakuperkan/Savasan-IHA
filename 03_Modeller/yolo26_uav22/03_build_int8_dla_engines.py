#!/usr/bin/env python3
"""
INT8 Deployment - Step 3: INT8 DLA Engine'leri Oluşturma
Canlı izleme için: tail -f /tmp/int8_dla_build.log
"""
import subprocess
import sys
from pathlib import Path

# Konfigürasyon
MODEL_DIR = Path("/home/nvidia/Savasan_IHA_Workspace/03_Modeller/yolo26_uav22")
ONNX_MODEL = MODEL_DIR / "model_yolo26_static_b1.onnx"
CALIB_CACHE = MODEL_DIR / "int8_calibration.cache"
DLA0_ENGINE = MODEL_DIR / "model_int8_dla0.engine"
DLA1_ENGINE = MODEL_DIR / "model_int8_dla1.engine"
WORKSPACE_GB = 8

def build_dla_engine(dla_core: int, engine_path: Path) -> bool:
    if not CALIB_CACHE.exists():
        print(f"[ERROR] Kalibrasyon cache yok: {CALIB_CACHE}")
        print("[INFO] Önce 02_build_int8_calibration.py çalıştırın")
        return False

    print(f"[INFO] INT8 DLA{dla_core} engine oluşturuluyor...")

    cmd = [
        "/usr/src/tensorrt/bin/trtexec",
        f"--onnx={ONNX_MODEL}",
        f"--saveEngine={engine_path}",
        f"--memPoolSize=workspace:{WORKSPACE_GB}G",
        "--fp16",
        "--int8",
        f"--calib={CALIB_CACHE}",
        f"--useDLACore={dla_core}",
        "--allowGPUFallback",
        "--profilingVerbosity=detailed",
    ]

    log_file = f"/tmp/int8_dla{dla_core}_build.log"
    print(f"[INFO] Çıktı: {log_file}")

    with open(log_file, "w") as log:
        result = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False
        )

        log.write(result.stdout)
        log.write(result.stderr)

        if result.returncode != 0:
            print(f"[ERROR] DLA{dla_core} build başarısız! Detaylar: tail -f {log_file}")
            return False

    if engine_path.exists():
        print(f"[SUCCESS] DLA{dla_core} engine oluşturuldu: {engine_path}")
        print(f"[INFO] Dosya boyutu: {engine_path.stat().st_size / 1024 / 1024:.1f} MB")
        return True
    else:
        print(f"[ERROR] DLA{dla_core} engine dosyası oluşturulamadı!")
        return False

def main():
    print("[INFO] INT8 DLA Engine Build Pipeline Başlatılıyor...")
    print(f"[INFO] ONNX Model: {ONNX_MODEL}")
    print(f"[INFO] Kalibrasyon Cache: {CALIB_CACHE}")

    # DLA0 Engine
    dla0_success = build_dla_engine(0, DLA0_ENGINE)

    # DLA1 Engine
    dla1_success = build_dla_engine(1, DLA1_ENGINE)

    if dla0_success and dla1_success:
        print("\n[SUCCESS] Tüm DLA engine'leri başarıyla oluşturuldu!")
        print("[INFO] Sonraki adım: 04_analyze_layer_info.py")
        return True
    else:
        print("\n[ERROR] Bazı DLA engine'leri oluşturulamadı")
        return False

if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)