#pragma once
// Small SDK helpers: geometry, fuel, bounded logging.
#include <cambc.h>
#include <cstdint>
#include <cstdio>
#include "config.h"

namespace tt {

using cambc::vec2;

inline int cheb(vec2 a, vec2 b)
{
    const int dx = a.x > b.x ? a.x - b.x : b.x - a.x;
    const int dy = a.y > b.y ? a.y - b.y : b.y - a.y;
    return dx > dy ? dx : dy;
}

inline long fuel_left() { return static_cast<long>(cambc::fuel_remaining()); }

inline bool fuel_ok(long need) { return fuel_left() > need + cfg::fuel_reserve; }

inline constexpr vec2 kDirs[8] = { {0, 1}, {1, 1}, {1, 0}, {1, -1}, {0, -1}, {-1, -1}, {-1, 0}, {-1, 1} };

// Per-turn fuel burn that changes trajectories without changing intent (a perturbation seed).
inline void noise_burn()
{
    if constexpr (cfg::noise_burn > 0) {
        volatile int sink = 0;
        for (int i = 0; i < cfg::noise_burn; ++i) sink += i;
        (void)sink;
    }
}

} // namespace tt

// Bounded logging: one formatted line costs ~1K fuel, so lines print only with fuel to spare.
namespace tt {
// Body slot printed in front of every log line (-1 in the draft).
inline int g_log_slot = -1;
} // namespace tt

#define TT_LOG(level, ...)                                                                    \
    do {                                                                                      \
        if (cfg::log_level >= (level) && ::tt::fuel_left() > cfg::fuel_log_spare) {          \
            std::printf("[%d] ", ::tt::g_log_slot);                                           \
            std::printf(__VA_ARGS__);                                                         \
        }                                                                                     \
    } while (0)
