/**
 * @file phase5_runner_config.hpp
 * @brief Faz-5 çalıştırıcı ortam yapılandırması (test edilebilir yardımcılar).
 */
#ifndef SAVASAN_RUNNERS_PHASE5_RUNNER_CONFIG_HPP_
#define SAVASAN_RUNNERS_PHASE5_RUNNER_CONFIG_HPP_

#include "autopilot/alc_link_bridge.hpp"
#include "runners/phase5_runtime.hpp"

namespace savasan::runners {

/// @brief Hız/titreme eşiklerinde minimum histerezis boşluğunu uygular.
/// @return Eşikler ayarlandıysa true.
bool NormalizeAutoProfileHysteresis(AutoProfileConfig* cfg);

/// @brief Otomatik profil/yeniden başlatma eşiklerini ortam değişkenlerinden okur.
AutoProfileConfig ReadAutoProfileConfigFromEnv();

/// @brief ALC köprü yapılandırmasını ortam değişkenlerinden oluşturur.
savasan::autopilot::AlcLinkBridge::Config BuildAlcConfigFromEnv();

/// @brief RunPhase3 başarısızlığında yeniden deneme sayısını okur (0 = yalnızca ilk deneme).
int ReadPhase5PipelineMaxRetriesFromEnv();

}  // namespace savasan::runners

#endif  // SAVASAN_RUNNERS_PHASE5_RUNNER_CONFIG_HPP_
