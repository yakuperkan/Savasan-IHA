# Optical Flow Kaynakları ve Entegrasyon Rehberi

**Proje:** Savaşan İHA - TEKNOFEST
**Güncelleme Tarihi:** 2026-04-12
**Kategori:** DeepStream & Computer Vision Kaynakları

---

## 🎯 Mevcut Optical Flow Kaynakları (Workspace İçinde)

### 1. DeepStream Optical Flow - NVIDIA Official
**Konum (klon sonrası):** `98_Reference_Repos/deepstream_python_apps/apps/deepstream-opticalflow/`  
Kaynak repo: [NVIDIA-AI-IOT/deepstream_python_apps](https://github.com/NVIDIA-AI-IOT/deepstream_python_apps) — `apps/deepstream-opticalflow` alt dizini.

**Dosyalar:**
- `deepstream-opticalflow.py` - Ana uygulama
- `README` - Kullanım talimatları

**Özellikler:**
- **nvof plugin:** NVIDIA GPU'larda hardware accelerated optical flow
- **nvofvisual plugin:** Motion vector verilerini görselleştirme
- **NV12 format:** İki kare arasındaki flow vectors hesaplar
- **NvDsOpticalFlowMeta:** Flow vectors user meta olarak eklenir (resmi ad)

**Python örnek pipeline (görselleştirme odaklı):**
```
streammux → queue → nvof → queue → nvofvisual → queue → tiler → …
```

**Ana C++ uygulama (`02_Ana_Sistem_CPP`, `BuildPhase3PipelineString`):** NvDCF + PGIE interval için OF’nin tracker öncesinde akması gerekir; sıra **`nvstreammux → nvof → nvinfer → nvtracker → …`** şeklindedir (`nvofvisual` yok).

**Kritik Kod Bölümleri:**
```python
# Optical flow element creation
nvof = Gst.ElementFactory.make("nvof", "nvopticalflow")
nvofvisual = Gst.ElementFactory.make("nvofvisual", "nvopticalflowvisual")

# Meta data extraction
flow_vectors = pyds.get_optical_flow_vectors(of_meta)
flow_vectors = flow_vectors.reshape(of_meta.rows, of_meta.cols, 2)

# HSV visualization
flow_visual = visualize_optical_flowvectors(flow_vectors)
```

**Kullanım:**
```bash
python3 deepstream-opticalflow.py <uri1> [uri2] ... [uriN] <output_folder>
```

---

## 🔗 Önemli Optical Flow GitHub Repoları

### 1. NVIDIA DeepStream Optical Flow
**Repo:** `NVIDIA-AI-IOT/deepstream-opticalflow`
**Link:** https://github.com/NVIDIA-AI-IOT/deepstream-opticalflow
**Açıklama:** NVIDIA'nın resmi DeepStream optical flow implementasyonu
**Önem:** Jetson platformlarında hardware accelerated
**İndirme:** `git clone https://github.com/NVIDIA-AI-IOT/deepstream-opticalflow.git`

### 2. RAFT (Recurrent All-Pairs Field Transforms)
**Repo:** `princeton-vl/RAFT`
**Link:** https://github.com/princeton-vl/RAFT
**Açıklama:** SOTA optical flow algoritması, yüksek performans
**Önem:** Akademik benchmark'larda en iyi performans
**PyTorch Implementasyonu:** `git clone https://github.com/princeton-vl/RAFT.git`

### 3. Pytorch-RAFT
**Repo:** `Duankaiwen/Pytorch-RAFT`
**Link:** https://github.com/Duankaiwen/Pytorch-RAFT
**Açıklama:** RAFT'ın PyTorch implementasyonu
**Önem:** Eğitim ve inference için kullanışlı
**İndirme:** `git clone https://github.com/Duankaiwen/Pytorch-RAFT.git`

### 4. Farneback Optical Flow
**Repo:** `torrvision/farneback2011`
**Link:** https://github.com/torrvision/farneback2011
**Açıklama:** Klasik Farneback algoritması
**Önem:** GPU加速，基准对比
**İndirme:** `git clone https://github.com/torrvision/farneback2011.git`

### 5. FlowNet 2.0
**Repo:** `NVIDIA/flownet2-pytorch`
**Link:** https://github.com/NVIDIA/flownet2-pytorch
**Açıklama:** NVIDIA'nın FlowNet 2.0 implementasyonu
**Önem:** NVIDIA GPU optimizasyonları
**İndirme:** `git clone https://github.com/NVIDIA/flownet2-pytorch.git`

### 6. PWC-Net
**Repo:** `NVIDIA/PWC-Net`
**Link:** https://github.com/NVIDIA/PWC-Net
**Açıklama:** NVIDIA'nın PWC-Net implementasyonu
**Önem:** Hızlı ve güvenilir optical flow
**İndirme:** `git clone https://github.com/NVIDIA/PWC-Net.git`

---

## 🚀 Savaşan İHA için Optical Flow Entegrasyon Stratejisi

### Mevcut NvDCF Tracker ile Entegrasyon

**Amaç:** Optical flow ile tracker performansını artırma

**Entegrasyon Noktaları:**
1. **Prediction Phase:** Tracker prediction için optical flow kullan
2. **Motion Compensation:** Hızlı hareket eden hedefleri daha iyi takip et
3. **Collision Avoidance:** Hedeflerin yaklaşma hızını hesapla

**Pipeline Değişikliği:**
```
... → NvDCF Tracker → Optical Flow → Enhanced Tracking → OSD → ...
```

**C++ Implementasyon:**
```cpp
// NvDsOpticalFlowMeta extraction in probe callback
NvDsOpticalFlowMeta* of_meta = (NvDsOpticalFlowMeta*)user_meta_data;

// Get flow vectors for specific ROI
std::vector<float> flow_vectors = get_flow_vectors_for_bbox(
    of_meta, bbox_x, bbox_y, bbox_width, bbox_height
);

// Calculate average motion
float avg_dx, avg_dy;
calculate_average_motion(flow_vectors, &avg_dx, &avg_dy);

// Enhance tracker prediction
enhance_tracker_prediction(tracker_obj, avg_dx, avg_dy);
```

---

## 📊 Optical Flow Algoritmaları Karşılaştırması

| Algoritma | Performans | Hız | GPU Desteği | Jetson Uygunluğu |
|-----------|------------|-----|------------|------------------|
| **NVIDIA nvof** | İyi | Çok Hızlı | ✅ Hardware | ✅ Mükemmel |
| **RAFT** | En İyi | Yavaş | ✅ CUDA | ⚠️ Sınırlı |
| **FlowNet 2.0** | İyi | Orta | ✅ CUDA | ✅ İyi |
| **PWC-Net** | İyi | Hızlı | ✅ CUDA | ✅ İyi |
| **Farneback** | Orta | Orta | ❌ CPU | ⚠️ Yavaş |

---

## 🔧 Kurulum ve Entegrasyon Adımları

### Adım 1: NVIDIA nvof Plugin Kurulumu
```bash
# DeepStream ile birlikte gelir
# Kontrol: gst-inspect-1.0 nvof
```

### Adım 2: Ek Repoları İndirme
```bash
cd /home/nvidia/Savasan_IHA_Workspace/98_Reference_Repos/

# RAFT için
git clone https://github.com/princeton-vl/RAFT.git

# FlowNet için
git clone https://github.com/NVIDIA/flownet2-pytorch.git

# PWC-Net için
git clone https://github.com/NVIDIA/PWC-Net.git
```

### Adım 3: Entegrasyon Testi
```bash
# Mevcut optical flow uygulamasını test et
cd /home/nvidia/Savasan_IHA_Workspace/98_Reference_Repos/deepstream_python_apps-master/apps/deepstream-opticalflow/

python3 deepstream-opticalflow.py file:///opt/nvidia/deepstream/deepstream/samples/streams/sample_720p.mp4 test_output
```

### Adım 4: Savaşan İHA Pipeline Entegrasyonu
```cpp
// ds_app.cpp içinde optical flow entegrasyonu
// Pipeline'a nvof elementi eklemek için DeepStream-Yolo config yapısını kullan
```

---

## 📚 Dokümantasyon Kaynakları

### NVIDIA Resmi Dokümanlar
- **DeepStream Optical Flow Plugin:** NVIDIA Developer Documentation
- **GStreamer nvof Plugin:** DeepStream Plugin Guide
- **TensorRT Optical Flow:** TensorRT Developer Guide

### Akademik Kaynaklar
- **RAFT Paper:** "Recurrent All-Pairs Field Transforms for Optical Flow" (CVPR 2021)
- **FlowNet 2.0 Paper:** "Learning to Fly" (ECCV 2016)
- **PWC-Net Paper:** "PWC-Net: CNNs for Optical Flow Using Pyramid, Warping, and Cost Volume" (CVPR 2018)

---

## 🎯 Savaşan İHA İçin Optik Akış Kullanım Senaryoları

### 1. Hedef Yaklaşma Hesaplama
```cpp
// Optical flow magnitude ile hedef yaklaşma hızını hesapla
float approach_speed = calculate_optical_flow_magnitude(
    current_frame, previous_frame, target_bbox
);
```

### 2. Çarpışma Önleme
```cpp
// Hedefin hareket vektörünü analiz et
if (is_collision_course(flow_vectors, current_pos, velocity)) {
    trigger_avoidance_maneuver();
}
```

### 3. Tracker Performance Enhancement
```cpp
// NvDCF prediction phase'i için motion compensation
NvDCF_TrackerPrediction enhanced_prediction = 
    apply_optical_flow_compensation(base_prediction, flow_vectors);
```

### 4. Hedef Sınıflandırma Desteği
```cpp
// Motion pattern analysis ile hedef tipi tahmini
TargetType type = classify_by_motion_pattern(flow_vectors);
```

---

## ⚡ Performans İyileştirme İpuçları

### Jetson Orin NX Optimizasyonları
1. **Hardware Acceleration:** nvof plugin kullan
2. **Resolution Optimization:** Optical flow için lower resolution
3. **Frame Rate:** Her kare için optical flow hesaplamasına gerek yok
4. **ROI Focusing:** Sadece tracking ROI'sinde optical flow hesapla

### Bellek Optimizasyonu
```cpp
// Optical flow meta data'yı gerektiği kadar sakla
void* of_meta = get_optical_flow_meta(frame_meta);
process_and_release(of_meta);
```

---

## 🔍 Hata Ayıklama ve Test

### Test Komutları
```bash
# nvof plugin kontrolü
gst-inspect-1.0 nvof

# Mevcut deepstream-opticalflow testi
cd deepstream_python_apps-master/apps/deepstream-opticalflow/
python3 deepstream-opticalflow.py file:///path/to/video.mp4 output_folder

# Performans benchmark
nvprof --print-gpu-trace python3 deepstream-opticalflow.py ...
```

### Sık Karşılaşılan Sorunlar
- **nvof element not found:** DeepStream kurulumunu kontrol et
- **Low FPS:** Resolution düşür, frame skip kullan
- **Memory overflow:** Batch size azalt

---

## 📝 Sonraki Adımlar

1. **Repo İndirme:** Yukarıdaki repoları 98_Reference_Repos içine klonla
2. **C++ Entegrasyon:** ds_app.cpp içinde optical flow probe'ı ekle
3. **Testing:** Mevcut video dosyaları ile test et
4. **Performance:** FPS ve memory profili çıkar
5. **Integration:** NvDCF tracker ile entegre et

---

## 🔄 Güncelleme Notları

- **2026-04-12:** İlk versiyon oluşturuldu
- **Kaynaklar:** Mevcut workspace kaynakları ve popüler GitHub repolar
- **Uyumluluk:** JetPack 6.2.2 + DeepStream

---

**Not:** Bu belge, Savaşan İHA projesinin optical flow entegrasyonu için temel referans olarak kullanılmalıdır. Detaylı implementasyon için belirtilen repolar ve dokümanlar incelenmelidir.