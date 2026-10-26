#pragma once
#include <algorithm>

namespace clockwork {
enum class Lifecycle { unknown, alive, reward, respawning };
inline constexpr unsigned clock_age_limit = 16;
inline constexpr unsigned wait_limit = 2;
inline constexpr unsigned unreachable_limit = 4;
inline constexpr unsigned rebuild_limit = 8;

struct Observation {
    unsigned round = 0, due = 0;
    int raw_timer = 0;
    Lifecycle state = Lifecycle::unknown;
    bool censored = true;
};
constexpr bool expired(const Observation& clock, unsigned round) {
    return clock.state == Lifecycle::unknown || round - clock.round > clock_age_limit;
}
constexpr bool premature(const Observation& clock, unsigned round, unsigned travel) {
    return !expired(clock, round) && clock.state == Lifecycle::respawning
        && clock.due > round + travel;
}
constexpr unsigned nominal_due(unsigned round, int timer) {
    return round + static_cast<unsigned>(std::max(0, timer));
}
// This is only permission to consider waiting. Visibility, safe tile and an
// actual current respawn observation are checked by the engine-facing caller.
constexpr bool can_wait(int raw_timer, unsigned waited) {
    return raw_timer <= 2 && waited < wait_limit;
}
} // namespace clockwork
