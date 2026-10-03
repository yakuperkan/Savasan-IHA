#include <cassert>
#include <chrono>
#include <cstring>
#include <cstdlib>

#include "runners/server_clock.hpp"

int main() {
  using savasan::runners::ApplyCompetitionHttpTimeSync;
  using savasan::runners::GetClockSyncSourceCstr;
  using savasan::runners::GetHttpClockSyncAgeMs;
  using savasan::runners::GetServerTimeOffsetMs;
  using savasan::runners::ServerTimeMsNow;

  setenv("SAVASAN_SERVER_TIME_OFFSET_MS", "123", 1);
  unsetenv("SAVASAN_SERVER_TIME_OFFSET_FILE");

  assert(GetServerTimeOffsetMs() == 123);

  const auto t0 = ServerTimeMsNow();
  const auto t1 = ServerTimeMsNow();
  assert(t1 >= t0);

  unsetenv("SAVASAN_SERVER_TIME_OFFSET_FILE");
  setenv("SAVASAN_SERVER_TIME_OFFSET_MS", "0", 1);
  using namespace std::chrono;
  const auto local_before = duration_cast<milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                                .count();
  ApplyCompetitionHttpTimeSync(static_cast<std::int64_t>(local_before + 5000));
  assert(GetHttpClockSyncAgeMs() >= 0);
  assert(std::strcmp(GetClockSyncSourceCstr(), "http") == 0);
  const auto t_sync = ServerTimeMsNow();
  assert(t_sync >= local_before + 4990);

  return 0;
}
