# 🚀 Savaşan İHA - Release Şablonu

Bu şablon her release için kullanılmalıdır. Lütfen ilgili bölümleri doldurun.

---

## 📋 Release Bilgileri

- **Sürüm:** `vX.Y.Z` (örn: v1.0.0, v1.2.3)
- **Tarih:** YYYY-MM-DD
- **Yayınlayan:** [GitHub Kullanıcı Adı]
- **Durum:** 🟢 Stable / 🟡 Beta / 🔴 Alpha

---

## 🎉 Yenilikler

### Yeni Özellikler
- [ ] Yeni özellik 1 açıklaması
- [ ] Yeni özellik 2 açıklaması
- [ ] Yeni özellik 3 açıklaması

### İyileştirmeler
- [ ] İyileştirme 1 açıklaması
- [ ] İyileştirme 2 açıklaması
- [ ] İyileştirme 3 açıklaması

---

## 🐛 Düzeltmeler

### Kritik Düzeltmeler
- [ ] Hata 1 düzeltmesi (Issue #123)
- [ ] Hata 2 düzeltmesi (Issue #456)

### Küçük Düzeltmeler
- [ ] Küçük hata 1
- [ ] Küçük hata 2

---

## 🔧 Teknik Değişiklikler

### Kod Değişiklikleri
- **Dosyalar:** `src/camera/csi_source.cpp`, `src/deepstream/ds_app.cpp`
- **Satır Sayısı:** +150, -50
- **Commit Aralığı:** `commit_hash1..commit_hash2`

### Konfigürasyon Değişiklikleri
- [ ] Yeni konfigürasyon parametresi: `NEW_PARAM=value`
- [ ] Deprecated: `OLD_PARAM` (yerine `NEW_PARAM` kullanın)

### Model Değişiklikleri
- **YOLO11:** `v1.0` → `v1.1`
- **YOLO26:** `v1.2` → `v1.3`
- **Optimizasyon:** FP16, INT8 desteği eklendi

---

## 📦 Assets

### İndirilebilir Paketler
- 📦 **savasan-airlock-vX.Y.Z-jetson-orin-nx.tar.gz** (150 MB)
  - Derlenmiş binary
  - Model dosyaları (.engine)
  - Konfigürasyon dosyaları
  - Çalıştırma scriptleri

### Model Dosyaları
- 🧠 **yolo11-uav-v1.1-fp16.engine** (120 MB)
  - Jetson Orin NX için optimize edilmiş
  - Input: 640x640
  - Precision: FP16

- 🧠 **yolo26-uav22-v1.3-fp32.engine** (180 MB)
  - Yüksek doğruluk için
  - Input: 1280x1280
  - Precision: FP32

---

## ⚠️ Breaking Changes

> 🚨 **DİKKAT:** Bu sürümde geriye uyumsuz değişiklikler var!

### 1. Konfigürasyon Dosyası Formatı
- **Eski:** `config.txt`
- **Yeni:** `config.yml`
- **Geçiş:** [Migration Guide](docs/MIGRATION_v1.0_to_v2.0.md) inceleyin

### 2. API Değişiklikleri
- **Eski:** `void oldFunction(int param)`
- **Yeni:** `void newFunction(float param)`
- **Geçiş:** Kodunuzu güncelleyin

---

## 📚 Dokümantasyon

### Güncellenen Dokümanlar
- [x] README.md - Ana güncellemeler eklendi
- [x] BUILD.md - Yeni derleme adımları
- [x] DEPLOYMENT.md - Deployment rehberi güncellendi
- [x] API_Documentation.md - Yeni API'ler eklendi

### Yeni Dokümanlar
- [ ] MIGRATION_v1.0_to_v2.0.md - Geçiş rehberi
- [ ] TROUBLESHOOTING.md - Sorun giderme

---

## 🚀 Upgrade Nasıl Yapılır?

### Otomatik Upgrade (Önerilen)
```bash
# Yeni sürümü çekin
git pull origin main

# Derleyin
cd 02_Ana_Sistem_CPP/build
cmake .. && make -j4
```

### Manuel Upgrade
1. Yeni sürümü indirin
2. Eski dosyaları yedekleyin
3. Yeni dosyaları çıkarın
4. Konfigürasyon dosyalarını güncelleyin
5. Derleyin ve çalıştırın

### Breaking Change Var İse
```bash
# Migration scriptini çalıştırın
./scripts/migrate_v1.0_to_v2.0.sh

# Manuel olarak güncelleyin
cp config/config.txt config/config.yml.backup
# config.yml'i düzenleyin
```

---

## 🧪 Test Sonuçları

### Unit Testler
- ✅ Test Suite 1: Geçti (100/100)
- ✅ Test Suite 2: Geçti (85/90)
- ⚠️ Test Suite 3: Kısmen geçti (40/50)

### Entegrasyon Testleri
- ✅ CSI Kamera: Geçti
- ✅ USB Kamera: Geçti
- ❌ DLA Modu: Başarısız (Bilinen issue #789)

### Performance Testleri
- **YOLO11 FP16:** 60 FPS (hedef: 55 FPS) ✅
- **YOLO26 FP32:** 45 FPS (hedef: 40 FPS) ✅
- **Memory Usage:** 2.1GB (hedef: 2.5GB) ✅

---

## 🐛 Bilinen Sorunlar

- **Issue #123:** USB kamera bazen bağlanmıyor
  - **Çözüm:** Kamera modülünü yeniden başlatın
  - **Durum:** 🟡 Beklemede

- **Issue #456:** YOLO26 INT8 modu düşük performans
  - **Çözüm:** FP16 kullanın
  - **Durum:** 🟡 İnceleniyor

---

## 📞 Destek

### Sorun Bildirme
- GitHub Issues: [yakuperkan/Savasan_IHA_Jetson/issues](https://github.com/yakuperkan/Savasan_IHA_Jetson/issues)
- Şablonu kullanarak issue oluşturun

### Destek Alırken Şunları Ekleyin
1. Jetson modeli ve JetPack sürümü
2. Kullanılan model ve optimizasyon
3. Komut satırı çıktısı (log)
4. Sistem kaynakları (cpuinfo, meminfo)

---

## 🙏 Teşekkürler

Bu sürüme katkıda bulunanlar:
- @contributor1 - Yeni özellikler
- @contributor2 - Bug düzeltmeleri
- @contributor3 - Dokümantasyon

---

## 📊 İstatistikler

- **Toplam Commit:** 25
- **Dosya Değişikliği:** 50
- **Satır Ekleme:** +1,500
- **Satır Silme:** -300
- **Geliştirici:** 3 kişi
- **Çözülen Issue:** 5
- **Kapalı PR:** 3

---

## 🔗 Kaynaklar

- [GitHub Repository](https://github.com/yakuperkan/Savasan_IHA_Jetson)
- [Dokümantasyon](docs/)
- [Releases](https://github.com/yakuperkan/Savasan_IHA_Jetson/releases)
- [Issues](https://github.com/yakuperkan/Savasan_IHA_Jetson/issues)

---

**⚠️ Önemli Not:** Bu sürümü production ortamında kullanmadan önce test edin.