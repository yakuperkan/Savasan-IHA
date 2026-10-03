/**
 * @file test_lock_state_file.cpp
 * @brief lock_state_file.hpp atomik yazma / içerik doğrulaması.
 */
#include "runners/lock_state_file.hpp"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>

namespace {

std::string ReadFile(const char* path) {
  std::ifstream in(path);
  assert(in.good());
  std::string s;
  std::getline(in, s);
  return s;
}

}  // namespace

int main() {
  const char* path = "/tmp/test_savasan_lock_state_unit.txt";
  std::remove(path);
  std::remove((std::string(path) + ".tmp").c_str());

  assert(savasan::runners::WriteLockStateFile(true, path));
  assert(ReadFile(path) == "1");

  assert(savasan::runners::WriteLockStateFile(false, path));
  assert(ReadFile(path) == "0");

  assert(!savasan::runners::WriteLockStateFile(true, nullptr));
  assert(!savasan::runners::WriteLockStateFile(true, ""));

  std::remove(path);
  return 0;
}
