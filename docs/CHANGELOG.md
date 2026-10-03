# Değişiklik Günlüğü (Yarışma Modu)

Önemli commit'ler ve sahada bilinmesi gereken notlar. Push yapılmadan önce yerel `main` geçmişine bakın: `git log --oneline -10`.

---

## 2026-07-03 — Donanımsal sınırlar: interface + RX ring buffer (Sprint D)

### Kod

| Alan | Değişiklik |
|------|------------|
| `IAlcLinkBridge` | Yeni sanal metot `EnqueueLockNonBlocking(coords)` (varsayılan: `SendLockCoordinates`). `EnqueueNoLockNonBlocking()` zaten mevcut. |
| `phase5_callbacks.cpp` | `TryEnqueueLockNonBlocking` içindeki `dynamic_cast<AlcLinkBridge*>` kaldırıldı; sıcak yol tamamen sanal çağrı üzerinden. |
| `AlcLinkBridge` | `EnqueueLockNonBlocking` override eder: TX worker açıksa `EnqueueLockCoordinates`, aksi hâlde `SendLockCoordinates`. |
| `rx_ring_buffer.hpp` (yeni) | Header-only `RxRingBuffer<N>` — `std::array` + head/size, O(1) push/pop, `Peek()` ile bitişik kopyalama. |
| `alc_link_bridge` | `std::vector<uint8_t> rx_buffer_` → `RxRingBuffer<kRxMaxBytes>`. `DrainAndParseRx`/`ParseRxBufferLocked` 32 baytlık `frame[]` staging üzerinden yürüyor (CRC/XOR ham pointer ister). |

### Kazanım

- Kare başına `dynamic_cast` maliyeti (RTTI walk) sıfır.
- RX parse: `insert(end,…)` ve `erase(begin,begin+n)` O(n) kaydırmaları kalktı → O(1).
- Heap tahsisatı sıfır (sabit `std::array<uint8_t, 4096>`), fragmantasyon riski ortadan kalktı.

Toplam: **24/24** PASS (`build-ci` + `build-app`).

---

## 2026-07-03 — Güdüm test boru hattı + dokümantasyon (Sprint C)

### Test

| Hedef | Kapsam |
|-------|--------|
| `test_lock_metrics` | Jitter (≥3 örnek), `lock_loss_count`, tampon temizliği |
| `test_phase5_guidance_control_loop_integration` | `hold_zero` anında sıfır; degrade aktifken setpoint yok |

Toplam: **24/24** PASS (`build-ci` + `build-app`).

### Dokümantasyon

- `AGENTS.md`: Kalman tabanlı güdüm maddeleri güncel mimariye çekildi (durumsuz `WorldTargetEstimator`, `TryOcclusionHoldLast`).
- `docs/GUIDANCE_USER_MANUAL.md`: Oklüzyon/degrade tablosu eklendi.

---

**Commit mesaji:** `fix: OSD meta birikimi, display sync ve yarışma tracker env` — hash: `git log -1 --format=%h`

**Önceki commit:** `f6f9d6f` — yarışma modu Faz5 sertleştirme, Python prototip temizliği, README/PERFORMANCE_BASELINE güncellemesi.

**Yerel main (push öncesi):** `git log --oneline -3`

### Kod

| Dosya | Değişiklik |
|-------|------------|
| `ds_app.cpp/hpp` | `ResetDisplayMetaForAcquire()` — display meta pool rect/line sıfırlama; `nv3dsink` varsayılan `sync=true`; stream kuyruk derinliği 1 |
| `track_selector.cpp` | OSD iz birikimi fix; kilit varken fazla bbox gizleme (`HideObjectBbox`) |
| `gst_app_helpers.cpp` | Saat overlay aynı meta reset |
| `savasan.env` | `SAVASAN_TRACKER_PROFILE=default`, `SAVASAN_TRACKER_AUTO_RESTART=0` |

### Bilinen hatalar / dersler

1. **OSD kutu izi:** `nvds_acquire_display_meta_from_pool` sonrası `num_rects` sıfırlanmazsa kırmızı kutular birikir.
2. **SEGV (23:41):** `nvds_remove_obj_meta_from_frame` / `nvds_clear_display_meta_list` kullanımı çökertti — **kullanılmıyor**.
3. **Servis başlamıyor:** `/etc/savasan/savasan.env` içinde `SAVASAN_TRACKER_PROFILE=stable` → `validate_env` FAIL. Geçerli değerler: `default`, `calm`, `aggressive`, `auto`. `stable` yalnızca preset dosya adı (`presets/stable.env`).
4. **Tracker kapatma ayrımı:** `SAVASAN_TRACKER_AUTO_RESTART=0` → profil restart döngüsü kapalı; **nvtracker açık kalır**. Tam kapatma: `SAVASAN_TRACKER_DISABLE=1` (yarışmada önerilmez).

### nvinfer A/B (ölçülmüş)

| Profil | interval | FPS avg | Latency p95 |
|--------|----------|---------|-------------|
| MAXN | 2 | 57.8 | 61 ms |
| MAXN | 1 | 47.9 | 78 ms |

**Karar:** `interval=2` korunur (`config_infer_primary.txt` + varsayılan env).

### Üretim binary

```text
/home/nvidia/Savasan_IHA_Workspace/02_Ana_Sistem_CPP/build-app/savasan_iha
```

Systemd: `savasan-airlock.service` → `/etc/savasan/savasan.env`

### Servis kurtarma (sık kullanılan)

```bash
sudo systemctl reset-failed savasan-airlock
/usr/local/bin/savasan-validate-env.sh /etc/savasan/savasan.env
sudo systemctl start savasan-airlock
```

### Push durumu

- `f6f9d6f` ve sonraki commit'ler **henüz remote'a push edilmedi** (GitHub token/SSH gerekli).
