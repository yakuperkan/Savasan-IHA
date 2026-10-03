#!/usr/bin/env python3
"""
INT8 Deployment - Step 6: DeepStream Config Önerileri
Hangi engine hangi parametrelerle kullanılmalı?
"""
import json
from pathlib import Path

# Konfigürasyon
MODEL_DIR = Path("/home/nvidia/Savasan_IHA_Workspace/03_Modeller/yolo26_uav22")
DEEPSTREAM_CONFIG = Path("/home/nvidia/Savasan_IHA_Workspace/04_DeepStream_System/configs/config_infer_primary_yolo26.txt")

def generate_deepstream_configs():
    """Her engine için DeepStream config önerileri oluştur"""

    config_suggestions = {
        "fp16_gpu_baseline": {
            "engine_path": "model_fp16_baseline.engine",
            "engine_type": "FP16 GPU",
            "description": "Standart FP16 GPU inference - maksimum doğruluk",
            "deepstream_config": {
                "enable-dla": 0,
                "use-dla-core": -1,
                "network-mode": 2,  # FP16
                "network-type": 0,   # Detector
            },
            "recommended_for": "Production maksimum doğruluk gerektiğinde"
        },
        "int8_dla0": {
            "engine_path": "model_int8_dla0.engine",
            "engine_type": "INT8 DLA Core 0",
            "description": "INT8 quantization + DLA core 0 - power efficient",
            "deepstream_config": {
                "enable-dla": 1,
                "use-dla-core": 0,
                "network-mode": 1,  # INT8
                "network-type": 0,   # Detector
            },
            "recommended_for": "Power-efficient gerçek zamanlı inference"
        },
        "int8_dla1": {
            "engine_path": "model_int8_dla1.engine",
            "engine_type": "INT8 DLA Core 1",
            "description": "INT8 quantization + DLA core 1 - power efficient",
            "deepstream_config": {
                "enable-dla": 1,
                "use-dla-core": 1,
                "network-mode": 1,  # INT8
                "network-type": 0,   # Detector
            },
            "recommended_for": "Multi-DLA load balancing ve güç verimliliği"
        }
    }

    return config_suggestions

def generate_deepstream_config_snippets():
    """DeepStream config dosyası için kullanılabilir snippet'ler"""

    snippets = {
        "fp16_gpu_baseline": """
# YOLO26 FP16 GPU Baseline Konfigürasyonu
[model]
engine-create-func-name=NvDsInferContextCustomDetectPostProcessor
pre-process-mode=1
num-detected-classes=6
interval=0
gie-unique-id=1
process-mode=1
network-mode=2  # FP16
batch-size=1
network-type=0
cluster-mode=2
maintain-aspect-ratio=1
parse-bbox-func-name=NvDsInferParseCustomYoloV5
custom-lib-path=/opt/nvidia/deepstream/deepstream/lib/libnvdsinfer_custom_impl_Yolo.so
enable-dla=0
use-dla-core=0
""",
        "int8_dla": """
# YOLO26 INT8 DLA Konfigürasyonu
[model]
engine-create-func-name=NvDsInferContextCustomDetectPostProcessor
pre-process-mode=1
num-detected-classes=6
interval=0
gie-unique-id=1
process-mode=1
network-mode=1  # INT8
batch-size=1
network-type=0
cluster-mode=2
maintain-aspect-ratio=1
parse-bbox-func-name=NvDsInferParseCustomYoloV5
custom-lib-path=/opt/nvidia/deepstream/deepstream/lib/libnvdsinfer_custom_impl_Yolo.so
enable-dla=1
use-dla-core=0  # veya 1
"""
    }

    return snippets

def main():
    print("[INFO] DeepStream Config Önerileri Oluşturuluyor...")

    # Config önerileri oluştur
    config_suggestions = generate_deepstream_configs()
    snippets = generate_deepstream_config_snippets()

    # Mevcut DeepStream config'i oku
    current_config = ""
    if DEEPSTREAM_CONFIG.exists():
        current_config = DEEPSTREAM_CONFIG.read_text()

    # Önerileri kaydet
    artifacts_dir = MODEL_DIR / "deployment_artifacts"
    artifacts_dir.mkdir(parents=True, exist_ok=True)

    # Config önerileri JSON
    suggestions_path = artifacts_dir / "deepstream_config_suggestions.json"
    suggestions_path.write_text(json.dumps(config_suggestions, indent=2))

    # Config snippet'leri
    snippets_path = artifacts_dir / "deepstream_config_snippets.txt"
    snippets_path.write_text("".join(snippets.values()))

    print(f"\n[SUCCESS] DeepStream config önerileri oluşturuldu!")
    print(f"[INFO] Öneriler: {suggestions_path}")
    print(f"[INFO] Snippet'ler: {snippets_path}")

    # Konsol özeti
    print(f"\n{'='*60}")
    print(f"DEEPSTREAM CONFIG ÖNERİLERİ:")
    print(f"{'='*60}")

    for engine_name, config in config_suggestions.items():
        print(f"\n{engine_name.upper()}:")
        print(f"  Engine:           {config['engine_path']}")
        print(f"  Type:             {config['engine_type']}")
        print(f"  Description:      {config['description']}")
        print(f"  enable-dla:       {config['deepstream_config']['enable-dla']}")
        print(f"  use-dla-core:     {config['deepstream_config']['use-dla-core']}")
        print(f"  network-mode:     {config['deepstream_config']['network-mode']}")
        print(f"  Recommended for:   {config['recommended_for']}")

    print(f"\n{'='*60}")
    print(f"DEEPSTREAM CONFIG DİFF ÖNERİLERİ:")
    print(f"{'='*60}")
    print(f"\nINT8 DLA için değişiklikler:")
    print(f"  network-mode: 2 -> 1  (FP16 -> INT8)")
    print(f"  enable-dla:   0 -> 1  (GPU -> DLA)")
    print(f"  use-dla-core: -1 -> 0 veya 1 (Core seçimi)")

    return True

if __name__ == "__main__":
    main()