/**
 * @file seyir_guidance.hpp
 * @brief Telemetri-öncelikli seyir güdüm faz makinesi + komut niyeti üretimi.
 *
 * Fazlar: ARAMA → YAKLASMA → GORSEL → KILIT | KACINMA
 *
 * Öncelik sırası (üstteki alttakini veto eder):
 *   1. Geofence kaçışı  — saha sınırı / HSS (ceza ve eleme sebebi)
 *   2. APF              — rakip itmesi (çarpışma)
 *   3. Nokta hedefi     — operatörün haritadan gönderdiği varış noktası
 *   4. Takip            — kestirim + güdüm + kilit bandı + görsel trim
 *
 * Operatör komutu emniyetin altında, görevin üstündedir: şartname §6.1.7 bu
 * komutu otonomiyi bozmayan komut sayar, ama hiçbir operatör komutu çarpışma
 * veya saha ihlali pahasına uygulanmaz.
 *
 * Görsel telemetriyi bypass etmez; yalnızca nişan noktasını kaydıran trim üretir.
 *
 * Bu başlık yalnızca NİYET üretir: her tikte bir koordinat (istem 2) ve bir irtifa
 * (istem 0) komutu hazırlanır. Bunlardan hangisinin gerçekten karta gideceğine
 * @ref seyir_command_scheduler.hpp karar verir — kart bir tikte tek paket bekler.
 *
 * Yön değişimi koordinatla yapılır; istem 1 (heading) vendor kaynağında hiç
 * uçmadığı için üretim yolunda kullanılmaz.
 *
 * Yatay güdüm zinciri MSO'nun `takip_kopru.py` sıralamasını izler:
 * @ref control::TargetEstimator (gecikme telafisi) → @ref control::PursuitGuidance
 * (nişan noktası) → kilit bandı koruması → görsel trim. Tek kare "lead pursuit"
 * yaklaşımı emekliye ayrıldı: manevra yapan hedefte sabit hız varsayımı savruluyordu.
 */
#ifndef SAVASAN_RUNNERS_SEYIR_GUIDANCE_HPP_
#define SAVASAN_RUNNERS_SEYIR_GUIDANCE_HPP_

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>

#include "autopilot/seyir_mode_commands.hpp"
#include "competition/siha_types.hpp"
#include "control/local_frame.hpp"
#include "control/pursuit_guidance.hpp"
#include "control/target_estimator.hpp"
#include "evasion/geo_kinematics.hpp"
#include "evasion/geofence.hpp"
#include "evasion/potential_field_evasion.hpp"
#include "runners/rival_telemetry_file.hpp"

namespace savasan::runners {

enum class SeyirPhase : uint8_t {
  kArama = 0,
  kYaklasma = 1,
  kGorsel = 2,
  kKilit = 3,
  kKacinma = 4,
};

inline bool EnvTelemOnlyGuidance() {
  const char* e = std::getenv("SAVASAN_SEYIR_TELEM_ONLY");
  return e != nullptr && (e[0] == '1' || (e[0] == 't' && e[1] == 'r'));
}

inline const char* SeyirPhaseName(const SeyirPhase p) {
  switch (p) {
    case SeyirPhase::kYaklasma:
      return "YAKLASMA";
    case SeyirPhase::kGorsel:
      return "GORSEL";
    case SeyirPhase::kKilit:
      return "KILIT";
    case SeyirPhase::kKacinma:
      return "KACINMA";
    default:
      return "ARAMA";
  }
}

struct SeyirGuidanceConfig {
  bool enabled = false;
  /// Vendor uçan kodu int32 mikro-derece yazar; float32 yalnızca PDF örneğidir.
  autopilot::seyir::LatLonWireFormat coord_format =
      autopilot::seyir::LatLonWireFormat::kInt32MicroDegrees;
  autopilot::seyir::Cmd2TailVariant cmd2_tail = autopilot::seyir::Cmd2TailVariant::kTailAt12;
  evasion::PotentialFieldConfig apf_config{};
  float own_cruise_speed_mps = 20.0f;
  float rival_stale_max_s = 3.0f;
  float approach_enter_m = 800.0f;
  float visual_enter_m = 120.0f;       ///< Telemetri menzili (opsiyonel gate)
  float visual_bbox_min = 0.04f;       ///< Normalize bbox kısa kenar (~%4, şartname %5 altı)
  int visual_confirm_frames = 3;
  int default_pitch_deg = 8;
  float heading_kp = 1.0f;             ///< Görsel nx hatası (norm) → nişan kaydırma kazancı
  float vision_fov_deg = 60.0f;        ///< Yatay görüş açısı varsayımı (nx → derece)
  float vision_trim_max_deg = 25.0f;   ///< Görsel trim'in nişanı kaydırabileceği üst sınır
  float vision_trim_min_range_m = 80.0f;  ///< Çok yakında nişan noktası çökmesin
  float altitude_kp_m = 30.0f;         ///< Görsel ny hatası → Δirtifa (m)
  float min_alt_m = 35.0f;             ///< Komut irtifası alt sınırı
  float max_alt_m = 500.0f;            ///< Komut irtifası üst sınırı (yarışma bandı)
  float apf_escape_lead_m = 300.0f;    ///< Kaçış yönünde nişan noktası mesafesi
  int last_locked_exclude_id = -1;     ///< Art arda aynı hedefe kilit yasağı

  control::TargetEstimatorConfig estimator{};
  control::PursuitSettings pursuit{};
  evasion::GeofenceSettings geofence{};

  /// @name Kilit bandı koruması (MSO `takip_kopru.py`)
  /// Bu mesafenin altına inilince nişan noktası yana kırılır: hedef görüş
  /// konisinde kalır ama burun üstüne dikilmez, mesafe kendiliğinden açılır.
  ///
  /// MSO 70 m kullanır; bizde 25 m. MSO'nun ölçümü üreticinin ham takip
  /// algoritmasıyla yapıldı (öngörülü fren yoktu, rakibe 0-30 m'ye dalıyordu),
  /// 70 m orada kaba bir emniyet kemeriydi. Bizim portumuzda o fren var.
  /// Ayrıca 34 m üstündeki her değer kilidi imkânsız kılar — hedef ekranın
  /// %5'ini ancak 34 m'de kaplar. 25 m kilit penceresini (28-34 m) serbest
  /// bırakıp KOPMA'dan (18 m) önce devreye giren yumuşak katman olur.
  /// @{
  bool band_guard_enabled = true;
  float band_min_separation_m = 25.0f;
  float band_break_angle_deg = 25.0f;
  /// @}

  /// @name Operatör nokta hedefi (RF 0x2A)
  /// Şartname §6.1.7: "Yer Kontrol Bilgisayarı üzerinden araca komut göndermek
  /// (varış noktası tanımlama, irtifasını değiştirme...) otonomiyi bozmaz ve
  /// manuel moda geçiş olarak değerlendirilmez." Bu yüzden nokta hedefi
  /// takibin önüne geçebilir; ancak emniyet katmanlarının (geofence, APF)
  /// önüne geçemez — operatör komutu çarpışma önlemenin üstünde değildir.
  /// @{
  bool goto_enabled = true;
  /// Bu yarıçapa girilince nokta düşer ve uçak normal göreve döner. Sabit
  /// kanat uçak noktanın tam üstünden geçemez; yarıçap dönüş çapı mertebesinde
  /// olmalı, yoksa uçak noktanın etrafında sonsuz tur atar.
  float goto_arrive_radius_m = 40.0f;
  /// @}
};

struct SeyirGuidanceInput {
  /// Monoton saat, saniye — @c steady_clock epoch'undan. Rakip ölçümlerinin
  /// @c recv_tp damgasıyla aynı tabanda olmalı, kestirici ikisini karşılaştırır.
  double t_s = 0.0;

  double own_lat = 0.0;
  double own_lon = 0.0;
  float own_alt_m = 0.0f;
  float own_yaw_deg = 0.0f;
  float own_speed_mps = 20.0f;
  bool gps_valid = false;
  /// Gidiş yönü geçerli mi? Güdümün enerji kapağı ve öngörülü çarpışma
  /// emniyeti buna dayanır. MSO ölçümü (saglamlik.py): track yokken sert
  /// kaçışta asgari ayrım 10 m'den 3 m'ye düşüyor — yani takip emniyetsiz.
  bool own_yaw_valid = true;

  RivalPoolSnapshot rivals{};
  std::vector<savasan::competition::HssKoordinatBilgisi> hss_zones;
  /// Saha sınırı köşeleri (RF/sunucu). Boşsa geofence kısıt uygulayamaz.
  BoundarySnapshot boundary{};
  std::vector<evasion::ThreatPrediction> apf_threats;

  bool vision_lock_valid = false;
  float vision_nx = 0.5f;  ///< [0,1] hedef merkez
  float vision_ny = 0.5f;
  float vision_bbox_w = 0.0f;
  float vision_bbox_h = 0.0f;
  int vision_streak = 0;  ///< Ardışık görsel kare
  bool lock_timer_active = false;  ///< 4 sn AV kilidi sürüyor

  /// Operatörün haritadan gönderdiği varış noktası (RF 0x2A).
  GotoTargetSnapshot goto_target{};
};

struct SeyirGuidanceOutput {
  bool active = false;
  SeyirPhase phase = SeyirPhase::kArama;
  int selected_target_id = -1;
  /// Yatay niyet: koordinata yönel (istem 2). Görsel trim bu noktayı kaydırır.
  autopilot::SeyirModePacket coord_cmd{};
  bool has_coord = false;
  /// Dikey niyet: irtifa değiştir (istem 0).
  autopilot::SeyirModePacket alt_cmd{};
  bool has_alt = false;
  int alt_cmd_m = 0;  ///< Zamanlayıcının "irtifa değişti mi" karşılaştırması için.
  bool vision_trim_active = false;
  float aim_range_m = 0.0f;
  float aim_bearing_deg = 0.0f;
  float stale_s = 0.0f;
  double rival_rx_lat_deg = 0.0;  ///< Ham (gecikmeli) rakip raporu.
  double rival_rx_lon_deg = 0.0;
  double rival_dr_lat_deg = 0.0;  ///< Kestirilen "şu an" konumu.
  double rival_dr_lon_deg = 0.0;
  double aim_lat_deg = 0.0;
  double aim_lon_deg = 0.0;
  evasion::ApfForceBreakdown apf_forces{};

  /// @name Takip güdümü raporu (log/telemetri; komut üretimini etkilemez)
  /// @{
  control::PursuitStage pursuit_stage = control::PursuitStage::kNone;
  bool safety_brake = false;    ///< Öngörülü çarpışma freni devrede.
  bool band_guard_active = false;  ///< Nişan kilit bandı için yana kırıldı.
  /// @warning Karta GÖNDERİLMEZ — gaz otopilota bırakıldı. Yalnızca
  /// "gaz gönderseydik ne olurdu" analizi için.
  int desired_throttle_pct = 0;
  float maneuver_dps = 0.0f;    ///< Hedefin gözlenen dönüş şiddeti.
  /// Telemetriden hesaplanan geometrik kilit (kamera kilidinden bağımsız).
  bool geom_lock_valid = false;
  float geom_fill_ratio = 0.0f;
  bool geom_in_hit_area = false;
  float geom_lock_s = 0.0f;
  /// @}

  /// @name Geofence
  /// @{
  evasion::GeofenceViolation geofence_reason = evasion::GeofenceViolation::kNone;
  int geofence_hss_id = -1;
  /// Takip nişanı sınır/HSS süzgecine takıldı: o tik komut üretilmez.
  bool aim_rejected_by_geofence = false;
  /// @}

  /// @name Operatör nokta hedefi
  /// @{
  bool goto_active = false;  ///< Bu tikin komutu nokta hedefine gidiyor.
  GotoState goto_state = GotoState::kIdle;
  GotoRejectReason goto_reason = GotoRejectReason::kNone;
  int goto_seq = 0;
  float goto_distance_m = 0.0f;
  /// @}
};

/// @brief Tikler arası taşınan güdüm durumu (kestirici + güdüm filtreleri).
///
/// @ref ComputeSeyirGuidance durumsuz değildir: kestirim de nişan yumuşatma da
/// geçmişe bakar. Sahibi @ref Phase5Runtime, ömrü uçuş boyudur.
struct SeyirGuidanceState {
  bool frame_ready = false;
  control::LocalFrame frame{};
  control::TargetEstimator estimator{};
  control::PursuitGuidance pursuit{};
  control::LockCounter lock_counter{};
  evasion::Geofence geofence{};
  int tracked_target_id = -1;
  bool has_last_meas_t = false;
  double last_meas_t_s = 0.0;

  /// @name Operatör nokta hedefi durumu
  /// Sonuç (varıldı/reddedildi) yeni bir nokta gelene kadar korunur: operatör
  /// arayüzü sonucu kaçırmasın diye durum anlık değil kalıcıdır.
  /// @{
  int goto_seq_seen = -1;  ///< Bu sıra numarası zaten değerlendirildi.
  bool goto_engaged = false;
  GotoState goto_state = GotoState::kIdle;
  GotoRejectReason goto_reason = GotoRejectReason::kNone;
  /// @}

  /// @brief Referans noktasını kendi ilk geçerli konumumuza sabitler.
  ///
  /// Yerel düzlem projeksiyonu referanstan uzaklaştıkça bozulur; kalkış
  /// noktası saha ölçeğinde (birkaç km) yeterince yakındır.
  void EnsureFrame(const double lat, const double lon, const SeyirGuidanceConfig& cfg) {
    if (frame_ready) {
      return;
    }
    frame = control::LocalFrame(lat, lon);
    estimator = control::TargetEstimator(frame, cfg.estimator);
    pursuit = control::PursuitGuidance(frame, cfg.pursuit);
    geofence = evasion::Geofence(frame, cfg.geofence);
    frame_ready = true;
  }

  /// @brief Saha verisini yerel düzleme aktarır (her tik, veri küçüktür).
  void RefreshGeofenceData(
      const BoundarySnapshot& boundary,
      const std::vector<savasan::competition::HssKoordinatBilgisi>& hss_zones) {
    std::vector<control::GeoPoint> corners;
    corners.reserve(boundary.corners.size());
    for (const auto& c : boundary.corners) {
      corners.push_back(control::GeoPoint{c.lat_deg, c.lon_deg});
    }
    geofence.SetBoundaryLatLon(corners);

    std::vector<evasion::HssDisc> discs;
    discs.reserve(hss_zones.size());
    for (const auto& z : hss_zones) {
      // SetHssLatLon girişte x=enlem, y=boylam bekler.
      discs.push_back(evasion::HssDisc{z.id, z.hss_enlem, z.hss_boylam, z.hss_yaricap});
    }
    geofence.SetHssLatLon(discs);
  }

  /// @brief Hedef değişti/kayboldu: kestirim ve nişan geçmişini at.
  void DropTarget() {
    estimator.Reset();
    pursuit.Reset();
    tracked_target_id = -1;
    has_last_meas_t = false;
  }
};

/// @brief RF rakip havuzunu APF tehdit listesine dönüştürür.
/// @param exclude_team_no Kovalanan hedef; negatifse hiçbiri elenmez.
inline std::vector<evasion::ThreatPrediction> RivalsToThreatPredictions(
    const evasion::PotentialFieldConfig& apf_cfg, const RivalPoolSnapshot& pool,
    const double own_lat, const double own_lon, const int exclude_team_no = -1) {
  std::vector<evasion::ThreatPrediction> out;
  for (const auto& t : pool.targets) {
    if (!t.valid) {
      continue;
    }
    if (exclude_team_no >= 0 && t.target_id == exclude_team_no) {
      continue;
    }
    evasion::ThreatPrediction tp{};
    tp.team_no = t.target_id;
    tp.lat_deg = t.lat_deg;
    tp.lon_deg = t.lon_deg;
    tp.distance_m = evasion::HaversineDistanceM(own_lat, own_lon, t.lat_deg, t.lon_deg);
    tp.in_range = tp.distance_m < apf_cfg.d0_m;
    tp.stale = false;
    out.push_back(tp);
  }
  return out;
}

inline RivalTargetSnapshot SelectBestRival(const SeyirGuidanceConfig& cfg,
                                           const RivalPoolSnapshot& pool, const double own_lat,
                                           const double own_lon) {
  RivalTargetSnapshot best{};
  float best_d = std::numeric_limits<float>::max();
  for (const auto& t : pool.targets) {
    if (!t.valid) {
      continue;
    }
    if (cfg.last_locked_exclude_id >= 0 && t.target_id == cfg.last_locked_exclude_id) {
      continue;
    }
    const float age_s =
        t.recv_tp.time_since_epoch().count() > 0
            ? std::chrono::duration<float>(std::chrono::steady_clock::now() - t.recv_tp).count()
            : static_cast<float>(std::max(0, t.time_diff_ms)) / 1000.0f;
    if (age_s > cfg.rival_stale_max_s && t.time_diff_ms > static_cast<int>(cfg.rival_stale_max_s * 1000.0f)) {
      // Hem recv_tp hem time_diff ile kaba stale elemesi.
      if (age_s > cfg.rival_stale_max_s * 2.0f) {
        continue;
      }
    }
    const float d = evasion::HaversineDistanceM(own_lat, own_lon, t.lat_deg, t.lon_deg);
    if (d < best_d) {
      best_d = d;
      best = t;
    }
  }
  return best;
}

/// @brief APF kaçış niyeti üretir.
///
/// @param chased_team_no Kovaladığımız hedef APF'den muaf tutulur. Aynı uçaktan
/// hem kaçıp hem onu kovalayamayız: itme yarıçapı kilit mesafesinden (34 m)
/// büyük olduğu için muafiyet olmadan hedefe hiç yaklaşılamaz, kilit puanı
/// alınamaz. Muaf tutulan hedefle çarpışmayı takip güdümünün kendi katmanları
/// önler: nişan 28 m geriye kurulur, 18 m'de KOPMA, ayrıca 6 sn ileriye bakan
/// öngörülü fren (reaktif fren 1-1.5 sn komut gecikmemizde geç kalıyor).
inline bool ComputeApfSeyirEscape(const SeyirGuidanceConfig& cfg, const SeyirGuidanceInput& in,
                                  const int chased_team_no, SeyirGuidanceOutput* out) {
  if (out == nullptr || !in.gps_valid) {
    return false;
  }
  const auto threats = RivalsToThreatPredictions(cfg.apf_config, in.rivals, in.own_lat,
                                                 in.own_lon, chased_team_no);
  // HSS artık APF'de değil: yasak bölge yönetimi @ref evasion::Geofence'e
  // devredildi (poligon sınır + rota kontrolü + dönüş yarıçapına göre önleyici
  // bant). APF yalnızca rakip itmesi yapar.
  const auto apf = evasion::ComputeApfSetpoint(cfg.apf_config, in.own_lat, in.own_lon,
                                               in.own_yaw_deg, in.own_alt_m, threats);
  if (!apf.active) {
    return false;
  }
  out->active = true;
  out->phase = SeyirPhase::kKacinma;
  out->apf_forces = apf.forces;

  // Kaçış yönü koordinata çevrilir: istem 1 (heading) hiç uçmadığı için yön
  // değişimini kaçış vektörü üzerindeki bir nişan noktasıyla veriyoruz.
  const auto escape_pt = evasion::OffsetLatLon(in.own_lat, in.own_lon, apf.escape_heading_deg,
                                               cfg.apf_escape_lead_m);
  out->aim_lat_deg = escape_pt.lat_deg;
  out->aim_lon_deg = escape_pt.lon_deg;
  out->aim_bearing_deg = apf.escape_heading_deg;
  out->aim_range_m = cfg.apf_escape_lead_m;

  auto c = autopilot::seyir::PackCoordinateSteer(escape_pt.lat_deg, escape_pt.lon_deg,
                                                 cfg.coord_format, cfg.cmd2_tail);
  out->coord_cmd = autopilot::seyir::ToSeyirModePacket(c);
  out->has_coord = out->coord_cmd.valid;

  out->alt_cmd_m = static_cast<int>(
      std::lround(std::max(cfg.min_alt_m, static_cast<float>(apf.escape_alt_m))));
  auto a = autopilot::seyir::PackAltitudeChange(cfg.default_pitch_deg, out->alt_cmd_m);
  out->alt_cmd = autopilot::seyir::ToSeyirModePacket(a);
  out->has_alt = out->alt_cmd.valid;
  return true;
}

inline bool VisionCenteringEligible(const SeyirGuidanceConfig& cfg, const SeyirGuidanceInput& in) {
  if (EnvTelemOnlyGuidance()) {
    return false;
  }
  const bool vision_ok = in.vision_lock_valid &&
                         std::max(in.vision_bbox_w, in.vision_bbox_h) >= cfg.visual_bbox_min &&
                         in.vision_streak >= cfg.visual_confirm_frames;
  return vision_ok || in.lock_timer_active;
}

/// @brief Görsel nx/ny hatasından nişan noktası trim'i (telemetri nişanının üzerine).
///
/// Yatay hata heading istemine değil, nişan noktasının yönüne uygulanır: kendi
/// konumumuzdan, düzeltilmiş kerteriz ile aynı menzilde yeni bir nokta kurulur.
/// Böylece kamera telemetriyi bypass etmeden merkeze çeker.
inline void ApplyVisualCenteringTrim(const SeyirGuidanceConfig& cfg, const SeyirGuidanceInput& in,
                                     const int telem_alt_cmd, SeyirGuidanceOutput* out) {
  if (out == nullptr || !VisionCenteringEligible(cfg, in)) {
    return;
  }
  const float nx_err = in.vision_nx - 0.5f;  // sağ +
  const float ny_err = in.vision_ny - 0.5f;  // aşağı +

  const float trim_deg = std::clamp(cfg.heading_kp * nx_err * cfg.vision_fov_deg,
                                    -cfg.vision_trim_max_deg, cfg.vision_trim_max_deg);
  const float trimmed_bearing = out->aim_bearing_deg + trim_deg;
  const float trim_range_m = std::max(cfg.vision_trim_min_range_m, out->aim_range_m);
  const auto pt =
      evasion::OffsetLatLon(in.own_lat, in.own_lon, trimmed_bearing, trim_range_m);
  auto c = autopilot::seyir::PackCoordinateSteer(pt.lat_deg, pt.lon_deg, cfg.coord_format,
                                                 cfg.cmd2_tail);
  if (c.valid) {
    out->coord_cmd = autopilot::seyir::ToSeyirModePacket(c);
    out->has_coord = out->coord_cmd.valid;
    out->aim_lat_deg = pt.lat_deg;
    out->aim_lon_deg = pt.lon_deg;
    out->aim_bearing_deg = trimmed_bearing;
  }

  const float d_alt = -cfg.altitude_kp_m * ny_err;  // hedef yukarı (ny<0.5) → tırman
  out->alt_cmd_m = static_cast<int>(std::lround(
      std::clamp(static_cast<float>(telem_alt_cmd) + d_alt, cfg.min_alt_m, cfg.max_alt_m)));
  auto a = autopilot::seyir::PackAltitudeChange(cfg.default_pitch_deg, out->alt_cmd_m);
  out->alt_cmd = autopilot::seyir::ToSeyirModePacket(a);
  out->has_alt = out->alt_cmd.valid;

  out->vision_trim_active = true;
  out->phase = in.lock_timer_active ? SeyirPhase::kKilit : SeyirPhase::kGorsel;
}

/// @brief Matematiksel açı (x=doğu ekseninden CCW, radyan) → pusula derecesi.
inline float MathAngleToCompassDeg(const double angle_rad) {
  double deg = 90.0 - angle_rad * 180.0 / 3.14159265358979323846;
  deg = std::fmod(deg, 360.0);
  if (deg < 0.0) {
    deg += 360.0;
  }
  return static_cast<float>(deg);
}

/// @brief Rakip raporunun Jetson'a varış anı (kestiricinin zaman tabanında).
inline double RivalRecvTimeS(const RivalTargetSnapshot& r, const double fallback_t_s) {
  if (r.recv_tp.time_since_epoch().count() <= 0) {
    return fallback_t_s;
  }
  return std::chrono::duration<double>(r.recv_tp.time_since_epoch()).count();
}

/// @brief Nişan noktasını rakipten uzaklaştırır (kilit bandı koruması).
///
/// Kırılma açısı görüş konisinin içinde kalır: hedef burun hattından çıkmaz,
/// yalnızca mesafe açılır. @return Kırılma uygulandıysa true.
inline bool ApplyLockBandGuard(const SeyirGuidanceConfig& cfg, const double own_x,
                               const double own_y, const double tgt_x, const double tgt_y,
                               const control::LocalFrame& frame, SeyirGuidanceOutput* out) {
  if (!cfg.band_guard_enabled) {
    return false;
  }
  const double dx = tgt_x - own_x;
  const double dy = tgt_y - own_y;
  const double range_m = std::hypot(dx, dy);
  if (range_m <= 0.5 || range_m >= cfg.band_min_separation_m) {
    return false;
  }
  const double lead_m = std::max(cfg.band_min_separation_m * 2.0, range_m * 2.5);
  const double bearing_rad = std::atan2(dy, dx);
  const double break_rad = cfg.band_break_angle_deg * 3.14159265358979323846 / 180.0;
  const double a = bearing_rad + break_rad;
  const double nx = own_x + lead_m * std::cos(a);
  const double ny = own_y + lead_m * std::sin(a);
  const auto geo = frame.ToGeo(nx, ny);
  auto c = autopilot::seyir::PackCoordinateSteer(geo.lat_deg, geo.lon_deg, cfg.coord_format,
                                                 cfg.cmd2_tail);
  if (!c.valid) {
    return false;
  }
  out->coord_cmd = autopilot::seyir::ToSeyirModePacket(c);
  out->has_coord = out->coord_cmd.valid;
  out->aim_lat_deg = geo.lat_deg;
  out->aim_lon_deg = geo.lon_deg;
  out->aim_range_m = static_cast<float>(lead_m);
  out->aim_bearing_deg = MathAngleToCompassDeg(a);
  out->band_guard_active = true;
  return true;
}

/// @brief Operatörün gönderdiği noktayı değerlendirir ve gerekirse komut üretir.
///
/// İki ayrı iş yapar: yeni bir sıra numarası geldiyse noktayı bir kez süzgeçten
/// geçirip kabul/red kararı verir, aktif bir nokta varsa her tikte varış
/// kontrolü yapıp komutu üretir.
///
/// Süzgeç kabul anında çalışır, her tikte değil: sınır ve HSS verisi uçuş
/// sırasında değişebilir (hakem HSS açar), ama kabul edilmiş bir noktayı
/// sonradan iptal etmeye gerek yok — HSS'e yaklaşıldığında geofence kaçışı
/// zaten devreye girer ve komutu devralır.
///
/// @return Bu tikte nokta hedefi komutu üretildiyse true.
inline bool ComputeGotoSteer(const SeyirGuidanceConfig& cfg, const SeyirGuidanceInput& in,
                             const control::LocalPoint& own_local, SeyirGuidanceState* state,
                             SeyirGuidanceOutput* out) {
  if (!cfg.goto_enabled || state == nullptr || out == nullptr) {
    return false;
  }
  const auto& g = in.goto_target;

  // Operatör iptal etti (valid=0): aktif noktayı bırak, sonucu sıfırla.
  if (!g.valid) {
    if (state->goto_engaged) {
      state->goto_engaged = false;
      state->goto_state = GotoState::kIdle;
      state->goto_reason = GotoRejectReason::kNone;
    }
    state->goto_seq_seen = -1;
    out->goto_state = state->goto_state;
    out->goto_reason = state->goto_reason;
    return false;
  }

  const auto tgt = state->frame.ToLocal(g.lat_deg, g.lon_deg);

  if (g.seq != state->goto_seq_seen) {
    state->goto_seq_seen = g.seq;
    state->goto_engaged = false;
    state->goto_reason = GotoRejectReason::kNone;

    if (!in.gps_valid) {
      state->goto_state = GotoState::kRejected;
      state->goto_reason = GotoRejectReason::kNoGpsFix;
    } else if (!state->geofence.has_boundary()) {
      // Sınır bilinmeden noktayı doğrulayamayız. Körlemesine uçmak yerine
      // reddediyoruz: saha dışına çıkmak -200 puan ve 10 sn'de eleme.
      state->goto_state = GotoState::kRejected;
      state->goto_reason = GotoRejectReason::kNoBoundaryData;
    } else if (!state->geofence.PointSafe(tgt.x, tgt.y)) {
      int hss_id = -1;
      const auto v = state->geofence.Violation(tgt.x, tgt.y, &hss_id);
      state->goto_state = GotoState::kRejected;
      state->goto_reason = (v == evasion::GeofenceViolation::kInsideHss ||
                            v == evasion::GeofenceViolation::kNearHss)
                               ? GotoRejectReason::kInsideHss
                               : GotoRejectReason::kOutsideBoundary;
      out->geofence_hss_id = hss_id;
    } else if (!state->geofence.RouteSafe(own_local.x, own_local.y, tgt.x, tgt.y)) {
      state->goto_state = GotoState::kRejected;
      state->goto_reason = GotoRejectReason::kRouteBlocked;
    } else {
      state->goto_state = GotoState::kActive;
      state->goto_engaged = true;
    }
  }

  out->goto_seq = g.seq;
  out->goto_state = state->goto_state;
  out->goto_reason = state->goto_reason;

  if (!state->goto_engaged) {
    return false;
  }

  const double dist_m = std::hypot(tgt.x - own_local.x, tgt.y - own_local.y);
  out->goto_distance_m = static_cast<float>(dist_m);
  if (dist_m <= cfg.goto_arrive_radius_m) {
    state->goto_engaged = false;
    state->goto_state = GotoState::kArrived;
    out->goto_state = GotoState::kArrived;
    return false;
  }

  auto c = autopilot::seyir::PackCoordinateSteer(g.lat_deg, g.lon_deg, cfg.coord_format,
                                                 cfg.cmd2_tail);
  if (!c.valid) {
    return false;
  }
  out->active = true;
  out->goto_active = true;
  out->phase = SeyirPhase::kArama;
  out->coord_cmd = autopilot::seyir::ToSeyirModePacket(c);
  out->has_coord = out->coord_cmd.valid;
  out->aim_lat_deg = g.lat_deg;
  out->aim_lon_deg = g.lon_deg;
  out->aim_range_m = static_cast<float>(dist_m);
  out->aim_bearing_deg =
      MathAngleToCompassDeg(std::atan2(tgt.y - own_local.y, tgt.x - own_local.x));

  // İrtifa: operatör istediyse onunki, istemediyse mevcut irtifayı koru.
  const float alt_src = g.has_alt ? g.alt_m : in.own_alt_m;
  out->alt_cmd_m =
      static_cast<int>(std::lround(std::clamp(alt_src, cfg.min_alt_m, cfg.max_alt_m)));
  auto a = autopilot::seyir::PackAltitudeChange(cfg.default_pitch_deg, out->alt_cmd_m);
  out->alt_cmd = autopilot::seyir::ToSeyirModePacket(a);
  out->has_alt = out->alt_cmd.valid;
  return true;
}

inline SeyirGuidanceOutput ComputeSeyirGuidance(const SeyirGuidanceConfig& cfg,
                                                const SeyirGuidanceInput& in,
                                                SeyirGuidanceState* state) {
  SeyirGuidanceOutput out{};
  if (!cfg.enabled || !in.gps_valid || state == nullptr) {
    return out;
  }

  state->EnsureFrame(in.own_lat, in.own_lon, cfg);
  state->RefreshGeofenceData(in.boundary, in.hss_zones);

  // Nokta hedefi durumu her yolda raporlanır: güdüm emniyet nedeniyle erken
  // dönse bile operatör noktasının akıbetini görmeye devam etmeli.
  out.goto_state = state->goto_state;
  out.goto_reason = state->goto_reason;
  out.goto_seq = state->goto_seq_seen >= 0 ? state->goto_seq_seen : 0;

  const auto own_local0 = state->frame.ToLocal(in.own_lat, in.own_lon);
  const double own_track_rad = (90.0 - in.own_yaw_deg) * 3.14159265358979323846 / 180.0;

  // 0) Geofence — sınır ve HSS her şeyin önünde gelir. Saha dışına çıkmak ya
  //    da HSS'e girmek doğrudan cezadır; rakip kaçışı bile bunu bekleyemez.
  {
    const auto esc = state->geofence.EvaluateEscape(own_local0.x, own_local0.y, in.own_speed_mps,
                                                    in.own_yaw_valid, own_track_rad);
    out.geofence_reason = esc.reason;
    out.geofence_hss_id = esc.hss_id;
    if (esc.needed && esc.has_target) {
      auto c = autopilot::seyir::PackCoordinateSteer(esc.target_lat_deg, esc.target_lon_deg,
                                                     cfg.coord_format, cfg.cmd2_tail);
      if (c.valid) {
        out.active = true;
        out.phase = SeyirPhase::kKacinma;
        out.coord_cmd = autopilot::seyir::ToSeyirModePacket(c);
        out.has_coord = out.coord_cmd.valid;
        out.aim_lat_deg = esc.target_lat_deg;
        out.aim_lon_deg = esc.target_lon_deg;
        out.aim_range_m = static_cast<float>(
            std::hypot(esc.target_x - own_local0.x, esc.target_y - own_local0.y));
        out.aim_bearing_deg = MathAngleToCompassDeg(
            std::atan2(esc.target_y - own_local0.y, esc.target_x - own_local0.x));
        out.alt_cmd_m = static_cast<int>(
            std::lround(std::clamp(in.own_alt_m, cfg.min_alt_m, cfg.max_alt_m)));
        auto a = autopilot::seyir::PackAltitudeChange(cfg.default_pitch_deg, out.alt_cmd_m);
        out.alt_cmd = autopilot::seyir::ToSeyirModePacket(a);
        out.has_alt = out.alt_cmd.valid;
        return out;
      }
    }
  }

  // 1) Hedef seçimi — APF muafiyeti için kovalanan hedef önce bilinmeli.
  const auto rival = SelectBestRival(cfg, in.rivals, in.own_lat, in.own_lon);
  if (!rival.valid) {
    state->DropTarget();
  } else {
    if (state->tracked_target_id != rival.target_id) {
      state->DropTarget();
      state->tracked_target_id = rival.target_id;
    }
    // Kestirici kaçış sırasında da beslenir: APF bittiğinde takip bayat bir
    // kestirimle değil, kaldığı yerden devam etsin.
    const double t_recv_s = RivalRecvTimeS(rival, in.t_s);
    if (state->estimator.AddMeasurement(rival.lat_deg, rival.lon_deg, rival.alt_m, rival.spd_mps,
                                        rival.hdg_deg, rival.time_diff_ms, t_recv_s,
                                        rival.target_id)) {
      state->has_last_meas_t = true;
      state->last_meas_t_s = t_recv_s;
    }
  }

  // 2) APF kaçınma — rakip itmesi (kovalanan hedef muaf)
  if (ComputeApfSeyirEscape(cfg, in, rival.valid ? rival.target_id : -1, &out)) {
    return out;
  }

  // 3) Operatör nokta hedefi — takibin ÜSTÜNDE, emniyetin ALTINDA.
  //    Operatör haritadan bir nokta gönderdiyse bilinçli bir karar vermiştir;
  //    takip ona göre ikincildir. Ama emniyet (geofence, APF) operatörün de
  //    üstündedir. Nokta düşünce takip kaldığı yerden devam eder — kestirici
  //    bu sırada beslenmeye devam ettiği için bayat veriyle başlamaz.
  if (ComputeGotoSteer(cfg, in, own_local0, state, &out)) {
    return out;
  }

  if (!rival.valid) {
    out.phase = SeyirPhase::kArama;
    return out;
  }
  out.selected_target_id = rival.target_id;
  out.stale_s = static_cast<float>(std::max(0, rival.time_diff_ms)) / 1000.0f;
  out.rival_rx_lat_deg = rival.lat_deg;
  out.rival_rx_lon_deg = rival.lon_deg;

  // Ölçüm akışı kesildiyse kestirim uydurmaya başlar; takibi bırakmak daha güvenli.
  if (!state->has_last_meas_t || in.t_s - state->last_meas_t_s > cfg.rival_stale_max_s) {
    out.phase = SeyirPhase::kArama;
    return out;
  }

  control::TargetEstimate est{};
  if (!state->estimator.Predict(in.t_s, 0.0, &est)) {
    out.phase = SeyirPhase::kArama;
    return out;
  }
  out.rival_dr_lat_deg = est.lat_deg;
  out.rival_dr_lon_deg = est.lon_deg;

  // 4) Takip güdümü — nişan noktası
  control::PursuitOwnState own{};
  own.x = own_local0.x;
  own.y = own_local0.y;
  own.alt_m = in.own_alt_m;
  own.speed_mps = std::max(cfg.own_cruise_speed_mps, in.own_speed_mps);
  own.has_track = in.own_yaw_valid;
  own.track_rad = own_track_rad;

  const auto geom = state->pursuit.ComputeLockGeometry(own, est);
  out.geom_lock_valid = geom.valid;
  out.geom_fill_ratio = static_cast<float>(geom.fill_ratio);
  out.geom_in_hit_area = geom.in_hit_area;
  out.geom_lock_s = static_cast<float>(state->lock_counter.Update(in.t_s, geom.in_hit_area));

  // TRACK ZORUNLU: güdümün enerji kapağı da öngörülü çarpışma emniyeti de
  // gidiş yönüne dayanır. Track yoksa takip emniyetli değildir, arama fazında kal.
  if (!in.own_yaw_valid) {
    out.phase = SeyirPhase::kArama;
    return out;
  }

  const auto cmd = state->pursuit.Tick(in.t_s, own, est, true);
  if (!cmd.valid) {
    out.phase = SeyirPhase::kArama;
    return out;
  }

  // Güdüm rakibi kovalarken bizi saha dışına ya da HSS'e sokabilir. Nişan
  // noktası ve oraya giden yol geofence süzgecinden geçmek zorunda; geçmezse
  // bu tikte komut üretilmez. Durum kalıcıysa uçak önleyici banda girer ve
  // yukarıdaki geofence kaçışı zaten devralır.
  if (state->geofence.has_boundary() &&
      (!state->geofence.PointSafe(cmd.aim_x, cmd.aim_y) ||
       !state->geofence.RouteSafe(own.x, own.y, cmd.aim_x, cmd.aim_y))) {
    out.aim_rejected_by_geofence = true;
    out.phase = SeyirPhase::kArama;
    return out;
  }

  out.active = true;
  out.pursuit_stage = cmd.stage;
  out.safety_brake = cmd.safety_brake;
  out.desired_throttle_pct = cmd.throttle_pct;
  out.maneuver_dps = static_cast<float>(cmd.maneuver_dps);
  out.aim_range_m = static_cast<float>(cmd.range_m);
  out.aim_lat_deg = cmd.aim_lat_deg;
  out.aim_lon_deg = cmd.aim_lon_deg;
  out.aim_bearing_deg =
      MathAngleToCompassDeg(std::atan2(cmd.aim_y - own.y, cmd.aim_x - own.x));
  out.phase = cmd.stage == control::PursuitStage::kBreakaway
                  ? SeyirPhase::kKacinma
                  : (cmd.range_m <= cfg.approach_enter_m ? SeyirPhase::kYaklasma
                                                         : SeyirPhase::kArama);

  const int telem_alt_cmd = static_cast<int>(std::lround(std::clamp(
      static_cast<float>(cmd.altitude_m), cfg.min_alt_m, cfg.max_alt_m)));
  auto coord = autopilot::seyir::PackCoordinateSteer(cmd.aim_lat_deg, cmd.aim_lon_deg,
                                                     cfg.coord_format, cfg.cmd2_tail);
  if (!coord.valid) {
    out.phase = SeyirPhase::kArama;
    out.active = false;
    return out;
  }
  out.coord_cmd = autopilot::seyir::ToSeyirModePacket(coord);
  out.has_coord = out.coord_cmd.valid;
  out.alt_cmd_m = telem_alt_cmd;
  auto alt = autopilot::seyir::PackAltitudeChange(cfg.default_pitch_deg, telem_alt_cmd);
  out.alt_cmd = autopilot::seyir::ToSeyirModePacket(alt);
  out.has_alt = out.alt_cmd.valid;

  // 5) Kilit bandı koruması — güdüm fazla sokulduysa nişanı yana kır
  ApplyLockBandGuard(cfg, own.x, own.y, est.x, est.y, state->frame, &out);

  // 6) Görsel ince ayar — nişan noktasını kaydırır, telemetri hedefini bırakmaz
  ApplyVisualCenteringTrim(cfg, in, telem_alt_cmd, &out);
  return out;
}

}  // namespace savasan::runners

#endif  // SAVASAN_RUNNERS_SEYIR_GUIDANCE_HPP_
