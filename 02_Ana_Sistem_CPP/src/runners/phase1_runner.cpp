/**
 * @file phase1_runner.cpp
 * @brief Faz-1 çalıştırıcısı: çıkarımsız ham görüntü boru hattı duman testi.
 */
#include "runners/phase1_runner.hpp"

#include <iostream>
#include <string>

#include <gst/gst.h>

#include "runners/gst_app_helpers.hpp"

namespace savasan::runners {

// Basit görüntü boru hattını kurar, PLAYING'e geçirir, verilen süre kadar
// çalıştırır ve düzgünce kapatır (systemd watchdog bildirimleriyle).
bool RunPhase1(savasan::IngestConfig cfg, int seconds) {
  gst_init(nullptr, nullptr);
  const std::string pipeline_str = NormalizePipelineForParse(BuildPhase1TestPipeline(cfg));
  std::cout << "Faz1 pipeline: " << pipeline_str << '\n';

  GError* err = nullptr;
  GstElement* pipeline = gst_parse_launch(pipeline_str.c_str(), &err);
  if (!pipeline) {
    std::cerr << "gst_parse_launch basarisiz: " << (err ? err->message : "?") << '\n';
    if (err) {
      g_error_free(err);
    }
    return false;
  }

  if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    std::cerr << "PLAYING durumuna gecemedi (display sink icin DISPLAY=:0 gerekli olabilir).\n";
    gst_object_unref(pipeline);
    return false;
  }

  SdNotify("READY=1");
  SdNotify("WATCHDOG=1");
  g_usleep(static_cast<gulong>(seconds) * G_USEC_PER_SEC);

  SdNotify("STOPPING=1");
  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(pipeline);
  std::cout << "Faz1 duman testi tamam.\n";
  return true;
}

}  // namespace savasan::runners
