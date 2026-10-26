#pragma once
// Small helpers shared by every module: geometry, fuel and bounded logging.
#include <cambc.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace wb {

using cambc::bitboard;
using cambc::vec2;
using cambc::team;

inline int cheb(vec2 a, vec2 b) { return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y)); }

inline bool in_bounds(vec2 p) { return p.x >= 0 && p.y >= 0 && p.x < cambc::map::width() && p.y < cambc::map::height(); }

inline long fuel() { return static_cast<long>(cambc::fuel_remaining()); }

inline bitboard square(vec2 c, int radius) {
    bitboard b;
    b.draw_chebyshev(c, static_cast<float>(radius));
    return b;
}

inline constexpr vec2 kDirs[8] = {{0, 1}, {1, 1}, {1, 0}, {1, -1}, {0, -1}, {-1, -1}, {-1, 0}, {-1, 1}};

// Bounded logging: a formatted line costs ~1K fuel, so only a few per champion per game.
inline int& log_budget() {
    static int budget = 40;
    return budget;
}

template <class... A>
inline void logf(const char* fmt, A... args) {
    if (log_budget() <= 0 || fuel() < 20000) return;
    --log_budget();
    if constexpr (sizeof...(A) == 0) std::fputs(fmt, stdout);
    else std::printf(fmt, args...);
}

// Errors are always reported (bounded separately) so health checks see them.
inline int& error_budget() {
    static int budget = 30;
    return budget;
}

template <class... A>
inline void log_error(const char* fmt, A... args) {
    if (error_budget() <= 0) return;
    --error_budget();
    if constexpr (sizeof...(A) == 0) std::fputs(fmt, stdout);
    else std::printf(fmt, args...);
}

} // namespace wb
