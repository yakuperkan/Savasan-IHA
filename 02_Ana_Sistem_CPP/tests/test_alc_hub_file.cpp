/**
 * @file test_alc_hub_file.cpp
 * @brief alc_hub_file.hpp atomik hub yazma doğrulaması.
 */
#include "runners/alc_hub_file.hpp"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

namespace {

std::string ReadAll(const char* path) {
  std::ifstream in(path);
  assert(in.good());
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

int main() {
  const char* path = "/tmp/test_savasan_alc_hub_unit.env";
  std::remove(path);
  std::remove((std::string(path) + ".tmp").c_str());

  savasan::runners::AlcHubSnapshot s{};
  s.valid = true;
  s.enlem = 41.1;
  s.boylam = 36.2;
  s.irtifa_m = 120.5f;
  s.lock = 1;
  s.hedef_x = 400;
  s.hedef_y = 300;
  assert(savasan::runners::WriteAlcHubFile(s, path));
  const std::string body = ReadAll(path);
  assert(body.find("valid=1") != std::string::npos);
  assert(body.find("lock=1") != std::string::npos);
  assert(body.find("hedef_x=400") != std::string::npos);
  assert(body.find("enlem=") != std::string::npos);

  assert(!savasan::runners::WriteAlcHubFile(s, nullptr));
  assert(!savasan::runners::WriteAlcHubFile(s, ""));

  std::remove(path);
  return 0;
}
