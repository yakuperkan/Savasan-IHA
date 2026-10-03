/**
 * @file rx_ring_buffer.hpp
 * @brief Sabit boyutlu, dinamik tahsisatsız (heap-free) dairesel RX tamponu.
 *
 * Seri porttan gelen baytların O(1) push/pop maliyetiyle biriktirilmesi ve
 * pencere içi erişim için kullanılır. `std::vector::insert/erase` gibi O(n)
 * kaydırmalar yerine head/size indeksleri ile ilerler.
 */
#ifndef SAVASAN_AUTOPILOT_RX_RING_BUFFER_HPP_
#define SAVASAN_AUTOPILOT_RX_RING_BUFFER_HPP_

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace savasan::autopilot {

/**
 * @brief Sabit kapasiteli dairesel RX tamponu (thread-safe değil; çağıran tarafta
 *        gerekli mutex tutulmalıdır).
 *
 * @tparam Capacity Bayt cinsinden toplam kapasite (2'nin katları önerilir).
 */
template <std::size_t Capacity>
class RxRingBuffer {
 public:
  /// @brief Tampondaki geçerli bayt sayısı.
  std::size_t Size() const noexcept { return size_; }
  /// @brief Tampon boş mu.
  bool Empty() const noexcept { return size_ == 0; }
  /// @brief Toplam kapasite.
  static constexpr std::size_t GetCapacity() noexcept { return Capacity; }
  /// @brief Yazılabilecek kalan bayt sayısı.
  std::size_t Available() const noexcept { return Capacity - size_; }
  /// @brief Tamponu boşaltır.
  void Clear() noexcept {
    head_ = 0;
    size_ = 0;
  }

  /// @brief n baytlık veriyi tamponun arkasına ekler.
  /// @return Yer yoksa false döner (kısmi yazım yapılmaz).
  bool PushBack(const uint8_t* src, std::size_t n) noexcept {
    if (src == nullptr || n == 0) {
      return n == 0;
    }
    if (n > Available()) {
      return false;
    }
    const std::size_t tail = (head_ + size_) % Capacity;
    const std::size_t first_chunk = std::min(n, Capacity - tail);
    std::memcpy(&data_[tail], src, first_chunk);
    const std::size_t rest = n - first_chunk;
    if (rest > 0) {
      std::memcpy(&data_[0], src + first_chunk, rest);
    }
    size_ += n;
    return true;
  }

  /// @brief Baştan n bayt tüketir; n >= Size() ise tamponu tamamen boşaltır.
  void PopFront(std::size_t n) noexcept {
    if (n >= size_) {
      Clear();
      return;
    }
    head_ = (head_ + n) % Capacity;
    size_ -= n;
  }

  /// @brief Verilen indeksteki baytı (0 = baş) döndürür (sınır kontrolü yok).
  uint8_t At(std::size_t idx) const noexcept {
    return data_[(head_ + idx) % Capacity];
  }

  /// @brief offset'ten itibaren n baytı @p dst üzerine bitişik olarak kopyalar.
  /// @details CRC/XOR gibi bitişik pointer gerektiren hesaplamalar için kullanılır.
  void Peek(std::size_t offset, std::size_t n, uint8_t* dst) const noexcept {
    if (dst == nullptr || n == 0) {
      return;
    }
    const std::size_t start = (head_ + offset) % Capacity;
    const std::size_t first_chunk = std::min(n, Capacity - start);
    std::memcpy(dst, &data_[start], first_chunk);
    const std::size_t rest = n - first_chunk;
    if (rest > 0) {
      std::memcpy(dst + first_chunk, &data_[0], rest);
    }
  }

 private:
  std::array<uint8_t, Capacity> data_{};
  std::size_t head_ = 0;   ///< Bir sonraki okunacak baytın indeksi.
  std::size_t size_ = 0;   ///< Tampondaki geçerli bayt sayısı.
};

}  // namespace savasan::autopilot

#endif  // SAVASAN_AUTOPILOT_RX_RING_BUFFER_HPP_
