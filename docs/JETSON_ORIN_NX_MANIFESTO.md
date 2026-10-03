# Jetson Orin NX — Donanım ve yazılım envanteri

**Proje bağlamı:** TEKNOFEST Savaşan İHA (Fighter UAV) — gerçek zamanlı hedef izleme  
**Donanım:** NVIDIA Jetson Orin NX (16 GB RAM)  
**Mimari:** ARM64 (aarch64) | Compute Capability: 8.7

---

## 1. Çekirdek yazılım yığını

* **İşletim sistemi:** Ubuntu 22.04 LTS  
* **NVIDIA platform:** JetPack 6.2.2  
* **CUDA:** 12.6 (`/usr/local/cuda-12.6`)  
* **cuDNN:** 9.x  
* **Çıkarım motoru:** NVIDIA TensorRT (modeller için)

---

## 2. Görüntü / OpenCV (özel derleme)

* **OpenCV:** 4.10.0 (kaynak derlemesi; CUDA 12.6 cudev uyumu için)  
* **Kurulum kökü:** `/usr/local/opencv4`  
* **Donanım ivmesi:** CUDA, cuBLAS, cuFFT, NPP, cuDEV, GStreamer, V4L, TBB, NEON  
* **Kütüphane yolu:** `/usr/local/opencv4/lib`

---

## 3. Python ortamları ve kütüphaneler

* **Sanal ortamlar (örnek isimler):** `yolo_v5`, `yolo_v8`, `yolo_v11`, `yolo26` — konum: `~/envs/`  
* **OpenCV (Python):** `/usr/local/opencv4` derlemesinin venv içi `site-packages` ile symlink bağlantısı  
* **PyTorch:** JetPack 6 ile uyumlu, GPU etkin derleme (ör. `jp/v61` kanalı; cuDNN 9.x ile eşleşen kurulum)

---

Operasyonel yasaklar, AI asistan kuralları ve DeepStream canlı parametre politikası: **[`.cursorrules`](.cursorrules)** dosyasında tanımlıdır; bu manifesto yalnızca cihazda bulunan sürüm ve yol bilgisini listeler.
