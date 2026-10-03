/**
 * @file alc_link_bridge.cpp
 * @brief @ref savasan::autopilot::AlcLinkBridge sınıfının uygulaması.
 *
 * Bu dosya, Alacakart otopilot protokolünün paket oluşturma/çözümleme
 * fonksiyonlarını (isimsiz ad alanında) ve seri port üzerinden blokesiz
 * gönderim/alım mantığını içerir.
 */
#include "alc_link_bridge.hpp"
#include "common/log.hpp"
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#include <array>
#include <cerrno>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <utility>

namespace {

// --- Protokol sabitleri (paket başlıkları, tipleri ve boyutları) ---
constexpr uint8_t kPacketHeader0 = 0xAA;            ///< Giden paket başlık baytı 0.
constexpr uint8_t kPacketHeader1 = 0x55;            ///< Giden paket başlık baytı 1.
constexpr uint8_t kTypeNoLock = 0x00;               ///< Paket tipi: kilit yok.
constexpr uint8_t kTypeLockCoordinatesV1 = 0x01;    ///< Paket tipi: kilit koordinatı (V1, XOR).
constexpr uint8_t kTypeEvasionCommand = 0x02;       ///< Paket tipi: kaçış komutu.
constexpr uint8_t kTypeLockCoordinatesV2 = 0x11;    ///< Paket tipi: kilit koordinatı (V2, CRC16).
constexpr uint8_t kLockPacketV2Version = 0x01;      ///< V2 kilit paketi alt sürüm baytı.
constexpr size_t kLockCoordinatesV1PacketSize = 8;  ///< V1 kilit paketi uzunluğu (bayt).
constexpr size_t kLockCoordinatesV2PacketSize = 20; ///< V2 kilit paketi uzunluğu (bayt).
constexpr auto kLogIntervalFastControl = std::chrono::milliseconds(250); ///< Hızlı kontrol logu periyodu.
constexpr auto kLogIntervalDefault = std::chrono::seconds(1);            ///< Varsayılan log periyodu.
constexpr auto kLogIntervalSlow = std::chrono::seconds(2);               ///< Seyrek log periyodu.

/// @brief Tamsayı baud hızını termios @c speed_t sabitine çevirir (bilinmiyorsa B115200).
speed_t IntBaudToTermiosSpeed(int baud) {
  switch (baud) {
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
    case 460800: return B460800;
    case 921600: return B921600;
    default: return B115200;
  }
}

/// @brief Baud hızı desteklenen listede mi (termios fallback yok).
bool IsSupportedBaudRate(int baud) {
  switch (baud) {
    case 9600:
    case 19200:
    case 38400:
    case 57600:
    case 115200:
    case 230400:
    case 460800:
    case 921600:
      return true;
    default:
      return false;
  }
}

/// @brief Bozuk RX akışında bir sonraki 0xAA 0x55 veya 0x54 senkron noktasına kadar
/// atlanacak bayt sayısını hesaplar; bulunamazsa @p fallback kullanılır.
template <std::size_t Capacity>
size_t ComputeRxResyncSkip(const savasan::autopilot::RxRingBuffer<Capacity>& buf,
                           const size_t fallback) {
  if (buf.Size() <= 1) {
    return buf.Size();
  }
  for (size_t i = 1; i < buf.Size(); ++i) {
    const uint8_t b = buf.At(i);
    if (b == 0xAA && i + 1 < buf.Size() && buf.At(i + 1) == kPacketHeader1) {
      return i;
    }
    if (b == 0x54) {
      return i;
    }
  }
  return std::min(fallback, buf.Size());
}

/// @brief İki baytı little-endian sırayla 16-bit işaretsiz tamsayıya çözer.
/// ALC_LINK V2.0.1 paketi little-endian'dır.
uint16_t DecodeU16LE(const uint8_t lo, const uint8_t hi) {
  return static_cast<uint16_t>(static_cast<uint16_t>(lo) |
                               (static_cast<uint16_t>(hi) << 8U));
}

/// @brief Dört baytı little-endian sırayla 32-bit işaretli tamsayıya çözer.
int32_t DecodeS32LE(const uint8_t b0, const uint8_t b1, const uint8_t b2, const uint8_t b3) {
  return static_cast<int32_t>(static_cast<uint32_t>(b0) |
                              (static_cast<uint32_t>(b1) << 8U) |
                              (static_cast<uint32_t>(b2) << 16U) |
                              (static_cast<uint32_t>(b3) << 24U));
}

/// @brief [begin, end_exclusive) aralığındaki baytların XOR sağlama toplamını hesaplar.
uint8_t ComputeXorChecksum(const uint8_t* data, const size_t begin, const size_t end_exclusive) {
  uint8_t checksum = 0;
  for (size_t i = begin; i < end_exclusive; ++i) {
    checksum ^= data[i];
  }
  return checksum;
}

/// @brief [begin, end_exclusive) aralığı için CRC-16/CCITT (0x1021, başlangıç 0xFFFF) hesaplar.
uint16_t ComputeCrc16Ccitt(const uint8_t* data, const size_t begin,
                           const size_t end_exclusive) {
  uint16_t crc = 0xFFFFU;
  for (size_t i = begin; i < end_exclusive; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8U;
    for (int bit = 0; bit < 8; ++bit) {
      if ((crc & 0x8000U) != 0U) {
        crc = static_cast<uint16_t>((crc << 1U) ^ 0x1021U);
      } else {
        crc = static_cast<uint16_t>(crc << 1U);
      }
    }
  }
  return crc;
}

/// @brief Bir koordinatı 16-bit tamsayıya kodlar.
/// @param value Kodlanacak koordinat.
/// @param normalize true ise [0,1] aralığı 0..65535'e ölçeklenir; false ise piksel kırpılır.
/// @return Kodlanmış değer; geçersiz (NaN/Inf) girişte 0.
uint16_t EncodeCoordinateToU16(const float value, const bool normalize) {
  if (!std::isfinite(value)) {
    return 0;
  }

  if (normalize) {
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    return static_cast<uint16_t>(std::lround(clamped * 65535.0f));
  }

  const float clamped_px = std::clamp(value, 0.0f, 65535.0f);
  return static_cast<uint16_t>(std::lround(clamped_px));
}

/// @brief 8 baytlık V1 kilit koordinatı paketini oluşturur (XOR sağlamalı, eski protokol).
std::array<uint8_t, kLockCoordinatesV1PacketSize> BuildLockCoordinatesPacketV1(
    const savasan::autopilot::LockCoordinates& coords,
    const bool normalize_coordinates) {
  std::array<uint8_t, kLockCoordinatesV1PacketSize> packet{};
  const uint16_t x_val = EncodeCoordinateToU16(coords.x, normalize_coordinates);
  const uint16_t y_val = EncodeCoordinateToU16(coords.y, normalize_coordinates);

  packet[0] = kPacketHeader0;
  packet[1] = kPacketHeader1;
  packet[2] = kTypeLockCoordinatesV1;
  packet[3] = static_cast<uint8_t>((x_val >> 8) & 0xFF);
  packet[4] = static_cast<uint8_t>(x_val & 0xFF);
  packet[5] = static_cast<uint8_t>((y_val >> 8) & 0xFF);
  packet[6] = static_cast<uint8_t>(y_val & 0xFF);
  packet[7] = ComputeXorChecksum(packet.data(), 2, 7);
  return packet;
}

/// @brief 20 baytlık V2 kilit koordinatı paketini oluşturur (track_id + sıra + zaman + CRC16).
/// @param sequence Artan paket sıra numarası (kayıp/yeniden sıralama tespiti için).
/// @param timestamp_ms Gönderim zaman damgası (ms, 32-bit'e sarmalanmış).
std::array<uint8_t, kLockCoordinatesV2PacketSize> BuildLockCoordinatesPacketV2(
    const savasan::autopilot::LockCoordinates& coords,
    const bool normalize_coordinates,
    const uint16_t sequence,
    const uint32_t timestamp_ms) {
  std::array<uint8_t, kLockCoordinatesV2PacketSize> packet{};
  const uint16_t x_val = EncodeCoordinateToU16(coords.x, normalize_coordinates);
  const uint16_t y_val = EncodeCoordinateToU16(coords.y, normalize_coordinates);

  packet[0] = kPacketHeader0;
  packet[1] = kPacketHeader1;
  packet[2] = kTypeLockCoordinatesV2;
  packet[3] = kLockPacketV2Version;
  packet[4] = static_cast<uint8_t>((x_val >> 8) & 0xFF);
  packet[5] = static_cast<uint8_t>(x_val & 0xFF);
  packet[6] = static_cast<uint8_t>((y_val >> 8) & 0xFF);
  packet[7] = static_cast<uint8_t>(y_val & 0xFF);

  packet[8] = static_cast<uint8_t>((coords.track_id >> 24U) & 0xFFU);
  packet[9] = static_cast<uint8_t>((coords.track_id >> 16U) & 0xFFU);
  packet[10] = static_cast<uint8_t>((coords.track_id >> 8U) & 0xFFU);
  packet[11] = static_cast<uint8_t>(coords.track_id & 0xFFU);
  packet[12] = static_cast<uint8_t>((sequence >> 8U) & 0xFFU);
  packet[13] = static_cast<uint8_t>(sequence & 0xFFU);
  packet[14] = static_cast<uint8_t>((timestamp_ms >> 24U) & 0xFFU);
  packet[15] = static_cast<uint8_t>((timestamp_ms >> 16U) & 0xFFU);
  packet[16] = static_cast<uint8_t>((timestamp_ms >> 8U) & 0xFFU);
  packet[17] = static_cast<uint8_t>(timestamp_ms & 0xFFU);

  const uint16_t crc = ComputeCrc16Ccitt(packet.data(), 2, kLockCoordinatesV2PacketSize - 2);
  packet[18] = static_cast<uint8_t>((crc >> 8U) & 0xFFU);
  packet[19] = static_cast<uint8_t>(crc & 0xFFU);
  return packet;
}

/// @brief 4 baytlık "kilit yok" paketini oluşturur.
/// @param checksum_mode 1 ise sağlama baytı sabit 0xFF, aksi hâlde XOR.
std::array<uint8_t, 4> BuildNoLockPacket(const int checksum_mode) {
  std::array<uint8_t, 4> packet{};
  packet[0] = kPacketHeader0;
  packet[1] = kPacketHeader1;
  packet[2] = kTypeNoLock;
  packet[3] = (checksum_mode == 1) ? 0xFF : ComputeXorChecksum(packet.data(), 2, 3);
  return packet;
}

/// @brief Dosya tanıtıcısının yazılabilir olmasını poll ile bekler (EINTR'de tekrar dener).
/// @return Süre içinde yazılabilir olduysa true, zaman aşımı/hatada false.
bool WaitWritable(const int fd, const int timeout_ms) {
  struct pollfd pfd {};
  pfd.fd = fd;
  pfd.events = POLLOUT;
  while (true) {
    const int rc = ::poll(&pfd, 1, timeout_ms);
    if (rc > 0) {
      return (pfd.revents & POLLOUT) != 0;
    }
    if (rc == 0) {
      return false;
    }
    if (errno == EINTR) {
      continue;
    }
    return false;
  }
}

}  // namespace

namespace savasan {
namespace autopilot {

AlcLinkBridge::AlcLinkBridge() : AlcLinkBridge(Config()) {}

AlcLinkBridge::AlcLinkBridge(const Config& config)
    : serial_fd_(-1)
    , config_(config)
    , connected_(false)
    , heartbeat_running_(false)
    , last_coords_{0.0f, 0.0f, false, 0}
    , rx_frame_count_(0)
    , lock_sequence_counter_(0) {
}

AlcLinkBridge::~AlcLinkBridge() {
    Disconnect();
}

// Seri portu açar; başarılıysa bağlantıyı işaretler ve TX iş parçacığını başlatır.
// Kilit bölgesini kısa tutmak için loglama mutex dışında yapılır.
bool AlcLinkBridge::Connect() {
    std::string device_path;
    bool already_connected = false;
    bool open_failed = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        device_path = config_.device_path;
        if (connected_) {
            already_connected = true;
        } else if (!OpenSerialPort()) {
            open_failed = true;
        } else {
            connected_ = true;
            lock_sequence_counter_ = 0;
            StartTxWorkerLocked();
        }
    }
    if (already_connected) {
        savasan::common::Log(savasan::common::LogLevel::kWarn, "AlcLinkBridge", "Zaten bagli");
        return true;
    }
    if (open_failed) {
        savasan::common::Log(savasan::common::LogLevel::kError, "AlcLinkBridge",
                             "Seri port acilamadi: " + device_path);
        return false;
    }
    savasan::common::Log(savasan::common::LogLevel::kInfo, "AlcLinkBridge",
                         "Baglandi: " + device_path);
    return true;
}

void AlcLinkBridge::Disconnect() {
    StopHeartbeat();
    StopTxWorker();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        CloseSerialPort();
        connected_ = false;
    }
    savasan::common::Log(savasan::common::LogLevel::kInfo, "AlcLinkBridge", "Baglanti kesildi");
}

bool AlcLinkBridge::IsConnected() const {
    return connected_.load();
}

// Geçerli koordinatı TX kuyruğuna ekler; kuyruk doluysa en eski öğeyi düşürerek
// daima en güncel veriyi tutar (sıcak yolda blokesiz kalmak için).
bool AlcLinkBridge::EnqueueLockCoordinates(const LockCoordinates& coords) {
    if (!connected_) {
        return false;
    }
    if (!coords.valid || !std::isfinite(coords.x) || !std::isfinite(coords.y)) {
        return false;
    }
    {
        std::lock_guard<std::mutex> ql(tx_mutex_);
        if (tx_lock_queue_.size() >= kTxMaxQueueSize) {
            tx_lock_queue_.pop_front();
        }
        tx_lock_queue_.push_back(coords);
    }
    tx_cv_.notify_one();
    return true;
}

bool AlcLinkBridge::EnqueueNoLock() {
    if (!connected_) {
        return false;
    }
    {
        std::lock_guard<std::mutex> ql(tx_mutex_);
        tx_no_lock_pending_ = true;
    }
    tx_cv_.notify_one();
    return true;
}

bool AlcLinkBridge::EnqueueHeartbeat() {
    if (!connected_.load()) {
        return false;
    }
    {
        std::lock_guard<std::mutex> ql(tx_mutex_);
        tx_heartbeat_pending_ = true;
    }
    tx_cv_.notify_one();
    return true;
}

bool AlcLinkBridge::EnqueueRawPacket(const uint8_t* data, const size_t len) {
    if (!connected_ || data == nullptr || len == 0 || len > 20) {
        return false;
    }
    TxRawPacket pkt{};
    std::copy_n(data, len, pkt.data.begin());
    pkt.len = len;
    {
        std::lock_guard<std::mutex> ql(tx_mutex_);
        if (tx_raw_queue_.size() >= kTxMaxRawQueueSize) {
            tx_raw_queue_.pop_front();
        }
        tx_raw_queue_.push_back(pkt);
    }
    tx_cv_.notify_one();
    return true;
}

bool AlcLinkBridge::EnqueueSeyirPacket(const uint8_t* data, const size_t len) {
    if (!connected_ || data == nullptr || len == 0 || len > 20) {
        return false;
    }
    {
        std::lock_guard<std::mutex> ql(tx_mutex_);
        std::copy_n(data, len, tx_seyir_latest_.data.begin());
        tx_seyir_latest_.len = len;
        tx_seyir_pending_ = true;
    }
    tx_cv_.notify_one();
    return true;
}

// Tüm TX kuyrukları ve bekleyen bayraklar boşalana kadar (ya da zaman aşımına
// kadar) kısa uyku adımlarıyla yoklayarak bekler.
void AlcLinkBridge::WaitTxIdle(const std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        bool pending = false;
        {
            std::lock_guard<std::mutex> ql(tx_mutex_);
            pending = tx_no_lock_pending_ || tx_seyir_pending_ ||
                      tx_heartbeat_pending_ || !tx_lock_queue_.empty() || !tx_raw_queue_.empty();
        }
        if (!pending) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

// Koordinatı doğrular; TX iş parçacığı çalışıyorsa kuyruğa alır, aksi hâlde
// senkron olarak doğrudan porta yazar. Loglama sıklık sınırlamasıyla yapılır.
bool AlcLinkBridge::SendLockCoordinates(const LockCoordinates& coords) {
    if (!connected_) {
        if (savasan::common::ShouldLogEvery("alc_lock_no_connection", kLogIntervalSlow)) {
            SAVASAN_LOG_IF(savasan::common::LogLevel::kWarn, "AlcLinkBridge",
                           "Baglanti yok, koordinat gonderilemedi");
        }
        return false;
    }

    if (!coords.valid) {
        if (savasan::common::ShouldLogEvery("alc_invalid_lock_coords", kLogIntervalDefault)) {
            savasan::common::Log(savasan::common::LogLevel::kWarn, "AlcLinkBridge",
                                 "Geçersiz lock coordinate (valid=false), paket gönderilmedi");
        }
        return false;
    }
    if (!std::isfinite(coords.x) || !std::isfinite(coords.y)) {
        if (savasan::common::ShouldLogEvery("alc_non_finite_lock_coords", kLogIntervalDefault)) {
            savasan::common::Log(savasan::common::LogLevel::kWarn, "AlcLinkBridge",
                                 "Geçersiz lock coordinate (NaN/Inf), paket gönderilmedi");
        }
        return false;
    }

    if (tx_worker_running_.load(std::memory_order_acquire)) {
        return EnqueueLockCoordinates(coords);
    }

    bool success = false;
    const uint32_t track_id = coords.track_id;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!connected_) {
            return false;
        }
        if (WriteLockPacketLocked(coords)) {
            last_coords_ = coords;
            last_send_time_ = std::chrono::steady_clock::now();
            DrainAndParseRx();
            success = true;
        }
    }
    if (success && savasan::common::ShouldLogEvery("alc_lock_sent", kLogIntervalDefault)) {
        SAVASAN_LOG_F(savasan::common::LogLevel::kInfo, "AlcLinkBridge",
                      "Kilit koordinati gonderildi track_id=%u", track_id);
    }
    return success;
}

// Yapılandırılmış sürüme göre kilit paketini üretir: sürüm >= 2 ise sıra/zaman
// damgalı CRC16'lı V2 paketi, aksi hâlde XOR sağlamalı V1 paketi yazılır.
bool AlcLinkBridge::WriteLockPacketLocked(const LockCoordinates& coords) {
    if (config_.lock_packet_version >= 2) {
        const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now().time_since_epoch())
                                .count();
        const uint32_t timestamp_ms = static_cast<uint32_t>(now_ms & 0xFFFFFFFFULL);
        const uint16_t sequence = lock_sequence_counter_++;
        const auto packet = BuildLockCoordinatesPacketV2(coords, config_.normalize_coordinates,
                                                         sequence, timestamp_ms);
        return WriteData(packet.data(), packet.size());
    }
    const auto packet = BuildLockCoordinatesPacketV1(coords, config_.normalize_coordinates);
    return WriteData(packet.data(), packet.size());
}

bool AlcLinkBridge::SendNoLock() {
    if (!connected_) {
        return false;
    }

    if (tx_worker_running_.load(std::memory_order_acquire)) {
        return EnqueueNoLock();
    }

    int checksum_mode = 0;
    bool success = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!connected_) {
            return false;
        }
        checksum_mode = config_.no_lock_checksum_mode;
        const auto packet = BuildNoLockPacket(checksum_mode);
        success = WriteData(packet.data(), packet.size());
        if (success) {
            DrainAndParseRx();
            last_coords_.valid = false;
        }
    }
    if (success && savasan::common::ShouldLogEvery("alc_nolock_sent", kLogIntervalDefault)) {
        savasan::common::Log(savasan::common::LogLevel::kInfo, "AlcLinkBridge",
                             "Kilit yok paketi gonderildi");
    }
    return success;
}

bool AlcLinkBridge::EnqueueNoLockNonBlocking() {
    if (!connected_) {
        return false;
    }
    if (tx_worker_running_.load(std::memory_order_acquire)) {
        return EnqueueNoLock();
    }
    return SendNoLock();
}

bool AlcLinkBridge::EnqueueLockNonBlocking(const LockCoordinates& coords) {
    if (!connected_ || !coords.valid ||
        !std::isfinite(coords.x) || !std::isfinite(coords.y)) {
        return false;
    }
    if (tx_worker_running_.load(std::memory_order_acquire)) {
        return EnqueueLockCoordinates(coords);
    }
    return SendLockCoordinates(coords);
}

// 5 baytlık kaçış komutu paketi oluşturur (XOR sağlamalı) ve gönderir/kuyruğa alır.
bool AlcLinkBridge::SendEvasionCommand(uint8_t evasion_type) {
    if (!connected_) {
        return false;
    }

    uint8_t packet[5];
    packet[0] = kPacketHeader0;
    packet[1] = kPacketHeader1;
    packet[2] = kTypeEvasionCommand;
    packet[3] = evasion_type;
    packet[4] = ComputeXorChecksum(packet, 2, 4);

    if (tx_worker_running_.load(std::memory_order_acquire)) {
        const bool ok = EnqueueRawPacket(packet, sizeof(packet));
        if (ok) {
            SAVASAN_LOG_F(savasan::common::LogLevel::kInfo, "AlcLinkBridge",
                          "Kacis komutu kuyruga alindi type=%d",
                          static_cast<int>(evasion_type));
        }
        return ok;
    }

    bool success = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!connected_) {
            return false;
        }
        success = WriteData(packet, sizeof(packet));
        if (success) {
            DrainAndParseRx();
        }
    }
    if (success) {
        SAVASAN_LOG_F(savasan::common::LogLevel::kInfo, "AlcLinkBridge",
                      "Kacis komutu gonderildi type=%d", static_cast<int>(evasion_type));
    }
    return success;
}

// 12 baytlık güdüm setpoint paketi kaldırıldı (Seyir Modu tek güdüm yolu).

bool AlcLinkBridge::SendSeyirModeCommand(const SeyirModePacket& pkt, const bool dry_run) {
    if (!pkt.valid || pkt.length == 0 || pkt.length > SeyirModePacket::kMaxBytes) {
        return false;
    }
    if (!connected_ && !dry_run) {
        return false;
    }

    if (dry_run) {
        if (savasan::common::ShouldLogEvery("alc_seyir_dry_run", kLogIntervalDefault)) {
            SAVASAN_LOG_F(savasan::common::LogLevel::kInfo, "AlcLinkBridge",
                          "DRY_RUN seyir len=%zu istem=%u", pkt.length,
                          pkt.length > 3 ? static_cast<unsigned>(pkt.bytes[3]) : 0U);
        }
        return true;
    }

    if (tx_worker_running_.load(std::memory_order_acquire)) {
        const bool ok = EnqueueSeyirPacket(pkt.bytes.data(), pkt.length);
        if (ok && savasan::common::ShouldLogEvery("alc_seyir_sent", kLogIntervalFastControl)) {
            SAVASAN_LOG_F(savasan::common::LogLevel::kInfo, "AlcLinkBridge",
                          "Seyir komutu kuyruga alindi len=%zu istem=%u", pkt.length,
                          static_cast<unsigned>(pkt.bytes[3]));
        }
        return ok;
    }

    bool ok = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!connected_) {
            return false;
        }
        ok = WriteData(pkt.bytes.data(), pkt.length);
        if (ok) {
            DrainAndParseRx();
        }
    }
    if (ok && savasan::common::ShouldLogEvery("alc_seyir_sent", kLogIntervalFastControl)) {
        SAVASAN_LOG_F(savasan::common::LogLevel::kInfo, "AlcLinkBridge",
                      "Seyir komutu gonderildi len=%zu istem=%u", pkt.length,
                      static_cast<unsigned>(pkt.bytes[3]));
    }
    return ok;
}

bool AlcLinkBridge::SendRawBytes(const uint8_t* data, size_t length) {
    if (data == nullptr || length == 0 || !connected_) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_) {
        return false;
    }
    const bool ok = WriteData(data, length);
    if (ok) {
        DrainAndParseRx();
    }
    return ok;
}

TelemetrySnapshot AlcLinkBridge::GetTelemetrySnapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return telemetry_snapshot_;
}

void AlcLinkBridge::StartHeartbeat() {
    if (heartbeat_running_) {
        return;
    }

    heartbeat_running_ = true;
    heartbeat_thread_ = std::thread(&AlcLinkBridge::HeartbeatThread, this);
    savasan::common::Log(savasan::common::LogLevel::kInfo, "AlcLinkBridge", "Heartbeat baslatildi");
}

void AlcLinkBridge::StopHeartbeat() {
    if (!heartbeat_running_) {
        return;
    }

    heartbeat_running_ = false;
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }
    savasan::common::Log(savasan::common::LogLevel::kInfo, "AlcLinkBridge", "Heartbeat durduruldu");
}

// Seri portu blokesiz (O_NONBLOCK) açar ve 8N1 ham (raw) modda, donanım/yazılım
// akış kontrolü kapalı olacak şekilde yapılandırır.
bool AlcLinkBridge::OpenSerialPort() {
    serial_fd_ = open(config_.device_path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);

    if (serial_fd_ == -1) {
        savasan::common::Log(savasan::common::LogLevel::kError, "AlcLinkBridge",
                             "Seri port acilamadi: " + config_.device_path);
        return false;
    }

    struct termios options;
    if (tcgetattr(serial_fd_, &options) != 0) {
        OnWriteFailureLocked(strerror(errno));
        return false;
    }

    const speed_t spd = IntBaudToTermiosSpeed(config_.baud_rate);
    if (!IsSupportedBaudRate(config_.baud_rate)) {
        savasan::common::Log(savasan::common::LogLevel::kWarn, "AlcLinkBridge",
                             "Desteklenmeyen baud " + std::to_string(config_.baud_rate) +
                                 " -> termios B115200 kullanilacak");
    }
    if (cfsetispeed(&options, spd) != 0 || cfsetospeed(&options, spd) != 0) {
        OnWriteFailureLocked(strerror(errno));
        return false;
    }

    // 8N1 ayarı + akış kontrolünü devre dışı bırakma
    options.c_cflag &= ~PARENB;   // Parite yok
    options.c_cflag &= ~CSTOPB;   // 1 stop biti
    options.c_cflag &= ~CSIZE;    // Veri biti maskesini temizle
    options.c_cflag |= CS8;       // 8 veri biti
    options.c_cflag |= CREAD;     // Alıcıyı etkinleştir
    options.c_cflag |= CLOCAL;    // Modem kontrol hatlarını yoksay
    options.c_cflag &= ~CRTSCTS;  // Donanım akış kontrolü kapalı

    // Ham (raw) mod: satır işleme ve yankı yok
    options.c_iflag &= ~(IXON | IXOFF | IXANY);          // Yazılım akış kontrolü kapalı
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG); // Canonical mod/yankı/sinyal kapalı
    options.c_oflag &= ~OPOST;                           // Çıkış işleme kapalı

    // Okuma zaman aşımı ayarı
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 10;  // 1 saniye (10 desisaniye) zaman aşımı

    if (tcsetattr(serial_fd_, TCSANOW, &options) != 0) {
        OnWriteFailureLocked(strerror(errno));
        return false;
    }

    rx_buffer_.Clear();
    last_rx_time_ = std::chrono::steady_clock::now();
    connect_time_ = last_rx_time_;
    link_health_warned_ = false;

    return true;
}

void AlcLinkBridge::CloseSerialPort() {
    if (serial_fd_ != -1) {
        close(serial_fd_);
        serial_fd_ = -1;
    }
}

// Veriyi tamamı yazılana kadar döngüyle yazar. Kısmi yazım, EINTR ve
// EAGAIN/EWOULDBLOCK (blokesiz port doluyken) durumlarını yönetir; kalıcı
// hatada bağlantıyı sıfırlar.
bool AlcLinkBridge::WriteData(const uint8_t* data, size_t length) {
    if (serial_fd_ == -1) {
        return false;
    }

    size_t total_written = 0;
    while (total_written < length) {
        const ssize_t written =
            write(serial_fd_, data + total_written, length - total_written);
        if (written > 0) {
            total_written += static_cast<size_t>(written);
            continue;
        }
        if (written == 0) {
            if (!WaitWritable(serial_fd_, 100)) {
                OnWriteFailureLocked("write timeout");
                return false;
            }
            continue;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (!WaitWritable(serial_fd_, 100)) {
                OnWriteFailureLocked("poll timeout");
                return false;
            }
            continue;
        }
        OnWriteFailureLocked(strerror(errno));
        return false;
    }
    return true;
}

// Porttan okunabilen tüm baytları RX tamponuna aktarır (taşma olursa tamponu
// sıfırlar), ardından tampondaki tam paketleri çözümler.
void AlcLinkBridge::DrainAndParseRx() {
    if (serial_fd_ == -1) {
        return;
    }

    uint8_t tmp[256];
    while (true) {
        const ssize_t n = read(serial_fd_, tmp, sizeof(tmp));
        if (n > 0) {
            const size_t incoming = static_cast<size_t>(n);
            if (incoming > rx_buffer_.Available()) {
                if (savasan::common::ShouldLogEvery("alc_rx_overflow", kLogIntervalSlow)) {
                    savasan::common::Log(savasan::common::LogLevel::kWarn, "AlcLinkBridge",
                                         "RX tamponu tasildi, sifirlaniyor");
                }
                rx_buffer_.Clear();
            }
            (void)rx_buffer_.PushBack(tmp, incoming);
            last_rx_time_ = std::chrono::steady_clock::now();
            continue;
        }
        if (n == 0) {
            break;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        OnWriteFailureLocked(strerror(errno));
        return;
    }

    ParseRxBufferLocked();
    MaybeWarnLinkHealthLocked();
}

// RX tamponunu baştan tarayarak iki tür çerçeveyi çözümler:
//  1) 0xAA 0x55 başlıklı ACK paketleri (sağlama doğrulanır, sayaç artırılır),
//  2) 0x54 ile başlayıp 0x2a ile biten 32 baytlık telemetri paketi.
// Eksik veri durumunda erken döner (daha çok bayt beklenir); bozuk hizalamada
// 1 bayt atılarak yeniden senkron olunur.
void AlcLinkBridge::ParseRxBufferLocked() {
    // Baştan n bayt tüketir (dairesel tampon head'ini O(1) ilerletir).
    auto consume = [&](size_t n) { rx_buffer_.PopFront(n); };

    // Parse için kullanılan bitişik staging tamponu; en büyük paket telemetri (32 bayt).
    constexpr size_t kStagingSize = 32;
    uint8_t frame[kStagingSize];

    while (!rx_buffer_.Empty()) {
        const uint8_t b0 = rx_buffer_.At(0);
        if (b0 == 0xAA) {
            if (rx_buffer_.Size() < 2) {
                return;
            }
            if (rx_buffer_.At(1) != kPacketHeader1) {
                consume(1);
                continue;
            }
            if (rx_buffer_.Size() < 3) {
                return;
            }
            const uint8_t type = rx_buffer_.At(2);
            size_t expected_len = 0;
            if (type == kTypeNoLock) {
                expected_len = 4;
            } else if (type == kTypeLockCoordinatesV1) {
                expected_len = kLockCoordinatesV1PacketSize;
            } else if (type == kTypeLockCoordinatesV2) {
                expected_len = kLockCoordinatesV2PacketSize;
            } else if (type == kTypeEvasionCommand) {
                expected_len = 5;
            } else {
                consume(1);
                continue;
            }
            if (rx_buffer_.Size() < expected_len) {
                return;
            }
            if (expected_len > kStagingSize) {
                // Beklenmedik durum; bayrağı tüketip senkron ol.
                consume(1);
                continue;
            }
            // Paketi bitişik staging tampona kopyala (CRC/XOR ham pointer ister).
            rx_buffer_.Peek(0, expected_len, frame);

            bool valid_checksum = false;
            if (type == kTypeLockCoordinatesV2 && expected_len == kLockCoordinatesV2PacketSize) {
                const uint16_t crc =
                    ComputeCrc16Ccitt(frame, 2, kLockCoordinatesV2PacketSize - 2);
                const uint16_t got = static_cast<uint16_t>(
                    (static_cast<uint16_t>(frame[kLockCoordinatesV2PacketSize - 2]) << 8U) |
                    static_cast<uint16_t>(frame[kLockCoordinatesV2PacketSize - 1]));
                valid_checksum = (crc == got);
            } else if (type == kTypeNoLock) {
                const uint8_t checksum_xor =
                    ComputeXorChecksum(frame, 2, expected_len - 1);
                const uint8_t got = frame[expected_len - 1];
                valid_checksum = (got == checksum_xor || got == 0xFF);
            } else {
                const uint8_t checksum =
                    ComputeXorChecksum(frame, 2, expected_len - 1);
                const uint8_t got = frame[expected_len - 1];
                valid_checksum = (checksum == got);
            }
            if (valid_checksum) {
                rx_frame_count_.fetch_add(1);
                if (savasan::common::ShouldLogEvery("alc_rx_ack", kLogIntervalSlow)) {
                    SAVASAN_LOG_F(savasan::common::LogLevel::kDebug, "AlcLinkBridge",
                                  "RX ACK type=%d", static_cast<int>(type));
                }
                consume(expected_len);
            } else {
                if (savasan::common::ShouldLogEvery("alc_rx_checksum", kLogIntervalDefault)) {
                    SAVASAN_LOG_F(savasan::common::LogLevel::kWarn, "AlcLinkBridge",
                                  "RX ACK checksum hatasi type=%d", static_cast<int>(type));
                }
                const size_t skip = ComputeRxResyncSkip(rx_buffer_, expected_len);
                consume(skip);
            }
            continue;
        }

        // ALC_LINK V2.0.1 (ALACAKART V2.2.9) telemetri paketi: 0x54 ile başlayan,
        // 0x2a ile biten 32 baytlık little-endian çerçeve.
        if (b0 == 0x54) {
            constexpr size_t kTelemetryLen = 32;
            static_assert(kTelemetryLen <= kStagingSize, "staging tampon telemetri paketini almalı");
            if (rx_buffer_.Size() < kTelemetryLen) {
                return;
            }
            // Bitiş baytı (0x2a '*') yoksa hatalı hizalama; 1 bayt atıp yeniden senkron.
            if (rx_buffer_.At(31) != 0x2a) {
                consume(1);
                continue;
            }
            rx_buffer_.Peek(0, kTelemetryLen, frame);

            rx_frame_count_.fetch_add(1);
            telemetry_snapshot_.valid = true;
            telemetry_snapshot_.frame_count = rx_frame_count_.load();
            std::copy_n(frame, kTelemetryLen, telemetry_snapshot_.raw.begin());

            const uint8_t* b = frame;
            telemetry_snapshot_.arac_modu = b[1];
            telemetry_snapshot_.sistem_durum = b[2];
            telemetry_snapshot_.arac_id = b[3];
            telemetry_snapshot_.enlem =
                static_cast<double>(DecodeS32LE(b[4], b[5], b[6], b[7])) / 1000000.0;
            telemetry_snapshot_.boylam =
                static_cast<double>(DecodeS32LE(b[8], b[9], b[10], b[11])) / 1000000.0;
            telemetry_snapshot_.uydu_sayisi = b[12];
            telemetry_snapshot_.gps_hiz_mps = static_cast<float>(b[13]) / 3.6f;  // km/h -> m/s
            const float yaw_deg = static_cast<float>(DecodeU16LE(b[14], b[15]));
            telemetry_snapshot_.yaw_deg = yaw_deg;
            // Roll (x ekseni): b[18] işaret (1=negatif), b[19] mutlak derece.
            telemetry_snapshot_.roll_deg =
                (b[18] == 1) ? -static_cast<float>(b[19]) : static_cast<float>(b[19]);
            // Pitch (y ekseni): b[20] işaret, b[21] mutlak derece.
            telemetry_snapshot_.pitch_deg =
                (b[20] == 1) ? -static_cast<float>(b[21]) : static_cast<float>(b[21]);

            // Değişken kuyruk b[23..30], seçim biti b[22] ile belirlenir.
            const uint8_t secim = b[22];
            const uint8_t* tail = &b[23];
            if (secim == 0 || secim == 3) {
                telemetry_snapshot_.irtifa_m =
                    static_cast<float>(DecodeU16LE(tail[0], tail[1])) / 10.0f;
                telemetry_snapshot_.voltaj_v =
                    static_cast<float>(DecodeU16LE(tail[2], tail[3])) / 100.0f;
                telemetry_snapshot_.akim_a =
                    static_cast<float>(DecodeU16LE(tail[4], tail[5])) / 10.0f;
                telemetry_snapshot_.harc_akim_mah = DecodeU16LE(tail[6], tail[7]);
            } else if (secim == 1) {
                telemetry_snapshot_.motor_durumu = tail[0];
                telemetry_snapshot_.gps_durum = static_cast<uint8_t>((tail[1] >> 5U) & 0x07U);
                telemetry_snapshot_.gps_saat = tail[3];
                telemetry_snapshot_.gps_dakika = tail[4];
                telemetry_snapshot_.gps_saniye = tail[5];
                telemetry_snapshot_.gps_zaman_gecerli = true;
            } else if (secim == 2) {
                telemetry_snapshot_.sicaklik_c = tail[0];
                telemetry_snapshot_.gps_gun = tail[1];
                telemetry_snapshot_.gps_ay = tail[2];
                telemetry_snapshot_.gps_yil = static_cast<uint16_t>(tail[3] + 2000);
            }

            // Güdüm/durum-kestirim için hız vektörü: yer hızı + pusula (NED varsayımı).
            const float yaw_rad = yaw_deg * 3.14159265358979f / 180.0f;
            telemetry_snapshot_.vel_x_mps = telemetry_snapshot_.gps_hiz_mps * std::cos(yaw_rad);
            telemetry_snapshot_.vel_y_mps = telemetry_snapshot_.gps_hiz_mps * std::sin(yaw_rad);
            telemetry_snapshot_.vel_z_mps = 0.0f;
            // Yaw hızı: ardışık kareler arası pusula farkından (±180° sarma düzeltmeli).
            const auto now_tp = std::chrono::steady_clock::now();
            if (telem_prev_valid_) {
                float dyaw = yaw_deg - telem_prev_yaw_deg_;
                while (dyaw > 180.0f) dyaw -= 360.0f;
                while (dyaw < -180.0f) dyaw += 360.0f;
                const float dt =
                    std::chrono::duration<float>(now_tp - telem_prev_tp_).count();
                telemetry_snapshot_.yaw_rate_dps = (dt > 1e-3f) ? (dyaw / dt) : 0.0f;
            }
            telem_prev_yaw_deg_ = yaw_deg;
            telem_prev_tp_ = now_tp;
            telem_prev_valid_ = true;

            if (savasan::common::ShouldLogEvery("alc_rx_telemetry", kLogIntervalSlow)) {
                savasan::common::Log(savasan::common::LogLevel::kDebug, "AlcLinkBridge",
                                     "RX telemetry frame=0x54 len=32");
            }
            consume(kTelemetryLen);
            continue;
        }

        consume(1);
    }
}

void AlcLinkBridge::MaybeWarnLinkHealthLocked() {
    if (link_health_warned_) {
        return;
    }
    const auto elapsed = std::chrono::steady_clock::now() - connect_time_;
    if (elapsed < std::chrono::seconds(3)) {
        return;
    }
    if (rx_frame_count_.load() > 0) {
        return;
    }
    link_health_warned_ = true;
    savasan::common::Log(savasan::common::LogLevel::kError, "AlcLinkBridge",
                         "3 sn icinde gecerli RX kare yok; kablo/baud (" +
                             std::to_string(config_.baud_rate) + ") uyumsuzlugu olabilir");
}

void AlcLinkBridge::OnWriteFailureLocked(const char* reason) {
    savasan::common::Log(savasan::common::LogLevel::kError, "AlcLinkBridge",
                         "Seri I/O hatasi: " + std::string(reason ? reason : "?") +
                             " -> baglanti sifirlaniyor");
    CloseSerialPort();
    connected_ = false;
}

// Heartbeat döngüsü: periyodik olarak TX kuyruğuna heartbeat isteği ekler.
void AlcLinkBridge::HeartbeatThread() {
    while (heartbeat_running_.load()) {
        std::this_thread::sleep_for(config_.send_interval);

        if (!heartbeat_running_.load()) {
            break;
        }
        if (!connected_.load()) {
            continue;
        }
        (void)EnqueueHeartbeat();
    }
}

void AlcLinkBridge::StartTxWorkerLocked() {
    if (tx_worker_running_.load(std::memory_order_acquire)) {
        return;
    }
    tx_worker_running_.store(true, std::memory_order_release);
    tx_thread_ = std::thread(&AlcLinkBridge::TxWorkerThread, this);
}

void AlcLinkBridge::StopTxWorker() {
    {
        std::lock_guard<std::mutex> ql(tx_mutex_);
        if (!tx_worker_running_.load(std::memory_order_acquire)) {
            return;
        }
        tx_worker_running_.store(false, std::memory_order_release);
        tx_lock_queue_.clear();
        tx_raw_queue_.clear();
        tx_seyir_pending_ = false;
        tx_no_lock_pending_ = false;
        tx_heartbeat_pending_ = false;
    }
    tx_cv_.notify_all();
    if (tx_thread_.joinable()) {
        tx_thread_.join();
    }
}

// TX iş parçacığı döngüsü: seyir öncelikli; ham paket / kilit-yok / heartbeat düşük öncelik.
void AlcLinkBridge::TxWorkerThread() {
    while (true) {
        LockCoordinates lock_item{};
        TxRawPacket seyir_item{};
        TxRawPacket raw_item{};
        bool has_lock = false;
        bool has_seyir = false;
        bool has_raw = false;
        bool send_no_lock = false;
        bool send_heartbeat = false;
        {
            std::unique_lock<std::mutex> ql(tx_mutex_);
            tx_cv_.wait(ql, [this]() {
                return !tx_worker_running_.load(std::memory_order_acquire) || tx_no_lock_pending_ ||
                       !tx_lock_queue_.empty() || tx_seyir_pending_ || !tx_raw_queue_.empty() ||
                       tx_heartbeat_pending_;
            });
            if (!tx_worker_running_.load(std::memory_order_acquire)) {
                break;
            }
            if (!tx_lock_queue_.empty()) {
                lock_item = tx_lock_queue_.back();
                tx_lock_queue_.clear();
                has_lock = true;
                tx_no_lock_pending_ = false;
            }
            if (tx_seyir_pending_) {
                seyir_item = tx_seyir_latest_;
                tx_seyir_pending_ = false;
                has_seyir = true;
            }
            if (!has_lock && !has_seyir) {
                if (!tx_raw_queue_.empty()) {
                    raw_item = tx_raw_queue_.front();
                    tx_raw_queue_.pop_front();
                    has_raw = true;
                } else if (tx_no_lock_pending_) {
                    tx_no_lock_pending_ = false;
                    send_no_lock = true;
                } else if (tx_heartbeat_pending_) {
                    tx_heartbeat_pending_ = false;
                    send_heartbeat = true;
                }
            }
        }

        if (!has_lock && !has_seyir && !has_raw && !send_no_lock && !send_heartbeat) {
            continue;
        }

        bool disconnected = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!connected_) {
                disconnected = true;
            } else {
                if (has_seyir) {
                    if (WriteData(seyir_item.data.data(), seyir_item.len)) {
                        DrainAndParseRx();
                    }
                }
                if (has_lock) {
                    if (WriteLockPacketLocked(lock_item)) {
                        last_coords_ = lock_item;
                        last_send_time_ = std::chrono::steady_clock::now();
                        DrainAndParseRx();
                    }
                }
                if (has_raw) {
                    if (WriteData(raw_item.data.data(), raw_item.len)) {
                        DrainAndParseRx();
                    }
                }
                if (send_no_lock) {
                    const auto packet = BuildNoLockPacket(config_.no_lock_checksum_mode);
                    if (WriteData(packet.data(), packet.size())) {
                        last_coords_.valid = false;
                        DrainAndParseRx();
                    }
                }
                if (send_heartbeat && last_coords_.valid) {
                    if (WriteLockPacketLocked(last_coords_)) {
                        DrainAndParseRx();
                    }
                }
            }
        }

        if (disconnected) {
            std::lock_guard<std::mutex> ql(tx_mutex_);
            if (has_lock) {
                tx_lock_queue_.push_back(lock_item);
            }
            if (has_seyir) {
                tx_seyir_latest_ = seyir_item;
                tx_seyir_pending_ = true;
            }
            if (has_raw) {
                if (tx_raw_queue_.size() >= kTxMaxRawQueueSize) {
                    tx_raw_queue_.pop_front();
                }
                tx_raw_queue_.push_front(raw_item);
            }
            if (send_no_lock) {
                tx_no_lock_pending_ = true;
            }
            if (send_heartbeat) {
                tx_heartbeat_pending_ = true;
            }
            ++tx_dropped_on_disconnect_;
            if (savasan::common::ShouldLogEvery("alc_tx_drop_disconnect", kLogIntervalDefault)) {
                SAVASAN_LOG_F(savasan::common::LogLevel::kWarn, "AlcLinkBridge",
                              "Baglanti kopuk, TX paketi yeniden kuyruga alindi (toplam=%u)",
                              tx_dropped_on_disconnect_);
            }
        }
    }
}

} // namespace autopilot
} // namespace savasan
