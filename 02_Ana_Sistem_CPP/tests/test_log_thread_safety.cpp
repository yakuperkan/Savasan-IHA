#include "common/log.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int kThreads = 8;
constexpr int kLogsPerThread = 250;

bool LineLooksComplete(const std::string& line) {
  if (line.size() < 20 || line.front() != '[') {
    return false;
  }
  const std::size_t info_pos = line.find("][INFO][T");
  if (info_pos == std::string::npos) {
    return false;
  }
  // Karışmış satırlarda birden fazla log başlığı görülür.
  if (line.find("][INFO][", info_pos + 1) != std::string::npos) {
    return false;
  }
  return line.find("] msg", info_pos) != std::string::npos;
}

}  // namespace

int main() {
  const char* const path = "/tmp/savasan_log_thread_test.txt";
  if (std::freopen(path, "w", stdout) == nullptr) {
    return 1;
  }

  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([t]() {
      const std::string tag = "T" + std::to_string(t);
      for (int i = 0; i < kLogsPerThread; ++i) {
        savasan::common::Log(savasan::common::LogLevel::kInfo, tag, "msg" + std::to_string(i));
      }
    });
  }
  for (auto& th : threads) {
    th.join();
  }
  std::fflush(stdout);

  std::ifstream in(path);
  std::string line;
  int complete_lines = 0;
  while (std::getline(in, line)) {
    if (!LineLooksComplete(line)) {
      return 2;
    }
    ++complete_lines;
  }
  if (complete_lines != kThreads * kLogsPerThread) {
    return 3;
  }

  std::remove(path);
  return 0;
}
