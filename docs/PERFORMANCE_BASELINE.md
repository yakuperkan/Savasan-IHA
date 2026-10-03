# Performance Baseline

Optimizasyon değişiklikleri yalnızca Jetson üzerinde ölçümle kabul edilir.

---

## INT8 QDQ trtexec A/B (2026-07-01)

**Koşu:** `run_ab_benchmark.sh`, trtexec `--loadEngine`, warmup=200, iterations=1000.
**Not:** FP16 = `yolo26_uav22` 1280; INT8 = `yolo26s_int8_qdq` 960 — farklı model + çözünürlük.

| Engine | Throughput | Latency mean | Latency p95 |
|--------|------------|--------------|-------------|
| FP16 1280 GPU | 25.3 qps | 41.0 ms | 49.7 ms |
| **INT8 QDQ 960 GPU** | **49.5 qps** | **20.8 ms** | **24.1 ms** |
| INT8 QDQ 960 DLA0 | 11.7 qps | 85.8 ms | 92.9 ms |

**DeepStream smoke (INT8 GPU, phase5 usb, 25 sn):** ~60 FPS, ~17 ms pipeline latency, infer ~7 ms.

**Karar:** Üretim adayı GPU INT8 (`config_infer_primary_yolo26s_int8_qdq.txt`). DLA bu modelde yavaş (hibrit fallback).

Log: `03_Modeller/int8_qdq/logs/ab_benchmark_20260701_110145.txt`

---

## Son doğrulanmış baseline (2026-06-10)

**Koşu:** `phase5 usb hybrid`, fakesink (`SAVASAN_DISPLAY=`), YOLO26 FP16, tracker NvDCF, `SAVASAN_ALC_DISABLE=1`, `NVDS_ENABLE_LATENCY_MEASUREMENT=1`, 25 sn.

| Profil | nvinfer interval | FPS avg | FPS p95 | Latency p95 | GPU avg | VDD avg |
|--------|------------------|---------|---------|-------------|---------|---------|
| MAXN (mode 0) | 2 | **57.8** | 59.2 | **61 ms** | 97.6% | 14.2 W |
| MAXN (mode 0) | 1 | 47.9 | 50.4 | 78 ms | 98.6% | 15.7 W |
| 25W (mode 3) | 2 | 45.6 | 58.9 | 132 ms | 95.5% | 11.9 W |
| 25W (mode 3) | 1 | 18.4 | 19.5 | 196 ms | 98.5% | 10.5 W |

**Karar:** `interval=2` korunur. `SAVASAN_AIRLOCK_NVINFER_INTERVAL=1` prod'da kullanılmaz.

Ham log örnekleri: `/tmp/nvinfer_ab_maxn_*`, `/tmp/nvinfer_ab_*` (Jetson'da).

---

## Ölçüm prosedürü

```bash
# Güç modu (uçuşta MAXN)
nvpmodel -q

# Headless latency test
export SAVASAN_DISPLAY=
export NVDS_ENABLE_LATENCY_MEASUREMENT=1
export SAVASAN_RUN_SECONDS=25
export SAVASAN_ALC_DISABLE=1
export SAVASAN_SETPOINT_TX_ENABLE=0

bash scripts/run_latency_test.sh phase5 usb

# Uzun koşu benchmark
./scripts/benchmark_phase3.sh usb 120
```

Paralel güç/ısı:

```bash
tegrastats --interval 500
```

Telemetri satırları (`~1 Hz`):

```
[TELEMETRİ] FPS: … | Latency: … ms | GPU: …%
```

CSV export: `SAVASAN_TELEMETRY_CSV=/tmp/metrics.csv`

---

## Kabul kuralı

Değişikliği yalnızca şu durumda merge et:

1. FPS veya latency tutarlı iyileşir (en az 2 post-change koşu)
2. Seri reconnect / GStreamer hata patlaması yok
3. Lock/track güvenilirliği regresyon göstermez

Regression gate:

```bash
./scripts/performance_regression_check.sh /path/to/baseline.log /path/to/candidate.log 5
```

---

## Önerilen test matrisi (yarışma öncesi)

| Senaryo | Komut / not |
|---------|-------------|
| USB headless | `run_latency_test.sh phase5 usb` |
| CSI headless | `run_latency_test.sh phase5 csi` |
| Gerçek seri dry-run | `SAVASAN_ALC_DISABLE=0 SAVASAN_SETPOINT_TX_ENABLE=0` |
| Tracker preset | `source scripts/load_tracker_preset.sh stable` |
| Güç modu | MAXN vs 25W karşılaştırması |

---

## nvinfer interval A/B (özet)

- **interval=2:** Her 3. karede YOLO; NvDCF ara kareleri taşır. MAXN'de ~58 FPS, ~62 ms p95 latency.
- **interval=1:** GPU tavanında infer sıklığı artar; FPS düşer, latency artar, güç tüketimi yükselir.
- **interval=0:** Koşulmadı; interval=1'den kötü beklenir.

Override (acil test only): `SAVASAN_AIRLOCK_NVINFER_INTERVAL=N`
