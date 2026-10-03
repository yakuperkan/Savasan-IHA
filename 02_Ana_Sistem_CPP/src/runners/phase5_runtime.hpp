/**
 * @file phase5_runtime.hpp
 * @brief Faz-5 (İHA takip + güdüm) çalışma zamanı durumu (state) yapıları.
 *
 * @ref Phase5Runtime, faz-5 boyunca paylaşılan tüm durumu mantıksal alt
 * yapılara ayırarak tutar: güdüm (@ref GuidanceState), takip/tracker
 * (@ref TrackerState) ve sistem sağlığı/yarışma (@ref SystemHealth). Bu
 * ayrıştırma, eskiden 40+ alanlı tek struct'ın yönetilebilirliğini artırır.
 */
#ifndef SAVASAN_RUNNERS_PHASE5_RUNTIME_HPP_
#define SAVASAN_RUNNERS_PHASE5_RUNTIME_HPP_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "autopilot/alc_link_types.hpp"
#include "autopilot/ialc_link_bridge.hpp"
#include "competition/siha_types.hpp"
#include "control/vehicle_state_estimator.hpp"
#include "evasion/potential_field_evasion.hpp"
#include "runners/lock_metrics.hpp"
#include "runners/rival_telemetry_file.hpp"
#include "runners/seyir_command_scheduler.hpp"
#include "runners/seyir_guidance.hpp"

namespace savasan::runners {

struct Phase5RuntimeFlags;

/// @brief Görev modu. Bu iş akışında tek mod (AIR_LOCK) desteklenir.
enum class MissionMode {
  kAirLock = 0, ///< Havada kilit modu (LOCK koordinatı seri üzerinden iletilir).
};

/// @brief Tracker profili önerisi (otomatik profil mantığı tarafından üretilir).
enum class TrackerProfileRecommendation {
  kDefault = 0,    ///< Varsayılan profil.
  kCalm = 1,       ///< Sakin (düşük hız/titreme) profil.
  kAggressive = 2, ///< Agresif (yüksek hız) profil.
};

/// @brief Otomatik tracker profil seçimi ve yeniden başlatma eşik/zamanlamaları.
struct AutoProfileConfig {
  float aggressive_speed_mps = 10.0f; ///< Agresif profil için hız eşiği (m/s).
  float calm_speed_mps = 6.0f;        ///< Sakin profil için hız eşiği (m/s).
  float aggressive_jitter = 0.20f;    ///< Agresif profil için titreme eşiği.
  float calm_jitter = 0.10f;          ///< Sakin profil için titreme eşiği.
  std::chrono::seconds recommend_cooldown{8};      ///< İki öneri arası asgari süre.
  std::chrono::seconds recommend_log_interval{2};  ///< Öneri logu periyodu.
  std::chrono::seconds restart_min_dwell{5};       ///< Yeniden başlatma öncesi asgari kalış süresi.
  std::chrono::seconds restart_cooldown{10};       ///< İki yeniden başlatma arası bekleme.
  float restart_precheck_max_jitter = 0.30f;       ///< Yeniden başlatmaya izin için azami titreme.
  uint32_t restart_precheck_max_comm_fails = 1;    ///< Yeniden başlatmaya izin için azami iletişim hatası.
  std::chrono::seconds restart_postcheck_timeout{5};///< Yeniden başlatma sonrası doğrulama süresi.
  uint32_t max_auto_restarts = 5;                  ///< Azami otomatik yeniden başlatma sayısı.
  uint32_t max_consecutive_failures = 2;           ///< Azami ardışık başarısızlık.
  uint32_t max_failures_in_window = 3;             ///< Pencere içindeki azami başarısızlık.
  std::chrono::seconds failure_window{60};         ///< Başarısızlık sayım penceresi.
};

/// @brief HSS yasak bölge önbelleği (RF 0x24).
struct HssCache {
  std::vector<savasan::competition::HssKoordinatBilgisi> zones; ///< Aktif HSS bölgeleri.
  std::chrono::steady_clock::time_point last_update_tp{};       ///< Son başarılı güncelleme.
};

/// @brief RF'den gelen yarışma verisi (HSS). Rakip havuzu @ref GuidanceState'te.
struct CompetitionState {
  HssCache hss_cache{};  ///< HSS yasak bölge önbelleği (RF).
};

/// @brief Tracker profil yönetimi: aktif/önerilen profil, yeniden başlatma ve metrik akümülatörler.
struct TrackerProfileState {
  AutoProfileConfig auto_cfg{};      ///< Otomatik profil yapılandırması.
  bool auto_enabled = false;         ///< Otomatik profil önerisi etkin mi.
  bool auto_restart = false;         ///< Otomatik yeniden başlatma etkin mi.
  std::string configured = "default";///< Yapılandırılmış (başlangıç) profil.
  std::string active = "default";    ///< Şu an aktif profil.
  TrackerProfileRecommendation recommended = TrackerProfileRecommendation::kDefault; ///< Güncel öneri.
  std::chrono::steady_clock::time_point last_recommend_log_tp{}; ///< Son öneri logu anı.
  std::chrono::steady_clock::time_point applied_tp{};            ///< Profilin uygulandığı an.
  std::chrono::steady_clock::time_point last_restart_request_tp{};///< Son yeniden başlatma isteği anı.
  std::atomic<bool> restart_requested{false}; ///< Yeniden başlatma istendi mi (thread-safe bayrak).
  std::string requested{};                    ///< İstenen profil adı.
  bool validation_pending = false;            ///< Profil değişimi doğrulama bekliyor mu.
  std::string validation_target{};            ///< Doğrulanacak hedef profil.
  std::string validation_fallback{};          ///< Doğrulama başarısızsa dönülecek profil.
  std::chrono::steady_clock::time_point validation_deadline{}; ///< Doğrulama son anı.
  bool switch_health_failed = false;          ///< Profil geçişi sağlık kontrolü başarısız mı.
  std::string switch_health_fail_reason{};    ///< Başarısızlık nedeni.
  uint32_t switch_count = 0;                  ///< Toplam profil geçişi sayısı.
  uint32_t switch_fail_count = 0;             ///< Başarısız profil geçişi sayısı.
  std::chrono::steady_clock::time_point last_switch_tp{}; ///< Son geçiş anı.
  double jitter_sum = 0.0;                     ///< Titreme metriği toplamı (ortalama için).
  uint64_t miss_sum = 0;                       ///< Tespitsiz kare toplamı.
  uint32_t lock_valid_samples = 0;            ///< Geçerli kilit örnek sayısı.
  uint32_t metric_samples = 0;                ///< Toplam metrik örnek sayısı.
};

/// @brief Degrade (bozulmuş) mod durumu ve iletişim/pipeline sağlık bayrakları.
struct DegradedModeState {
  std::atomic<bool> active{false};                ///< Degrade mod aktif mi.
  std::atomic<uint32_t> comm_fail_count{0};       ///< Ardışık iletişim hatası sayısı.
  std::atomic<uint32_t> comm_success_streak{0};   ///< Degrade iken ardışık başarılı TX sayısı.
  std::atomic<bool> pipeline_healthy{false};      ///< DeepStream pipeline sağlıklı mı.
  std::atomic<bool> telemetry_valid_recent{false};///< Yakın zamanda geçerli telemetri alındı mı.
};

/// @brief Seyir Modu güdüm döngüsü durumu.
struct GuidanceState {
  std::shared_ptr<control::VehicleStateEstimator> state_estimator;  ///< Araç durum kestiricisi.
  bool latest_lock_valid = false;                                   ///< Son karede kilit var mıydı.
  /// Şartname 4 sn AV kilidi dolmuş mu (LockState::valid_lock). Güdümün KILIT fazına
  /// geçmesi buna bağlıdır; @c latest_lock_valid yalnızca "kilitli" demektir.
  bool latest_av_lock_valid = false;
  std::atomic<bool> seyir_tx_enable{false};  ///< Seyir komutu ACM0 TX (SAVASAN_SEYIR_TX_ENABLE).
  std::atomic<bool> control_log_enable{true};
  std::atomic<bool> send_only_on_lock{false};
  std::atomic<bool> guidance_active_prev{false};
  std::atomic<MissionMode> mission_mode{MissionMode::kAirLock};
  std::atomic<bool> mission_mode_dynamic_enabled{false};
  std::string mission_mode_file;
  int mission_mode_poll_interval_ms = 250;
  std::atomic<bool> apf_active{false};
  std::vector<evasion::ThreatPrediction> apf_threats;

  SeyirGuidanceConfig seyir_cfg{};
  RivalPoolSnapshot rival_pool{};
  BoundarySnapshot boundary_rf{};
  /// Operatörün gönderdiği nokta hedefi ve güdümün ona verdiği yanıt.
  /// Yanıt hub dosyası üzerinden RF ile operatöre geri döner (0x33).
  GotoTargetSnapshot goto_target{};
  GotoState goto_state = GotoState::kIdle;
  GotoRejectReason goto_reason = GotoRejectReason::kNone;
  int goto_seq = 0;
  SeyirPhase seyir_phase = SeyirPhase::kArama;
  int seyir_vision_streak = 0;
  int seyir_selected_id = -1;
  bool has_last_valid_seyir = false;
  savasan::autopilot::SeyirModePacket last_valid_seyir{};
  std::chrono::steady_clock::time_point last_valid_seyir_tp{};

  /// Tek paket düzeni: hangi istemin ne zaman gideceğine bu ikili karar verir.
  SeyirSchedulerConfig seyir_scheduler_cfg{};
  SeyirSchedulerState seyir_scheduler{};

  /// Kestirici + nişan filtrelerinin tikler arası hafızası.
  /// @warning YALNIZCA güdüm tikinden erişilir (@ref RunGuidanceControlStep);
  /// diğer alanların aksine @ref Phase5Runtime::mutex ile korunmaz.
  SeyirGuidanceState seyir_state{};
};

/// @brief DeepStream takip durumu: kilit metrikleri, profil yönetimi ve pipeline referansı.
struct TrackerState {
  TrackerProfileState profile;                       ///< Profil yönetim durumu.
  LockMetrics lock_metrics{};                        ///< Kilit kalite metrikleri.
  std::atomic<unsigned int> pipeline_width{0};       ///< Son probe'dan okunan pipeline genişliği.
  std::atomic<unsigned int> pipeline_height{0};      ///< Son probe'dan okunan pipeline yüksekliği.
  std::atomic<bool> lock_valid_seen{false};          ///< En az bir geçerli kilit görüldü mü.
  std::atomic<std::uint32_t> lock_track_id_competition{0}; ///< Yarışmaya bildirilen kilit track_id.
  void* gst_nvinfer_element = nullptr;               ///< nvinfer GStreamer öğesi (sahiplenilmez).
};

/// @brief Sistem sağlığı: degrade mod ve RF HSS önbelleği.
struct SystemHealth {
  DegradedModeState degraded;
  CompetitionState competition;
};

/// @brief Faz-5 boyunca paylaşılan tüm çalışma zamanı durumu.
struct Phase5Runtime {
  ~Phase5Runtime();

  /// @brief Ortam değişkenlerinden okunan bayrakları guidance alanına uygular.
  void ApplyRuntimeFlags(const Phase5RuntimeFlags& flags);

  std::shared_ptr<savasan::autopilot::IAlcLinkBridge> bridge;     ///< Otopilot köprüsü.
  GuidanceState guidance;  ///< Güdüm durumu.
  TrackerState tracker;    ///< Takip/tracker durumu.
  SystemHealth health;     ///< Sistem sağlığı/yarışma durumu.
  std::mutex mutex;        ///< Paylaşılan alanlar (HSS, rakip havuzu, kilit) için kilit.
};

}  // namespace savasan::runners

#endif  // SAVASAN_RUNNERS_PHASE5_RUNTIME_HPP_
