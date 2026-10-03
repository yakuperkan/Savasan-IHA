#!/bin/bash
# ==============================================================================
# SAVASAN IHA - 60 FPS VE DUSUK LATENCY KURULUM SCRIPT'I
# Orin NX performansi icin gerekli sistem ayarlarini yapar.
# ==============================================================================

echo "--- 60 FPS Optimizasyonlari Baslatiliyor ---"

# 1. Maksimum Performans Modu (85W)
echo "[1/5] Maksimum performans modu (nvpmodel -m 0)..."
sudo nvpmodel -m 0

# 2. Clock Frekanslarini Sabitle
echo "[2/5] Frekanslari maksimuma sabitleme (jetson_clocks)..."
sudo jetson_clocks

# 3. CPU Governor Performance Modu
echo "[3/5] CPU governor performance ayari..."
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor

# 4. Fan Kontrolu (Maksimum sogutma)
echo "[4/5] Fan hizi maksimuma (255)..."
echo 255 | sudo tee /sys/devices/platform/pwm-fan/hwmon/hwmon*/pwm1

# 5. GStreamer ve DeepStream Debug Loglarini Kapat
echo "[5/5] Debug loglari kapatiliyor (ENV)..."
export GST_DEBUG=0
export NVDS_DEBUG=0
export NVDS_LOG_LEVEL=3

# 6. Swap Off (Opsiyonel - 16GB RAM varsa onerilir)
# echo "[6/6] Swap devre disi birakiliyor..."
# sudo swapoff -a

echo "--- Kurulum Tamamlandi ---"
echo "Simdi uygulamanizi calistirabilirsiniz."
echo "Ornek: ./02_Ana_Sistem_CPP/build/savasan_iha phase3 usb display"
