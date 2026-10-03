#!/bin/bash
# UAV Dataset İndirme Script'i
# VisDrone ve diğer UAV dataset'leri için

echo "🚁 UAV Dataset İndirme Script'i"
echo "================================"

# Dataset seçimi
echo ""
echo "Hangi dataset'i indirmek istiyorsun?"
echo "1) VisDrone (En popüler, çok çeşitli UAV görüntüleri)"
echo "2) UAVDT (UAV video tracking dataset)"
echo "3) Drone-vs-Bird (Drone vs kuş ayrımı)"
echo "4) AU-AIR (Aerial Universal Image dataset)"
echo "5) DOTA (Aerial image detection dataset)"
echo ""
read -p "Seçimin (1-5): " choice

# İndirme klasörü
DATASET_DIR="$HOME/Savasan_IHA_Workspace/04_Denemeler_Sandbox/uav_datasets"
mkdir -p "$DATASET_DIR"
cd "$DATASET_DIR"

case $choice in
    1)
        echo "📥 VisDrone dataset'i indiriliyor..."
        echo "VisDrone: en popüler UAV detection dataset'i"
        echo "10.000+ UAV görüntüsü, çeşitli senaryolar"
        echo ""
        echo "Manuel indirme gerekli (Google Drive ile):"
        echo "1) https://github.com/VisDrone/VisDrone-Dataset"
        echo "2) 'Task 1: Object Detection in Images' indir"
        echo "3) Train/Val/Test setlerini indir"
        echo ""
        echo "Alternatif: Kaggle'dan indirebilirsin"
        echo "kaggle datasets download -d visdrone/visdrone"
        ;;
    2)
        echo "📥 UAVDT dataset'i indiriliyor..."
        echo "UAVDT: UAV video tracking için optimize edilmiş"
        echo ""
        echo "İndirme linkleri:"
        echo "1) https://github.com/xyaowen/UAVDT"
        echo "2) Baidu Drive linklerinden indir"
        echo "3) Videolardan frame extraction gerekebilir"
        ;;
    3)
        echo "📥 Drone-vs-Bird dataset'i indiriliyor..."
        echo "Drone vs kuş ayrımı için özelleşmiş"
        echo ""
        echo "İndirme:"
        echo "https://github.com/michaelsgregg/drone-vs-bird-dataset"
        ;;
    4)
        echo "📥 AU-AIR dataset'i indiriliyor..."
        echo "Aerial Universal Image dataset"
        echo "32.000+ aerial image"
        echo ""
        echo "İndirme:"
        echo "https://github.com/https://github.com/cvlab-stonybrook/AU-AIR"
        ;;
    5)
        "📥 DOTA dataset'i indiriliyor..."
        echo "Large-scale aerial image dataset"
        echo "2.800+ aerial image"
        echo ""
        echo "İndirme:"
        echo "https://captain-whu.github.io/DOTA/"
        ;;
    *)
        echo "Geçersiz seçim!"
        exit 1
        ;;
esac

echo ""
echo "✅ Dataset indirildikten sonra kalibrasyon için hazırlama:"
echo "1) Dataset içindeki images klasörünü bul"
echo "2) Kalibrasyon klasörüne kopyala:"
echo "   cp -r /path/to/dataset/images/*.jpg $HOME/Savasan_IHA_Workspace/03_Modeller/yolo26_uav22/calibration_images/"
echo "3) Görüntü sayısını kontrol et:"
echo "   ls $HOME/Savasan_IHA_Workspace/03_Modeller/yolo26_uav22/calibration_images/ | wc -l"