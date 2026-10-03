/**
 * @file usb_source.hpp
 * @brief USB (MJPEG) kamera ingest dalı kurucusu.
 */
#ifndef SAVASAN_CAMERA_USB_SOURCE_HPP_
#define SAVASAN_CAMERA_USB_SOURCE_HPP_

#include <string>

#include "pipeline/ingest_config.hpp"

namespace savasan::camera {

/// @brief MJPEG USB kamera için GStreamer ingest dalı string'ini üretir.
///
/// Pipeline akışı: v4l2src (MJPEG) -> nvv4l2decoder -> nvvidconv -> NVMM NV12.
/// @param cfg Çözünürlük, FPS ve V4L2 cihaz yolu gibi ingest yapılandırması.
/// @return GStreamer ingest dalı string'i.
std::string BuildUsbMjpegIngestBranch(const IngestConfig& cfg);

}  // namespace savasan::camera

#endif  // SAVASAN_CAMERA_USB_SOURCE_HPP_
