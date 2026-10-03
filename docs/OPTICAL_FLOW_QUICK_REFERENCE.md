# Optical Flow Hızlı Referans Kılavuzu

**Proje:** Savaşan İHA
**Tarih:** 2026-04-12
**Amaç:** Quick reference for optical flow integration

---

## 🚀 Hızlı Başlangıç

### Mevcut Kaynakları Test Etme
`deepstream_python_apps` repoyu `98_Reference_Repos/` altına klonladıysanız:

```bash
cd /home/nvidia/Savasan_IHA_Workspace/98_Reference_Repos/deepstream_python_apps/apps/deepstream-opticalflow/
python3 deepstream-opticalflow.py file:///opt/nvidia/deepstream/deepstream/samples/streams/sample_720p.mp4 test_output
```

Klasör yoksa önce [NVIDIA-AI-IOT/deepstream_python_apps](https://github.com/NVIDIA-AI-IOT/deepstream_python_apps) içindeki `apps/deepstream-opticalflow` yolunu workspace’e alın.

### Plugin Kontrolü
```bash
gst-inspect-1.0 nvof
gst-inspect-1.0 nvofvisual
```

---

## 📚 Doküman İndeksi

| Doküman | Konum | Amaç |
|---------|-------|------|
| **Ana Kaynaklar Dokümanı** | `OPTICAL_FLOW_RESOURCES.md` | GitHub repolar, algoritmalar, genel bilgi |
| **C++ Entegrasyon Kılavuzu** | `OPTICAL_FLOW_CPP_INTEGRATION_GUIDE.md` | Detaylı implementasyon adımları |
| **Hızlı Referans** | `OPTICAL_FLOW_QUICK_REFERENCE.md` | Bu belge - hızlı bakış için |
| **Güncellenmiş Referans İndeksi** | `REFERENCE_DEEPSTREAM_REPOS_INDEX.md` | Tüm kaynakların indeksi |

---

## Üretim hattı sırası (`savasan_iha` / `ds_app.cpp`)

NvDCF + `nvinfer` `interval>0` senaryosunda OF meta’sının dedektör/takip ile aynı zamanda akması için **nvof, nvinfer ve nvtracker’dan önce** (mux çıkışında) yer alır:

```text
… ! nvstreammux … ! nvof ! nvinfer … ! nvtracker ! nvvidconv ! nvdsosd ! …
```

Kaynak: `02_Ana_Sistem_CPP/src/deepstream/ds_app.cpp` içinde `BuildPhase3PipelineString` (plugin yoksa `nvof` atlanır).

**Not:** NVIDIA’nın `deepstream-opticalflow` Python örneği görselleştirme odaklıdır (`nvof` → `nvofvisual` → …); bu, demo pipeline’ıdır. Ana C++ uygulamada `nvofvisual` yoktur; görselleştirme gerekirse ayrı deneysel graf veya `tee` ile yapılır.

---

## 🔧 C++ / meta — temel adımlar

### 1. Header (probe veya özel işleme için)
```cpp
#include <nvds_optical_flow_meta.h>
#include <nvbufsurface.h>
```

### 2. Element (üretimde genelde pipeline string ile)
`nvof` fabrika adıyla oluşturulur; `gst_element_factory_make("nvof", "nvopticalflow")` tipik örnektir. Görselleştirme için isteğe bağlı: `nvofvisual` (ayrı deney hattı).

### 3. Bağlantı (üretim)

Doğru sıra **mux → nvof → nvinfer → nvtracker** şeklindedir; `nvof`’u tracker’dan sonraya koymayın (OF meta zamanlaması ve PGIE interval ile uyumsuz olur).

```text
nvstreammux → nvof → nvinfer → nvtracker → …
```

---

## 🧪 Meta Verisi İşleme - Temel Kod

### Probe Callback
```cpp
GstPadProbeReturn ofProbeCallback(GstPad* pad, GstPadProbeInfo* info, gpointer user_data)
{
    GstBuffer* buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    NvDsBatchMeta* batch_meta = gst_buffer_get_nvds_batch_meta(buffer);
    
    if (batch_meta) {
        processOpticalFlowMeta(batch_meta);
    }
    
    return GST_PAD_PROBE_OK;
}
```

### Meta Extraction
```cpp
NvDsOpticalFlowMeta* of_meta = (NvDsOpticalFlowMeta*)user_meta->user_meta_data;

// Flow vectors okuma
float2* flow_vectors = (float2*)mapOfBuffer(of_meta->buffer);

// Boyutlar
int width = of_meta->cols;
int height = of_meta->rows;
```

---

## 🎯 Tracker Enhancement - Örnek Kod

### Motion Compensation
```cpp
void enhanceTrackerPrediction(NvDsObjectMeta* obj_meta, float dx, float dy)
{
    if (obj_meta->tracker_confidence > 0.5f) {
        obj_meta->rect_params.left += dx * PREDICTION_WEIGHT;
        obj_meta->rect_params.top += dy * PREDICTION_WEIGHT;
    }
}
```

### Collision Detection
```cpp
bool isCollisionCourse(NvDsObjectMeta* obj_meta, float dx, float dy)
{
    float obj_x = obj_meta->rect_params.left + obj_meta->rect_params.width / 2;
    float obj_y = obj_meta->rect_params.top + obj_meta->rect_params.height / 2;
    
    float screen_cx = m_displayWidth / 2;
    float screen_cy = m_displayHeight / 2;
    
    float velocity_angle = atan2(dy, dx);
    float to_center_angle = atan2(screen_cy - obj_y, screen_cx - obj_x);
    
    return fabs(velocity_angle - to_center_angle) < COLLISION_THRESHOLD;
}
```

---

## ⚡ Performans

Jetson’da `nvof` için özellikleri yerelde doğrulayın:

```bash
gst-inspect-1.0 nvof
```

Tipik ayarlar: `preset-level`, `grid-size`, `pool-size`, `gpu-id` (dGPU; Jetson’da çoğu senaryoda varsayılan yeterli). Çözünürlük, üst akıştaki `nvstreammux` width/height ile belirlenir; `nvof` ayrı `input-width` ile yarım rez çözümü sunmaz (uygunsuz property örnekleri kullanmayın).

Dedektör yükünü azaltmak için `nvinfer` config’te `interval` kullanılır; OF her karede üretilmeye devam eder.

---

## 📊 Önemli GitHub Repoları

### İndirme Komutları
```bash
cd /home/nvidia/Savasan_IHA_Workspace/98_Reference_Repos/

# NVIDIA official
git clone https://github.com/NVIDIA-AI-IOT/deepstream-opticalflow.git

# SOTA algorithms
git clone https://github.com/princeton-vl/RAFT.git
git clone https://github.com/Duankaiwen/Pytorch-RAFT.git

# NVIDIA implementations
git clone https://github.com/NVIDIA/flownet2-pytorch.git
git clone https://github.com/NVIDIA/PWC-Net.git

# Classic algorithms
git clone https://github.com/torrvision/farneback2011.git
```

### Algoritma Seçimi
- **Jetson Orin NX:** NVIDIA nvof (hardware accelerated)
- **Maksimum Accuracy:** RAFT
- **Hız + Accuracy:** FlowNet 2.0 / PWC-Net

---

## 🔍 Hata Ayıklama

### Plugin Kontrolü
```bash
gst-inspect-1.0 nvof
gst-inspect-1.0 nvofvisual
```

### Debug Logging
```cpp
g_print("Optical Flow: Width=%d, Height=%d\n", of_meta->cols, of_meta->rows);
g_print("Avg Motion: dx=%.2f, dy=%.2f\n", avg_dx, avg_dy);
```

### Performance Monitoring
```cpp
auto start = std::chrono::high_resolution_clock::now();
// Optical flow işlemi
auto end = std::chrono::high_resolution_clock::now();
float ms = std::chrono::duration<float, std::milli>(end - start).count();
```

---

## 📝 Entegrasyon Checklist

### Pipeline Entegrasyonu
- [ ] nvof elementleri eklendi
- [ ] Elementler bağlandı
- [ ] Pipeline çalışıyor

### Meta İşleme
- [ ] Probe callback implement edildi
- [ ] Meta extraction çalışıyor
- [ ] Flow vectors okunabiliyor

### Tracker Entegrasyonu
- [ ] Motion compensation aktif
- [ ] Collision detection eklendi
- [ ] Performance gain ölçüldü

### Optimizasyon
- [ ] Resolution optimize edildi
- [ ] Frame skip aktif
- [ ] ROI focusing kullanılıyor

### Testing
- [ ] Mevcut videolarla test edildi
- [ ] FPS profili çıkarıldı
- [ ] Memory profili çıkarıldı

---

## 🚀 Hızlı Test Komutları

### Python Test
```bash
cd 98_Reference_Repos/deepstream_python_apps/apps/deepstream-opticalflow/
python3 deepstream-opticalflow.py file:///path/to/video.mp4 output/
```

### C++ (ana uygulama)
```bash
cd /home/nvidia/Savasan_IHA_Workspace/02_Ana_Sistem_CPP/build
cmake --build . -j4
./savasan_iha phase3 csi display
# veya Phase 4 hibrit: ./savasan_iha phase4 csi hybrid display
```

Performans ölçümü için mevcut telemetri çıktısı OSD sonrası probe ile (`ds_app`) veya ortamda `NVDS_ENABLE_LATENCY_MEASUREMENT=1` kullanılır; ayrıca `./savasan_iha` için `--debug-optical-flow` bayrağı tanımlı değildir.

---

## 📞 Yardım ve Destek

### Sorun Giderme
1. **nvof element not found:** DeepStream kurulumunu kontrol et
2. **Low FPS:** Resolution düşür, frame skip kullan
3. **Memory overflow:** Batch size azalt, ROI focusing kullan

### Detaylı Dokümantasyon
- **Ana Doküman:** `OPTICAL_FLOW_RESOURCES.md`
- **C++ Kılavuzu:** `OPTICAL_FLOW_CPP_INTEGRATION_GUIDE.md`
- **Referans İndeksi:** `REFERENCE_DEEPSTREAM_REPOS_INDEX.md`

---

## 🎯 Önemli Notlar

1. **GPU Memory:** Optical flow önemli miktarda GPU belleği kullanır
2. **Performance:** Her kare için hesaplama FPS'i düşürebilir
3. **Accuracy vs Speed:** Trade-off dikkatli yönetilmeli
4. **Jetson Orin NX:** Hardware accelerated nvof en iyi seçenek

---

**Bu hızlı referans kılavuzu, Savaşan İHA projesinde optical flow entegrasyonu için temel komutları ve kod örneklerini içerir.**