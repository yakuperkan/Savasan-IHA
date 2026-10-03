# DeepStream Gst-nvtracker Kapsamlı Dokümantasyonu

**Kaynak:** [NVIDIA DeepStream Gst-nvtracker Documentation](https://docs.nvidia.com/metropolis/deepstream/dev-guide/text/DS_plugin_gst-nvtracker.html)

## Önemli Bilgiler

### Temel Tracker Türleri

DeepStream SDK aşağı referans tracker algoritmalarını sunar:

1. **IOU Tracker**: En hafif, sadece IOU bazlı eşleştirme
2. **NvSORT**: NVIDIA-enhanced SORT, Kalman filtre ile iyileştirilmiş
3. **NvDeepSORT**: Derin öğrenme tabanlı Re-ID ile DeepSORT
4. **NvDCF**: Discriminative Correlation Filter tabanlı görsel tracking
5. **MaskTracker**: SAM2 ile çoklu obje tracking ve segmentasyon (Geliştirici Önizlemesi)

### Konfigürasyon Yapısı

```yaml
[tracker]
enable=1
tracker-width=640
tracker-height=384
gpu-id=0
ll-lib-file=/opt/nvidia/deepstream/deepstream/lib/libnvds_nvmultiobjecttracker.so
ll-config-file=config_tracker_NvDCF_accuracy.yml
```

### NvDCF Tracker Önemli Parametreler

**VisualTracker:**
- `visualTrackerType`: {DUMMY=0, NvDCF=1, NvDCF_VPI=2}
- `useColorNames`: Renk isim özelliklerini kullan
- `useHog`: Histogram-of-Oriented-Gradient özelliklerini kullan
- `featureImgSizeLevel`: 1-5 arası (12x12'den 36x36'ya kadar)
- `filterLr`: DCF filtresi için öğrenme oranı (0.0-1.0)
- `gaussianSigma`: İstenilen yanıt için standart sapma

**TargetManagement:**
- `maxTargetsPerStream`: Her akış için maksimum hedef sayısı (varsayılan >10)
- `probationAge`: Deneme süresi (frame sayısı)
- `maxShadowTrackingAge`: Maksimum gölge takip süresi
- `minTrackerConfidence`: Hedef güven eşiği (0.0-1.0)

**DataAssociator:**
- `associationMatcherType`: {GREEDY=0, CASCADED=1}
- `checkClassMatch`: Sınıf bazlı eşleştirme
- `minMatchingScore4Iou`: Minimum IOU eşiği

### Re-Identifikasyon (Re-ID) Modülü

```yaml
ReID:
  reidType: 1  # NvDEEPSORT=1, Reid based reassoc=2
  batchSize: 100
  reidFeatureSize: 256
  reidHistorySize: 100
  inferDims: [3, 256, 128]
  networkMode: 1  # fp32=0, fp16=1, int8=2
```

### Sub-batching Özelliği

Akış gruplarını alt gruplara bölerek paralel işleme:
```yaml
[tracker]
sub-batches=0,1;2,3  # Source 0&1 bir grupta, 2&3 farklı grupta
```

### Metadata Management

**Terminat Track Listesi:**
- Hedef sonlandırıldığında tam gezi geçmişi çıktısı
- `outputTerminatedTracks: 1` ile etkinleştirilebilir

**Shadow Tracking Data:**
- Gölge takip modundaki hedef verileri çıktısı
- `outputShadowTracks: 1` ile etkinleştirilebilir

**Past-frame Target Data:**
- Geçmiş kare hedef verileri
- Otomatik olarak misc data içine dahildirilir

### Latency Measurement API

```c
// Metadata'den referans zaman damgası alımı
GstReferenceTimestampMeta *meta = gst_buffer_get_reference_timestamp_meta(buf, NULL);
```

## Jetson Orin NX Optimize Edilecekleri

1. **PVA Backend**: DCF işlemleri için enerji verimliliği
   ```yaml
   VisualTracker:
     vpiBackend4DcfTracker: 2  # PVA=2, CUDA=1
   ```

2. **Batch Processing**: GPU kullanımını optimize et
3. **Sub-batching**: Birden fazla akış için paralel işleme

## İHA Manevraları İçin Öneriler

**Hızlı Manevralar için:**
- NvSORT + Kalman Filter
- `probationAge`: 1-2 kare
- `maxShadowTrackingAge`: 30-40 kare

**Yüksek Doğruluk için:**
- NvDCF_accuracy veya NvDeepSORT
- Re-ID modülü etkin
- `enableReAssoc: 1` ile hedef yeniden ilişkilendirme

**CSI/USB Kameralar için:**
- `tracker-width`: 640, `tracker-height`: 384
- `featureImgSizeLevel`: 3-4 (24x24 veya 30x30)
