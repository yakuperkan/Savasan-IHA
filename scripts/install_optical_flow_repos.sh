#!/bin/bash

# Optical Flow Repoları İndirme Scripti
# Proje: Savaşan İHA
# Tarih: 2026-04-12

echo "========================================="
echo "Optical Flow Repoları İndirme Scripti"
echo "========================================="
echo ""

# Referans repolar klasörüne git
REPO_DIR="/home/nvidia/Savasan_IHA_Workspace/98_Reference_Repos"
cd "$REPO_DIR" || { echo "Referans repolar klasörü bulunamadı!"; exit 1; }

echo "Referans repolar klasörü: $REPO_DIR"
echo ""

# GitHub repolarını tanımla
declare -a repos=(
    "https://github.com/NVIDIA-AI-IOT/deepstream-opticalflow.git|deepstream-opticalflow"
    "https://github.com/princeton-vl/RAFT.git|RAFT"
    "https://github.com/Duankaiwen/Pytorch-RAFT.git|Pytorch-RAFT"
    "https://github.com/NVIDIA/flownet2-pytorch.git|flownet2-pytorch"
    "https://github.com/NVIDIA/PWC-Net.git|PWC-Net"
    "https://github.com/torrvision/farneback2011.git|farneback2011"
)

# Repoları indir
for repo in "${repos[@]}"; do
    IFS='|' read -r url name <<< "$repo"

    echo "Indiriliyor: $name"
    echo "URL: $url"

    if [ -d "$name" ]; then
        echo "⚠️  Klasör zaten var: $name (güncelleniyor...)"
        cd "$name" || continue
        git pull
        cd ..
    else
        git clone "$url"
        if [ $? -eq 0 ]; then
            echo "✅ Başarıyla indirildi: $name"
        else
            echo "❌ İndirme hatası: $name"
        fi
    fi

    echo ""
done

echo "========================================="
echo "İndirme İşlemi Tamamlandı"
echo "========================================="
echo ""

# Özet göster
echo "İndirilen/Var olan repolar:"
for repo in "${repos[@]}"; do
    IFS='|' read -r url name <<< "$repo"
    if [ -d "$name" ]; then
        echo "✅ $name"
    else
        echo "❌ $name (indirilemedi)"
    fi
done

echo ""
echo "Dokümantasyon için bkz:"
echo "  - docs/OPTICAL_FLOW_RESOURCES.md"
echo "  - docs/OPTICAL_FLOW_CPP_INTEGRATION_GUIDE.md"
echo "  - docs/OPTICAL_FLOW_QUICK_REFERENCE.md"
echo ""

echo "Optical flow plugin kontrolü:"
gst-inspect-1.0 nvof >/dev/null 2>&1
if [ $? -eq 0 ]; then
    echo "✅ nvof plugin yüklü"
else
    echo "❌ nvof plugin bulunamadı - DeepStream kurulumunu kontrol edin"
fi

gst-inspect-1.0 nvofvisual >/dev/null 2>&1
if [ $? -eq 0 ]; then
    echo "✅ nvofvisual plugin yüklü"
else
    echo "❌ nvofvisual plugin bulunamadı - DeepStream kurulumunu kontrol edin"
fi