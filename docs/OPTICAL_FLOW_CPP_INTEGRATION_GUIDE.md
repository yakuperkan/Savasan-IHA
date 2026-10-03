# Optical Flow C++ Entegrasyon Kılavuzu

**Proje:** Savaşan İHA - DeepStream Pipeline
**Hedef Defteri:** ds_app.cpp ve ds_app.hpp
**Güncelleme Tarihi:** 2026-04-12

---

## 🎯 Amaç

Savaşan İHA DeepStream pipeline'ına optical flow özelliği entegre etmek ve NvDCF tracker performansını artırmak.

---

## 📋 Ön Koşullar

### Mevcut Kütüphaneler
```bash
# DeepStream optical flow plugin kontrolü
gst-inspect-1.0 nvof
gst-inspect-1.0 nvofvisual
```

### Gerekli Başlık Dosyaları
```cpp
#include <nvds_optical_flow_meta.h>
#include <nvbufsurface.h>
#include <gst/gst.h>
#include <gst/nvdsgst/nvdsgstutils.h>
```

---

## 🔧 C++ Pipeline'a Optical Flow

### 1. Üretim kodunda mevcut durum

Ana uygulama `BuildPhase3PipelineString()` ile **GstLaunch tarzı string** kuruyor; `nvof` burada `nvstreammux` ile `nvinfer` arasına ekleniyor (`ResolveOpticalFlowElementName()` → `nvof` veya yoksa uyarı). Ayrı bir `GstElement* m_opticalFlow` üyesi yoktur.

Doğru sıra:

```text
… ! nvstreammux … ! nvof ! nvinfer … ! nvtracker ! nvvidconv ! nvdsosd ! …
```

### 2. Programatik `gst_element_link` kullanıyorsanız

OF meta’sının dedektör atlamalı (`nvinfer` `interval`) karelerde de tracker’a uygun gelmesi için **nvof’u nvtracker’dan önce**, `nvinfer` ile aynı hatta ve mux çıkışından hemen sonra bağlayın:

```text
mux → nvof → nvinfer → nvtracker → …
```

`nvofvisual` yalnızca görselleştirme deneyleri içindir; üretim `savasan_iha` grafiğinde kullanılmaz.

İsteğe bağlı ara kuyruk: `nvof` sonrası `queue max-size-buffers=2 leaky=2` → `nvinfer`.

### 3. Özel meta işleme (probe)

Kendi `ofProbeCallback` / `processOpticalFlowMeta` kodunuzu ekliyorsanız başlıklar ve `NvDsFrameMeta::frame_user_meta_list` içinde `NVDS_OPTICAL_FLOW_META` dolaşımı aşağıdaki bölümdeki örnekle uyumludur; probe’u genelde `nvinfer` veya `nvtracker` pad’ine değil, ihtiyaca göre OSD öncesi/sonrası bir dala koyun.

---

## 🧪 Optical Flow Meta Verisi İşleme

### 1. Probe Callback Implementasyonu

```cpp
GstPadProbeReturn DsApp::ofProbeCallback(GstPad* pad, GstPadProbeInfo* info, gpointer user_data)
{
    DsApp* app = static_cast<DsApp*>(user_data);
    
    GstBuffer* buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    if (!buffer) {
        return GST_PAD_PROBE_OK;
    }
    
    NvDsBatchMeta* batch_meta = gst_buffer_get_nvds_batch_meta(buffer);
    if (batch_meta) {
        app->processOpticalFlowMeta(batch_meta);
    }
    
    return GST_PAD_PROBE_OK;
}
```

### 2. Optical Flow Meta Verisi Okuma

```cpp
void DsApp::processOpticalFlowMeta(NvDsBatchMeta* batch_meta)
{
    NvDsFrameMeta* frame_meta = NULL;
    NvDsMetaList* frame_meta_list = &batch_meta->frame_meta_list;
    
    while (frame_meta_list != NULL) {
        frame_meta = (NvDsFrameMeta*)frame_meta_list->data;
        
        // Frame user meta listesinde optical flow meta verisini ara
        NvDsMetaList* user_meta_list = &frame_meta->frame_user_meta_list;
        while (user_meta_list != NULL) {
            NvDsUserMeta* user_meta = (NvDsUserMeta*)user_meta_list->data;
            
            if (user_meta && user_meta->base_meta.meta_type == NVDS_OPTICAL_FLOW_META) {
                // Optical flow meta verisini al
                NvDsOpticalFlowMeta* of_meta = 
                    (NvDsOpticalFlowMeta*)user_meta->user_meta_data;
                
                if (of_meta) {
                    processOpticalFlowVectors(of_meta, frame_meta);
                }
            }
            
            user_meta_list = user_meta_list->next;
        }
        
        frame_meta_list = frame_meta_list->next;
    }
}
```

### 3. Flow Vectors İşleme

```cpp
void DsApp::processOpticalFlowVectors(NvDsOpticalFlowMeta* of_meta, NvDsFrameMeta* frame_meta)
{
    // Flow vector verilerine eriş
    NvOFBufferPtr buffer = of_meta->buffer;
    
    // Flow vectors boyutları
    int width = of_meta->cols;
    int height = of_meta->rows;
    
    // Buffer'dan flow vectors okuma
    float2* flow_vectors = (float2*)mapOfBuffer(buffer);
    
    // Frame'deki nesneler için motion hesaplama
    for (NvDsObjectMeta* obj_meta = frame_meta->obj_meta_list; 
         obj_meta != NULL; obj_meta = obj_meta->next) {
        
        if (obj_meta->class_id == TARGET_CLASS_ID) {
            // Object bounding box için motion analizi
            float avg_dx = 0.0f, avg_dy = 0.0f;
            int count = 0;
            
            // ROI içindeki flow vectors ortalama al
            int roi_x = obj_meta->rect_params.left;
            int roi_y = obj_meta->rect_params.top;
            int roi_w = obj_meta->rect_params.width;
            int roi_h = obj_meta->rect_params.height;
            
            for (int y = roi_y; y < roi_y + roi_h; y++) {
                for (int x = roi_x; x < roi_x + roi_w; x++) {
                    if (x >= 0 && x < width && y >= 0 && y < height) {
                        int idx = y * width + x;
                        avg_dx += flow_vectors[idx].x;
                        avg_dy += flow_vectors[idx].y;
                        count++;
                    }
                }
            }
            
            if (count > 0) {
                avg_dx /= count;
                avg_dy /= count;
                
                // Object meta'ya motion information ekle
                updateObjectMotionInfo(obj_meta, avg_dx, avg_dy);
                
                // Tracker prediction enhancement
                enhanceTrackerPrediction(obj_meta, avg_dx, avg_dy);
            }
        }
    }
    
    // Buffer'ı unmap et
    unmapOfBuffer(buffer);
}
```

---

## 🎯 Tracker ile Entegrasyon

### 1. Tracker Prediction Enhancement

```cpp
void DsApp::enhanceTrackerPrediction(NvDsObjectMeta* obj_meta, float dx, float dy)
{
    // NvDCF tracker prediction phase'i için motion compensation
    if (obj_meta->tracker_confidence > 0.5f) { // Tracker active
        
        // Current velocity hesapla
        float current_velocity_x = dx / dt;  // dt: frame interval
        float current_velocity_y = dy / dt;
        
        // Predicted position güncelle
        obj_meta->rect_params.left += dx * PREDICTION_WEIGHT;
        obj_meta->rect_params.top += dy * PREDICTION_WEIGHT;
        
        // Velocity history güncelle
        updateVelocityHistory(obj_meta->object_id, current_velocity_x, current_velocity_y);
    }
}
```

### 2. Hedef Yaklaşma Hesaplama

```cpp
float DsApp::calculateApproachSpeed(NvDsObjectMeta* obj_meta, float dx, float dy)
{
    // Camera parametreleri
    float focal_length = getFocalLength();
    float object_width_meters = getTargetRealWidth(obj_meta->class_id);
    float pixel_width = obj_meta->rect_params.width;
    
    // Distance hesapla
    float distance = (focal_length * object_width_meters) / pixel_width;
    
    // Approach speed hesapla (radial velocity)
    float approach_speed = -(dx * distance) / pixel_width;
    
    return approach_speed;
}
```

### 3. Çarpışma Önleme

```cpp
bool DsApp::isCollisionCourse(NvDsObjectMeta* obj_meta, float dx, float dy)
{
    // Current position
    float obj_x = obj_meta->rect_params.left + obj_meta->rect_params.width / 2;
    float obj_y = obj_meta->rect_params.top + obj_meta->rect_params.height / 2;
    
    // Screen center
    float screen_cx = m_displayWidth / 2;
    float screen_cy = m_displayHeight / 2;
    
    // Distance to center
    float dist_to_center = sqrt(pow(obj_x - screen_cx, 2) + pow(obj_y - screen_cy, 2));
    
    // Velocity vector direction
    float velocity_angle = atan2(dy, dx);
    float to_center_angle = atan2(screen_cy - obj_y, screen_cx - obj_x);
    
    // Angle difference
    float angle_diff = fabs(velocity_angle - to_center_angle);
    
    // Collision threshold kontrolü
    float collision_threshold = 0.3f; // 17 derece
    
    if (dist_to_center < MIN_DISTANCE_THRESHOLD && angle_diff < collision_threshold) {
        return true;
    }
    
    return false;
}
```

---

## 📊 Performans Optimizasyonları

### 1. Resolution Optimizasyonu

```cpp
// Optical flow için lower resolution kullan
m_opticalFlow->setProperty("gpu-id", m_gpuId);
m_opticalFlow->setProperty("precision", 1);  // 0: FP32, 1: FP16

// Resolution ayarla (input resolution'dan daha küçük)
m_opticalFlow->setProperty("input-width", m_displayWidth / 2);
m_opticalFlow->setProperty("input-height", m_displayHeight / 2);
```

### 2. Frame Rate Kontrolü

```cpp
// Her kare için optical flow hesaplamaya gerek yok
int optical_flow_skip_frames = 2; // Her 3 karede bir hesapla
int frame_counter = 0;

void DsApp::updateFrameCounter() {
    frame_counter++;
    if (frame_counter % optical_flow_skip_frames != 0) {
        // Optical flow hesapla
    }
}
```

### 3. ROI Focusing

```cpp
// Sadece tracking ROI'sinde optical flow hesapla
void DsApp::processOpticalFlowForROI(NvDsOpticalFlowMeta* of_meta, 
                                      NvDsObjectMeta* target_obj)
{
    // Hedef ROI'sine odakla
    int roi_x = target_obj->rect_params.left;
    int roi_y = target_obj->rect_params.top;
    int roi_w = target_obj->rect_params.width;
    int roi_h = target_obj->rect_params.height;
    
    // Sadece ROI içindeki flow vectors işle
    processFlowVectorsInROI(of_meta, roi_x, roi_y, roi_w, roi_h);
}
```

---

## 🧪 Test ve Hata Ayıklama

### 1. Debug Logging

```cpp
void DsApp::logOpticalFlowStats(NvDsOpticalFlowMeta* of_meta)
{
    g_print("Optical Flow Stats:\n");
    g_print("  Width: %d, Height: %d\n", of_meta->cols, of_meta->rows);
    g_print("  Buffer Size: %zu bytes\n", of_meta->buffer->bufSize);
    
    // Average magnitude hesapla
    float avg_magnitude = calculateAverageMagnitude(of_meta);
    g_print("  Average Magnitude: %.2f pixels/frame\n", avg_magnitude);
}
```

### 2. Performans Monitor

```cpp
class OpticalFlowMonitor {
private:
    std::chrono::time_point<std::chrono::high_resolution_clock> m_start;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_end;
    int m_frame_count;
    
public:
    void start() { m_start = std::chrono::high_resolution_clock::now(); }
    void end() { m_end = std::chrono::high_resolution_clock::now(); }
    
    void incrementFrame() { m_frame_count++; }
    
    float getFPS() {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(m_end - m_start);
        return (m_frame_count * 1000.0f) / duration.count();
    }
};
```

---

## 🔗 Konfigürasyon Dosyası

**config_infer_primary.txt ekle:**
```
[optical-flow]
enable=1
gpu-id=0
precision=1
buffer-pool-size=4
input-width=640
input-height=480
```

---

## 🚀 Kullanım Örneği

### Komut Satırı Test
```bash
# Debug modunda çalıştır
./ds_app --debug-optical-flow

# Performance profile
./ds_app --profile-optical-flow

# ROI mode
./ds_app --optical-flow-roi
```

### Görselleştirme
```cpp
// Optical flow vectors'ı görselleştir
void DsApp::visualizeOpticalFlow(NvDsOpticalFlowMeta* of_meta)
{
    // Frame buffer'a overlay çiz
    drawFlowVectorsOnFrame(of_meta, m_frameBuffer);
    
    // OSD element'ine ekle
    nvds_osd_add_text_meta(m_osdCtx, frame_meta, "Motion: X=%.2f, Y=%.2f", 
                           obj_meta->rect_params.left, obj_meta->rect_params.top);
}
```

---

## 📝 Entegrasyon Checklist

- [ ] Pipeline'a nvof elementleri eklendi
- [ ] Optical flow meta processing callback implement edildi
- [ ] Tracker prediction enhancement aktif
- [ ] Collision detection logic eklendi
- [ ] Performance optimization uygulandı
- [ ] Debug logging eklendi
- [ ] Test video dosyaları ile test edildi
- [ ] FPS ve memory profili çıkarıldı
- [ ] Konfigürasyon dosyaları güncellendi
- [ ] Dokümantasyon güncellendi

---

## ⚠️ Önemli Notlar

1. **GPU Memory:** Optical flow önemli miktarda GPU belleği kullanır, dikkatli yönetilmeli
2. **Performance:** Her kare için optical flow hesaplaması FPS'i düşürebilir
3. **Accuracy:** Lower resolution daha hızlı ama daha az doğru sonuç verir
4. **Integration:** NvDCF tracker ile entegrasyon için proper synchronization gerekli

---

## 🔄 Sonraki Adımlar

1. **Testing:** Mevcut test seti ile entegrasyonu test et
2. **Profiling:** Performance profili çıkar ve optimize et
3. **Validation:** Tracker performansını karşılaştır
4. **Documentation:** Kullanım kılavuzu oluştur
5. **Deployment:** Production için optimize et

---

**Bu kılavuz, Savaşan İHA projesinin DeepStream pipeline'ına optical flow entegrasyonu için detaylı bir implementasyon rehberi sağlar.**