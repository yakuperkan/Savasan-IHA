# 📚 Savaşan İHA - Referanslar ve Kaynaklar

Bu doküman, Savaşan İHA projesinde kullanılan tüm referansları, kaynakları ve third-party kütüphaneleri tek bir yerde toplar.

---

## 🛠️ Temel Teknolojiler

### NVIDIA DeepStream SDK
- **Versiyon:** 6.2+
- **Lisans:** Proprietary
- **Amaç:** Gerçek zamanlı görüntü işleme pipeline
- **Dokümantasyon:** [NVIDIA DeepStream SDK Documentation](https://docs.nvidia.com/metropolis/deepstream/)
- **Kurulum:** JetPack ile otomatik
- **Kullanım:** Ana pipeline, nvinfer, nvtracker, OSD

### TensorRT
- **Versiyon:** 8.5+
- **Lisans:** Proprietary
- **Amaç:** Derin öğrenme modeli optimizasyonu
- **Dokümantasyon:** [NVIDIA TensorRT Documentation](https://docs.nvidia.com/deeplearning/tensorrt/)
- **Kullanım:** Model → ONNX → TensorRT Engine dönüşümü

### CUDA
- **Versiyon:** 11.4+
- **Lisans:** Proprietary
- **Amaç:** GPU hesaplama
- **Dokümantasyon:** [NVIDIA CUDA Toolkit](https://developer.nvidia.com/cuda-toolkit)

### GStreamer
- **Versiyon:** 1.20+
- **Lisans:** LGPL
- **Amaç:** Medya pipeline framework
- **Dokümantasyon:** [GStreamer Documentation](https://gstreamer.freedesktop.org/documentation/)
- **Kullanım:** DeepStream'in alt yapısı

---

## 🧠 AI Modelleri

### YOLO11 (Ultralytics)
- **Sürüm:** v8.x
- **Lisans:** GPL-3.0
- **Kaynak:** [YOLO11 GitHub](https://github.com/ultralytics/ultralytics)
- **Model Tipleri:** Detection
- **Input Size:** 640x640, 1280x1280
- **Optimizasyon:** FP16, INT8
- **Kullanım:** UAV hedef tespiti

### YOLO26 (Ultralytics Fork)
- **Sürüm:** Özel geliştirme
- **Lisans:** GPL-3.0
- **Kaynak:** [YOLO GitHub](https://github.com/ultralytics/ultralytics)
- **Model Tipleri:** Advanced Detection
- **Input Size:** Çoklu boyut
- **Optimizasyon:** FP32, FP16, INT8
- **Kullanım:** Yüksek doğruluklu tespit

---

## 🔄 Tracker Algoritmaları

### NvDCF (NVIDIA Deep Convolutional Filter)
- **Lisans:** Proprietary (DeepStream içinde)
- **Amaç:** Yüksek performanslı nesne takibi
- **Dokümantasyon:** DeepStream SDK
- **Avantajlar:**
  - GPU tabanlı hesaplama
  - Gerçek zamanlı performans
  - Düşük CPU kullanımı
- **Kullanım:** Ana tracker sistemi

### ByteTrack
- **Kaynak:** [ByteTrack GitHub](https://github.com/ifzhang/ByteTrack)
- **Lisans:** GPL-3.0
- **Durum:** Referans olarak incelendi (98_Reference_Repos/)
- **Neden:** NvDCF ile performans kıyaslaması için

### DeepSORT
- **Kaynak:** [DeepSORT GitHub](https://github.com/mikel-brostrom/Yolov5_DeepSort_Pytorch)
- **Lisans:** MIT
- **Durum:** Referans olarak incelendi (98_Reference_Repos/)
- **Neden:** Tracker algoritma araştırması için

---

## 📷 Kamera Modülleri

### CSI Kamera (MIPI CSI-2)
- **Lisans:** Linux Kernel (GPL)
- **Dökümantasyon:** [NVIDIA Jetson Camera](https://docs.nvidia.com/jetson/jetson-arch/index.html#camera_sensors)
- **Sürücüler:** JetPack ile otomatik
- **Performans:** Yüksek bant genişliği, düşük gecikme

### USB Kamera (V4L2)
- **Lisans:** Linux Kernel (GPL)
- **Dökümantasyon:** [Video4Linux2 API](https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/v4l2.html)
- **Sürücüler:** Linux kernel
- **Kullanım:** `/dev/video0`, `/dev/video1`

---

## 🐧 Linux ve Sistem

### Ubuntu 20.04 LTS
- **Versiyon:** 20.04.x
- **Lisans:** Open Source
- **Amaç:** İşletim sistemi

### JetPack 5.1+
- **Versiyon:** 5.1.x
- **Lisans:** Proprietary
- **Amaç:** Jetson platformu için SDK
- **İçerikler:**
  - CUDA
  - TensorRT
  - DeepStream
  - OpenCV
  - GStreamer

---

## 📚 Geliştirme Araçları

### CMake
- **Versiyon:** 3.16+
- **Lisans:** BSD-3-Clause
- **Amaç:** Build sistem

### GCC/G++
- **Versiyon:** 9.4+
- **Lisans:** GPL
- **Amaç:** C++ derleyici

### Python 3
- **Versiyon:** 3.8+
- **Lisans:** PSF
- **Amaç:** Script ve prototip geliştirme

---

## 🧩 Third-Party Kütüphaneler

### OpenCV
- **Versiyon:** 4.5+
- **Lisans:** Apache-2.0
- **Kaynak:** [OpenCV GitHub](https://github.com/opencv/opencv)
- **Kullanım:** Görüntü işleme, kamera arabirimleri

### Protobuf
- **Versiyon:** 3.x
- **Lisans:** BSD-3-Clause
- **Kullanım:** Veri serileştirme

### nvdsinfer_customparser
- **Lisans:** Proprietary (DeepStream sample)
- **Kullanım:** YOLO parse için custom parser

---

## 📚 Referans Repolar (Yerel)

### DeepStream Reference Apps
- **Yol:** `98_Reference_Repos/deepstream_reference_apps/`
- **Lisans:** Apache-2.0
- **Amaç:** DeepStream örnekleri
- **Kaynak:** [NVIDIA DeepStream Reference Apps](https://github.com/NVIDIA-AI-IOT/deepstream_reference_apps)

### DeepStream Python Apps
- **Yol:** `98_Reference_Repos/deepstream_python_apps/`
- **Lisans:** Apache-2.0
- **Amaç:** Python DeepStream örnekleri
- **Kaynak:** [NVIDIA DeepStream Python Apps](https://github.com/NVIDIA-AI-IOT/deepstream_python_apps)

### DeepStream-Yolo
- **Yol:** `98_Reference_Repos/DeepStream-Yolo-master/`
- **Lisans:** MIT
- **Amaç:** YOLO entegrasyonu referansı
- **Kaynak:** [DeepStream-Yolo](https://github.com/marcoslucianops/DeepStream-Yolo)

### TensorRT Samples
- **Yol:** `98_Reference_Repos/TensorRT/`
- **Lisans:** Proprietary
- **Amaç:** TensorRT örnekleri

### MAVLink C Library
- **Yol:** `98_Reference_Repos/mavlink_c_library_v2/`
- **Lisans:** LGPL-3.0
- **Amaç:** MAVLink iletişim protokolü
- **Kaynak:** [MAVLink](https://mavlink.io/en/)

---

## 🏗️ Build ve Deployment

### SystemD Service
- **Dosya:** `02_Ana_Sistem_CPP/config/systemd/savasan-airlock.service`
- **Lisans:** Open Source
- **Amaç:** Otomatik başlatma

### Deployment Scriptler
- **Konum:** `scripts/`
- **Scriptler:**
  - `run_phase1.sh` - Phase 1 çalıştırma
  - `run_phase2.sh` - Phase 2 çalıştırma
  - `run_phase3.sh` - Phase 3 çalıştırma
  - `analyze_fps_bottleneck.sh` - FPS analizi
  - `setup_60fps.sh` - 60 FPS kurulumu

---

## 📖 Dokümantasyon Kaynakları

### NVIDIA Dokümantasyon
- [DeepStream SDK Documentation](https://docs.nvidia.com/metropolis/deepstream/)
- [TensorRT Documentation](https://docs.nvidia.com/deeplearning/tensorrt/)
- [CUDA Programming Guide](https://docs.nvidia.com/cuda/cuda-c-programming-guide/)
- [Jetson Platform](https://developer.nvidia.com/jetson-platform)

### YOLO Dokümantasyon
- [Ultralytics Documentation](https://docs.ultralytics.com/)
- [YOLO GitHub Repository](https://github.com/ultralytics/ultralytics)

### Open Source Dokümantasyon
- [GStreamer Documentation](https://gstreamer.freedesktop.org/documentation/)
- [OpenCV Documentation](https://docs.opencv.org/)
- [V4L2 Documentation](https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/v4l2.html)

---

## 🔗 Online Kaynaklar

### Forumlar ve Topluluklar
- [NVIDIA Developer Forums](https://forums.developer.nvidia.com/)
- [Jetson Forum](https://forums.developer.nvidia.com/c/agx-jetson/jetson-embedded-systems/70)
- [Stack Overflow - DeepStream](https://stackoverflow.com/questions/tagged/deepstream)

### Bloglar ve Makaleler
- [NVIDIA Blog](https://developer.nvidia.com/blog/)
- [Ultralytics Blog](https://ultralytics.com/blog)

### Video Eğitimler
- [NVIDIA YouTube](https://www.youtube.com/user/nvidia)
- [DeepStream Tutorials](https://www.youtube.com/results?search_query=deepstream+tutorial)

---

## 📜 Lisanslar Özeti

### Proprietary (Kapalı Kaynak)
- NVIDIA DeepStream SDK
- NVIDIA TensorRT
- NVIDIA CUDA

### Open Source (Açık Kaynak)
- **GPL-3.0:** YOLO11, YOLO26, ByteTrack
- **MIT:** OpenCV, DeepSORT
- **Apache-2.0:** DeepStream Reference Apps
- **BSD-3-Clause:** CMake, Protobuf
- **LGPL-3.0:** GStreamer, MAVLink

### Proje Lisansı
- **Savaşan İHA:** MIT License
- **Detaylar:** [LICENSE](../LICENSE) dosyasını inceleyin

---

## 🧪 Test ve Benchmarking

### Performance Benchmarking
- **Script:** `scripts/analyze_fps_bottleneck.sh`
- **Metricler:** FPS, CPU, GPU, Memory, Latency
- **Araçlar:** `tegrastats`, `nvtop`, `htop`

### Model Benchmarking
- **YOLO11 Benchmark:** scripts/yolo11_benchmark.sh (hazırlanıyor)
- **YOLO26 Benchmark:** scripts/yolo26_benchmark.sh (hazırlanıyor)

---

## 🌐 Versiyon Uyumluluğu

### Donanım Versiyonları
| Platform | JetPack | DeepStream | TensorRT | CUDA |
|----------|---------|------------|----------|------|
| Jetson Orin NX | 5.1+ | 6.2+ | 8.5+ | 11.4+ |
| Jetson Orin Nano | 5.1+ | 6.2+ | 8.5+ | 11.4+ |
| Jetson Xavier NX | 4.6+ | 6.0+ | 8.2+ | 11.4+ |

### Yazılım Versiyonları
| Yazılım | Minimum | Önerilen |
|---------|---------|----------|
| GCC | 9.4 | 11.0+ |
| Python | 3.8 | 3.10 |
| OpenCV | 4.5 | 4.6+ |
| GStreamer | 1.20 | 1.22+ |

---

## 📞 Destek ve Yardım

### Resmi Destek
- **NVIDIA:** [NVIDIA Developer Support](https://developer.nvidia.com/support)
- **Ultralytics:** [YOLO GitHub Issues](https://github.com/ultralytics/ultralytics/issues)

### Topluluk Destek
- **Savaşan İHA Issues:** [GitHub Issues](https://github.com/yakuperkan/Savasan_IHA_Jetson/issues)
- **Jetson Forum:** [NVIDIA Jetson Forum](https://forums.developer.nvidia.com/c/agx-jetson/jetson-embedded-systems/70)

---

## 🔧 Geliştirici Kaynakları

### IDE ve Araçlar
- **VS Code:** [Visual Studio Code](https://code.visualstudio.com/)
- **CLion:** [JetBrains CLion](https://www.jetbrains.com/clion/)
- **GDB:** GNU Debugger

### Code Quality
- **Clang-Tidy:** C++ linter
- **Valgrind:** Memory debugger
- **AddressSanitizer:** Memory sanitizer

---

## 📋 Checklist

### Yeni Geliştirici İçin
- [ ] DeepStream SDK dokümantasyonunu oku
- [ ] YOLO11 GitHub repository'sini incele
- [ ] Jetson Orin NX hardware'ı öğren
- [ ] TensorRT'e model export etmeyi öğren
- [ ] NvDCF tracker konfigürasyonunu anla
- [ ] Referans repoları incele

### Production İçin
- [ ] Tüm testleri çalıştır
- [ ] Performance benchmark yap
- [ ] Security review gerçekleştir
- [ ] Dokümantasyon güncelle
- [ ] Deployment scriptlerini test et

---

**Son Güncelleme:** 2026-04-10

Bu doküman sürekli güncellenmektedir. Yeni kaynaklar eklendikçe buraya eklenecektir.