# DeepStream Referans Repoları — İndeks ve Kod Referans Rehberi

**Kök dizin (workspace):**  
[`98_Reference_Repos/`](98_Reference_Repos/)

Bu belge, aşağıdaki referans repoları ve konu başlıklarını (YOLO, DeepStream örnekleri, analytics, Python apps, **QR/OpenCV**, optical flow) tarayarak oluşturulmuştur. İleride kod yazarken **öncelik sırası**: C++ üretim kodu → bu indeksteki yollar; Python örnekleri yalnızca **mantık kıyaslaması** içindir.

**Sürüm uyarısı:** `deepstream_reference_apps-master` README’si **DeepStream 9.0**; `deepstream_python_apps-master` de DS 9.0 / Ubuntu 24.04 vurgular. Sizin [JETSON_ORIN_NX_MANIFESTO.md](JETSON_ORIN_NX_MANIFESTO.md) **JetPack 6.2.2** (DeepStream sürümü JetPack ile kilitli) ile çalışıyor. API’ler çoğunlukla uyumludur; kopyalarken **yerel** `/opt/nvidia/deepstream/deepstream/` sürümüne göre derleme ve config doğrulanmalıdır.

---

## 1. DeepStream-Yolo (`DeepStream-Yolo-master`)

**Rol (sizin tanımınız):** YOLO’yu DeepStream **nvinfer** C++ motoruna bağlayan **Custom Parser / custom impl** için **tek kaynak** (projede başka parser kaynağı kullanılmaması önerilir).

### Mutlak odak: `nvdsinfer_custom_impl_Yolo/`

| Dosya / alt dizin | Amaç |
|-------------------|------|
| [`nvdsparsebbox_Yolo.cpp`](98_Reference_Repos/DeepStream-Yolo-master/nvdsinfer_custom_impl_Yolo/nvdsparsebbox_Yolo.cpp) | CPU tarafı bbox parse: `NvDsInferParseYolo` → `NvDsInferParseCustomYolo`, tensor decode, `NvDsInferParseObjectInfo` listesi, `CHECK_CUSTOM_PARSE_FUNC_PROTOTYPE(NvDsInferParseYolo)` |
| `nvdsparsebbox_Yolo_cuda.cu` | GPU bbox parser (config’te `parse-bbox-func-name=NvDsInferParseYoloCuda` ile seçilebilir) |
| `nvdsinfer_yolo_engine.cpp` | TensorRT engine oluşturma (`engine-create-func-name=NvDsInferYoloCudaEngineGet`) |
| `yolo.cpp`, `yolo.h`, `yoloForward*.cu`, `yoloPlugins.*` | YOLO ağ ön-işleme / plugin / ileri geçiş |
| `calibrator.cpp`, `calibrator.h` | INT8 kalibrasyon |
| `utils.cpp`, `utils.h` | Yardımcılar (ör. `clamp`) |
| `layers/` | Darknet tarzı katman implementasyonları (cfg/weights yolu için) |
| `Makefile` | `libnvdsinfer_custom_impl_Yolo.so` üretimi |

### Kök config ve etiketler

- Çok sayıda [`config_infer_primary_*.txt`](98_Reference_Repos/DeepStream-Yolo-master/) (YOLOv5–v13, YOLO11/26, YOLOX, RT-DETR, vb.).
- Tipik anahtarlar: `parse-bbox-func-name`, `custom-lib-path=nvdsinfer_custom_impl_Yolo/libnvdsinfer_custom_impl_Yolo.so`, `engine-create-func-name`, `labelfile-path`, `num-detected-classes`, NMS `[class-attrs-all]`.
- Örnek parça: [`config_infer_primary_yoloV8.txt`](98_Reference_Repos/DeepStream-Yolo-master/config_infer_primary_yoloV8.txt) (`NvDsInferParseYolo` + `NvDsInferYoloCudaEngineGet`).
- [`labels.txt`](98_Reference_Repos/DeepStream-Yolo-master/labels.txt), [`deepstream_app_config.txt`](98_Reference_Repos/DeepStream-Yolo-master/deepstream_app_config.txt), [`docs/`](98_Reference_Repos/DeepStream-Yolo-master/docs/) (model başına kullanım).

### Özet

- Parser girişi: `std::vector<NvDsInferLayerInfo>` + `NvDsInferNetworkInfo` + `NvDsInferParseDetectionParams` → çıkış `std::vector<NvDsInferParseObjectInfo>`.
- Projede **bbox mantığı değişecekse** önce bu repodaki ilgili YOLO sürümü dalını düzenleyin; `nvinfer` config’teki isim/soyağacı ile eşleşmesini koruyun.

---

## 2. deepstream_reference_apps (`deepstream_reference_apps-master`)

**Rol:** **GStreamer pipeline iskeleti** ve **pad probe** kalıpları için **ana rehber** (NVIDIA resmi referans uygulamalar koleksiyonu).

### Probe (`gst_pad_add_probe` + `gst_buffer_get_nvds_batch_meta`) — doğrudan örnek veren C++ dosyaları

| Uygulama | Dosya | Not |
|----------|--------|-----|
| **BodyPose 3D** | [`deepstream-bodypose-3d/sources/deepstream_pose_estimation_app.cpp`](98_Reference_Repos/deepstream_reference_apps-master/deepstream-bodypose-3d/sources/deepstream_pose_estimation_app.cpp) | `pgie_src_pad_buffer_probe`, `sgie_src_pad_buffer_probe`, `osd_sink_pad_buffer_probe`; `NvDsBatchMeta` / frame / object dolaşımı; `gst_pad_add_probe(..., GST_PAD_PROBE_TYPE_BUFFER, ...)` |
| **Paralel inference** | [`deepstream_parallel_inference_app/tritonclient/sample/apps/deepstream-parallel-infer/deepstream_parallel_infer_app.cpp`](98_Reference_Repos/deepstream_reference_apps-master/deepstream_parallel_inference_app/tritonclient/sample/apps/deepstream-parallel-infer/deepstream_parallel_infer_app.cpp) | Benzer probe isimleri; `NVGSTDS_ELEM_ADD_PROBE` makroları ile mux üzerinde probe; çoklu model / metamux YAML |
| **Anomaly / yön** | [`anomaly/plugins/gst-dsdirection/gstdsdirection.cpp`](98_Reference_Repos/deepstream_reference_apps-master/anomaly/plugins/gst-dsdirection/gstdsdirection.cpp) | Plugin içinde `NvDsBatchMeta` okuma (element tarafı probe olmadan meta erişimi örneği) |
| **IPC SR** | [`deepstream-ipc-test-sr/video_template_impl/vt_impl.cpp`](98_Reference_Repos/deepstream_reference_apps-master/deepstream-ipc-test-sr/video_template_impl/vt_impl.cpp) | `NvDsBatchMeta` referansı (tam probe eğitimi için üstteki iki app daha temiz) |

**Önerilen okuma sırası:** önce `deepstream_pose_estimation_app.cpp` (tek dosyada PGIE/SGIE/OSD probe üçlüsü), sonra `deepstream_parallel_infer_app.cpp` (daha karmaşık graf).

### Pipeline / mimari (probe dışı, iskelet için)

| Proje | Kullanım |
|-------|----------|
| [`runtime_source_add_delete/`](98_Reference_Repos/deepstream_reference_apps-master/runtime_source_add_delete/) | Çalışırken kaynak ekleme/silme (probe içermeyebilir; graf yaşam döngüsü) |
| [`deepstream-dynamicsrcbin-test/`](98_Reference_Repos/deepstream_reference_apps-master/deepstream-dynamicsrcbin-test/) | Çoklu dinamik kaynak + tek decoder |
| [`deepstream-custom-tile-config/`](98_Reference_Repos/deepstream_reference_apps-master/deepstream-custom-tile-config/) | `nvmultistreamtiler` özel döşeme |
| [`deepstream-masktracker/`](98_Reference_Repos/deepstream_reference_apps-master/deepstream-masktracker/), [`deepstream-tracker-3d/`](98_Reference_Repos/deepstream_reference_apps-master/deepstream-tracker-3d/) | İleri tracker / segmentasyon örnekleri |
| [`legacy_apps/`](98_Reference_Repos/deepstream_reference_apps-master/legacy_apps/) | Eski örnekler; içinde **occupancy-analytics** ile örtüşen içerik var |

### Bu repoda ikincil YOLO parser kopyaları

- [`deepstream_parallel_inference_app/.../nvdsinfer_custom_impl_Yolo/nvdsparsebbox_Yolo.cpp`](98_Reference_Repos/deepstream_reference_apps-master/deepstream_parallel_inference_app/tritonclient/sample/gst-plugins/gst-nvinferserver/nvdsinfer_custom_impl_Yolo/nvdsparsebbox_Yolo.cpp) — **birincil referans olarak DeepStream-Yolo kullanın**; burası Triton/parallel örnek bağlamı içindir.

Üst düzey liste: [README.md](98_Reference_Repos/deepstream_reference_apps-master/README.md).

---

## 3. occupancy-analytics (`deepstream-occupancy-analytics-master`)

**Rol:** **Hedef / hat / bölge analitiği** ve **nvdsanalytics** meta işleme için ileri seviye **C++ mantık** örneği (giriş-çıkım, doluluk sayımı).

### Bu klon içindeki C++ yapı taşları

| Dosya | Amaç |
|--------|------|
| [`deepstream_nvdsanalytics_meta.cpp`](98_Reference_Repos/deepstream-occupancy-analytics-master/deepstream_nvdsanalytics_meta.cpp) | `analytics_custom_parse_nvdsanalytics_meta_data`: `NvDsUserMeta` → `NvDsAnalyticsFrameMeta` cast; `objLCCumCnt["Entry"]` / `["Exit"]` ile özet sayaçlar; [`analytics.h`](98_Reference_Repos/deepstream-occupancy-analytics-master/includes/analytics.h) içindeki `AnalyticsUserMeta` struct’ına doldurma |
| [`includes/nvdsmeta_schema.h`](98_Reference_Repos/deepstream-occupancy-analytics-master/includes/nvdsmeta_schema.h) | Şema / mesaj tarafı başlıklar (Kafka/msgconv ile birlikte düşünülür) |

README, tam uygulamanın **deepstream-test5** tabanlı olduğunu ve `config/`, `deepstream-test5-analytics` binary’sini varsaydığını belirtir. Bu workspace klonunda yalnızca **meta ayrıştırma kütüphanesi** katmanı öne çıkıyor; tam test5 ağacı yoksa tam akış için NVIDIA sample_apps veya tam repo yapısı gerekir.

### Çift kopya notu

Aynı occupancy örneğinin bir sürümü ayrıca şurada bulunur:  
[`legacy_apps/deepstream-occupancy-analytics/`](98_Reference_Repos/deepstream_reference_apps-master/legacy_apps/deepstream-occupancy-analytics/)  
(`deepstream_nvdsanalytics_meta.cpp`, `includes/analytics.h`).

**Savaşan İHA bağlamı:** “bölge ihlali” veya çizgi geçişi için `NvDsAnalyticsFrameMeta` ve user_meta zinciri bu dosyalarda somutlaşır; kilit XY üretimi için probe içinde benzer user_meta taraması yapılabilir.

---

## 4. deepstream_python_apps (`deepstream_python_apps-master`)

**Rol:** Yalnızca **genel mantık kıyaslaması** (pipeline sırası, test1–test4 akışları, `pyds` ile meta dolaşımı). **Üretim önceliği C++** ([.cursorrules](.cursorrules), [00_DEEPSTREAM_PLAN.md](00_DEEPSTREAM_PLAN.md)).

### Dizim: [`apps/`](98_Reference_Repos/deepstream_python_apps-master/apps/)

| Klasör | Kısa not |
|--------|-----------|
| `deepstream-test1` … `deepstream-test4` | Klasik öğretici pipeline’lar |
| `deepstream-test1-usbcam` | USB kamera |
| `deepstream-test1-rtsp-out`, `deepstream-rtsp-in-rtsp-out` | RTSP |
| `deepstream-nvdsanalytics` | Analytics plugin Python karşılığı (C++ occupancy ile kavramsal eşleme) |
| `deepstream-imagedata-multistream*` | Buffer / Cupy / redaksiyon |
| `deepstream-segmentation`, `deepstream-segmask` | Segmentasyon |
| `deepstream-opticalflow` | Optical flow |
| `deepstream-preprocess-test` | Ön işleme |
| `deepstream-demux-multi-in-multi-out` | Demux |
| `runtime_source_add_delete` | Çalışma zamanı kaynak yönetimi |
| `common/` | `bus_call`, `FPS`, `platform_info`, `utils` |

**Not:** Repo README, DS 9.0 ve Python bindings durumu (wheel / PyServiceMaker yönlendirmesi) hakkında uyarılar içerir; Jetson’daki PyDS kurulumunuz manifesto ile ayrı yönetilir.

---

## Hızlı görev → repo eşlemesi

| Görev | Birincil referans |
|--------|-------------------|
| YOLO çıkış tensor → `NvDsInferParseObjectInfo` | `DeepStream-Yolo-master/nvdsinfer_custom_impl_Yolo/` |
| `config_infer_primary` + `custom-lib-path` | `DeepStream-Yolo-master/config_infer_primary_*.txt` |
| Pad probe, batch/frame/obj meta | `reference_apps` → `deepstream_pose_estimation_app.cpp`, `deepstream_parallel_infer_app.cpp` |
| Çizgi / ROI / analytics sayım, user_meta | `occupancy-analytics` → `deepstream_nvdsanalytics_meta.cpp` |
| Python’da aynı fikri doğrulama | `python_apps` → ilgili `apps/deepstream-*` |

---

## Cursor / AI kullanım notu

Kod veya probe yazarken:

1. **Struct alanları** için [DEEPSTREAM_INCLUDES_INDEX.md](DEEPSTREAM_INCLUDES_INDEX.md) (`nvdsmeta.h`).
2. **YOLO parser ve engine** için yalnızca **DeepStream-Yolo** `nvdsinfer_custom_impl_Yolo`.
3. **Probe stili** için **reference_apps** içindeki isimlendirilmiş `*_pad_buffer_probe` fonksiyonları.

*İndeks, `98_Reference_Repos/` altındaki mevcut klonlara göre 2026-04-01 tarihinde üretilmiştir; upstream güncellenirse özellikle DeepStream-Yolo `docs/` ve config dosya adları kontrol edilmelidir.*

---

## 5. DeepStream Optical Flow (`deepstream_python_apps-master/apps/deepstream-opticalflow`)

**Rol:** **NVIDIA nvof plugin** implementasyonu ve **motion vector** meta verisi işleme için referans (hardware accelerated optical flow).

### Ana Kaynaklar

| Dosya | Amaç |
|------|------|
| [`deepstream-opticalflow.py`](98_Reference_Repos/deepstream_python_apps-master/apps/deepstream-opticalflow/deepstream-opticalflow.py) | Ana uygulama, pipeline kurulumu, nvof elementi kullanımı |
| `README` | Kullanım talimatları ve plugin açıklamaları |

### Pipeline Yapısı
```
streammux → queue → nvof → queue → nvofvisual → queue → tiler → ...
```

### Kritik Kod Bölümleri

**Element Creation:**
```python
nvof = Gst.ElementFactory.make("nvof", "nvopticalflow")
nvofvisual = Gst.ElementFactory.make("nvofvisual", "nvopticalflowvisual")
```

**Meta Data Extraction:**
```python
of_meta = pyds.NvDsOpticalFlowMeta.cast(of_user_meta.user_meta_data)
flow_vectors = pyds.get_optical_flow_vectors(of_meta)
flow_vectors = flow_vectors.reshape(of_meta.rows, of_meta.cols, 2)
```

**Özellikler:**
- **nvof plugin:** NVIDIA GPU'larda hardware accelerated optical flow (Turing+)
- **nvofvisual plugin:** Motion vector verilerini görselleştirme
- **NV12 format:** İki kare arasındaki flow vectors hesaplar
- **NvOpticalFlowMeta:** Flow vectors user meta olarak eklenir

### Kullanım
```bash
python3 deepstream-opticalflow.py <uri1> [uri2] ... [uriN] <output_folder>
```

### C++ Implementasyon için Önemli Notlar

1. **Header Files:** `nvds_optical_flow_meta.h`, `nvbufsurface.h`
2. **Meta Types:** `NVDS_OPTICAL_FLOW_META`
3. **Buffer Mapping:** GPU-CPU memory mapping gerekli
4. **Integration Point:** Tracker prediction enhancement için ideal

---

## Optical Flow Ek Kaynaklar ve Dokümantasyon

### Yeni Eklenen Dokümanlar

| Doküman | Konum | İçerik |
|---------|-------|--------|
| **Optical Flow Kaynakları** | [`OPTICAL_FLOW_RESOURCES.md`](OPTICAL_FLOW_RESOURCES.md) | GitHub repolar, algoritmalar, entegrasyon stratejileri |
| **C++ Entegrasyon Kılavuzu** | [`OPTICAL_FLOW_CPP_INTEGRATION_GUIDE.md`](OPTICAL_FLOW_CPP_INTEGRATION_GUIDE.md) | Detaylı C++ implementasyon adımları ve kod örnekleri |

### Önemli GitHub Repoları (İndirmek İçin)

```bash
# NVIDIA resmi
git clone https://github.com/NVIDIA-AI-IOT/deepstream-opticalflow.git

# SOTA algoritmalar
git clone https://github.com/princeton-vl/RAFT.git
git clone https://github.com/Duankaiwen/Pytorch-RAFT.git

# NVIDIA implementasyonları
git clone https://github.com/NVIDIA/flownet2-pytorch.git
git clone https://github.com/NVIDIA/PWC-Net.git

# Klasik algoritmalar
git clone https://github.com/torrvision/farneback2011.git
```

### Algoritma Karşılaştırması

| Algoritma | Performans | Hız | GPU | Jetson |
|-----------|------------|-----|-----|---------|
| **NVIDIA nvof** | İyi | Çok Hızlı | ✅ HW | ✅ Mükemmel |
| **RAFT** | En İyi | Yavaş | ✅ CUDA | ⚠️ Sınırlı |
| **FlowNet 2.0** | İyi | Orta | ✅ CUDA | ✅ İyi |
| **PWC-Net** | İyi | Hızlı | ✅ CUDA | ✅ İyi |

### Entegrasyon Görevleri

| Görev | Birincil Referans |
|--------|-------------------|
| nvof plugin entegrasyonu | `deepstream_python_apps-master/apps/deepstream-opticalflow/` |
| Meta verisi işleme (C++) | `OPTICAL_FLOW_CPP_INTEGRATION_GUIDE.md` |
| Tracker enhancement | `OPTICAL_FLOW_RESOURCES.md` → "NvDCF Tracker ile Entegrasyon" |
| Performans optimizasyonu | `OPTICAL_FLOW_CPP_INTEGRATION_GUIDE.md` → "Performans Optimizasyonları" |

---

*Optical flow kaynakları 2026-04-12 tarihinde eklenmiştir.*
