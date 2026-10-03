/**
 * @file camera_tune.cpp
 * @brief USB BRIO saha kalibrasyonu: canli onizleme + trackbar + profil kaydet/yukle.
 *
 * Kullanim:
 *   camera_tune [/dev/video0]
 *   1/2/3 — profil yukle (outdoor_sunny / outdoor_overcast / hangar_indoor)
 *   S     — aktif profile kaydet
 *   Q/Esc — cik
 */
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifndef SAVASAN_SOURCE_DIR
#define SAVASAN_SOURCE_DIR "."
#endif

namespace {

constexpr int kWidth = 640;
constexpr int kHeight = 480;
constexpr int kFps = 60;

struct TuneValues {
  int exposure_abs = 42;
  int gain = 8;
  int wb_temp = 5000;
  int brightness = 128;
  int contrast = 128;
  int saturation = 128;
};

struct TuneState {
  std::string device = "/dev/video0";
  std::string profile_dir;
  std::string active_profile = "outdoor_sunny";
  TuneValues vals{};
  bool applying_trackbar = false;
};

TuneState g_state;

std::string EnvOrDefault(const char* key, const char* fallback) {
  const char* v = std::getenv(key);
  return (v != nullptr && v[0] != '\0') ? v : fallback;
}

int RunShell(const std::string& cmd) {
  return std::system(cmd.c_str());
}

bool V4l2SetCtrl(const std::string& device, const std::string& spec) {
  const std::string cmd =
      "v4l2-ctl --device=" + device + " --set-ctrl=" + spec + " >/dev/null 2>&1";
  return RunShell(cmd) == 0;
}

bool V4l2SetCtrlPair(const std::string& device, const std::string& primary,
                     const std::string& legacy) {
  if (V4l2SetCtrl(device, primary)) {
    return true;
  }
  if (!legacy.empty()) {
    return V4l2SetCtrl(device, legacy);
  }
  return false;
}

void ApplyManualMode(const std::string& device) {
  V4l2SetCtrlPair(device, "auto_exposure=1", "exposure_auto=1");
  V4l2SetCtrl(device, "exposure_dynamic_framerate=0");
  V4l2SetCtrlPair(device, "white_balance_automatic=0", "white_balance_temperature_auto=0");
}

void ApplyValuesToDevice(const TuneState& st) {
  const auto& v = st.vals;
  ApplyManualMode(st.device);
  V4l2SetCtrlPair(st.device, "exposure_time_absolute=" + std::to_string(v.exposure_abs),
                  "exposure_absolute=" + std::to_string(v.exposure_abs));
  V4l2SetCtrl(st.device, "gain=" + std::to_string(v.gain));
  V4l2SetCtrl(st.device, "white_balance_temperature=" + std::to_string(v.wb_temp));
  V4l2SetCtrl(st.device, "brightness=" + std::to_string(v.brightness));
  V4l2SetCtrl(st.device, "contrast=" + std::to_string(v.contrast));
  V4l2SetCtrl(st.device, "saturation=" + std::to_string(v.saturation));
}

void SyncTrackbarsFromState() {
  g_state.applying_trackbar = true;
  cv::setTrackbarPos("exposure", "camera_tune", g_state.vals.exposure_abs);
  cv::setTrackbarPos("gain", "camera_tune", g_state.vals.gain);
  cv::setTrackbarPos("wb_K", "camera_tune", g_state.vals.wb_temp);
  cv::setTrackbarPos("brightness", "camera_tune", g_state.vals.brightness);
  cv::setTrackbarPos("contrast", "camera_tune", g_state.vals.contrast);
  cv::setTrackbarPos("saturation", "camera_tune", g_state.vals.saturation);
  g_state.applying_trackbar = false;
}

void OnTrackbar(int /*value*/, void* /*userdata*/) {
  if (g_state.applying_trackbar) {
    return;
  }
  ApplyValuesToDevice(g_state);
}

std::string Trim(const std::string& s) {
  const auto start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return "";
  }
  const auto end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

bool ParseProfileFile(const std::string& path, TuneValues* out) {
  std::ifstream in(path);
  if (!in.is_open()) {
    return false;
  }
  TuneValues v = *out;
  std::string line;
  while (std::getline(in, line)) {
    line = Trim(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }
    const auto eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    const std::string key = Trim(line.substr(0, eq));
    const int val = std::atoi(line.substr(eq + 1).c_str());
    if (key == "SAVASAN_USB_EXPOSURE_ABSOLUTE") {
      v.exposure_abs = val;
    } else if (key == "SAVASAN_USB_GAIN") {
      v.gain = val;
    } else if (key == "SAVASAN_USB_WB_TEMPERATURE") {
      v.wb_temp = val;
    } else if (key == "SAVASAN_USB_BRIGHTNESS") {
      v.brightness = val;
    } else if (key == "SAVASAN_USB_CONTRAST") {
      v.contrast = val;
    } else if (key == "SAVASAN_USB_SATURATION") {
      v.saturation = val;
    }
  }
  *out = v;
  return true;
}

bool SaveProfileFile(const std::string& path, const TuneValues& v) {
  std::ofstream out(path, std::ios::trunc);
  if (!out.is_open()) {
    return false;
  }
  out << "# USB BRIO kamera profili (v4l2-ctl birimleri)\n";
  out << "SAVASAN_USB_EXPOSURE_ABSOLUTE=" << v.exposure_abs << "\n";
  out << "SAVASAN_USB_GAIN=" << v.gain << "\n";
  out << "SAVASAN_USB_WB_TEMPERATURE=" << v.wb_temp << "\n";
  out << "SAVASAN_USB_BRIGHTNESS=" << v.brightness << "\n";
  out << "SAVASAN_USB_CONTRAST=" << v.contrast << "\n";
  out << "SAVASAN_USB_SATURATION=" << v.saturation << "\n";
  return true;
}

std::string ProfilePath(const TuneState& st, const std::string& name) {
  return st.profile_dir + "/" + name + ".env";
}

bool LoadNamedProfile(TuneState* st, const std::string& name) {
  const std::string path = ProfilePath(*st, name);
  TuneValues v = st->vals;
  if (!ParseProfileFile(path, &v)) {
    std::cerr << "[camera_tune] Profil okunamadi: " << path << "\n";
    return false;
  }
  st->vals = v;
  st->active_profile = name;
  ApplyValuesToDevice(*st);
  SyncTrackbarsFromState();
  std::cout << "[camera_tune] Yuklendi: " << name << " (" << path << ")\n";
  return true;
}

bool SaveActiveProfile(TuneState* st) {
  const std::string path = ProfilePath(*st, st->active_profile);
  if (!SaveProfileFile(path, st->vals)) {
    std::cerr << "[camera_tune] Kayit basarisiz: " << path << "\n";
    return false;
  }
  std::cout << "[camera_tune] Kaydedildi: " << path << "\n";
  return true;
}

struct LumaStats {
  float mean = 0.0f;
  float highlight_ratio = 0.0f;
};

LumaStats ComputeLumaStats(const cv::Mat& bgr) {
  LumaStats s;
  cv::Mat gray;
  cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
  const int step = 4;
  uint64_t sum = 0;
  uint64_t count = 0;
  uint64_t highlights = 0;
  for (int y = 0; y < gray.rows; y += step) {
    const uint8_t* row = gray.ptr<uint8_t>(y);
    for (int x = 0; x < gray.cols; x += step) {
      const uint8_t v = row[x];
      sum += v;
      highlights += (v > 235) ? 1u : 0u;
      ++count;
    }
  }
  if (count == 0) {
    return s;
  }
  s.mean = static_cast<float>(sum) / static_cast<float>(count);
  s.highlight_ratio = static_cast<float>(highlights) / static_cast<float>(count);
  return s;
}

void DrawHud(cv::Mat* frame, const TuneState& st, const LumaStats& stats) {
  int y = 22;
  const auto line = [&](const std::string& text) {
    cv::putText(*frame, text, cv::Point(8, y), cv::FONT_HERSHEY_SIMPLEX, 0.5,
                cv::Scalar(0, 255, 0), 1, cv::LINE_AA);
    y += 20;
  };
  line("Profil: " + st.active_profile + "  |  1=sunny 2=overcast 3=indoor  S=kaydet  Q=cik");
  std::ostringstream o;
  o.setf(std::ios::fixed);
  o.precision(1);
  o << "Y mean=" << stats.mean << "  highlight=" << (stats.highlight_ratio * 100.0f) << "%";
  line(o.str());
  o.str("");
  o << "exp=" << st.vals.exposure_abs << " gain=" << st.vals.gain << " wb=" << st.vals.wb_temp
    << " bri=" << st.vals.brightness << " con=" << st.vals.contrast
    << " sat=" << st.vals.saturation;
  line(o.str());
}

bool OpenCapture(cv::VideoCapture* cap, const std::string& device) {
  if (!cap->open(device, cv::CAP_V4L2)) {
    return false;
  }
  cap->set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
  cap->set(cv::CAP_PROP_FRAME_WIDTH, kWidth);
  cap->set(cv::CAP_PROP_FRAME_HEIGHT, kHeight);
  cap->set(cv::CAP_PROP_FPS, kFps);
  return cap->isOpened();
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 2) {
    g_state.device = argv[1];
  } else {
    g_state.device = EnvOrDefault("SAVASAN_V4L2_DEVICE", "/dev/video0");
  }
  g_state.profile_dir = EnvOrDefault(
      "SAVASAN_CAMERA_PROFILE_DIR",
      (std::string(SAVASAN_SOURCE_DIR) + "/config/camera_profiles").c_str());

  if (RunShell("command -v v4l2-ctl >/dev/null 2>&1") != 0) {
    std::cerr << "HATA: v4l2-ctl bulunamadi (v4l-utils kurulu olmali).\n";
    return 1;
  }

  if (!LoadNamedProfile(&g_state, g_state.active_profile)) {
    std::cerr << "[camera_tune] Varsayilan profil yuklenemedi; dahili defaultlar kullaniliyor.\n";
    ApplyValuesToDevice(g_state);
  }

  cv::VideoCapture cap;
  if (!OpenCapture(&cap, g_state.device)) {
    std::cerr << "HATA: Kamera acilamadi: " << g_state.device << "\n";
    return 1;
  }

  cv::namedWindow("camera_tune", cv::WINDOW_NORMAL);
  cv::resizeWindow("camera_tune", kWidth, kHeight);
  cv::createTrackbar("exposure", "camera_tune", &g_state.vals.exposure_abs, 500, OnTrackbar);
  cv::createTrackbar("gain", "camera_tune", &g_state.vals.gain, 255, OnTrackbar);
  cv::createTrackbar("wb_K", "camera_tune", &g_state.vals.wb_temp, 7500, OnTrackbar);
  cv::createTrackbar("brightness", "camera_tune", &g_state.vals.brightness, 255, OnTrackbar);
  cv::createTrackbar("contrast", "camera_tune", &g_state.vals.contrast, 255, OnTrackbar);
  cv::createTrackbar("saturation", "camera_tune", &g_state.vals.saturation, 255, OnTrackbar);
  SyncTrackbarsFromState();

  std::cout << "[camera_tune] Cihaz: " << g_state.device << "  Profil dizini: " << g_state.profile_dir
            << "\n";

  cv::Mat frame;
  while (true) {
    if (!cap.read(frame) || frame.empty()) {
      std::cerr << "[camera_tune] Kare okunamadi.\n";
      break;
    }
    const LumaStats stats = ComputeLumaStats(frame);
    DrawHud(&frame, g_state, stats);
    cv::imshow("camera_tune", frame);

    const int key = cv::waitKey(1) & 0xFF;
    if (key == 'q' || key == 'Q' || key == 27) {
      break;
    }
    if (key == '1') {
      LoadNamedProfile(&g_state, "outdoor_sunny");
    } else if (key == '2') {
      LoadNamedProfile(&g_state, "outdoor_overcast");
    } else if (key == '3') {
      LoadNamedProfile(&g_state, "hangar_indoor");
    } else if (key == 's' || key == 'S') {
      SaveActiveProfile(&g_state);
    }
  }

  cap.release();
  cv::destroyAllWindows();
  return 0;
}
