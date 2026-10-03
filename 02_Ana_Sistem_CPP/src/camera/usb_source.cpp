/**
 * @file usb_source.cpp
 * @brief USB (MJPEG) kamera ingest dalı string üretimi.
 */
#include "camera/usb_source.hpp"

#include <sstream>

namespace savasan::camera {

std::string BuildUsbMjpegIngestBranch(const IngestConfig& cfg) {
  std::ostringstream o;
  // v4l2src MJPEG akışını alır; donanım çözücü (nvv4l2decoder) ile çözer ve
  // nvvidconv ile NVMM NV12 formatına dönüştürür. Çıkış sondaki "! " ile biter.
  o << "v4l2src name=savasan_v4l2src device=" << cfg.v4l2_device << " io-mode=2"
    << " ! image/jpeg,width=" << cfg.width << ",height=" << cfg.height << ",framerate=" << cfg.fps << "/1"
    << " ! nvv4l2decoder enable-max-performance=1 name=savasan_decoder"
    << " ! nvvidconv"
    << " ! capsfilter caps=\"video/x-raw(memory:NVMM),width=" << cfg.width
    << ",height=" << cfg.height
    << ",framerate=" << cfg.fps << "/1,format=NV12\" ! ";
  return o.str();
}

}  // namespace savasan::camera
