#!/usr/bin/env python3
"""
INT8 Deployment - Step 4: Layer Bilgilerini Analiz Etme
Hangi katmanlar DLA'da, hangileri GPU'da çalışıyor?
Canlı izleme için: tail -f /tmp/layer_analysis.log
"""
import subprocess
import json
import re
from pathlib import Path

# Konfigürasyon
MODEL_DIR = Path("/home/nvidia/Savasan_IHA_Workspace/03_Modeller/yolo26_uav22")
FP16_ENGINE = MODEL_DIR / "model_fp16_baseline.engine"
DLA0_ENGINE = MODEL_DIR / "model_int8_dla0.engine"
DLA1_ENGINE = MODEL_DIR / "model_int8_dla1.engine"

def analyze_engine(engine_path: Path, log_prefix: str) -> dict:
    if not engine_path.exists():
        print(f"[WARNING] Engine yok: {engine_path}")
        return {}

    print(f"[INFO] {engine_path.name} analiz ediliyor...")

    cmd = [
        "/usr/src/tensorrt/bin/trtexec",
        f"--loadEngine={engine_path}",
        "--dumpLayerInfo",
        "--profilingVerbosity=detailed",
        "--skipInference",
        "--noDataTransfers",
    ]

    log_file = f"/tmp/layer_{log_prefix}.log"
    result = subprocess.run(cmd, capture_output=True, text=True, check=False)

    with open(log_file, "w") as log:
        log.write(result.stdout)
        log.write(result.stderr)

    # Layer bilgilerini analiz et
    layer_info = {
        "total_layers": 0,
        "dla_layers": 0,
        "gpu_layers": 0,
        "unsupported_layers": [],
        "layer_types": {},
    }

    for line in result.stdout.splitlines():
        if "Layer(" in line:
            layer_info["total_layers"] += 1
            if "DLA" in line:
                layer_info["dla_layers"] += 1
            elif "GPU" in line:
                layer_info["gpu_layers"] += 1

        # Desteklenmeyen katmanları tespit et
        if "unsupported" in line.lower() or "not supported" in line.lower():
            layer_info["unsupported_layers"].append(line.strip())

        # Layer türlerini analiz et
        layer_type_match = re.search(r'\((\w+)\)', line)
        if layer_type_match:
            layer_type = layer_type_match.group(1)
            layer_info["layer_types"][layer_type] = layer_info["layer_types"].get(layer_type, 0) + 1

    print(f"[INFO] {engine_path.name}: {layer_info['total_layers']} katman")
    print(f"[INFO]   - DLA: {layer_info['dla_layers']}, GPU: {layer_info['gpu_layers']}")

    return layer_info

def main():
    print("[INFO] Layer Analizi Başlatılıyor...")
    print(f"[INFO] Analiz sonuçları: /tmp/layer_*.log")

    # Tüm engine'leri analiz et
    fp16_info = analyze_engine(FP16_ENGINE, "fp16")
    dla0_info = analyze_engine(DLA0_ENGINE, "dla0")
    dla1_info = analyze_engine(DLA1_ENGINE, "dla1")

    # Özet rapor oluştur
    summary = {
        "fp16_baseline": fp16_info,
        "int8_dla0": dla0_info,
        "int8_dla1": dla1_info,
        "comparison": {
            "dla0_gpu_fallback_ratio": dla0_info["gpu_layers"] / max(dla0_info["total_layers"], 1),
            "dla1_gpu_fallback_ratio": dla1_info["gpu_layers"] / max(dla1_info["total_layers"], 1),
        }
    }

    # Raporu kaydet
    report_path = MODEL_DIR / "deployment_artifacts" / "layer_analysis_report.json"
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(summary, indent=2))

    print(f"\n[SUCCESS] Layer analizi tamamlandı!")
    print(f"[INFO] Rapor: {report_path}")
    print(f"[INFO] GPU Fallback oranları:")
    print(f"[INFO]   - DLA0: {summary['comparison']['dla0_gpu_fallback_ratio']:.1%}")
    print(f"[INFO]   - DLA1: {summary['comparison']['dla1_gpu_fallback_ratio']:.1%}")

    # Desteklenmeyen katmanlar varsa uyar
    if dla0_info["unsupported_layers"]:
        print(f"\n[WARNING] DLA0'da desteklenmeyen katmanlar tespit edildi:")
        for layer in dla0_info["unsupported_layers"][:5]:  # İlk 5 tanesini göster
            print(f"[WARNING]   - {layer}")

    return True

if __name__ == "__main__":
    main()