# Savasan C++ Hardening ve Refactor

Bu dokuman, planlanan fazlari ve uygulanan degisiklikleri tek yerde toplar.
Amac: ekip icinde veya farkli yapay zeka araclariyla teknik tartisma yaparken ortak referans olmasi.

## 1) Plan (Hedefler)

### Faz A - Guvenilirlik Hardening

- Serial yazma hattini guvenli hale getirme
  - `tcflush(TCOFLUSH)` riskini giderme
  - partial write / `EAGAIN` / `EINTR` davranislarini dogru ele alma
  - `termios` cagri donuslerini zorunlu kontrol etme
- Faz5 ust-seviye hata tepkisi
  - `SendLockCoordinates` / `SendNoLock` / `SendSetpointCommand` basarisizliklarinin degrade/fallback etkisine donusturulmesi
- Null guvenligi
  - `EvasionController` icinde `bridge_` kullanimini sertlestirme
- Hafif log standardi
  - bagimsiz (harici kutuphane olmadan) seviye + rate-limit yaklasimi

### Faz B - Minimum Test Kapisi

- CMake test altyapisi (`enable_testing`, test hedefleri)
- Donanimsiz unit test seti:
  - `pid_controller`
  - `world_target_estimator`
  - `vehicle_state_estimator`
  - `evasion` tetik mantigi
- Basit CI kapisi:
  - build + unit test

### Faz C - main.cpp Kademeli Parcalama

- `config_resolver` benzeri ayristirma (argv+env parse)
- `phase_runner/dispatch` ayristirma
- `main.cpp` icinde sorumluluk azaltma

### Faz D - Olcum Tabanli Performans

- Performans degisikliklerini benchmark ile dogrulama
- Olcum tekrarlanabilirligi icin script + dokuman olusturma

## 2) Uygulanan Degisiklikler (Tamamlananlar)

## Faz A Sonucu - Tamamlandi

- **Seri I/O hardening**
  - `02_Ana_Sistem_CPP/src/autopilot/alc_link_bridge.cpp`
  - partial write retry, `poll` tabanli write hazirlik bekleme, `tcdrain` ile guvenli tamamlama
  - `tcgetattr/cfsetispeed/cfsetospeed/tcsetattr` hata kontrolleri eklendi
- **Faz5 fail-safe/degrade**
  - `02_Ana_Sistem_CPP/src/main.cpp`
  - send hatalarinda degrade moda gecis ve guidance tarafinda kontrollu davranis
- **Null guvenligi + komut sonuc kontrolu**
  - `02_Ana_Sistem_CPP/src/evasion/evasion_controller.cpp`
- **Hafif logging standardi**
  - `02_Ana_Sistem_CPP/src/common/log.hpp`
  - log seviyesi (`ERROR/WARN/INFO/DEBUG`) + basit rate-limit

## Faz B Sonucu - Tamamlandi

- **CMake test altyapisi**
  - `02_Ana_Sistem_CPP/CMakeLists.txt`
  - `SAVASAN_BUILD_APP` / `SAVASAN_BUILD_TESTS` secenekleri
  - `enable_testing()` + test hedefleri
- **Unit test dosyalari**
  - `02_Ana_Sistem_CPP/tests/test_pid_controller.cpp`
  - `02_Ana_Sistem_CPP/tests/test_world_target_estimator.cpp`
  - `02_Ana_Sistem_CPP/tests/test_vehicle_state_estimator.cpp`
  - `02_Ana_Sistem_CPP/tests/test_evasion_controller.cpp`
- **CI build+test kapisi**
  - `.github/workflows/build_and_test.yml`

## Faz C Sonucu - Tamamlandi

- **Startup parse/cozumleme ayristirma**
  - `02_Ana_Sistem_CPP/src/app/startup_config.hpp`
  - `02_Ana_Sistem_CPP/src/app/startup_config.cpp`
- **Phase dispatch ayristirma**
  - `02_Ana_Sistem_CPP/src/app/phase_dispatch.hpp`
  - `02_Ana_Sistem_CPP/src/app/phase_dispatch.cpp`
- **main sadeleme**
  - `02_Ana_Sistem_CPP/src/main.cpp`
  - `main` artik parse + dispatch modullerini cagiriyor

## Faz D Sonucu - Tamamlandi

- **Benchmark script**
  - `scripts/benchmark_phase3.sh`
- **Olcum sureci dokumani**
  - `docs/PERFORMANCE_BASELINE.md`

## 3) Dogrulama Notlari

- Tests-only modda derleme ve test: basarili
  - 4/4 unit test gecti
- Ana uygulama derlemesi: basarili
  - `02_Ana_Sistem_CPP/build` uzerinden dogrulandi
- Duzenlenen dosyalarda linter hatasi: gorulmedi

## 4) Kisa Ozet

- Planin Faz A, B, C, D adimlari bu iterasyonda tamamlandi.
- En kritik kazanim: serial/fail-safe guvenilirligi ve test kapisi.
- Sonraki tartisma konusu olarak performans tarafinda sayisal benchmark karsilastirmalari ve gerekirse faz bazli micro-refactor derinlestirmesi onerilir.
