# Lock HUD + ROI Gate Uygulama Plani (Rapor Taslagi)

Bu dokuman, ITU ATA videosundan cikarilan operasyonel derslere gore bizim sistemde uygulanacak iyilestirmeleri planlamak ve raporu standart formatta doldurmak icin hazirlanmistir.

Tarih: 2026-04-17
Proje: Savasan IHA Jetson (DeepStream + NvDCF + Phase5)

## 1) Amac

- Kilit davranisini operator tarafinda daha anlasilir hale getirmek.
- Gereksiz kilit resetlerini azaltmak.
- Yarismaya yonelik canli HUD metrikleri ile operasyon guvenini artirmak.
- Raporlama tarafinda olculebilir KPI seti olusturmak.

## 2) Kapsam

Bu iterasyonda asagidaki 5 baslik hedeflenir:

1. Lock reason codes + HUD gorunurlugu
2. Net ROI-gate kurali (sayac artisi/azalmasi)
3. Grace window fail-safe
4. Lock health skoru (0..1)
5. (Opsiyonel) BBox scale stabilization (EMA ile baslangic)

## 3) Mevcut Durum (Referans)

- Lock state ve 4sn dogrulama mantigi mevcut.
- HUD banner mevcut (kilit var/yok + sayac goruntuleniyor).
- Track secim politikasi conf/center/id-switch parametreleri ile calisiyor.
- Phase5 ve ALC link tarafinda kontrol/telemetri baglantisi mevcut.

Not: ITU ATA tarafi icin kesin algoritma adi acik kaynakta dogrulanamamistir; bu plan "ekrandan gozlenen operasyon disiplini" baz alir.

## 4) Uygulama Plani (Oncelik Sirasi)

### Faz 1 - Kritik ve Hizli Kazanc (1 gun)

### 4.1 Lock Reason Codes + HUD

HUD ve log ciktilarinda asagidaki reason kodlarinin gorunmesi:

- `roi_out`
- `low_conf`
- `id_switch`
- `missed`
- `grace_hold` (kilit korunuyor ama riskli durum)

Hedef:
- Operator "neden kilit yok/koptu" bilgisini aninda gorebilsin.

### 4.2 ROI-Gate Kurali

Kural:
- BBox merkezi ROI icindeyse lock sayaci artsin.
- ROI disina cikinca policy'ye gore kontrollu azalis veya reset uygulansin.
- ROI durumu HUD'da acik gorunsun (`ROI: IN/OUT`).

Hedef:
- Kilit davranisini deterministic hale getirmek.

### 4.3 Grace Window

Kural:
- Kisa sureli kayiplarda (ornek 0.3-0.6s) kilit hemen dusurulmesin.
- Bu periyotta reason `grace_hold` yazilsin.
- Sure asildiginda `missed` ile temiz dusus yapilsin.

Hedef:
- Tek kare/ani titreşim kaynakli yanlis resetleri azaltmak.

## 5) Faz 2 - Onemli Iyilestirme (0.5-1 gun)

### 5.1 Lock Health Skoru

Tek bir `0..1` skor:

- confidence
- center proximity (merkeze yakinlik)
- temporal stability
- id consistency

Ornek birlesim:

`health = 0.35*conf + 0.25*center + 0.25*stability + 0.15*id_consistency`

HUD:
- `LOCK_HEALTH: 0.82 (GOOD)` benzeri seviye etiketi.

Hedef:
- Operator ve test ekibi icin sade ama etkili karar metriği.

## 6) Faz 3 - Opsiyonel (1-2 gun)

### 6.1 BBox Scale Stabilization

Ilk adim (dusuk risk):
- width/height icin EMA + clamp

Ileri adim (gerekiyorsa):
- log-polar/Fourier tabanli scale refinement POC

Hedef:
- BBox jitter kaynakli lock resetlerini azaltmak.

## 7) Teknik Is Paketi (Dosya Bazli)

- `02_Ana_Sistem_CPP/src/tracking/track_selector.cpp`
  - reason code olusturma
  - HUD text genisletme
  - ROI IN/OUT + grace bilgisi
- `02_Ana_Sistem_CPP/src/tracking/lock_selection_policy.hpp/.cpp`
  - ROI gate policy
  - grace window mantigi
  - lock health skoru hesaplama
- `02_Ana_Sistem_CPP/src/tracking/lock_state.hpp`
  - reason/status alanlari
- `02_Ana_Sistem_CPP/src/runners/phase5_callbacks.cpp` (gerekiyorsa)
  - health/reason telemetry aktarimi

## 8) Ortam Degiskenleri (Onerilen)

- `SAVASAN_LOCK_ROI_X_MIN`, `SAVASAN_LOCK_ROI_X_MAX`
- `SAVASAN_LOCK_ROI_Y_MIN`, `SAVASAN_LOCK_ROI_Y_MAX`
- `SAVASAN_LOCK_GRACE_MS`
- `SAVASAN_LOCK_HEALTH_ENABLE`
- `SAVASAN_LOCK_HEALTH_MIN`

## 9) KPI ve Basari Kriteri (Rapor Doldurma Kismi)

Bu alanlar test sonrasi doldurulacak:

- Kilit basari orani (adet):
  - Once:
  - Sonra:
- Ortalama lock suresi (sn):
  - Once:
  - Sonra:
- Lock reset nedeni dagilimi (%):
  - roi_out:
  - low_conf:
  - id_switch:
  - missed:
- Yanlis reset orani:
  - Once:
  - Sonra:
- Operator algi puani (1-5):
  - Once:
  - Sonra:

Kabul kosullari (minimum):
- Yanlis reset oraninda en az %20 azalma
- Lock basari oraninda artis veya en az esit kalma
- HUD reason dogruluk kontrolunde >= %95 tutarlilik

## 10) Test Plani (Kisa)

- Senaryo A: Hedef ROI icinde stabil
- Senaryo B: Hedef ROI sinirinda salinim
- Senaryo C: Kisa sureli kayip (grace test)
- Senaryo D: ID switch benzeri zorlu sahne
- Senaryo E: Dusuk confidence/uzak hedef

Her senaryoda:
- reason kodu dogru mu?
- state gecisleri beklenen sekilde mi?
- lock health davranisi mantikli mi?

## 11) Risk ve Azaltim

- Risk: Fazla agresif ROI reset
  - Azaltim: ROI disinda direkt reset yerine controlled decay
- Risk: Health skoru yanlis alarm uretebilir
  - Azaltim: threshold env ile runtime tuning
- Risk: HUD kalabaligi
  - Azaltim: compact tek satir + detay mode opsiyonu

## 12) Sonuc Ozeti

En yuksek etkiyi `Reason + ROI Gate + Grace Window` birlikte verir.
Bu paket tamamlandiginda:
- Operator "neden kilit yok/koptu"yu aninda gorecek,
- kilit davranisi daha deterministic olacak,
- gereksiz resetler azalacak,
- rapor metrikleri sayisal olarak savunulabilir hale gelecektir.

