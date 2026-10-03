# Nano Banana Pro — Sistem Mimarisi Görsel Promptu (chart değil)

Bu metni **Nano Banana Pro** (veya benzeri görsel üretim aracına) yapıştır. Amaç: **akış şeması / Mermaid / blok diyagram** görünümü **istemiyoruz**; bunun yerine **teknik infografik, katmanlı kesit veya izometrik “sistem posteri”** üretmek.

---

## Ana prompt (İngilizce — çoğu model için en iyi sonuç)

```
Create a high-end technical illustration of an autonomous interceptor UAV software stack running on NVIDIA Jetson. NOT a flowchart, NOT a Mermaid-style diagram, NOT a chart with axes. Style: cinematic tech infographic, layered cross-section, subtle isometric depth, clean typography labels, dark slate background with soft teal and amber accents, premium aerospace UI mood. No cartoon style.

Visual story (bottom to top or left to right as layered slabs, like a product cutaway):
1) Sensors: CSI camera and USB camera feeding NVMM NV12 video memory.
2) DeepStream slab: nvstreammux batch, optional nvopticalflow, nvinfer YOLO TensorRT on GPU, nvtracker NvDCF multi-object tracking, nvvidconv, nvdsosd, sink/record/UDP. Show GPU acceleration as a glowing chip plane, not as arrows.
3) Optional “Sun Mode” side path: small NV12 luminance statistics / gamma hint as a thin parallel lane (optional, subtle).
4) Decision slab: TrackSelector, LockSelectionPolicy, LockState (~4s validated lock), WorldTargetEstimator, VehicleStateEstimator fusion with innovation gate, EvasionController.
5) Control slab: PidController multi-axis, SetpointCommand velocities and yaw rate.
6) Communications slab: AlcLinkBridge serial link to autopilot; emphasize bidirectional: TX setpoints AND RX telemetry feeding back into fusion (closed loop), drawn as a compact cable bridge module, not a graph.

Optional upper orchestration ribbon (thin, separate): Phase runners, AIR_LOCK mission mode, optional competition HTTP reporting (non-flight-critical).

Composition rules:
- Use layered glass panels, subtle glow, micro iconography (camera lens, GPU die, serial plug), minimal arrows (at most a few soft light trails), no dense arrow spaghetti.
- Text labels short and professional (English), 12–18 readable labels total.
- Aspect ratio 16:9 or 3:2, 4K-friendly detail, crisp edges, no watermark.
```

---

## Kısa Türkçe varyant (araç Türkçe destekliyorsa)

```
NVIDIA Jetson üzerinde çalışan otonom İHA yazılım yığınının üst düzey teknik illüstrasyonu. Akış şeması veya grafik istemiyorum; katmanlı kesit, cam paneller, izometrik derinlik, koyu arka plan, sakin teknoloji estetiği. Alt katmanda CSI/USB kamera ve NVMM bellek yolu; ortada DeepStream (YOLO TensorRT, NvDCF takip, OSD); üstte kilit ve kestirim (TrackSelector, LockState ~4s, WorldTargetEstimator, VehicleStateEstimator, kaçış); kontrol PID; en sonda Alacakart seri köprüsü — TX setpoint ve RX telemetrinin füzyona geri beslemesi kapalı döngü olarak hissedilsin. Üstte ince bir şeritte faz runner ve AIR_LOCK / yarışma HTTP opsiyonları. Ok yoğunluğu düşük, etiketler kısa ve profesyonel.
```

---

## Negatif prompt (model destekliyorsa ekle)

```
flowchart, org chart, mind map, bar chart, line chart, pie chart, Mermaid, dense arrows, spaghetti diagram, whiteboard sketch, clipart, childish icons, low resolution text, watermark, logo of real companies
```

---

## Referans (senin tarafında tut)

- Metin kaynağı: `docs/taslaklar.md` → “Sistem Mimarisi — İnceleme ve Revizyon”.
- Vektör/PNG çıktı: `docs/diagrams/sistem_mimari.png` (Mermaid) — **Nano Banana’ya referans görsel olarak yükleme**: stil karışmasın; sadece bileşen listesi için bak.

---

## İpuçları

- **İlk tur** geniş sahne; **ikinci tur** “same style, tighter crop on serial bridge + fusion loop” ile detay iste.
- Poster için: çıktıyı `docs/diagrams/` altına `sistem_mimari_nanobanana.png` gibi kaydet; rapora göm.
