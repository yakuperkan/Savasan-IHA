#!/bin/bash
# UAV Dataset INT8 Kalibrasyon Hazırlama Script'i
# Hem YOLO11 hem YOLO26 için ortak kalibrasyon

set -e

echo "🚁 UAV Dataset INT8 Kalibrasyon Hazırlama"
echo "======================================="
echo ""

# Ana değişkenler
WORKSPACE="$HOME/Savasan_IHA_Workspace"
DATASET_DIR="$WORKSPACE/04_Denemeler_Sandbox/uav_datasets"
CALIB_DIR_COMMON="$WORKSPACE/03_Modeller/calibration_images_common"
VENV_NAME="yolo26"

# Kalibrasyon klasörlerini oluştur
mkdir -p "$CALIB_DIR_COMMON"
mkdir -p "$DATASET_DIR"

echo "📁 Kalibrasyon klasörleri oluşturuldu:"
echo "   Dataset: $DATASET_DIR"
echo "   Kalibrasyon: $CALIB_DIR_COMMON"
echo ""

# Dataset seçimi
echo "Dataset indirme yöntemi seç:"
echo "1) Kaggle (VisDrone) - En kolayı"
echo "2) GitHub (VisDrone) - Alternatif"
echo "3) GitHub (UAVDT) - Video tracking"
echo "4) Zaten dataset'im var, sadece hazırla"
echo ""
read -p "Seçimin (1-4): " choice

case $choice in
    1)
        echo "📥 VisDrone dataset'i Kaggle'dan indiriliyor..."
        if ! command -v kaggle &> /dev/null; then
            echo "⚠️  Kaggle CLI kurulu değil. Kurmak ister misin? (y/n)"
            read -p "Cevap: " install_kaggle
            if [ "$install_kaggle" = "y" ]; then
                pip install kaggle
                echo "🔑 Kaggle API key'inin ~/ .kaggle/kaggle.json dosyasına kaydedildiğinden emin ol"
            else
                echo "❌ Kaggle olmadan devam edilemiyor"
                exit 1
            fi
        fi

        cd "$DATASET_DIR"
        kaggle datasets download -d visdrone/visdrone
        unzip visdrone.zip
        echo "✅ VisDrone indirildi ve çıkarıldı"

        # VisDrone görüntülerini bul
        VISDRONE_IMG="$DATASET_DIR/VisDrone2019-DET-train/images"
        if [ -d "$VISDRONE_IMG" ]; then
            echo "📸 VisDrone görüntüleri kopyalanıyor..."
            cp "$VISDRONE_IMG"/*.jpg "$CALIB_DIR_COMMON/" 2>/dev/null || true
            echo "✅ Görüntüler kopyalandı"
        fi
        ;;

    2)
        echo "📥 VisDrone dataset'i GitHub'dan indiriliyor..."
        git clone https://github.com/VisDrone/VisDrone-Dataset.git "$DATASET_DIR/VisDrone"

        # Görüntüleri bul ve kopyala
        find "$DATASET_DIR/VisDrone" -name "*.jpg" -type f | head -600 | xargs -I {} cp {} "$CALIB_DIR_COMMON/"
        echo "✅ VisDrone görüntüleri kopyalandı"
        ;;

    3)
        echo "📥 UAVDT dataset'i GitHub'dan indiriliyor..."
        git clone https://github.com/xyaowen/UAVDT.git "$DATASET_DIR/UAVDT"
        echo "⚠️  UAVDT videolardan frame extraction gerekebilir"
        echo "📝 Manual frame extraction script'i oluşturuluyor..."

        # Basit frame extraction script'i
        cat > "$DATASET_DIR/extract_frames.py" << 'EOF'
import cv2
import os
from pathlib import Path

video_dir = Path("UAVDT/data")
output_dir = Path("extracted_frames")
output_dir.mkdir(exist_ok=True)

frame_count = 0
max_frames = 600

for video_file in video_dir.glob("*.mp4"):
    if frame_count >= max_frames:
        break

    cap = cv2.VideoCapture(str(video_file))
    frame_num = 0

    while True:
        ret, frame = cap.read()
        if not ret or frame_count >= max_frames:
            break

        if frame_num % 10 == 0:  # Her 10. frame'i al
            output_path = output_dir / f"frame_{frame_count:06d}.jpg"
            cv2.imwrite(str(output_path), frame)
            frame_count += 1
            if frame_count % 50 == 0:
                print(f"Extracted {frame_count} frames...")

        frame_num += 1

    cap.release()

print(f"✅ Total {frame_count} frames extracted")
EOF

        echo "🚀 Frame extraction için: cd $DATASET_DIR && source ~/envs/$VENV_NAME/bin/activate && python3 extract_frames.py"
        ;;

    4)
        echo "📁 Mevcut dataset'i belirt:"
        read -p "Dataset görüntü klasörü yolu: " user_dataset
        if [ -d "$user_dataset" ]; then
            echo "📸 Görüntüler kopyalanıyor..."
            find "$user_dataset" -name "*.jpg" -o -name "*.png" | head -600 | xargs -I {} cp {} "$CALIB_DIR_COMMON/"
            echo "✅ Görüntüler kopyalandı"
        else
            echo "❌ Klasör bulunamadı: $user_dataset"
            exit 1
        fi
        ;;

    *)
        echo "❌ Geçersiz seçim"
        exit 1
        ;;
esac

# Kalibrasyon görüntü sayısını kontrol et
IMG_COUNT=$(ls "$CALIB_DIR_COMMON"/*.jpg 2>/dev/null | wc -l)
echo ""
echo "📊 Kalibrasyon görüntü sayısı: $IMG_COUNT"

if [ "$IMG_COUNT" -lt 100 ]; then
    echo "⚠️  Daha fazla görüntü önerilir (500+ ideal)"
else
    echo "✅ Yeterli görüntü sayısı!"
fi

# YOLO11 ve YOLO26 için symlink oluştur
YOLO11_CALIB="$WORKSPACE/03_Modeller/yolo11_uav/calibration_images"
YOLO26_CALIB="$WORKSPACE/03_Modeller/yolo26_uav22/calibration_images"

mkdir -p "$YOLO11_CALIB" 2>/dev/null || true
mkdir -p "$YOLO26_CALIB" 2>/dev/null || true

# Mevcut görüntüleri koru, yeni görüntüleri ekle
for img in "$CALIB_DIR_COMMON"/*.jpg; do
    if [ -f "$img" ]; then
        basename_img=$(basename "$img")
        if [ ! -f "$YOLO11_CALIB/$basename_img" ]; then
            ln -s "$img" "$YOLO11_CALIB/$basename_img" 2>/dev/null || cp "$img" "$YOLO11_CALIB/"
        fi
        if [ ! -f "$YOLO26_CALIB/$basename_img" ]; then
            ln -s "$img" "$YOLO26_CALIB/$basename_img" 2>/dev/null || cp "$img" "$YOLO26_CALIB/"
        fi
    fi
done

echo "🔗 YOLO11 ve YOLO26 için kalibrasyon linkleri oluşturuldu"
echo ""
echo "📋 Sonraki adımlar:"
echo "1) INT8 build için:"
echo "   source ~/envs/$VENV_NAME/bin/activate"
echo "   cd $WORKSPACE/03_Modeller/yolo26_uav22"
echo "   python3 build_yolo26_int8_engine.py --calib-dir calibration_images --max-images $IMG_COUNT"
echo ""
echo "2) YOLO11 için:"
echo "   cd $WORKSPACE/03_Modeller/yolo11_uav"
echo "   python3 build_yolo11_int8_engine.py --calib-dir calibration_images --max-images $IMG_COUNT"
echo ""
echo "✅ Hazır!"