/**
 * @file seyir_command_scheduler.hpp
 * @brief Seyir Modu komut zamanlayıcısı — bir tikte tek paket.
 *
 * @ref seyir_guidance.hpp her tikte iki niyet üretir: koordinat (istem 2) ve
 * irtifa (istem 0). Kart bunları aynı anda beklemez; arka arkaya paket basmak
 * sahada komut düşmesine yol açtığı için tikte yalnızca biri gönderilir.
 *
 * Seçim kuralları (MSO'nun uçuş deneyiminden):
 *  - Kadans freni: iki gönderim arası @ref SeyirSchedulerConfig::min_interval_s
 *    altına inilmez.
 *  - Akış kesintisi: komut akışı @ref SeyirSchedulerConfig::flow_gap_s'den uzun
 *    kesilirse kart hedef irtifayı unutmuş olabilir; kesintiden sonraki ilk paket
 *    daima irtifa olur. (19 Ağustos uçuş bulgusu: mod geçişinde irtifa unutuluyor.)
 *  - İrtifa değişimi: hedef irtifa @ref SeyirSchedulerConfig::alt_epsilon_m kadar
 *    kaydıysa o tikte irtifa gönderilir.
 *  - Periyodik tazeleme: irtifa değişmese bile @ref
 *    SeyirSchedulerConfig::alt_refresh_s'de bir tekrarlanır.
 *  - Aksi hâlde koordinat gönderilir (yatay güdüm çoğunluk hâli).
 */
#ifndef SAVASAN_RUNNERS_SEYIR_COMMAND_SCHEDULER_HPP_
#define SAVASAN_RUNNERS_SEYIR_COMMAND_SCHEDULER_HPP_

#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>

#include "autopilot/alc_link_types.hpp"
#include "runners/seyir_guidance.hpp"

namespace savasan::runners {

struct SeyirSchedulerConfig {
  float min_interval_s = 0.2f;   ///< Kadans tavanı (5 Hz).
  float flow_gap_s = 2.0f;       ///< Bu kadar sessizlik sonrası irtifa tazelenir.
  int alt_epsilon_m = 2;         ///< Bu kadar sapma "irtifa değişti" sayılır.
  float alt_refresh_s = 3.0f;    ///< İrtifa değişmese de periyodik tekrar.
};

/// @brief Zamanlayıcının tikler arası hatırladığı durum.
struct SeyirSchedulerState {
  bool has_sent = false;
  std::chrono::steady_clock::time_point last_send_tp{};
  bool has_alt = false;
  int last_alt_m = 0;
  std::chrono::steady_clock::time_point last_alt_tp{};
};

enum class SeyirSendReason : uint8_t {
  kNone = 0,        ///< Gönderilecek geçerli niyet yok.
  kRateLimited = 1, ///< Kadans freni tuttu.
  kAltFlowGap = 2,  ///< Akış kesintisi sonrası irtifa tazeleme.
  kAltChanged = 3,  ///< Hedef irtifa kaydı.
  kAltRefresh = 4,  ///< Periyodik irtifa tekrarı.
  kCoord = 5,       ///< Koordinata yönel.
};

inline const char* SeyirSendReasonName(const SeyirSendReason r) {
  switch (r) {
    case SeyirSendReason::kRateLimited:
      return "KADANS";
    case SeyirSendReason::kAltFlowGap:
      return "IRTIFA_KESINTI";
    case SeyirSendReason::kAltChanged:
      return "IRTIFA_DEGISTI";
    case SeyirSendReason::kAltRefresh:
      return "IRTIFA_TAZELE";
    case SeyirSendReason::kCoord:
      return "KOORDINAT";
    default:
      return "YOK";
  }
}

struct SeyirScheduleResult {
  bool send = false;
  autopilot::SeyirModePacket packet{};
  SeyirSendReason reason = SeyirSendReason::kNone;
};

/// @brief Bu tikte hangi paketin karta gideceğine karar verir ve durumu ilerletir.
///
/// @param state Gönderim gerçekleştiyse (yalnızca o zaman) güncellenir.
inline SeyirScheduleResult SelectSeyirCommand(const SeyirSchedulerConfig& cfg,
                                              const SeyirGuidanceOutput& out,
                                              SeyirSchedulerState* state,
                                              const std::chrono::steady_clock::time_point now) {
  SeyirScheduleResult res{};
  if (state == nullptr || !out.active) {
    return res;
  }
  if (!out.has_coord && !out.has_alt) {
    return res;
  }

  const float since_send_s =
      state->has_sent
          ? std::chrono::duration<float>(now - state->last_send_tp).count()
          : std::numeric_limits<float>::max();
  if (since_send_s < cfg.min_interval_s) {
    res.reason = SeyirSendReason::kRateLimited;
    return res;
  }

  if (out.has_alt) {
    SeyirSendReason alt_reason = SeyirSendReason::kNone;
    if (!state->has_sent || since_send_s > cfg.flow_gap_s) {
      alt_reason = SeyirSendReason::kAltFlowGap;
    } else if (!state->has_alt || std::abs(out.alt_cmd_m - state->last_alt_m) >= cfg.alt_epsilon_m) {
      alt_reason = SeyirSendReason::kAltChanged;
    } else if (std::chrono::duration<float>(now - state->last_alt_tp).count() >=
               cfg.alt_refresh_s) {
      alt_reason = SeyirSendReason::kAltRefresh;
    }
    if (alt_reason != SeyirSendReason::kNone) {
      res.send = true;
      res.packet = out.alt_cmd;
      res.reason = alt_reason;
      state->has_alt = true;
      state->last_alt_m = out.alt_cmd_m;
      state->last_alt_tp = now;
      state->has_sent = true;
      state->last_send_tp = now;
      return res;
    }
  }

  if (out.has_coord) {
    res.send = true;
    res.packet = out.coord_cmd;
    res.reason = SeyirSendReason::kCoord;
    state->has_sent = true;
    state->last_send_tp = now;
  }
  return res;
}

}  // namespace savasan::runners

#endif  // SAVASAN_RUNNERS_SEYIR_COMMAND_SCHEDULER_HPP_
