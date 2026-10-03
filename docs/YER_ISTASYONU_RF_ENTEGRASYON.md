# Yer İstasyonu RF Entegrasyon Notu — AIR_LOCK

**Alıcı:** yer istasyonu / GCS yazılımı  
**Gönderen:** Jetson AIR_LOCK (Savasan IHA)  
**Tarih:** 2026-08-23  
**Kapsam:** telemetri paketi, araç mod kodu, haritadan tıklanan noktaya gitme

Bu not, MSO entegrasyonundan sonra **yer tarafının ayarlaması gereken** RF sözleşmesini anlatır. Kamikaze / QR bu workspace'te yoktur.

---

## 1. Ne değişti, ne değişmedi

| Konu | Eski (kırpılmış AlpaguLink) | Şimdi |
|------|-----------------------------|--------|
| Araç modu (`0x01` içindeki `mod`) | Yalnızca 0 / 1 / 2 yazılıyordu; 3 ve üstü 0'a kırpılıyordu | **Ham kart kodu** gider (0 … 7+) |
| Telemetri `0x01` | 35 bayt, kilit + hedef piksel | Aynı 35 bayt; alan sırası değişmedi |
| Kilit bitişi `0x31` | Boş payload | Boş payload (değişmedi) |
| Haritada tıklama | Kart görev listesi `0x10`/`0x12` (ACM0) | **`0x2A`** — nokta Jetson güdümüne gider, karta yazılmaz |
| Sınır / HSS | `0x25` / `0x24` | Aynı; artık geofence bunları kullanır |

**Kart görev listesi (`0x10` / `0x12`) artık yok sayılır.** Alacakart görev yükleme ACM0 ister; ACM0'ın tek sahibi C++ köprüdür. Haritada bir noktaya tıklayınca uçağın oraya gitmesi **görev listesi değildir** — şartname §6.1.7'deki "varış noktası tanımlama" komutudur ve `0x2A` ile yapılır.

---

## 2. Çerçeve (her paket)

Little-endian. XOR sağlama, `msg_id + length + payload` üzerinden.

```
AA 55  <msg_id>  <len>  <payload...>  <xor>
```

```python
import struct

def calc_checksum(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b
    return crc

def pack_frame(msg_id: int, payload: bytes) -> bytes:
    header = struct.pack("<BBBB", 0xAA, 0x55, msg_id, len(payload))
    xor = calc_checksum(header[2:] + payload)
    return header + payload + struct.pack("<B", xor)
```

Maksimum payload: 150 bayt.

---

## 3. Downlink — uçak → yer

### 3.1 `0x01` — telemetri (35 bayt)

Format: `<fffffHBBBBBHHHH`

| Offset | Tip | Alan | Birim / not |
|--------|-----|------|-------------|
| 0 | float32 | enlem | derece |
| 4 | float32 | boylam | derece |
| 8 | float32 | irtifa | metre |
| 12 | float32 | **yatış (roll)** | derece |
| 16 | float32 | **dikilme (pitch)** | derece |
| 20 | uint16 | pusula | 0…359 |
| 22 | uint8 | hız | m/s, 0…255'e kırpılmış |
| 23 | uint8 | batarya | volt × 10 (ör. 126 = 12.6 V) |
| 24 | uint8 | **araç modu (ham kod)** | aşağıdaki tablo |
| 25 | uint8 | uydu sayısı | |
| 26 | uint8 | kilit | 0 / 1 |
| 27 | uint16 | hedef_x | piksel, kare 800×600 varsayılan |
| 29 | uint16 | hedef_y | |
| 31 | uint16 | hedef_w | 0 olabilir |
| 33 | uint16 | hedef_h | 0 olabilir |

Yatış ve dikilme sırası MSO ile aynıdır: **önce roll, sonra pitch.** Ters unpack HUD'u yatar.

### 3.2 Araç mod kodu (ayarlanması gereken yer)

Kart ham kodu basar. Eski köprü 3+ değerleri 0 yapıyordu; yer tarafı bunu **kırmamalı**, isimle göstermeli.

| Kod | Ad | SIHA `iha_otonom` |
|-----|----|-------------------|
| 0 | Manuel | 0 |
| 1 | Denge | 0 |
| 2 | Otonom | 1 |
| 3 | Otonom eve dönüş (RTL) | 1 |
| 4 | Eğitim | 0 |
| 5 | Aktif takip | 1 |
| 6 | Serial (genel) | 0* |
| **7** | **Seyir** (yarışma güdümü bu modda) | **1** |
| 8…12 | Serial | 0* |

\* 6 ve 8–12 saha adıyla doğrulanmadı; bilinmeyen kodu "Bilinmiyor (N)" diye gösterin, 0'a çevirmeyin.

Yarışma HTTP'sinde otonom bayrağı: **2, 3, 5, 7 → 1.** Seyir (7) otonomdir; 0'a düşürülürse puan paketi manuel görünür.

Arayüz önerisi: `7 — Seyir` gibi hem sayı hem ad. Eski `if mod > 2: mod = 0` satırını silin.

### 3.3 `0x02` — durum satırı

UTF-8 metin, en fazla 120 bayt. Log kutusuna yazılır (HSS alındı, nokta reddedildi, vb.).

### 3.4 `0x31` — kilit bitişi

Payload **boş**. Kilit 1→0 olunca bir kez gider. SIHA `POST /api/kilitlenme_bilgisi` buradan tetiklenir. Sürekli kilit durumu `0x01` içindeki `kilit` bitidir.

### 3.5 `0x33` — varış noktası sonucu (yeni)

3 bayt: `<BBB` = `durum, sira, sebep`

| durum | Anlam |
|-------|--------|
| 0 | Boşta (gönderilmez) |
| 1 | Kabul, uçuluyor |
| 2 | Varış yarıçapına girildi (~40 m) |
| 3 | Reddedildi |

Red sebepleri (`durum == 3`):

| sebep | Anlam | Operatöre önerilen metin |
|-------|--------|--------------------------|
| 0 | — | |
| 1 | Saha dışı / kenar payı | Nokta saha dışında |
| 2 | HSS içinde | Nokta yasak bölgede |
| 3 | Düz rota HSS/sınır kesiyor | Yol güvenli değil, başka nokta seçin |
| 4 | Sınır poligonu henüz yok | Önce `0x25` gönderin |
| 5 | Uçakta GPS yok | |

Sıra numarasını yer göndermez. Uçak her `0x2A` için 1 artırır (0…255) ve `0x33` ile geri verir. Arayüz son tıklanan noktayı "bekliyor" işaretlesin, `0x33` gelince sonucu göstersin.

---

## 4. Uplink — yer → uçak

### 4.1 `0x25` — uçuş sınırı (önce bunu gönderin)

`adet(u8)` + `adet × (lat float32, lon float32)`. En az 3 köşe. Geofence ve harita tıklaması buna bağlıdır. Sınır yokken `0x2A` **sebep 4** ile reddedilir.

### 4.2 `0x24` — HSS listesi

`adet(u8)` + `adet × (id u8, lat f32, lon f32, yaricap_m f32)` — kayıt 13 bayt. Boş liste (`adet=0`) önbelleği temizler.

### 4.3 `0x20` — rakip telemetrisi (33 bayt)

`<fffffffiB>`: lat, lon, alt, spd, pitch, roll, hdg, time_diff_ms (int32), target_id (u8).

### 4.4 `0x2A` — haritada tıklanan nokta (MSO ziyaret noktası)

Şartname §6.1.7: yer kontrol bilgisayarından varış noktası tanımlamak **otonomiyi bozmaz**, manuel moda geçiş sayılmaz.

| Uzunluk | Format | Anlam |
|---------|--------|--------|
| 8 | `<ff` lat, lon | Noktaya git, irtifa mevcut irtifa |
| 10 | `<ffH` lat, lon, irtifa_m | Noktaya git, irtifa da iste (metre, 0…65535) |

Davranış:

1. Nokta karta yazılmaz; Jetson Komut 2 (koordinat) ile kendi uçurur.
2. Süzgeç: nokta saha içinde ve HSS dışında olmalı, düz yol da kesmemeli.
3. Öncelik: **geofence kaçışı > rakip APF > bu nokta > takip**. Emniyet operatörün üstündedir.
4. ~40 m yarıçapa girince nokta düşer, uçak takibe / aramaya döner (sabit kanat tam üstünden geçemez).
5. Yeni `0x2A` önceki noktanın yerini alır (kuyruk yok; MSO'da 5'lik kuyruk vardı, bizde tek aktif nokta).

```python
# Yalnızca koordinat
payload = struct.pack("<ff", lat, lon)
rfd.write(pack_frame(0x2A, payload))

# Koordinat + irtifa
payload = struct.pack("<ffH", lat, lon, int(alt_m))
rfd.write(pack_frame(0x2A, payload))
```

### 4.5 `0x2D` — noktayı iptal et

Boş payload. Uçak normal göreve döner.

### 4.6 Göndermeyin / yok sayılır

| Msg | Neden |
|-----|--------|
| `0x10` / `0x12` | Kart görev listesi; ACM0 ister, bu uçak açmaz |
| `0x21` / `0x22` / `0x23` / `0x30` | Kamikaze / QR — ayrı workspace |

---

## 5. Arayüz kontrol listesi

- [ ] `0x01` unpack: roll sonra pitch; `mod` ham bırakılsın
- [ ] Mod 7 = "Seyir", otonom = evet
- [ ] Harita tıklaması `0x2A` (8 veya 10 bayt), `0x10` değil
- [ ] Uçuş başında `0x25` (sınır) ve `0x24` (HSS) gitsin
- [ ] `0x33` ile kabul / red / varıldı gösterilsin
- [ ] İptal butonu `0x2D`
- [ ] Kilit bitişi `0x31` → SIHA kilitlenme POST
- [ ] `0x01.kilit` sürekli kilit durumu

---

## 6. Yer tarafında hızlı doğrulama

1. Uçak yerde, seyirde: `0x01` `mod == 7` gelmeli, 0 olmamalı.
2. Sınır göndermeden haritaya tıkla: `0x33` durum=3, sebep=4.
3. Saha içi boş bir noktaya tıkla: durum=1, uçak o yöne dönmeli.
4. HSS içine tıkla: durum=3, sebep=2.
5. İptal: `0x2D` sonrası uçak takibe / aramaya dönmeli.
