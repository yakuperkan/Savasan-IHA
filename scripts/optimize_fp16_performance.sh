#!/bin/bash
# FP16 Performance Optimizasyon Script'i

echo "⚡ FP16 Performance Optimizasyonu"
echo "================================"
echo ""

# Mevcut durumu analiz et
echo "📊 Mevcut performans analizi:"
echo "GPU Durumu:"
nvidia-smi --query-gpu=temperature,utilization.gpu,memory.used,memory.total --format=csv,noheader
echo ""

# Jetson özelinde tegrastats
if command -v tegrastats &> /dev/null; then
    echo "Jetson Stats (5 saniye):"
    timeout 5 tegrastats --interval 1000 2>/dev/null | head -5
    echo ""
fi

# DeepStream config optimizasyonu
CONFIG_FILE="/home/nvidia/Savasan_IHA_Workspace/02_Ana_Sistem_CPP/config/deepstream/config_infer_primary.txt"

echo "🔧 Optimizasyon önerileri:"
echo ""

# 1. Interval ayarı
INTERVAL=$(grep "^interval=" "$CONFIG_FILE" | cut -d'=' -f2)
echo "1) Interval: $INTERVAL"
echo "   - Her $INTERVAL karede bir inference yapılıyor"
echo "   - Daha yüksek FPS için: interval=0 (her kare) veya interval=1 (mevcut)"
echo "   - Düşük CPU yükü için: interval=2 veya 3"
echo ""

# 2. Pre-cluster threshold
THRESHOLD=$(grep "^pre-cluster-threshold=" "$CONFIG_FILE" | cut -d'=' -f2)
echo "2) Pre-cluster threshold: $THRESHOLD"
echo "   - Düşük threshold = daha fazla detection = daha CPU yükü"
echo "   - Yüksek threshold = daha az detection = daha az CPU yükü"
echo "   - Öneri: 0.30-0.40 arası deneyebilirsin"
echo ""

# 3. Batch size
BATCH=$(grep "^batch-size=" "$CONFIG_FILE" | cut -d'=' -f2)
echo "3) Batch size: $BATCH"
echo "   - Batch size 1 = düşük gecikme ama düşük GPU utilization"
echo "   - Batch size 2-4 = daha iyi GPU utilization ama daha yüksek gecikme"
echo ""

# 4. Network mode
NET_MODE=$(grep "^network-mode=" "$CONFIG_FILE" | cut -d'=' -f2)
echo "4) Network mode: $NET_MODE"
echo "   - 0: FP32 (yavaş, en hassas)"
echo "   - 1: INT8 (hızlı, kalibrasyon gerektirir)"
echo "   - 2: FP16 (mevcut, dengeli)"
echo "   - 3: INT8 + FP16 fallback"
echo ""

echo "🚀 Hızlı optimizasyon seçenekleri:"
echo "A) Interval artır (CPU yükünü azalt)"
echo "B) Threshold artır (daha az detection)"
echo "C) Batch size artır (GPU utilization)"
echo "D) Mevcut ayarları koru"
echo ""
read -p "Seçimin (A/B/C/D): " opt_choice

case $opt_choice in
    A)
        echo "📈 Interval artırılıyor..."
        sed -i 's/^interval=.*/interval=2/' "$CONFIG_FILE"
        echo "✅ Interval = 2 yapıldı (her 2 karede bir inference)"
        ;;
    B)
        echo "🎯 Threshold artırılıyor..."
        sed -i 's/^pre-cluster-threshold=.*/pre-cluster-threshold=0.35/' "$CONFIG_FILE"
        echo "✅ Threshold = 0.35 yapıldı (daha az detection)"
        ;;
    C)
        echo "📦 Batch size artırılıyor..."
        sed -i 's/^batch-size=.*/batch-size=2/' "$CONFIG_FILE"
        echo "✅ Batch size = 2 yapıldı (daha iyi GPU utilization)"
        ;;
    D)
        echo "✅ Mevcut ayarlar korundu"
        ;;
    *)
        echo "❌ Geçersiz seçim"
        exit 1
        ;;
esac

echo ""
echo "🧪 Test için:"
echo "source ~/envs/yolo26/bin/activate"
echo "cd ~/Savasan_IHA_Workspace"
echo "./run_phase2.sh"
echo ""
echo "⚡ FPS ve GPU utilization'ı izle!"