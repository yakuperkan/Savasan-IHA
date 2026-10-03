#include "runners/lock_metrics.hpp"

#include <cmath>

namespace {

bool Near(const float a, const float b, const float eps = 1e-5f) {
  return std::fabs(a - b) <= eps;
}

}  // namespace

int main() {
  savasan::runners::LockMetrics metrics;

  // Başlangıç: jitter ve kayıp sayacı sıfır.
  {
    const auto s = metrics.GetSnapshot();
    if (!Near(s.jitter, 0.0f) || s.lock_loss_count != 0U || s.lock_valid) {
      return 1;
    }
  }

  // 3'ten az örnekte jitter 0 kalmalı.
  metrics.Update(true, 0.50f, 0.50f, 0);
  metrics.Update(true, 0.51f, 0.50f, 0);
  {
    const auto s = metrics.GetSnapshot();
    if (!Near(s.jitter, 0.0f) || s.lock_loss_count != 0U || !s.lock_valid) {
      return 2;
    }
  }

  // Üçüncü geçerli örnekten sonra jitter > 0.
  metrics.Update(true, 0.49f, 0.50f, 0);
  {
    const auto s = metrics.GetSnapshot();
    if (s.jitter <= 0.0f || s.lock_loss_count != 0U || !s.lock_valid) {
      return 3;
    }
  }

  // Kilit kaybı: sayaç artar, örnek tamponu temizlenir, jitter sıfırlanır.
  metrics.Update(false, 0.49f, 0.50f, 2);
  {
    const auto s = metrics.GetSnapshot();
    if (s.lock_loss_count != 1U || s.lock_valid || s.jitter > 0.0f ||
        s.frames_without_detection != 2) {
      return 4;
    }
  }

  // İkinci kayıp geçişi sayacı bir artırır.
  metrics.Update(true, 0.5f, 0.5f, 0);
  metrics.Update(false, 0.5f, 0.5f, 1);
  {
    const auto s = metrics.GetSnapshot();
    if (s.lock_loss_count != 2U || s.lock_valid) {
      return 5;
    }
  }

  // Geçerli kilitte son konum snapshot'a yansır.
  metrics.Update(true, 0.42f, 0.58f, 0);
  {
    const auto s = metrics.GetSnapshot();
    if (!Near(s.hedef_norm_x, 0.42f) || !Near(s.hedef_norm_y, 0.58f)) {
      return 6;
    }
  }

  return 0;
}
