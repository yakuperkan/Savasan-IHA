/**
 * @file itelemetry_source.hpp
 * @brief Telemetri kaynağı arayüzü: araç telemetri anlık görüntüsünü (snapshot)
 *        sağlayan soyut sözleşmeyi tanımlar. Gerçek implementasyon otopilot
 *        seri köprüsünden, testlerde ise mock kaynaktan veri okur.
 */
#ifndef SAVASAN_TELEMETRY_ITELEMETRY_SOURCE_HPP_
#define SAVASAN_TELEMETRY_ITELEMETRY_SOURCE_HPP_

#include "autopilot/alc_link_types.hpp"

namespace savasan::telemetry {

/// @brief Araç telemetrisini sağlayan kaynaklar için saf sanal arayüz.
///        Bu arayüz sayesinde guidance/kontrol katmanı, telemetrinin nereden
///        geldiğinden (gerçek otopilot köprüsü ya da test mock'u) bağımsız çalışır.
class ITelemetrySource {
 public:
  virtual ~ITelemetrySource() = default;

  /// @brief Anlık telemetri görüntüsünü okur.
  /// @return Aracın o andaki konum/hız/yönelim gibi telemetri alanlarını içeren snapshot.
  virtual savasan::autopilot::TelemetrySnapshot ReadTelemetry() const = 0;
};

}  // namespace savasan::telemetry

#endif  // SAVASAN_TELEMETRY_ITELEMETRY_SOURCE_HPP_
