#!/bin/bash
# INT8 DLA Deployment - Ana Workflow
# Canlı izleme için: tail -f /tmp/int8_deployment.log

set -e  # Hata durumunda çık

LOG_FILE="/tmp/int8_deployment.log"
MODEL_DIR="/home/nvidia/Savasan_IHA_Workspace/03_Modeller/yolo26_uav22"

echo "$(date '+%Y-%m-%d %H:%M:%S') [START] INT8 DLA Deployment Workflow başlatılıyor" | tee -a "$LOG_FILE"
echo "Model Dizini: $MODEL_DIR" | tee -a "$LOG_FILE"

cd "$MODEL_DIR"

# Adım 1: FP16 Baseline Engine
echo "$(date '+%Y-%m-%d %H:%M:%S') [STEP 1] FP16 Baseline Engine oluşturuluyor..." | tee -a "$LOG_FILE"
echo "[CANLI İZLEME] tail -f /tmp/fp16_build.log" | tee -a "$LOG_FILE"

if python3 01_build_fp16_baseline.py 2>&1 | tee -a "$LOG_FILE"; then
    echo "$(date '+%Y-%m-%d %H:%M:%S') [STEP 1] ✅ FP16 Baseline Engine tamamlandı" | tee -a "$LOG_FILE"
else
    echo "$(date '+%Y-%m-%d %H:%M:%S') [STEP 1] ❌ FP16 Baseline Engine başarısız" | tee -a "$LOG_FILE"
    exit 1
fi

# Adım 2: INT8 Calibration Cache
echo "$(date '+%Y-%m-%d %H:%M:%S') [STEP 2] INT8 Calibration Cache oluşturuluyor..." | tee -a "$LOG_FILE"
echo "[CANLI İZLEME] tail -f /tmp/int8_calib_build.log" | tee -a "$LOG_FILE"

if python3 02_build_int8_calibration.py 2>&1 | tee -a "$LOG_FILE"; then
    echo "$(date '+%Y-%m-%d %H:%M:%S') [STEP 2] ✅ INT8 Calibration Cache tamamlandı" | tee -a "$LOG_FILE"
else
    echo "$(date '+%Y-%m-%d %H:%M:%S') [STEP 2] ❌ INT8 Calibration Cache başarısız" | tee -a "$LOG_FILE"
    exit 1
fi

# Adım 3: INT8 DLA Engine'leri
echo "$(date '+%Y-%m-%d %H:%M:%S') [STEP 3] INT8 DLA Engine'leri oluşturuluyor..." | tee -a "$LOG_FILE"
echo "[CANLI İZLEME] tail -f /tmp/int8_dla0_build.log veya /tmp/int8_dla1_build.log" | tee -a "$LOG_FILE"

if python3 03_build_int8_dla_engines.py 2>&1 | tee -a "$LOG_FILE"; then
    echo "$(date '+Y-%m-%d %H:%M:%S') [STEP 3] ✅ INT8 DLA Engine'leri tamamlandı" | tee -a "$LOG_FILE"
else
    echo "$(date '+Y-%m-%d %H:%M:%S') [STEP 3] ❌ INT8 DLA Engine'leri başarısız" | tee -a "$LOG_FILE"
    exit 1
fi

# Adım 4: Layer Analizi
echo "$(date '+%Y-%m-%d %H:%M:%S') [STEP 4] Layer Analizi yapılıyor..." | tee -a "$LOG_FILE"
echo "[CANLI İZLEME] tail -f /tmp/layer_*.log" | tee -a "$LOG_FILE"

if python3 04_analyze_layer_info.py 2>&1 | tee -a "$LOG_FILE"; then
    echo "$(date '+%Y-%m-%d %H:%M:%S') [STEP 4] ✅ Layer Analizi tamamlandı" | tee -a "$LOG_FILE"
else
    echo "$(date '+%Y-%m-%d %H:%M:%S') [STEP 4] ❌ Layer Analizi başarısız" | tee -a "$LOG_FILE"
    exit 1
fi

# Adım 5: Benchmark
echo "$(date '+%Y-%m-%d %H:%M:%S') [STEP 5] Engine Benchmark çalıştırılıyor..." | tee -a "$LOG_FILE"
echo "[CANLI İZLEME] tail -f /tmp/benchmark_*.log" | tee -a "$LOG_FILE"
echo "[NOT] Bu adım birkaç dakika sürebilir..." | tee -a "$LOG_FILE"

if python3 05_benchmark_engines.py 2>&1 | tee -a "$LOG_FILE"; then
    echo "$(date '+%Y-%m-%d %H:%M:%S') [STEP 5] ✅ Engine Benchmark tamamlandı" | tee -a "$LOG_FILE"
else
    echo "$(date '+%Y-%m-%d %H:%M:%S') [STEP 5] ❌ Engine Benchmark başarısız" | tee -a "$LOG_FILE"
    exit 1
fi

# Adım 6: DeepStream Config Önerileri
echo "$(date '+%Y-%m-%d %H:%M:%S') [STEP 6] DeepStream Config Önerileri oluşturuluyor..." | tee -a "$LOG_FILE"

if python3 06_deepstream_config.py 2>&1 | tee -a "$LOG_FILE"; then
    echo "$(date '+%Y-%m-%d %H:%M:%S') [STEP 6] ✅ DeepStream Config Önerileri tamamlandı" | tee -a "$LOG_FILE"
else
    echo "$(date '+%Y-%m-%d %H:%M:%S') [STEP 6] ❌ DeepStream Config Önerileri başarısız" | tee -a "$LOG_FILE"
    exit 1
fi

# Tamamlandı
echo "$(date '+%Y-%m-%d %H:%M:%S') [FINISH] ✅ Tüm workflow başarıyla tamamlandı!" | tee -a "$LOG_FILE"
echo "Deployment Artifacts: $MODEL_DIR/deployment_artifacts/" | tee -a "$LOG_FILE"
echo "Ana Log: $LOG_FILE" | tee -a "$LOG_FILE"

echo ""
echo "==================================================================="
echo "🎉 INT8 DLA DEPLOYMENT TAMAMLANDI! 🎉"
echo "==================================================================="
echo ""
echo "Oluşturulan Engine'ler:"
ls -lh *.engine 2>/dev/null || echo "Engine dosyaları bulunamadı"

echo ""
echo "Deployment Artifacts:"
ls -lh deployment_artifacts/ 2>/dev/null || echo "Artifacts bulunamadı"

echo ""
echo "Canlı İzleme Komutları:"
echo "  Ana log:        tail -f /tmp/int8_deployment.log"
echo "  FP16 build:     tail -f /tmp/fp16_build.log"
echo "  INT8 calib:     tail -f /tmp/int8_calib_build.log"
echo "  DLA build:      tail -f /tmp/int8_dla*.log"
echo "  Layer info:     tail -f /tmp/layer_*.log"
echo "  Benchmark:      tail -f /tmp/benchmark_*.log"

echo ""
echo "Sonraki adım: deployment_artifacts/ içindeki raporları inceleyin"
echo "==================================================================="