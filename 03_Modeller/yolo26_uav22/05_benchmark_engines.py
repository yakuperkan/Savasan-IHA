#!/usr/bin/env python3
"""
INT8 Deployment - Step 5: Engine Performans Benchmark
FPS, Latency, GPU Load, Power, Thermal karşılaştırması
Canlı izleme için: tail -f /tmp/benchmark_*.log
"""
import subprocess
import json
import re
import time
from pathlib import Path

# Konfigürasyon
MODEL_DIR = Path("/home/nvidia/Savasan_IHA_Workspace/03_Modeller/yolo26_uav22")
FP16_ENGINE = MODEL_DIR / "model_fp16_baseline.engine"
DLA0_ENGINE = MODEL_DIR / "model_int8_dla0.engine"
DLA1_ENGINE = MODEL_DIR / "model_int8_dla1.engine"
INFERENCE_ITERATIONS = 1000
WARMUP_ITERATIONS = 200

def run_benchmark(engine_path: Path, dla_core: int = None) -> dict:
    if not engine_path.exists():
        print(f"[WARNING] Engine yok: {engine_path}")
        return {}

    print(f"[INFO] Benchmark başlatılıyor: {engine_path.name}...")
    engine_name = engine_path.stem.replace("model_", "").replace("_engine", "")

    # trtexec benchmark komutu
    cmd = [
        "/usr/src/tensorrt/bin/trtexec",
        f"--loadEngine={engine_path}",
        f"--iterations={INFERENCE_ITERATIONS}",
        f"--warmUp={WARMUP_ITERATIONS}",
        "--duration=0",  # Süre kısıtlaması yok
        "--avgOnly",     # Sadece ortalama değerler
    ]

    if dla_core is not None:
        cmd.append(f"--useDLACore={dla_core}")

    log_file = f"/tmp/benchmark_{engine_name}.log"
    print(f"[INFO] Çıktı: {log_file}")

    # Benchmark çalıştır
    result = subprocess.run(cmd, capture_output=True, text=True, check=False)

    with open(log_file, "w") as log:
        log.write(result.stdout)
        log.write(result.stderr)

    # Sonuçları parse et
    benchmark_info = {
        "engine_name": engine_name,
        "iterations": INFERENCE_ITERATIONS,
        "fps": 0.0,
        "avg_latency_ms": 0.0,
        "p95_latency_ms": 0.0,
        "throughput_gbs": 0.0,
        "gpu_compute": 0.0,
        "gpu_memory": 0.0,
    }

    for line in result.stdout.splitlines():
        # FPS tespit et
        if "Throughput:" in line:
            match = re.search(r'(\d+\.\d+)\s+fps', line)
            if match:
                benchmark_info["fps"] = float(match.group(1))

        # Latency tespit et
        if "Latency:" in line:
            match = re.search(r'(\d+\.\d+)\s+ms', line)
            if match:
                benchmark_info["avg_latency_ms"] = float(match.group(1))

        # GPU Compute tespit et
        if "GPU Compute" in line:
            match = re.search(r'(\d+\.\d+)\s+%', line)
            if match:
                benchmark_info["gpu_compute"] = float(match.group(1))

        # GPU Memory tespit et
        if "GPU Memory" in line:
            match = re.search(r'(\d+\.\d+)\s+%', line)
            if match:
                benchmark_info["gpu_memory"] = float(match.group(1))

    # Alternatif latency tespiti
    if benchmark_info["avg_latency_ms"] == 0.0:
        for line in result.stdout.splitlines():
            if "HostLatency" in line:
                match = re.search(r'(\d+\.\d+)\s+ms', line)
                if match:
                    benchmark_info["avg_latency_ms"] = float(match.group(1))

    print(f"[INFO] {engine_name}: {benchmark_info['fps']:.1f} FPS, {benchmark_info['avg_latency_ms']:.1f} ms")
    return benchmark_info

def collect_power_thermal():
    """Güç ve termal verilerini topla"""
    try:
        # GPU güç ve termal bilgileri
        power_info = {}
        for file in Path("/sys/devices/17000000.gv11b/devfreq/17000000.gv11b").iterdir():
            if file.name in ["gpu_freq", "gpu_load", "gpu_voltage"]:
                try:
                    power_info[file.name] = file.read_text().strip()
                except:
                    pass

        # Sistem güç bilgileri
        for file in Path("/sys/bus/i2c/devices/0-0040/hwmon/hwmon*").iterdir():
            if file.name.startswith("in"):
                try:
                    power_info[file.name] = float(file.read_text().strip()) / 1000  # V değerini al
                except:
                    pass

        return power_info
    except Exception as e:
        print(f"[WARNING] Güç/termal bilgisi alınamadı: {e}")
        return {}

def main():
    print("[INFO] Performans Benchmark Başlatılıyor...")
    print(f"[INFO] Iterasyonlar: {INFERENCE_ITERATIONS} (Warmup: {WARMUP_ITERATIONS})")

    # Başlangıç güç/termal bilgisi
    initial_power = collect_power_thermal()

    # Benchmark çalıştır
    results = []

    if FP16_ENGINE.exists():
        fp16_result = run_benchmark(FP16_ENGINE)
        results.append(fp16_result)

    if DLA0_ENGINE.exists():
        dla0_result = run_benchmark(DLA0_ENGINE, dla_core=0)
        results.append(dla0_result)

    if DLA1_ENGINE.exists():
        dla1_result = run_benchmark(DLA1_ENGINE, dla_core=1)
        results.append(dla1_result)

    # Son güç/termal bilgisi
    final_power = collect_power_thermal()

    # Benchmark raporu oluştur
    report = {
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
        "test_setup": {
            "iterations": INFERENCE_ITERATIONS,
            "warmup_iterations": WARMUP_ITERATIONS,
        },
        "power_thermal": {
            "initial": initial_power,
            "final": final_power,
        },
        "results": results,
        "comparison": {},
    }

    # Karşılaştırma verileri
    if len(results) >= 2:
        baseline = results[0]  # İlk sonuç baseline kabul edilir

        for result in results[1:]:
            engine_name = result["engine_name"]
            report["comparison"][engine_name] = {
                "fps_improvement": (result["fps"] / baseline["fps"] - 1) * 100,
                "latency_improvement": (baseline["avg_latency_ms"] / result["avg_latency_ms"] - 1) * 100,
                "fps_ratio": result["fps"] / baseline["fps"],
                "latency_ratio": result["avg_latency_ms"] / baseline["avg_latency_ms"],
            }

    # Raporu kaydet
    report_path = MODEL_DIR / "deployment_artifacts" / "benchmark_report.json"
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2))

    print(f"\n[SUCCESS] Benchmark tamamlandı!")
    print(f"[INFO] Rapor: {report_path}")

    # Konsol özeti
    print(f"\n{'='*60}")
    print(f"BENCHMARK ÖZETİ:")
    print(f"{'='*60}")

    for result in results:
        print(f"\n{result['engine_name'].upper()}:")
        print(f"  FPS:            {result['fps']:.1f}")
        print(f"  Avg Latency:    {result['avg_latency_ms']:.1f} ms")
        print(f"  GPU Compute:    {result['gpu_compute']:.1f}%")
        print(f"  GPU Memory:     {result['gpu_memory']:.1f}%")

    if report["comparison"]:
        print(f"\n{'='*60}")
        print(f"KARŞILAŞTIRMA (vs Baseline):")
        print(f"{'='*60}")

        for engine_name, comp in report["comparison"].items():
            print(f"\n{engine_name.upper()}:")
            print(f"  FPS İyileşme:     {comp['fps_improvement']:+.1f}%")
            print(f"  Latency İyileşme: {comp['latency_improvement']:+.1f}%")
            print(f"  FPS Oranı:        {comp['fps_ratio']:.2f}x")
            print(f"  Latency Oranı:    {comp['latency_ratio']:.2f}x")

    print(f"\n{'='*60}")
    return True

if __name__ == "__main__":
    main()