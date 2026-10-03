# DeepStream C++ Includes — Yerel İndeks ve Probe Referansı

**Kaynak dizin (JetPack/DeepStream kurulumu):**  
`/opt/nvidia/deepstream/deepstream/sources/includes/`

Bu dosya, yukarıdaki dizindeki `.h` dosyalarının taranmasıyla oluşturulmuştur. DeepStream C++ **probe (pad probe)** ve metadata işleme için öncelikli başlıklar ve yapılar burada özetlenir.

---

## Kritik not: `nvds_obj_meta.h` / `nvds_frame_meta.h`

Bu Jetson kurulumunda **`nvds_obj_meta.h` ve `nvds_frame_meta.h` diye ayrı dosyalar bulunmuyor.**  
`NvDsFrameMeta`, `NvDsObjectMeta`, `NvDsBatchMeta`, `NvDsMetaType` ve ilgili pool/list API’leri **`nvdsmeta.h`** içinde tanımlıdır. Eski dokümantasyon veya farklı sürümlerde isimlendirme farkı olabilir; referans tek dosya: **`nvdsmeta.h`**.

---

## Probe (sonda) için tipik okuma zinciri

1. `GstBuffer *buf` probe callback içinde.
2. `NvDsBatchMeta *batch_meta = gst_buffer_get_nvds_batch_meta(buf);`  
   — Tanım: `gstnvdsmeta.h` (içeride `nvdsmeta.h`).
3. Çoklu yazıcı/okuyucu için: `nvds_acquire_meta_lock(batch_meta);` … işlem … `nvds_release_meta_lock(batch_meta);`
4. `for (NvDsMetaList *l_frame = batch_meta->frame_meta_list; l_frame != NULL; l_frame = l_frame->next)`  
   — `l_frame->data` → `(NvDsFrameMeta *)`
5. Her kare için: `frame_meta->obj_meta_list` üzerinde döngü → `(NvDsObjectMeta *)`
6. Kilit/hedef seçimi için tipik alanlar: `object_id`, `class_id`, `confidence`, `rect_params`, `tracker_bbox_info` / `detector_bbox_info`, `obj_label`

---

## `nvdsmeta.h` — Çekirdek yapılar (özet)

### `NvDsMetaType` (seçilmiş değerler)

| Sembol | Anlam |
|--------|--------|
| `NVDS_BATCH_META` | Batch kökü |
| `NVDS_FRAME_META` | Kare meta tipi |
| `NVDS_OBJ_META` | Nesne meta tipi |
| `NVDS_DISPLAY_META` | OSD çizim meta |
| `NVDS_CLASSIFIER_META` / `NVDS_LABEL_INFO_META` | Sınıflandırıcı çıktıları |
| `NVDS_USER_META` | Kullanıcı meta sarmalayıcısı |
| `NVDSINFER_TENSOR_OUTPUT_META` | Ham tensor çıktısı (nvinfer) |
| `NVDSINFER_SEGMENTATION_META` | Segmentasyon |
| `NVDS_TRACKER_PAST_FRAME_META` | Geçmiş kare (tracker) |
| `NVDS_TRACKER_BATCH_REID_META` / `NVDS_TRACKER_OBJ_REID_META` | ReID |
| `NVDS_TRACKER_TERMINATED_LIST_META` / `NVDS_TRACKER_SHADOW_LIST_META` | Track sonlandırma / gölge |
| `NVDS_PREPROCESS_FRAME_META` / `NVDS_PREPROCESS_BATCH_META` | nvdspreprocess |
| `NVDS_START_USER_META` | Özel kullanıcı Gst meta aralığı başlangıcı |

`nvds_get_user_meta_type("ORG.COMPONENT.DESC")` ile özel meta tipi üretilir.

### `NvDsBatchMeta`

- `max_frames_in_batch`, `num_frames_in_batch`
- Pool işaretçileri: `frame_meta_pool`, `obj_meta_pool`, `classifier_meta_pool`, `display_meta_pool`, `user_meta_pool`, `label_info_meta_pool`
- `frame_meta_list` (`NvDsFrameMetaList *` = `GList *`)
- `batch_user_meta_list`
- `meta_mutex` (`GRecMutex`)
- `misc_batch_info[MAX_USER_FIELDS]`

### `NvDsFrameMeta`

| Alan | Kullanım |
|------|----------|
| `pad_index` | streammux pad/port |
| `batch_id` | `NvBufSurface` içindeki yüzey indeksi |
| `frame_num`, `buf_pts`, `ntp_timestamp` | Zaman / sıra |
| `source_id` | Kaynak (kamera) kimliği |
| `source_frame_width/height` | mux girişi |
| `pipeline_width/height` | mux çıkışı |
| `num_obj_meta`, `bInferDone` | Nesne sayısı, inference tamamlandı mı |
| `obj_meta_list` | Tespit/takip nesneleri |
| `display_meta_list` | Kullanıcı OSD meta |
| `frame_user_meta_list` | Kare seviyesi user meta |
| `misc_frame_info[]` | Uygulama alanı |

### `NvDsObjectMeta`

| Alan | Kullanım |
|------|----------|
| `parent` | Hiyerarşik nesne (yoksa NULL) |
| `unique_component_id` | Meta üreten bileşen |
| `class_id` | Birincil sınıf indeksi |
| `object_id` | Takip ID; `UNTRACKED_OBJECT_ID` (0xFFFFFFFFFFFFFFFF) = takipsiz |
| `detector_bbox_info` | Dedektör kutusu (`NvBbox_Coords` içinde) |
| `tracker_bbox_info` | Tracker kutusu |
| `confidence` | Inference güveni (-0.1 = clustering/tracker özel durumu) |
| `tracker_confidence` | NvDCF; KLT/IOU’da -0.1 |
| `rect_params` | **Klip’lenmiş** OSD dikdörtgeni (`NvOSD_RectParams`) |
| `mask_params` | Segmentasyon maskesi |
| `text_params` | OSD metin |
| `obj_label[MAX_LABEL_SIZE]` | Sınıf metni |
| `classifier_meta_list` | İkincil sınıflandırıcılar |
| `obj_user_meta_list` | Nesneye bağlı user meta |
| `misc_obj_info[]` | Uygulama alanı |

### `NvDsBaseMeta`

- `batch_meta`, `meta_type`, `uContext`, `copy_func`, `release_func` — türetilmiş meta türlerinin kökü.

### Sık kullanılan pool API’leri (probe’da genelde okuma; yazarken pool’dan al)

- `nvds_acquire_frame_meta_from_pool`, `nvds_add_frame_meta_to_batch`, …
- `nvds_acquire_obj_meta_from_pool`, `nvds_add_obj_meta_to_frame`, `nvds_remove_obj_meta_from_frame`
- `nvds_acquire_display_meta_from_pool`, `nvds_add_display_meta_to_frame`
- `nvds_acquire_user_meta_from_pool`, `nvds_add_user_meta_to_frame` / `_to_obj` / `_to_batch`
- Temizleme: `nvds_clear_*_meta_list` ailesi
- Derin kopya: `nvds_copy_frame_meta`, `nvds_copy_obj_meta`, `nvds_copy_obj_meta_list`, …

---

## `gstnvdsmeta.h` — GstBuffer köprüsü

- `NvDsMeta`: `GstMeta` + `meta_data` (tip `meta_type`’a göre cast) + `copyfunc` / `freefunc` / `gst_to_nvds_meta_transform_func`
- `gst_buffer_add_nvds_meta`, `gst_buffer_get_nvds_meta`
- **`gst_buffer_get_nvds_batch_meta(GstBuffer *)`** → probe giriş noktası
- `GstNvDsMetaType`: `NVDS_BATCH_GST_META`, `NVDS_DECODER_GST_META`, `NVDS_DEWARPER_GST_META`, `NVDS_BUFFER_GST_AS_FRAME_USER_META`, …
- `nvds_copy_gst_meta_to_frame_meta` (nvstreammux2 ile GstMeta → frame user meta)

---

## `nvll_osd_struct.h` — Kutu ve OSD

- `NvBbox_Coords`: `left`, `top`, `width`, `height` (piksel, **unclipped** — `NvDsComp_BboxInfo` içinde)
- `NvOSD_RectParams`: `left`, `top`, `width`, `height`, `border_width`, `border_color`, arka plan renk bayrakları
- `NvOSD_TextParams`, `NvOSD_LineParams`, `NvOSD_ArrowParams`, `NvOSD_CircleParams`, `NvOSD_MaskParams`

Kilit (X, Y) genelde `rect_params` veya `tracker_bbox_info` / `detector_bbox_info` merkezinden türetilir; çözünürlük için `NvDsFrameMeta::pipeline_width/height` veya kaynak boyutları kullanılır.

---

## `nvds_tracker_meta.h` — Tracker ek verileri

- `TRACKER_STATE`: `EMPTY`, `ACTIVE`, `INACTIVE`, `TENTATIVE`, `PROJECTED`
- `NvDsTargetMiscDataFrame`, `NvDsTargetMiscDataObject`, `NvDsTargetMiscDataStream`, `NvDsTargetMiscDataBatch`
- `NvDsReidTensorBatch`, `NvDsTrajectoryBatch`, `NvDsObjConvexHull`  
User meta veya tracker çıktılarıyla birleştirildiğinde probe’da `NvDsUserMeta` + `meta_type` ile eşleştirilir.

---

## `nvds_roi_meta.h` — nvdspreprocess ROI

- `NvDsRoiMeta`: `roi` (`NvOSD_RectParams`), `frame_meta`, `object_meta`, ölçek oranları (`scale_ratio_x/y`, `offset_left/top`), `converted_buffer`, classifier/user meta listeleri.

---

## Tüm `.h` dosyaları — dizin indeksi (89 dosya)

Aşağıdaki liste `includes/` altında `**/*.h` glob ile üretilmiştir. Probe odaklı çalışmada çoğu zaman **`nvdsmeta.h` + `gstnvdsmeta.h` + `nvll_osd_struct.h`** yeterlidir; infer/tracker özelleştirmesi için ek başlıklar eklenir.

**Kök `includes/`**

| Dosya | Konu |
|-------|------|
| `nvdsmeta.h` | Batch / frame / object / display / user meta, `NvDsMetaType`, pool API |
| `gstnvdsmeta.h` | GstBuffer ↔ NvDsBatchMeta |
| `nvll_osd_struct.h`, `nvll_osd_api.h` | OSD geometri ve çizim |
| `nvds_roi_meta.h` | ROI preprocess meta |
| `nvds_tracker_meta.h` | Tracker yardımcı yapıları |
| `nvds_audio_meta.h` | Ses batch/frame meta |
| `nvds_analytics_meta.h` | Analytics meta |
| `nvds_latency_meta.h`, `nvds_latency_meta_internal.h` | Gecikme ölçümü |
| `nvds_opticalflow_meta.h` | Optical flow |
| `nvds_dewarper_meta.h` | Dewarper |
| `nvdsinfer.h`, `nvdsinfer_context.h`, `nvdsinfer_utils.h`, `nvdsinfer_custom_impl.h`, `nvdsinfer_tlt.h`, `nvdsinfer_dbscan.h`, `nvdsinfer_logger.h` | nvinfer |
| `gstnvdsinfer.h` | Gst nvinfer |
| `nvdstracker.h` | Tracker API |
| `nvbufsurface.h`, `nvbufsurftransform.h`, `nvbufaudio.h` | Yüzey/bellek |
| `NvDsMemoryAllocator.h`, `INvDsAllocator.h` | Allocators |
| `nvds_utils.h`, `nvds_parse.h`, `nvds_common_parser.h`, `nvds_yml_parser.h` | Yardımcılar / YAML |
| `nvds_version.h` | Sürüm |
| `nvdsmeta_schema.h`, `nvds_msgapi.h`, `nvmsgbroker.h` | Mesajlaşma / şema |
| `nvds_logger.h`, `nvtx_helper.h` | Log / NVTX |
| `nvds_mask_utils.h` | Maske |
| `nvds_obj_encode.h` | Obje encode |
| `nvdsdummyusermeta.h`, `nvdscustomusermeta.h` | Örnek user meta |
| `nvdsnmos.h` | NMOS |
| `gst-nvcommon.h`, `gst-nvevent.h`, `gst-nvmessage.h`, `gst-nvquery.h`, `gst-nvquery-internal.h` | Gst yardımcıları |
| `gstnvdsbufferpool.h`, `gst_nvdsaudio.h`, `gstnvipcmeta.h` | Gst pool / IPC |
| `gst-nvdscustomevent.h`, `gst-nvcustomevent.h`, `gst-nvdscustommessage.h`, `gst-nvdssr.h`, `gst-nvmultiurisrcbincreator.h`, `gst-nvdscommonconfig.h` | Özel olaylar / SR / multi-URI |
| `nvdsgstutils.h` | Gst utils |
| `nvds_appctx_server.h`, `nvds_rest_server.h`, `CivetServer.h`, `civetweb.h` | REST / sunucu |
| `NVWarp360.h` | Dewarp 360 |

**`includes/ds3d/common/`** (3D / analiz yardımcıları)

`common.h`, `defines.h`, `config.h`, `cuda_utils.h`, `memdata.h`, `safe_queue.h`, `signalshot.h`, `type_trait.h`, `idatatype.h`, `func_utils.h`, `typeid.h`, `ds3d_analysis_datatype.h`, `abi_frame.h`, `abi_window.h`, `abi_obj.h`, `abi_dataprocess.h`, `impl/impl_*.h`

**`includes/nvdsinferserver/`**

`infer_defines.h`, `infer_datatypes.h`, `infer_options.h`, `infer_ioptions.h`, `infer_icontext.h`, `infer_post_datatypes.h`, `infer_custom_process.h`

---

## Onay metni (kapsam)

Bu indeks, **`/opt/nvidia/deepstream/deepstream/sources/includes/` altındaki başlıklardan** özellikle **metadata, GstBuffer taşıması, OSD geometrisi ve tracker yardımcı tipleri** için referans oluşturur. Probe yazarken **`NvDsBatchMeta` → `NvDsFrameMeta` → `NvDsObjectMeta`** hiyerarşisi ve `gst_buffer_get_nvds_batch_meta` bu dosyalardaki tanımlarla uyumlu kullanılmalıdır.

**Dürüst sınır:** DeepStream’in tamamı (tüm örnek uygulamalar, her plugin’in iç API’si, Infer Server protokolü, üretim optimizasyon rehberleri) bu 89 başlıktan ibaret değildir. Ancak **standart video inference + tracker + OSD pipeline’ında pad probe ile meta okuma/yazma** için yukarıdaki yapılar ve dosya eşlemesi yerel SDK ile uyumludur ve bundan sonra yazılacak C++ probe kodunda **bu struct’lar ve `nvdsmeta.h` API’leri referans alınmalıdır**.

---

*İndeks, workspace içinde DeepStream sürümü ile birlikte güncellenmelidir; JetPack yükseltmesinde NVIDIA başlıklarını yeniden karşılaştırın.*
