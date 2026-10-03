/**
 * @file pursuit_guidance.hpp
 * @brief Manevra yapan rakip için nişan noktası — MSO `takip_gudum.py` portu.
 *
 * Kestiriciden gelen "hedef şu an nerede" tahminini karta basılabilir bir
 * komuta çevirir. Çıktı bir kontrol yasası DEĞİL, hareketli bir NİŞAN
 * NOKTASIDIR: karta saniyede birkaç kez "şu koordinata, şu irtifada git"
 * diyebiliyoruz, doğrudan yatış/yunuslama veremiyoruz.
 *
 * Tasarımı belirleyen üç ölçüm:
 *  1. 34 m — Brio'nun en dar ayarında Talon'un 1.72 m kanadı ekranın %5'ini
 *     ancak bu mesafede kaplar. Yani çok yakın uçmak zorundayız.
 *  2. 3.1 sn — 34 m'de 11 m/s kapanmayla çarpışmaya kalan süre; kendi komut
 *     gecikmemiz 1-1.5 sn. Güdümün birinci işi kilitlenmek değil ÇARPIŞMAMAK.
 *  3. R = v²/(g·tan22°) — dönüş yarıçapımız hızın karesiyle büyür. "Yetişmek
 *     için gazla" sezgisi bu yüzden yanlıştır: hızlanan uçak geniş yay çizip
 *     dönen hedefin çemberinin dışına savrulur.
 *
 * @note GAZ: hesap MSO'daki gibi yapılır ve @ref PursuitCommand::throttle_pct
 * ile raporlanır, ancak karta GÖNDERİLMEZ — gaz otopilota bırakıldı. Değer
 * yalnızca log/CSV analizinde "gaz gönderseydik ne olurdu" sorusu içindir.
 *
 * @warning Davranış MSO Python'u ile eşleşmelidir; doğrulaması
 * `test_pursuit_guidance_golden` (fixtures/mso_takip_gudum_ref.csv).
 */
#ifndef SAVASAN_CONTROL_PURSUIT_GUIDANCE_HPP_
#define SAVASAN_CONTROL_PURSUIT_GUIDANCE_HPP_

#include <cstdint>

#include "control/local_frame.hpp"
#include "control/target_estimator.hpp"

namespace savasan::control {

/// Kamera (Logitech Brio, 65° diyagonal ayarı, 16:9).
inline constexpr double kFovHorizontalDeg = 58.0;
inline constexpr double kFovVerticalDeg = 34.7;
inline constexpr double kTargetWingspanM = 1.72;  ///< X-UAV Talon kanat açıklığı.

/// Vuruş alanı: yatayda ekranın ortadaki %50'si, dikeyde ortadaki %80'i.
inline constexpr double kHitAreaHalfHorizontalDeg = 0.25 * kFovHorizontalDeg;  // ±14.5°
inline constexpr double kHitAreaHalfVerticalDeg = 0.40 * kFovVerticalDeg;      // ±13.9°

/// Uçak fiziği (saha ölçümü: 96 m yarıçap @ 20 m/s → 22° yatış).
inline constexpr double kGravity = 9.81;
inline constexpr double kBankAngleDeg = 22.0;

/// @brief Sabit yatıştaki dönüş ivmesi (~3.96 m/s²).
double TurnAcceleration();

/// @brief Ölçülmüş gaz-hız eğrisini tersine çevirir: istenen hız → gaz yüzdesi.
double ThrottleForSpeed(double desired_speed_mps);

/// @brief Gaz yüzdesi → beklenen denge hava hızı (rüzgâr kestirimi için).
double SpeedForThrottle(double throttle_pct);

enum class PursuitStage : uint8_t {
  kNone = 0,      ///< Hedef yok / menzil dışı.
  kApproach = 1,  ///< YAKLASMA — kuyruğa yaklaşma aşaması.
  kTrack = 2,     ///< TAKIP — burun hedefte.
  kBreakaway = 3, ///< KOPMA — çarpışma önleme.
};

const char* PursuitStageName(PursuitStage s);

struct PursuitSettings {
  // --- Mesafe ---
  /// 28 m = %6 doluluk mesafesi. 24/26/28/30/32 tarandı; 28 en yüksek vuruşu
  /// verdi. 32'ye çıkmak düz uçuşta vuruşu %77'den %62'ye düşürüyor.
  double nominal_range_m = 28.0;
  double min_separation_m = 22.0;  ///< Şartname: altına inilmez.
  double breakaway_range_m = 18.0; ///< Altı acil kopma.
  /// 900 m: 400 m'de takibi bırakınca kart 3 sn sonra tabana dönüp hedefi
  /// kalıcı kaybediyordu.
  double max_track_m = 900.0;
  double approach_threshold_m = 120.0;

  // --- Dikey ---
  /// Hedefin ALTINDA uçarız: kaybedersek çarpışma riski düşer. Ofset menzille
  /// ölçekli, yani kamera çerçevesinde sabit açı kaplar; sabit metre ofset
  /// 20 m menzilde 14° yapıp ±13.9° pencereyi taşırıyordu.
  double vertical_hit_margin = 0.50;
  double vertical_offset_min_m = 3.0;
  double vertical_offset_max_m = 8.0;
  double altitude_tau_s = 2.0;
  double altitude_floor_m = 60.0;
  double altitude_ceiling_m = 180.0;

  // --- Gaz / hız (hesaplanır, karta gitmez) ---
  int throttle_min = 15;
  int throttle_max = 85;
  double speed_floor_mps = 12.5;
  double speed_ceiling_mps = 30.0;
  /// Karekök kapanma profili ivmesi. 3.0 S manevrasını 34 m'ye indiriyor ama
  /// düz uçuşu %55'e düşürüyor (çok sert kapanma → aşma → kopma).
  double deceleration_mps2 = 1.2;
  double turn_cap_margin = 1.25;
  double kappa = 2.0;  ///< Yarıçapımız hedefinkinin en çok bu katı.
  double maneuver_floor_dps = 3.0;
  double deviation_deadband_deg = 25.0;
  double deviation_tau_s = 2.0;  ///< Spiral sürekli sapmadır, gürültü değil.
  double drift_tau_s = 8.0;
  double wind_tau_s = 6.0;
  double wind_max_mps = 15.0;

  // --- Nişan ---
  double aim_lead_s = 0.8;  ///< Kendi komut gecikmemiz.
  /// İleri sarma tavanı = maneuver_heading_margin / gözlenen manevra.
  /// Sabit 10 sn ileri sarma dönen hedefte boş havaya nişandı.
  double maneuver_heading_margin_deg = 40.0;
  double maneuver_filter_gain = 0.35;
  /// Nişan yumuşatma: kestirimin konum hatası ~4 m ve kart nişana azami dönüş
  /// hızıyla döndüğü için bu titreme burnu sürekli sallıyor.
  double smoothing_s = 2.5;
  double maneuver_tau_ref = 4.0;

  // --- Pusu (izlenebilirlik menzili) ---
  /// Kilit iki şart ister: menzil ≤ 34 m VE burun hedefte. İkincisi aslında bir
  /// dönüş hızı şartıdır ve menzil küçüldükçe gereken hız büyür. Tutamayacağımız
  /// menzile inmek kilidi değil SPİRALİ getirir.
  double ambush_ceiling_m = 140.0;
  double ambush_margin = 1.0;

  // --- Emniyet ---
  double safety_horizon_s = 6.0;
  /// Kopma tetiği menzili bu kadar ileri sararak bakar; reaktif tetik 1-1.5 sn
  /// geç kalıyor ve 11 m/s kapanmada bu 15 m ediyor.
  double reaction_delay_s = 1.0;
  double breakaway_min_s = 1.0;
  int breakaway_throttle = 15;
  double breakaway_aim_m = 120.0;
  double breakaway_altitude_drop_m = 15.0;
};

/// @brief Kendi uçağımızın güdüme giren durumu.
struct PursuitOwnState {
  double x = 0.0;
  double y = 0.0;
  double alt_m = 0.0;
  double speed_mps = 16.0;
  bool has_track = false;
  double track_rad = 0.0;  ///< Matematiksel açı (x ekseninden, CCW).
  /// Rüzgâr telafisi YALNIZCA ölçülmüş hava hızı varsa çalışır; gazdan
  /// modellemek rüzgârsız havada profili bozuyor (ölçüm: %74 → %64).
  bool has_airspeed = false;
  double airspeed_mps = 0.0;
};

/// @brief Telemetriden hesaplanan kilit geometrisi (kamera görüntüsü kullanılmaz).
struct LockGeometry {
  bool valid = false;
  double range_m = 0.0;
  double fill_ratio = 0.0;  ///< Hedefin kapladığı eksen oranı (şartname: ≥%5).
  bool has_errors = false;  ///< Kendi track'imiz yoksa hata açıları hesaplanamaz.
  double horizontal_error_deg = 0.0;
  double vertical_error_deg = 0.0;
  bool in_hit_area = false;
};

struct PursuitCommand {
  bool valid = false;
  PursuitStage stage = PursuitStage::kNone;
  double aim_lat_deg = 0.0;
  double aim_lon_deg = 0.0;
  double aim_x = 0.0;
  double aim_y = 0.0;
  int altitude_m = 0;
  int pitch_deg = 0;
  /// @warning Karta GÖNDERİLMEZ; yalnızca log/CSV analizi için.
  int throttle_pct = 0;
  double range_m = 0.0;
  double aspect_deg = 0.0;
  double maneuver_dps = 0.0;
  bool safety_brake = false;
};

/// @brief Kestirilen hedef durumundan nişan komutu üretir.
class PursuitGuidance {
 public:
  PursuitGuidance() = default;
  explicit PursuitGuidance(const LocalFrame& frame,
                           const PursuitSettings& settings = PursuitSettings{})
      : frame_(frame), s_(settings) {}

  void Reset();

  /// @brief Kamera çerçevesinde hedef nerede ve ne kadar büyük?
  LockGeometry ComputeLockGeometry(const PursuitOwnState& own, const TargetEstimate& tgt) const;

  /// @brief Komut üretir. @p has_target false ise aşama kNone'a düşer.
  PursuitCommand Tick(double t_s, const PursuitOwnState& own, const TargetEstimate& tgt,
                      bool has_target);

  PursuitStage stage() const { return stage_; }
  double last_range_m() const { return last_range_m_; }
  double maneuver_dps() const { return maneuver_dps_; }

 private:
  bool BreakawayNeeded(double t_s, const PursuitOwnState& own, const TargetEstimate& tgt,
                       double dx, double dy, double range_m);
  void UpdateManeuver(double t_s, double heading_deg);
  /// @brief Kestirim titremesini keser; hedefi filtrelenmiş hâliyle döndürür.
  TargetEstimate SmoothTarget(double t_s, const TargetEstimate& tgt);
  double DriftSpeed(double t_s, const TargetEstimate& tgt);
  bool UpdateWind(double t_s, const PursuitOwnState& own);

  LocalFrame frame_{};
  PursuitSettings s_{};

  PursuitStage stage_ = PursuitStage::kNone;
  bool has_breakaway_end_ = false;
  double breakaway_end_t_ = 0.0;
  double last_range_m_ = 0.0;

  bool has_last_heading_ = false;
  double last_heading_deg_ = 0.0;
  double last_heading_t_ = 0.0;
  double maneuver_dps_ = 0.0;

  bool has_smooth_ = false;
  double sx_ = 0.0;
  double sy_ = 0.0;
  double svx_ = 0.0;
  double svy_ = 0.0;
  bool has_smooth_t_ = false;
  double smooth_t_ = 0.0;

  bool has_deviation_t_ = false;
  double deviation_filtered_rad_ = 0.0;
  double deviation_t_ = 0.0;

  bool has_alt_filter_ = false;
  double alt_filtered_m_ = 0.0;
  bool has_alt_filter_t_ = false;
  double alt_filter_t_ = 0.0;

  bool has_drift_ = false;
  double drift_vx_ = 0.0;
  double drift_vy_ = 0.0;
  bool has_drift_t_ = false;
  double drift_t_ = 0.0;

  double wind_x_ = 0.0;
  double wind_y_ = 0.0;
  bool has_wind_t_ = false;
  double wind_t_ = 0.0;
  bool has_last_own_pos_ = false;
  double last_own_x_ = 0.0;
  double last_own_y_ = 0.0;
};

/// @brief Kesintisiz kilitlenme süresini takip eder (şartname: 4 sn).
///
/// Burada GEOMETRİK kilit sayılır; resmi kilit kamerada doğrulanır.
class LockCounter {
 public:
  explicit LockCounter(const double required_s = 4.0, const double tolerance_s = 1.0)
      : required_s_(required_s), tolerance_s_(tolerance_s) {}

  /// @return Kesintisiz kilit süresi (sn); kilit yoksa 0.
  double Update(double t_s, bool in_hit_area);

  double longest_s() const { return longest_s_; }
  int completed() const { return completed_; }

 private:
  double required_s_ = 4.0;
  double tolerance_s_ = 1.0;
  bool has_start_ = false;
  double start_t_ = 0.0;
  bool has_last_valid_ = false;
  double last_valid_t_ = 0.0;
  double longest_s_ = 0.0;
  int completed_ = 0;
};

}  // namespace savasan::control

#endif  // SAVASAN_CONTROL_PURSUIT_GUIDANCE_HPP_
