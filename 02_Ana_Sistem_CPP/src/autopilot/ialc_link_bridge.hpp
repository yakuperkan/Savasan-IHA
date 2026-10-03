/**
 * @file ialc_link_bridge.hpp
 * @brief Alacakart otopilot haberleşme köprüsü için saf sanal arayüz.
 *
 * Üretim kodu bu arayüze (soyutlamaya) bağımlıdır; gerçek implementasyon
 * @c AlcLinkBridge (seri port), birim testlerinde ise sahte (mock) köprü
 * kullanılır. Bu sayede güdüm/kaçış mantığı donanımdan bağımsız test edilebilir.
 */
#ifndef SAVASAN_AUTOPILOT_IALC_LINK_BRIDGE_HPP_
#define SAVASAN_AUTOPILOT_IALC_LINK_BRIDGE_HPP_

#include <cstddef>
#include <cstdint>
#include <string>

#include "autopilot/alc_link_types.hpp"

namespace savasan::autopilot {

/**
 * @brief Otopilot haberleşme köprüsü arayüzü.
 *
 * Kilit koordinatı, kaçış komutu ve Seyir Modu komutu gönderme; telemetri okuma
 * ve bağlantı/heartbeat yönetimi gibi temel yetenekleri tanımlar.
 */
class IAlcLinkBridge {
 public:
  virtual ~IAlcLinkBridge() = default;

  /// @brief Otopilota bağlanır (seri portu açar). @return Başarılıysa true.
  virtual bool Connect() = 0;
  /// @brief Bağlantıyı kapatır ve kaynakları serbest bırakır.
  virtual void Disconnect() = 0;
  /// @brief Bağlantının açık olup olmadığını döndürür.
  virtual bool IsConnected() const = 0;

  /// @brief Hedef kilit koordinatını otopilota gönderir.
  /// @param coords Gönderilecek kilit koordinatı.
  /// @return Gönderim (veya kuyruğa alma) başarılıysa true.
  virtual bool SendLockCoordinates(const LockCoordinates& coords) = 0;
  /// @brief Kilit koordinatını blokesiz TX kuyruğuna alır; varsayılan @ref SendLockCoordinates.
  /// @details Sıcak yol (pad-probe / güdüm) bu API'yi çağırır; gerçek köprü asenkron
  ///          worker'a devreder, mock/test uygulamaları senkron @ref SendLockCoordinates
  ///          çağırır. Bu sayede @c dynamic_cast gerekmez.
  virtual bool EnqueueLockNonBlocking(const LockCoordinates& coords) {
    return SendLockCoordinates(coords);
  }
  /// @brief "Kilit yok" durumunu otopilota bildirir.
  virtual bool SendNoLock() = 0;
  /// @brief "Kilit yok" paketini blokesiz TX kuyruğuna alır; TX worker yoksa @ref SendNoLock.
  virtual bool EnqueueNoLockNonBlocking() { return SendNoLock(); }
  /// @brief Kaçış manevrası komutu gönderir.
  /// @param evasion_type Kaçış tipi kodu.
  virtual bool SendEvasionCommand(uint8_t evasion_type) = 0;

  /// @brief Seyir Modu komut paketi gönderir (84 86 19 ... 42).
  /// @param pkt @ref seyir::PackedCommand çıktısından doldurulmuş paket.
  /// @param dry_run true ise paket yazılmaz, yalnızca loglanır.
  /// @details Varsayılan: destek yok (mock/test). Gerçek köprü ACM0'a basar.
  virtual bool SendSeyirModeCommand(const SeyirModePacket& /*pkt*/, bool /*dry_run*/) {
    return false;
  }

  /// @brief RF/GCS uplink ham baytlarını otopilota iletir (varsayılan: destek yok).
  /// @details AlpaguLink ACM0 açmaz; 0x10 görev paketini dosyaya yazar, C++ buradan basar.
  virtual bool SendRawBytes(const uint8_t* /*data*/, size_t /*length*/) { return false; }

  /// @brief Periyodik heartbeat (son koordinatı tekrar gönderme) iş parçacığını başlatır.
  virtual void StartHeartbeat() = 0;
  /// @brief Heartbeat iş parçacığını durdurur.
  virtual void StopHeartbeat() = 0;
  /// @brief Otopilottan gelen son telemetri anlık görüntüsünü döndürür.
  virtual TelemetrySnapshot GetTelemetrySnapshot() const = 0;
  /// @brief Bağlantı uç noktasının (örn. cihaz yolu) okunabilir tanımını döndürür.
  virtual std::string DescribeEndpoint() const = 0;
};

}  // namespace savasan::autopilot

#endif  // SAVASAN_AUTOPILOT_IALC_LINK_BRIDGE_HPP_
