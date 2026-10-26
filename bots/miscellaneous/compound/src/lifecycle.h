#pragma once
#include <algorithm>
#include <vector>

namespace cl {

enum class Phase { unknown, alive, reward, respawning };

// An observation, a reservation and an estimated deadline are different kinds of information.
// In particular timer zero only establishes eligibility; an occupied member can suppress respawn.
struct Observation
{
    Phase phase{ Phase::unknown };
    int round{-1};
    int eligible{-1};

    bool fresh(int now) const { return round == now; }
    bool can_capture(int now) const { return fresh(now) && phase == Phase::reward; }
    bool is_respawning(int now) const { return fresh(now) && phase == Phase::respawning; }
    bool suppressed(int now) const { return is_respawning(now) && eligible <= now; }
};

inline Observation observed_respawn(int now, int timer)
{
    return { Phase::respawning, now, now + std::max(0, timer) };
}

// Per-champion WASM instance, independent of Brain layout. An OFF build never creates this bank.
inline std::vector<Observation>& groups()
{
    static std::vector<Observation> observations;
    return observations;
}

} // namespace cl
