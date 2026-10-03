# BUILD

Bu dokuman, `02_Ana_Sistem_CPP` icindeki ana binary icin derleme adimlarini verir.

## Gereksinimler
- Jetson Orin NX (veya DeepStream uyumlu NVIDIA ortam)
- CMake >= 3.10
- GStreamer 1.0 gelistirme paketleri
- NVIDIA DeepStream (headers + runtime)

## Derleme
```bash
cd /home/nvidia/Savasan_IHA_Workspace/02_Ana_Sistem_CPP
mkdir -p build
cd build
cmake ..
cmake --build . -j4
```

Uretilen binary:
- `02_Ana_Sistem_CPP/build/savasan_iha`

## Debug probe acik derleme
```bash
cd /home/nvidia/Savasan_IHA_Workspace/02_Ana_Sistem_CPP/build
cmake -DSAVASAN_DEBUG=ON ..
cmake --build . -j4
```

## Hata ayiklama notlari
- nvinfer config yolu hataliysa uygulama baslangicta durur.
- tracker config veya tracker `.so` yoksa `phase3/phase4` baslamaz.
- X11 display yoksa display sink ile calisma basarisiz olabilir; bu durumda fake sink ile test et.

