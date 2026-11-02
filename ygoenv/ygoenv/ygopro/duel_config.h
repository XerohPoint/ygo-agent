#ifndef YGOENV_YGOPRO_DUEL_CONFIG_H_
#define YGOENV_YGOPRO_DUEL_CONFIG_H_

#include <cstdint>

namespace ygopro::duel_config {
// Shared by self-play and the standalone rules certification executable.
inline constexpr int32_t rules = 1;
inline constexpr int32_t options = (rules & 0xff) << 16;
inline constexpr int starting_lp = 8000;
inline constexpr int opening_hand = 5;
inline constexpr int draw_count = 1;
}  // namespace ygopro::duel_config

#endif
