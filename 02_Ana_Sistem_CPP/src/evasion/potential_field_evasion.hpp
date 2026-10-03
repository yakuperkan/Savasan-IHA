/**
 * @file potential_field_evasion.hpp
 * @brief Sunucu telemetrisi tabanlı yapay potansiyel alan (APF) rakip kaçışı.
 *
 * @note HSS ve saha sınırı burada DEĞİLDİR; yasak bölge yönetimi
 * @ref savasan::evasion::Geofence'e taşındı. Sebebi APF'nin daire yaklaşımının
 * yetersiz kalmasıydı: poligon sahayı temsil edemiyor, kaçış rotasının yasak
 * bölgeden geçip geçmediğini kontrol edemiyor ve dönüş yarıçapına göre
 * önleyici bant üretemiyordu. APF yalnızca rakip itmesi yapar.
 */
#ifndef SAVASAN_EVASION_POTENTIAL_FIELD_EVASION_HPP_
#define SAVASAN_EVASION_POTENTIAL_FIELD_EVASION_HPP_

#include <chrono>
#include <cstdint>
#include <vector>

#include "competition/siha_types.hpp"

namespace savasan::evasion {

/// @brief APF yapılandırması (ortam değişkenleri ile yüklenir).
struct PotentialFieldConfig {
  float k_rep = 500.0f;
  /// Rakip itme yarıçapı. Kilit 34 m'de alınır (hedef ekranın %5'ini ancak o
  /// mesafede kaplar); bu yarıçap kilit penceresini yutmayacak kadar dar
  /// tutulur. Kovaladığımız hedef ayrıca listeden çıkarılır — onunla
  /// çarpışmayı takip güdümünün KOPMA'sı ve öngörülü freni yönetir.
  float d0_m = 40.0f;
  float max_speed_mps = 8.0f;
  float max_yaw_rate_dps = 25.0f;
  float k_yaw = 0.5f;
  int max_stale_ms = 2000;
  int own_team_no = 0;
  /// Kapan eşiği ORAN olarak: bileşke kuvvet, bileşen büyüklükleri toplamının
  /// bu kadarından küçükse kuvvetler birbirini götürüyor demektir.
  float local_min_force_eps = 0.05f;
  float local_min_bias_deg = 15.0f;
};

/// @brief Dead reckoning sonrası tahmini rakip konumu.
struct ThreatPrediction {
  int team_no = 0;
  double lat_deg = 0.0;
  double lon_deg = 0.0;
  float distance_m = 0.0f;
  bool in_range = false;
  bool stale = false;
};

/// @brief APF itici güç bileşenleri (loglama / izlenebilirlik).
struct ApfForceBreakdown {
  float f_total = 0.0f;
  float f_threat = 0.0f;
  bool local_minimum_escape = false;
};

/// @brief APF kaçış çıkışı (Seyir Modu istem 0/2 ile uyumlu).
struct ApfResult {
  bool active = false;
  bool threat_active = false;
  ApfForceBreakdown forces{};
  float escape_heading_deg = 0.0f;  ///< Kaçış yönü (pusula, 0..360).
  int escape_alt_m = 35;            ///< Hedef irtifa (m).
};

/// @brief Ortam değişkenlerinden APF yapılandırması okur.
PotentialFieldConfig LoadPotentialFieldConfigFromEnv();

/// @brief Sunucu konum listesinden tahmini tehdit konumlarını üretir.
std::vector<ThreatPrediction> UpdateThreatPredictions(
    const PotentialFieldConfig& cfg, double own_lat_deg, double own_lon_deg,
    const std::vector<savasan::competition::DigerKonumBilgisi>& sources,
    std::chrono::steady_clock::time_point server_tp,
    std::chrono::steady_clock::time_point now);

/// @brief Rakip repulsive alanından kaçış yönü/irtifası hesaplar (Seyir Modu).
ApfResult ComputeApfSetpoint(const PotentialFieldConfig& cfg, double own_lat_deg,
                             double own_lon_deg, float own_yaw_deg, float own_alt_m,
                             const std::vector<ThreatPrediction>& threats);

}  // namespace savasan::evasion

#endif  // SAVASAN_EVASION_POTENTIAL_FIELD_EVASION_HPP_
