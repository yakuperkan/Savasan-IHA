#include "common/scope_profiler.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>

namespace {

void SetEnv(const char* key, const char* value) {
#if defined(_WIN32)
  _putenv_s(key, value);
#else
  setenv(key, value, 1);
#endif
}

void UnsetEnv(const char* key) {
#if defined(_WIN32)
  _putenv_s(key, "");
#else
  unsetenv(key);
#endif
}

bool LineContainsProfilerWarn(const std::string& line) {
  return line.find("][WARN][Perf]") != std::string::npos &&
         line.find("süre eşiği aşıldı") != std::string::npos;
}

}  // namespace

int main() {
  const char* const path = "/tmp/savasan_scope_profiler_test.txt";
  if (std::freopen(path, "w", stdout) == nullptr) {
    return 1;
  }

  SetEnv("SAVASAN_PROFILER", "1");
  SetEnv("SAVASAN_PROFILER_WARN_MS", "5");
  SetEnv("SAVASAN_PROFILER_LOG_INTERVAL_MS", "100");

  {
    savasan::common::ScopeProfiler fast_scope("fast_scope");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  {
    savasan::common::ScopeProfiler slow_scope("slow_scope", std::chrono::milliseconds(5));
    std::this_thread::sleep_for(std::chrono::milliseconds(12));
  }
  {
    savasan::common::ScopeProfiler cancelled_scope("cancelled_scope", std::chrono::milliseconds(1));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    cancelled_scope.Cancel();
  }

  savasan::common::TickTockTimer timer("manual_timer", std::chrono::milliseconds(5));
  timer.Tick();
  std::this_thread::sleep_for(std::chrono::milliseconds(12));
  timer.Tock();

  std::fflush(stdout);

  std::ifstream in(path);
  std::string line;
  int warn_lines = 0;
  int cancelled_warn_lines = 0;
  while (std::getline(in, line)) {
    if (LineContainsProfilerWarn(line)) {
      ++warn_lines;
    }
    if (line.find("cancelled_scope") != std::string::npos) {
      ++cancelled_warn_lines;
    }
  }

  UnsetEnv("SAVASAN_PROFILER");
  UnsetEnv("SAVASAN_PROFILER_WARN_MS");
  UnsetEnv("SAVASAN_PROFILER_LOG_INTERVAL_MS");
  std::remove(path);

  if (warn_lines < 2) {
    return 2;
  }
  if (cancelled_warn_lines != 0) {
    return 3;
  }
  return 0;
}
