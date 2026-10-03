# CONTRIBUTING

Bu repo icin katkilarin tutarli ve guvenli ilerlemesi hedeflenir.

## Temel Ilkeler
- Kucuk, odakli commitler at.
- Her degisiklikte ilgili dokumani da guncelle.
- Mevcut davranisi degistiren islerde calistirma adimini not et.

## Kod Degisikligi Akisi
1. Sorunu netlestir (hangi faz, hangi kamera, hangi config).
2. Kod degisikligini minimum kapsamda yap.
3. Derle ve ilgili fazi calistir.
4. Sonucu kisa not olarak PR/commit mesajina ekle.

## Test Beklentisi
Ana C++ sistemde su an resmi unit/integration test altyapisi sinirlidir.
Bu nedenle her PR icin en azindan su calistirmalardan biri beklenir:
- `./scripts/run_phase2.sh csi 60`
- `./scripts/run_phase3.sh csi 60`
- ihtiyaca gore `usb` varyanti

## Dokuman Senkronu
Pipeline, model config veya tracker davranisi degisirse su dosyalari da kontrol et:
- `README.md`
- `BUILD.md`
- `DEPLOYMENT.md`
- `API_Documentation.md`
- `.cursor/skills/savasan-jetson-deepstream/SKILL.md`

## Stil Notlari
- C++17 standardi korunur.
- Gereksiz buyuk refactor yerine hedefli duzeltme tercih edilir.
- DeepStream property isimlerinde canli referans dosyalari esas alin.

