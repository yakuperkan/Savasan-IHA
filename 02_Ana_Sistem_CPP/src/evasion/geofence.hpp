/**
 * @file geofence.hpp
 * @brief Saha sınırı + HSS yasak bölge yönetimi — MSO `gorev_beyni.py` portu.
 *
 * İki ayrı iş yapar ve ikisi de yerel düzlemde (metre) çalışır:
 *
 *  1. SÜZGEÇ — bir nişan noktası ya da rota güvenli mi? Takip güdümü rakibi
 *     kovalarken bizi sınır dışına veya HSS'in içine sokabilir; üretilen her
 *     hedef buradan geçmek zorundadır.
 *  2. KAÇIŞ — ihlal olmasa bile tehlikeli yaklaşıldıysa kaçış hedefi üretir.
 *
 * ÖNLEYİCİ BANT: uçak dönüşünü yay çizerek tamamlar, yani sınıra "değince"
 * dönmeye başlamak geç kalmaktır. Kaçış, dönüş yarıçapı kadar önceden başlar.
 * MSO ölçümü: bandı daraltmak kaçış sayısını düşürüyor ama sınır ihlalini
 * %3.7'den %9.0'a çıkarıyor — tarama monotondu, dar bant her zaman daha kötü.
 *
 * KAÇIŞ HEDEFİ: sabit "alan merkezine git" davranışı iki hata üretiyordu;
 * devriye hep aynı yönde döndüğü için 27 kaçışın 24'ü sağa dönüyordu (sola
 * dönmek daha kısa olsa bile), ve merkeze giden yol kontrol edilmediği için
 * kaçış rotası HSS'in içinden geçebiliyordu. Bunun yerine burnumuza göre
 * 8 yönde aday üretilir, rotası kesenler elenir, kalanlar "varılan yerin
 * ferahlığı eksi dönüş maliyeti" ile puanlanır. Hiç aday kalmazsa merkeze
 * düşülür — kaçış asla hedefsiz kalmaz.
 */
#ifndef SAVASAN_EVASION_GEOFENCE_HPP_
#define SAVASAN_EVASION_GEOFENCE_HPP_

#include <cstdint>
#include <string>
#include <vector>

#include "control/local_frame.hpp"

namespace savasan::evasion {

struct GeofenceSettings {
  double boundary_margin_m = 30.0;  ///< Nişan noktası sınır kenarına bundan yakın olamaz.
  double hss_margin_m = 20.0;       ///< HSS yarıçapına eklenen emniyet payı.

  /// @name Önleyici bant
  /// @{
  double band_min_m = 60.0;
  double band_multiplier = 1.3;  ///< bant = çarpan × dönüş yarıçapı
  double turn_radius_min_m = 25.0;
  double turn_radius_max_m = 200.0;  ///< Saha ölçümü: 96 m medyan, 147 m %75'lik.
  double nominal_bank_deg = 20.0;
  double assumed_speed_mps = 16.0;  ///< Hız bilinmiyorsa varsayım.
  /// @}

  /// @name Kaçış hedefi
  /// @{
  bool candidate_scoring = true;
  double escape_extra_m = 40.0;       ///< HSS içindeysek radyal çıkışa eklenen pay.
  double escape_distance_factor = 1.2;  ///< Aday mesafesi = bant × bu.
  double escape_turn_penalty = 0.5;   ///< Puan = ferahlık − |sapma°| × bu.
  /// @}
};

/// @brief Yerel düzleme çevrilmiş HSS dairesi.
struct HssDisc {
  int id = 0;
  double x = 0.0;
  double y = 0.0;
  double radius_m = 0.0;
};

enum class GeofenceViolation : uint8_t {
  kNone = 0,
  kOutsideBoundary = 1,  ///< Sınır dışındayız (gerçek ihlal).
  kInsideHss = 2,        ///< HSS içindeyiz (gerçek ihlal).
  kNearBoundary = 3,     ///< Sınıra önleyici bant kadar yaklaşıldı.
  kNearHss = 4,          ///< HSS'e önleyici bant kadar yaklaşıldı.
};

const char* GeofenceViolationName(GeofenceViolation v);

/// @brief Kaçış kararı ve hedefi.
struct GeofenceEscape {
  bool needed = false;
  GeofenceViolation reason = GeofenceViolation::kNone;
  int hss_id = -1;      ///< Sebep HSS ise kaynağın kimliği.
  bool has_target = false;
  double target_x = 0.0;
  double target_y = 0.0;
  double target_lat_deg = 0.0;
  double target_lon_deg = 0.0;
};

/// @brief Saha sınırı ve HSS'lere karşı nokta/rota süzgeci ve kaçış üretici.
class Geofence {
 public:
  Geofence() = default;
  explicit Geofence(const control::LocalFrame& frame,
                    const GeofenceSettings& settings = GeofenceSettings{})
      : frame_(frame), s_(settings) {}

  void SetFrame(const control::LocalFrame& frame);
  void SetSettings(const GeofenceSettings& s) { s_ = s; }

  /// @brief Saha sınırı poligonu (coğrafi köşeler, sıralı).
  void SetBoundaryLatLon(const std::vector<control::GeoPoint>& corners);
  /// @brief HSS listesi. Boş liste önbelleği temizler.
  void SetHssLatLon(const std::vector<HssDisc>& discs_latlon);

  bool has_boundary() const { return polygon_.size() >= 3; }
  const std::vector<HssDisc>& hss() const { return hss_; }

  /// @brief Nokta hem sınırın içinde hem tüm paylardan uzakta mı?
  ///
  /// @param extra_margin_m Görev planlaması için ek boşluk. Bir hedef sadece
  /// "yasak değil" olmamalı, önleyici kaçış bandının da dışında olmalı; aksi
  /// hâlde uçak oraya varır varmaz kaçış tetiklenir ve görev ileri-geri zıplar.
  bool PointSafe(double x, double y, double extra_margin_m = 0.0) const;

  /// @brief İki nokta arasındaki düz rota HSS veya sınırı kesiyor mu?
  bool RouteSafe(double ax, double ay, double bx, double by,
                 double extra_margin_m = 0.0) const;

  /// @brief Gerçek ihlal var mı (pay uygulanmadan)?
  GeofenceViolation Violation(double x, double y, int* hss_id = nullptr) const;

  /// @brief Kaçış gerekiyor mu ve gerekiyorsa nereye?
  /// @param speed_mps Dönüş yarıçapı (dolayısıyla bant) hıza bağlıdır.
  /// @param track_rad Gidiş yönü (matematiksel açı); aday üretiminin referansı.
  GeofenceEscape EvaluateEscape(double x, double y, double speed_mps, bool has_track,
                                double track_rad) const;

  /// @brief Sınıra/HSS'e bu mesafede yaklaşınca kaçış başlar.
  double PreventiveBandM(double speed_mps) const;
  double TurnRadiusM(double speed_mps) const;

  double BoundaryEdgeDistanceM(double x, double y) const;
  bool InsidePolygon(double x, double y) const;

 private:
  bool EscapeTarget(double x, double y, double band_m, bool has_track, double track_rad,
                    double* out_x, double* out_y) const;
  void PolygonCenter(double* cx, double* cy) const;

  control::LocalFrame frame_{};
  GeofenceSettings s_{};
  std::vector<control::LocalPoint> polygon_;
  std::vector<HssDisc> hss_;
};

}  // namespace savasan::evasion

#endif  // SAVASAN_EVASION_GEOFENCE_HPP_
