# Code Review and Improvements

Bu not, Faz5 odakli teknik duzeltmeleri ve acik mimari iyilestirme adaylarini ozetler.

## Uygulanan duzeltmeler

1. **No-lock checksum duzeltmesi**
   - `AlcLinkBridge::SendNoLock()` icinde checksum XOR kuralina uygun hale getirildi (`0x00`).

2. **Evasion thread-safety**
   - `EvasionController` durum alanlari icin `state_mutex_` eklendi.
   - Durum gecisleri `*Locked` yardimcilara ayrildi:
     - `ExecuteEvasionLocked`
     - `StopEvasionLocked`
     - `RecoveryLogicLocked`
     - `ShouldTriggerEvasionLocked`
   - `GetState()` ve `GetConfig()` kopya dondurup kilitli okuma yapar.

3. **Seri flow-control kapatma**
   - `CRTSCTS`, `IXON`, `IXOFF`, `IXANY` kapatildi.

4. **PID anti-windup**
   - Saturasyonda hatayi daha da buyuten integral birikimi engellendi
     (conditional clamping + mevcut `i_limit` korunumu).

5. **WorldTargetEstimator kalibre mesafe modeli**
   - `focal_length_px`, `target_real_width_m`, `image_width_px` eklendi.
   - Kalibrasyon parametreleri verilirse pinhole model kullanilir, aksi halde
     onceki `distance_gain/sqrt(area)` davranisi korunur.

6. **Log birligi**
   - `TrackSelector` icindeki kritik olay loglari `savasan::common::Log` ile
     standart hale getirildi.

7. **ALC lock protokolu v2 (geriye uyumlu)**
   - `SAVASAN_ALC_LOCK_PACKET_VERSION=2` ile lock paketi `track_id`, `sequence`,
     `timestamp_ms` ve CRC16-CCITT ile gonderilebilir.
   - Varsayilan `1` olarak birakildi; boylece mevcut legacy alicilar bozulmadan
     calismaya devam eder.

8. **VehicleStateEstimator innovation gate + alpha filtre**
   - Hiz/yaw kestirimine innovation clipping ve alpha filtresi eklendi.
   - Runtime ayarlari:
     - `SAVASAN_STATE_VELOCITY_ALPHA`
     - `SAVASAN_STATE_INNOVATION_GATE_MPS`
     - `SAVASAN_STATE_YAW_ALPHA`
     - `SAVASAN_STATE_YAW_GATE_DPS`

## Sonraki mimari adaylar (uygulanmadi)

- MAVLink v2 / CRC16 tabanli daha guclu seri protokol
- `savasan::` / `savasan_iha::` namespace birlestirme
- Vehicle/target kestiriminde KF/EKF genisletmesi
- `nvopticalflow` ciktisini guidance tarafina dogrudan tasima
- Evasion cooldown ve tekrarli tetikleme sınırlama
- `rx_buffer_` icin ust boyut korumasi
- DLA/GPU karma dagitim profili (Orin NX)
