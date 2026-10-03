/**
 * @file phase_dispatch.hpp
 * @brief Faz seçimi/sevk (dispatch) arayüzü. Yapılandırmadaki faz numarasına
 *        göre ilgili faz çalıştırıcısını (runner) çağırmak için kullanılan
 *        fonksiyon işaretçisi kümesi ve sevk fonksiyonunu tanımlar.
 */
#ifndef SAVASAN_APP_PHASE_DISPATCH_HPP_
#define SAVASAN_APP_PHASE_DISPATCH_HPP_

#include "app/startup_config.hpp"
#include "tracking/lock_mode.hpp"

namespace savasan::app {

/// @brief Her faz için çalıştırıcı fonksiyon işaretçilerini taşıyan yapı.
/// @details Bu dolaylı katman, faz mantığını test edilebilir kılar (gerçek
///          runner'lar yerine sahte/mock işaretçiler verilebilir).
struct PhaseDispatchRunners {
  bool (*run_phase1)(IngestConfig cfg) = nullptr;                          ///< Faz1: yalnızca kamera + fakesink.
  bool (*run_phase2)(IngestConfig cfg, const std::string& pgi_config, deepstream::SinkType sink,
                     int run_seconds) = nullptr;                           ///< Faz2: nvinfer + OSD.
  bool (*run_phase3)(IngestConfig cfg, const std::string& pgi_config,
                     const std::string& tracker_config, const std::string& ll_lib,
                     deepstream::SinkType sink, int run_seconds,
                     tracking::LockMode lock_mode) = nullptr;              ///< Faz3/4: nvinfer + nvtracker + probe.
  bool (*run_phase5)(IngestConfig cfg, const std::string& pgi_config,
                     const std::string& tracker_config, const std::string& ll_lib,
                     deepstream::SinkType sink, int run_seconds,
                     tracking::LockMode lock_mode) = nullptr;              ///< Faz5: Faz4 + alc_link (seri).
};

/// @brief Yapılandırmadaki faza göre uygun çalıştırıcıyı çağırır.
/// @param cfg Faz numarası ve ilgili parametreleri taşıyan başlangıç ayarı.
/// @param runners Faz çalıştırıcı fonksiyon işaretçileri.
/// @return Çalıştırma başarılıysa 0, aksi halde 1 (süreç çıkış kodu).
int ExecutePhase(const StartupConfig& cfg, const PhaseDispatchRunners& runners);

}  // namespace savasan::app

#endif  // SAVASAN_APP_PHASE_DISPATCH_HPP_
