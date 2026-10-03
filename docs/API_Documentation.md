# API Documentation (Project Level)

Bu dosya, proje icindeki ana akis ve giris noktalarinin yuksek seviye ozetidir.

## Entry Point
- Dosya: `02_Ana_Sistem_CPP/src/main.cpp`
- Fonksiyon: `int main(int argc, char** argv)`

## CLI Kullanimi
- `./savasan_iha phase1 [csi|usb]`
- `./savasan_iha phase2 [csi|usb] [display]`
- `./savasan_iha phase3 [csi|usb] [display]`
- `./savasan_iha phase4 [csi|usb] [hybrid|baseline] [display]`

## Faz Fonksiyonlari
- `RunPhase1(...)`: kamera duman testi (AI yok)
- `RunPhase2(...)`: nvinfer + OSD
- `RunPhase3(...)`: nvinfer + nvtracker + lock secimi

## Pipeline Uretimi
- `BuildPhase2PipelineString(...)`
- `BuildPhase3PipelineString(...)`

Bu fonksiyonlar `02_Ana_Sistem_CPP/src/deepstream/` altinda pipeline string uretir.

## Tracker / Lock
- `TrackSelector` (`02_Ana_Sistem_CPP/src/tracking/track_selector.*`)
- `LockState` (`02_Ana_Sistem_CPP/src/tracking/lock_state.*`)

`phase4` modunda `hybrid` ve `baseline` lock secimi desteklenir. Komut satirinda mod yoksa `SAVASAN_LOCK_MODE=hybrid|baseline` okunur (systemd uyumu).

## Telemetry / Probes
- `AttachEndTelemetryProbe(...)`
- `AttachCsiLatencyReferenceProbe(...)`

Amaç: FPS/latency ve son-asama metrik izlemesi.

## Ortam Degiskenleri
- `SAVASAN_LOCK_MODE` (`phase4`; `hybrid` / `baseline`; argv'de mod yoksa)
- `SAVASAN_PGI_CONFIG`
- `SAVASAN_TRACKER_CONFIG`
- `SAVASAN_TRACKER_LIB`
- `SAVASAN_RUN_SECONDS`
- `SAVASAN_V4L2_DEVICE`
- `SAVASAN_RECORD_FILE`
- `SAVASAN_UDP_ENABLE` (`1` = OSD sonrasi RTP/UDP ac; aksi halde kapali)
- `SAVASAN_UDP_HOST` / `SAVASAN_UDP_PORT` (`ENABLE=1` iken gerekli)
- `SAVASAN_UDP_CODEC` (`h264` / `h265`), `SAVASAN_UDP_BITRATE` (bps)
- `SAVASAN_OVERLAY_CLOCK`
- `NVDS_ENABLE_LATENCY_MEASUREMENT`

## Hata Davranisi
- Konfig dosyasi veya kutuphane okunamazsa uygulama pre-flight asamasinda cikar.
- GStreamer hata/uyari mesajlari BusWatch ile stderr'e yazilir.

