# Diğer Yapay Zekâlar İçin: Sistem Mimarisi İnceleme + Tamamlama Promptu

Bu dosyayı **Cursor, Claude, Copilot** veya başka bir asistana verebilirsin. Amaç: **Savaşan İHA** deposundaki sistem mimarisini **analiz etmek**, **eksikleri gidermek** ve gerekiyorsa **`docs/taslaklar.md` sonuna** veya ayrı bir rapora **profesyonel mimari metin** üretmektir.

---

## 1) Kopyala-yapıştır: Ana prompt (asistana ver)

```
@workspace Sen kıdemli bir Sistem Mimarı ve Kontrol Mühendisisin. Aşağıdaki bağlamı ve ÖNCEKİ İNCELEME BULGULARINI dikkate alarak Savaşan İHA projesinin sistem mimarisini gözden geçir, eksikleri tamamla ve tek bir tutarlı doküman üret.

GÖREVLERİN:
1) Repo içinde 02_Ana_Sistem_CPP (DeepStream, autopilot köprüsü, PID, estimator’lar) ve docs/MASTER_4_1_OTONOM_KILITLENME.md ile mevcut docs/taslaklar.md içindeki "Sistem Mimarisi (Master Prompt Çıktısı)" bölümünü KARŞILAŞTIR.
2) Çıktında şunları içer:
   - Mermaid.js: uçtan uca akış; subgraph ile grupla: Giriş (CSI/USB), DeepStream (mux, isteğe bağlı optical flow), Algılama (nvinfer/YOLO), Takip (nvtracker/NvDCF), Hedef seçimi ve kilit (TrackSelector, LockSelectionPolicy, LockState ~4 s), Kestirim (WorldTargetEstimator, VehicleStateEstimator, EvasionController), Kontrol (PID), Haberleşme (AlcLinkBridge — hem TX setpoint hem RX telemetri), isteğe bağlı üst modlar (faz runner’lar, AIR_LOCK görev modu, yarışma HTTP vb. — depoda varsa kısaca).
   - LaTeX: MASTER ile uyumlu olacak şekilde pinhole/alan mesafesi, normalize görüntü hatası, stand-off, lateral/düşey hata, mümkünse kilit sağlığı ve VehicleStateEstimator füzyon/yenilik kapısı özeti; PID (ölü bant, integral sınırı, anti-windup, slewrate) — kod alıntısı YOK, sadece matematik ve mantık.
   - Teknik entegrasyon: Jetson GPU’da nvinfer/nvtracker; NVMM/NV12; "çoğunlukla düşük kopya" ifadesi (CPU map gereken analiz yollarını tek cümleyle); seri hat bant genişliği ve varsayılan ~20 Hz gönderim; paket v1/v2.
3) ÖNCEKİ İNCELEME BULGULARI bölümündeki maddeleri tek tek ele al: kapatıldıysa dokümanda nasıl kapattığını yaz; hâlâ eksikse tamamla.
4) Kod dosyasından doğrudan kod bloğu kopyalama; dosya/sınıf adları referans olarak kullanılabilir.
5) Çıktıyı docs/taslaklar.md dosyasının EN ALTINA, "---" sonrasına yeni bir bölüm başlığıyla ekle: "## Sistem Mimarisi — İnceleme ve Revizyon (Diğer AI Çıktısı)" — veya kullanıcı ayrı dosya isterse docs/SISTEM_MIMARISI_REVIZYON.md oluştur.

Önce kısa bir plan ve bulgu özeti yaz, ardından tam metni üret.
```

---

## 2) Bağlam özeti (asistanın okuması için)

| Alan | Not |
|------|-----|
| Ana kod | `02_Ana_Sistem_CPP/` — DeepStream pipeline `ds_app.cpp`, kamera `csi_source.cpp` / `usb_source.cpp`, faz runner’lar `runners/` |
| Köprü | `AlcLinkBridge`: setpoint TX, telemetri RX (`DrainAndParseRx`), varsayılan gönderim ~50 ms (~20 Hz), paket sürümü 1 veya 2 |
| Kontrol / kestirim | `WorldTargetEstimator`, `VehicleStateEstimator`, `PidController`, `EvasionController`, kilit politikası ve `LockState` (~4 s doğrulama) |
| Kanonik matematik metni | `docs/MASTER_4_1_OTONOM_KILITLENME.md` |
| Mevcut taslak mimari | `docs/taslaklar.md` → "Sistem Mimarisi (Master Prompt Çıktısı)" |

---

## 3) Önceki inceleme bulguları (diğer AI bunları adreslemeli)

Aşağıdaki maddeler bir önceki gözden geçirmede **eksik veya zayıf** bulundu; revizyon bunları kapatmalıdır:

1. **Alacakart RX telemetrisi**: Mimari sadece TX gibi anlatılmıştı; kapalı döngü için RX (hız, yaw rate vb.) ve `VehicleStateEstimator` bağlantısı şemada ve metinde olmalı.
2. **MASTER_4_1 ile hizalama**: Normalize görüntü hatası, \(d = k_d/\sqrt{A}\), stand-off \(e_{\mathrm{long}}\), lateral/düşey hata, kilit sağlığı \(H_{\mathrm{lock}}\), yenilik kapısı + LPF — taslakta genel LaTeX vardı; MASTER ile **aynı tanımlar** veya açık gönderim gerekir.
3. **Mermaid optical flow**: İsteğe bağlı olduğu ve gerçek boru hattı sırasının yanlış okunmaması için dipnot veya sadeleştirilmiş tek hat.
4. **Üst mod / faz katmanı**: Phase1–5 runner’lar, AIR_LOCK görev modu, isteğe bağlı yarışma HTTP — depoda varsa mimari özetinde yer almalı.
5. **Sun Mode / NV12 ön-işleme**: Proje skill’lerinde geçiyorsa mimaride kısa bahis veya “yol haritası / opsiyonel” notu.
6. **Zero-copy iddiası**: NVMM doğru; CPU `NvBufSurfaceMap` gereken yollar için tek cümleyle muhafazakâr ifade.

---

## 4) Beklenen çıktı kalitesi

- Akademik Türkçe veya rapor dilinde Türkçe; tutarlı terimler (NvDCF = DeepSort ile karıştırılmaz).
- Mermaid derlenebilir (syntax hatasız) olmalı.
- LaTeX blokları rapora yapıştırılabilir.

---

## 5) Dosya yolu

- Varsayılan çıktı konumu: **`docs/taslaklar.md` append** (kullanıcı başka isterse `docs/SISTEM_MIMARISI_REVIZYON.md`).

---

*Bu dosya: insan + AI işbirliği için prompt ve analiz özeti taşır; repo köküne göre yollar `docs/` altındadır.*
