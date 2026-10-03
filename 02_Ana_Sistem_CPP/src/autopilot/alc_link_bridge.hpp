/**
 * @file alc_link_bridge.hpp
 * @brief Alacakart otopilot ile seri haberleşmeyi yöneten köprü sınıfı.
 *
 * @ref AlcLinkBridge, @ref IAlcLinkBridge arayüzünün gerçek (donanım)
 * implementasyonudur. Paketleri (kilit/kaçış/seyir) seri porta yazar,
 * gelen telemetriyi (0x54) çözümler. Sıcak yolda (pad-probe / güdüm döngüsü)
 * blokesiz (non-blocking) gönderim için ayrı bir TX iş parçacığı ve kuyruk
 * kullanır; ayrıca son koordinatı tekrar gönderen bir heartbeat iş parçacığı içerir.
 */
#ifndef ALC_LINK_BRIDGE_HPP
#define ALC_LINK_BRIDGE_HPP

#include <string>
#include <chrono>
#include <atomic>
#include <thread>
#include <mutex>
#include <array>
#include <vector>
#include <deque>
#include <condition_variable>

#include "autopilot/alc_link_types.hpp"
#include "autopilot/ialc_link_bridge.hpp"
#include "autopilot/rx_ring_buffer.hpp"

namespace savasan {
namespace autopilot {

/**
 * @brief Alacakart otopilot iletişim köprüsü
 *
 * Bu sınıf, kilit koordinatlarını ve kaçış komutlarını Alacakart otopilotuna
 * seri haberleşme üzerinden iletir. Non-blocking I/O ve thread-safe tasarım.
 */
class AlcLinkBridge : public IAlcLinkBridge {
public:
    /**
     * @brief Köprü yapılandırması (seri port ve protokol seçenekleri).
     */
    struct Config {
        std::string device_path = "/dev/ttyACM0";    ///< Seri port cihaz yolu.
        int baud_rate = 115200;                       ///< Baud hızı (bit/s).
        std::chrono::milliseconds send_interval{50};  ///< Heartbeat gönderim periyodu (~20 Hz).
        std::chrono::milliseconds timeout{1000};      ///< Bağlantı zaman aşımı.
        bool normalize_coordinates = true;            ///< true: [0,1] normalize, false: piksel.
        int lock_packet_version = 1;                  ///< 1=eski XOR, 2=track_id/seq/ts + CRC16.
        int no_lock_checksum_mode = 0;                ///< 0=XOR, 1=eski sabit 0xFF.
    };

public:
    /// @brief Varsayılan yapılandırma ile köprü oluşturur.
    AlcLinkBridge();
    /// @brief Verilen yapılandırma ile köprü oluşturur.
    explicit AlcLinkBridge(const Config& config);
    ~AlcLinkBridge() override;

    // --- Bağlantı yönetimi ---
    bool Connect() override;
    void Disconnect() override;
    bool IsConnected() const override;

    // --- Veri gönderimi ---
    bool SendLockCoordinates(const LockCoordinates& coords) override;
    /// @brief TX worker aktifken doğrudan kuyruğa alır; aksi halde senkron gönderim.
    bool EnqueueLockNonBlocking(const LockCoordinates& coords) override;
    bool SendNoLock() override;  ///< "Kilit yok" durumunu gönderir.
    /// @brief TX worker aktifken blokesiz kuyruğa alır; aksi halde @ref SendNoLock.
    bool EnqueueNoLockNonBlocking() override;
    bool SendEvasionCommand(uint8_t evasion_type) override;
    bool SendSeyirModeCommand(const SeyirModePacket& pkt, bool dry_run) override;
    bool SendRawBytes(const uint8_t* data, size_t length) override;

    /// @brief Kilit koordinatını blokesiz TX kuyruğuna ekler (sıcak yol).
    bool EnqueueLockCoordinates(const LockCoordinates& coords);
    /// @brief "Kilit yok" isteğini blokesiz olarak TX kuyruğuna ekler.
    bool EnqueueNoLock();
    /// @brief Heartbeat (son koordinat tekrarı) isteğini TX kuyruğuna ekler.
    bool EnqueueHeartbeat();
    /// @brief TX kuyruğu boşalana kadar bekler (entegrasyon testi / düzgün kapanış).
    /// @param timeout Beklemenin en fazla süresi.
    void WaitTxIdle(std::chrono::milliseconds timeout = std::chrono::milliseconds(200));

    // --- Heartbeat ---
    void StartHeartbeat() override;
    void StopHeartbeat() override;

    // --- Yapılandırma / sorgular ---
    const Config& GetConfig() const { return config_; }
    void SetConfig(const Config& config) { config_ = config; }
    TelemetrySnapshot GetTelemetrySnapshot() const override;
    std::string DescribeEndpoint() const override { return config_.device_path; }

private:
    /// @brief Seri portu açar ve 8N1 raw mod ayarlarını uygular.
    bool OpenSerialPort();
    /// @brief Seri portu kapatır.
    void CloseSerialPort();
    /// @brief Veriyi tamamı yazılana kadar (kısmi yazımları yöneterek) porta yazar.
    bool WriteData(const uint8_t* data, size_t length);
    /// @brief Yapılandırılmış sürüme göre (V1/V2) kilit paketini oluşturup yazar. mutex_ kilitli olmalı.
    bool WriteLockPacketLocked(const LockCoordinates& coords);
    /// @brief Porttan gelen tüm baytları okuyup RX tamponuna ekler ve çözümler.
    void DrainAndParseRx();
    /// @brief RX tamponundaki tam paketleri çözümler (ACK ve 0x54 telemetri). mutex_ kilitli olmalı.
    void ParseRxBufferLocked();
    /// @brief Yazma/okuma hatasında bağlantıyı sıfırlar ve loglar. mutex_ kilitli olmalı.
    void OnWriteFailureLocked(const char* reason);
    /// @brief Bağlantı sonrası uzun süre RX yoksa baud uyumsuzluğu uyarısı. mutex_ kilitli olmalı.
    void MaybeWarnLinkHealthLocked();
    /// @brief Heartbeat iş parçacığı gövdesi: periyodik olarak son koordinatı yeniden gönderir.
    void HeartbeatThread();
    /// @brief TX iş parçacığı gövdesi: kuyruktaki paketleri sırayla porta yazar.
    void TxWorkerThread();
    /// @brief TX iş parçacığını başlatır. mutex_ kilitli olmalı.
    void StartTxWorkerLocked();
    /// @brief TX iş parçacığını durdurur ve kuyrukları temizler.
    void StopTxWorker();
    /// @brief Hazır ham paketi (örn. kaçış) TX ham kuyruğuna ekler.
    bool EnqueueRawPacket(const uint8_t* data, size_t len);
    /// @brief Seyir Modu paketini "en son kazanır" olarak kuyruğa koyar.
    bool EnqueueSeyirPacket(const uint8_t* data, size_t len);

    /// @brief TX kuyruğunda bekleyen, en fazla 20 baytlık ham paket.
    struct TxRawPacket {
        std::array<uint8_t, 20> data{};
        size_t len = 0;
    };

    int serial_fd_;  ///< Açık seri port dosya tanıtıcısı (-1: kapalı).

    Config config_;  ///< Aktif yapılandırma.

    std::atomic<bool> connected_;          ///< Bağlantı durumu (iş parçacıkları arası okunur).
    std::atomic<bool> heartbeat_running_;  ///< Heartbeat iş parçacığının çalışma bayrağı.

    // --- İş parçacıkları ve senkronizasyon ---
    std::thread heartbeat_thread_;                ///< Heartbeat iş parçacığı.
    mutable std::mutex mutex_;                     ///< Seri port ve RX/telemetri durumunu korur.
    std::thread tx_thread_;                        ///< Blokesiz gönderim iş parçacığı.
    std::mutex tx_mutex_;                          ///< TX kuyruklarını ve bayraklarını korur.
    std::condition_variable tx_cv_;               ///< TX iş parçacığını uyandıran koşul değişkeni.
    std::deque<LockCoordinates> tx_lock_queue_;   ///< Bekleyen kilit koordinatları kuyruğu.
    std::deque<TxRawPacket> tx_raw_queue_;        ///< Bekleyen ham paket (kaçış vb.) kuyruğu.
    TxRawPacket tx_seyir_latest_{};               ///< En son seyir modu paketi (latest-wins).
    bool tx_no_lock_pending_ = false;             ///< "Kilit yok" gönderimi bekliyor mu.
    bool tx_seyir_pending_ = false;               ///< Seyir komutu gönderimi bekliyor mu.
    bool tx_heartbeat_pending_ = false;         ///< Heartbeat (son koordinat) bekliyor mu.
    std::atomic<bool> tx_worker_running_{false}; ///< TX iş parçacığı çalışıyor mu.
    static constexpr size_t kTxMaxQueueSize = 8;     ///< Kilit kuyruğu üst sınırı.
    static constexpr size_t kTxMaxRawQueueSize = 8;  ///< Ham kuyruk üst sınırı.
    static constexpr size_t kRxMaxBytes = 4096;      ///< RX tamponu üst sınırı (taşarsa sıfırlanır).

    LockCoordinates last_coords_;                          ///< Son gönderilen koordinat (heartbeat için).
    std::chrono::steady_clock::time_point last_send_time_; ///< Son gönderim zamanı.

    // --- RX tamponu ve telemetri istatistikleri ---
    RxRingBuffer<kRxMaxBytes> rx_buffer_;                ///< O(1) push/pop dairesel tampon.
    std::atomic<uint64_t> rx_frame_count_;              ///< Çözümlenen toplam kare sayısı.
    std::chrono::steady_clock::time_point last_rx_time_; ///< Son veri alım zamanı.
    TelemetrySnapshot telemetry_snapshot_;              ///< En güncel çözümlenmiş telemetri.
    uint16_t lock_sequence_counter_ = 0;               ///< V2 kilit paketi sıra numarası sayacı.

    // --- Yaw hızı türevi için ardışık telemetri kareleri arası pusula/zaman ---
    bool telem_prev_valid_ = false;                      ///< Önceki kare verisi geçerli mi.
    float telem_prev_yaw_deg_ = 0.0f;                   ///< Önceki karenin pusula açısı.
    std::chrono::steady_clock::time_point telem_prev_tp_{}; ///< Önceki karenin zaman damgası.

    std::chrono::steady_clock::time_point connect_time_{}; ///< Son başarılı port açılış zamanı.
    bool link_health_warned_ = false;                      ///< RX sağlık uyarısı bir kez loglandı mı.
    uint32_t tx_dropped_on_disconnect_ = 0;                ///< Kopma anında kaybedilen TX paket sayısı.
};

} // namespace autopilot
} // namespace savasan

#endif // ALC_LINK_BRIDGE_HPP
