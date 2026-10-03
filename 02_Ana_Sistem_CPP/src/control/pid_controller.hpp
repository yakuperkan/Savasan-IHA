/**
 * @file pid_controller.hpp
 * @brief Dört eksenli (X/Y/Z/Yaw) NED güdüm komutu üreten PID kontrolcü.
 */
#ifndef SAVASAN_CONTROL_PID_CONTROLLER_HPP_
#define SAVASAN_CONTROL_PID_CONTROLLER_HPP_

#include <chrono>
#include <cstdint>
#include <mutex>

namespace savasan::control {

/**
 * @brief Eksen bazlı (X/Y/Z/Yaw) PID hız kontrolcüsü.
 *
 * NED güdüm çıkışı üretir: ex→Kuzey/kapanma, ey→Doğu/yaw, ez→Aşağı/pitch.
 * Birimler çağıran tarafında eşlenir (m, deg → 0x03 oran komutu). Pozitif hata
 * pozitif çıkış verir (Kp>0); türev terimi
 * (e_k - e_{k-1})/dt standart işaretle hesaplanır. Anti-windup, ölü bant
 * (deadband), çıkış doygunluğu ve yön değişim hızı (slew rate) sınırlaması içerir.
 *
 * Thread-safe: tüm durum ve diagnostik @c state_mutex_ altında güncellenir.
 * @c Update tek iş parçacığından çağrılmalıdır (eşzamanlı çağrı seri hale getirilir).
 */
class PidControllerXYZYaw {
 public:
  /// @brief Tek bir eksenin PID katsayıları ve sınırları.
  struct AxisConfig {
    float kp = 0.5f;           ///< Oransal kazanç.
    float ki = 0.01f;          ///< İntegral kazancı.
    float kd = 0.0f;           ///< Türev kazancı.
    float i_limit = 5.0f;      ///< İntegral birikimi mutlak sınırı (anti-windup).
    float output_limit = 25.0f;///< Çıkış mutlak sınırı (m/s veya deg/s eksene göre).
    float deadband = 0.02f;    ///< Ölü bant: bu eşiğin altındaki hata sıfır sayılır.
    float slew_rate = 10.0f;   ///< Çıkış değişim hızı sınırı (birim/s).
  };

  /// @brief Dört eksenin tümü için yapılandırma demeti.
  struct Config {
    /// Kuzey/kapanma: metre tabanlı hata → m/s; uçuş zarfına uygun daha düşük sınırlar.
    AxisConfig x{
        .kp = 0.5f,
        .ki = 0.01f,
        .kd = 0.0f,
        .i_limit = 3.0f,
        .output_limit = 10.0f,
        .deadband = 0.02f,
        .slew_rate = 6.0f,
    };
    AxisConfig y{};   ///< NED Doğu / yatay (yaw) ekseni (deg/s).
    AxisConfig z{};     ///< NED Aşağı / dikey (pitch) ekseni (deg/s).
    AxisConfig yaw{};   ///< Yedek yaw ekseni; varsayılan devre dışı.
    bool enable_yaw_axis = false; ///< true ise eyaw ayrı yaw kanalından hesaplanır.
  };

  /// @brief Bir kontrol adımının hata girişleri.
  struct ErrorInput {
    bool valid = false; ///< Giriş geçerli mi (geçersizse kontrolcü sıfırlanır).
    float ex = 0.0f;    ///< NED Kuzey hatası (m, kapanma).
    float ey = 0.0f;    ///< NED Doğu açısal hatası (deg).
    float ez = 0.0f;    ///< NED Aşağı açısal hatası (deg).
    float eyaw = 0.0f;  ///< Ek yaw hatası (deg); enable_yaw_axis=true iken kullanılır.
  };

  /// @brief Kontrolcünün ürettiği hız/dönüş komutları.
  struct Output {
    bool valid = false;        ///< Çıkış geçerli mi (ilk adımda/atlamada false).
    float vx_cmd = 0.0f;       ///< Kuzey/kapanma komutu (m/s).
    float vy_cmd = 0.0f;       ///< Doğu/yaw oran komutu (deg/s).
    float vz_cmd = 0.0f;       ///< Aşağı/pitch oran komutu (deg/s); negatif = tırman.
    float yaw_rate_cmd = 0.0f; ///< Yedek yaw oranı (deg/s); enable_yaw_axis=false iken 0.
  };

  /// @brief Tek eksen doygunluk/slew kırpma sayaçları (kümülatif).
  struct AxisDiagnostics {
    uint32_t output_saturated_ticks = 0; ///< Çıkış doygunluğuna kırpılan adım sayısı.
    uint32_t slew_clipped_ticks = 0;     ///< Slew-rate kırpması uygulanan adım sayısı.
  };

  /// @brief Dört eksen diagnostik özeti.
  struct Diagnostics {
    AxisDiagnostics x{};
    AxisDiagnostics y{};
    AxisDiagnostics z{};
    AxisDiagnostics yaw{};
  };

  /// @brief Verilen yapılandırma ile kontrolcü oluşturur.
  explicit PidControllerXYZYaw(const Config& cfg) : cfg_(cfg) {}
  /// @brief Varsayılan yapılandırma ile kontrolcü oluşturur.
  explicit PidControllerXYZYaw() = default;

  /// @brief Bir kontrol adımı çalıştırır ve hız komutlarını üretir.
  /// @param err Eksen hataları.
  /// @param now_tp Adımın zaman damgası (dt hesaplaması için).
  /// @return Üretilen hız komutları; ilk adımda veya geçersiz dt'de valid=false.
  Output Update(const ErrorInput& err, std::chrono::steady_clock::time_point now_tp);
  /// @brief Tüm eksen durumlarını, diagnostikleri ve zaman damgasını sıfırlar.
  void Reset();

  /// @brief Aktif yapılandırmanın kopyasını döndürür.
  Config GetConfig() const;
  /// @brief Yapılandırmayı günceller.
  void SetConfig(const Config& cfg);
  /// @brief Kümülatif doygunluk/slew diagnostiklerini döndürür.
  Diagnostics GetDiagnostics() const;

 private:
  /// @brief Tek bir eksenin çalışma zamanı durumu.
  struct AxisState {
    float integral = 0.0f;     ///< Birikmiş integral terimi.
    float prev_error = 0.0f;   ///< Bir önceki adımın hatası (türev için).
    float prev_output = 0.0f;  ///< Bir önceki adımın çıkışı (slew sınırı için).
  };

  /// @brief Tek eksen PID adımının çıkışı ve kırpma bayrakları.
  struct AxisStepResult {
    float output = 0.0f;
    bool output_saturated = false;
    bool slew_clipped = false;
  };

  /// @brief Tek bir eksen için PID çıktısını hesaplar (anti-windup + slew dahil).
  static AxisStepResult UpdateAxis(const AxisConfig& cfg, AxisState* st, float error, float dt);
  /// @brief Tüm durumu sıfırlar; çağrıldığında @c state_mutex_ kilitli olmalı.
  void ResetLocked();
  /// @brief Tek eksen diagnostik sayacını artırır.
  static void AccumulateAxisDiagnostics(AxisDiagnostics* diag, const AxisStepResult& step);

  Config cfg_{};      ///< Aktif yapılandırma.
  AxisState x_;         ///< Kuzey ekseni durumu.
  AxisState y_;         ///< Doğu ekseni durumu.
  AxisState z_;         ///< Aşağı ekseni durumu.
  AxisState yaw_;       ///< Yedek yaw ekseni durumu.
  Diagnostics diagnostics_{}; ///< Kümülatif kırpma sayaçları.
  std::chrono::steady_clock::time_point last_tp_{}; ///< Son adım zaman damgası.
  mutable std::mutex state_mutex_;                  ///< Tüm durumu koruyan kilit.
};

}  // namespace savasan::control

#endif  // SAVASAN_CONTROL_PID_CONTROLLER_HPP_
