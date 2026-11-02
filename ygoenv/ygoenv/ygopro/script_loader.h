#pragma once
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include "duel_config.h"
#include "historical_scripts.h"

namespace ygopro {
// Caller owns the returned buffer. Shared with the production preload path.
inline uint8_t *read_card_script(const std::string &path, int *lenptr,
                                 int rule = duel_config::rules) {
  auto override = historical_card_script(path, rule);
  if (!override.empty()) {
    auto *buf = new uint8_t[override.size()];
    std::memcpy(buf, override.data(), override.size());
    *lenptr = static_cast<int>(override.size());
    return buf;
  }
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    *lenptr = 0;
    return nullptr;
  }
  file.seekg(0, std::ios::end);
  int len = file.tellg();
  file.seekg(0, std::ios::beg);
  auto suffix = historical_script_suffix(path, rule);
  uint8_t *buf = new uint8_t[len + suffix.size()];
  file.read((char *)buf, len);
  if (!suffix.empty()) std::memcpy(buf + len, suffix.data(), suffix.size());
  *lenptr = len + static_cast<int>(suffix.size());
  return buf;
}

} // namespace ygopro
