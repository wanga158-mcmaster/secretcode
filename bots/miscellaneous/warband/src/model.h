#pragma once
// Pure models (no SDK calls), unit-tested: camp clears by a pack, group-fight force, focus choice.
#include <algorithm>
#include <cmath>
#include <vector>

namespace wb::model {

// ---- camp clears ----
struct Monster {
    float hp = 0, str = 0;
};
struct CampForecast {
    int rounds = 0;       // rounds until the last monster dies
    float damage = 0;     // total damage the camps deal to the pack
    float peak = 0;       // camp damage per round at the start
};

// The pack deals `dps` per round to one monster at a time, harmful monsters first in Smith order
// (strength / health descending), so the forecast damage is the minimum for that dps. Camps act at
// round start: the monster killed in round k has attacked k - 1 times (the first hit wakes it).
// Monsters already in kill order (allocation-free; the hot path).
inline CampForecast forecast_sorted(const Monster* monsters, int n, float dps) {
    CampForecast f;
    if (dps <= 0) {
        f.rounds = 1 << 20;
        for (int i = 0; i < n; ++i) f.peak += monsters[i].str;
        f.damage = 1e9f;
        return f;
    }
    float cumulative = 0;
    for (int i = 0; i < n; ++i) {
        const auto& m = monsters[i];
        f.peak += m.str;
        cumulative += m.hp;
        const int k = static_cast<int>(std::ceil(cumulative / dps - 1e-4f));
        f.damage += m.str * static_cast<float>(std::max(0, k - 1));
        f.rounds = std::max(f.rounds, k);
    }
    return f;
}

// Smith order: strength per health descending, so harmless members go last.
inline bool smith_before(const Monster& a, const Monster& b) { return a.str * b.hp > b.str * a.hp; }

inline CampForecast forecast_clear(std::vector<Monster> monsters, float dps) {
    std::stable_sort(monsters.begin(), monsters.end(), smith_before);
    return forecast_sorted(monsters.data(), static_cast<int>(monsters.size()), dps);
}

// ---- champion fights ----
struct Fighter {
    float hp = 0;         // current health plus shield
    float dps = 0;        // expected damage per round in a brawl
    float burst = 0;      // ready ability damage that can land now
    int stuns = 0;        // ready stuns
    bool stun_immune = false;
};

// Lanchester square-law force of one side: total damage rate times total health. Bursts count as
// health removed from the other side, so they enter as dps over a short horizon.
inline float force(const std::vector<Fighter>& side, float horizon = 6.0f) {
    float dps = 0, hp = 0;
    for (const auto& f : side) {
        dps += f.dps + f.burst / horizon;
        hp += f.hp;
    }
    return dps * hp;
}

// Ratio of our force to theirs (large when they field nothing).
inline float force_ratio(const std::vector<Fighter>& us, const std::vector<Fighter>& them) {
    const float a = force(us), b = force(them);
    if (b <= 0) return a > 0 ? 100.0f : 1.0f;
    return a / b;
}

// Score of a focus candidate: lower is better. Time to kill under the pack's damage, discounted
// by the reward of the kill (carried gems) and by a stun already on it.
inline float focus_score(float effective_hp, float pack_dps, int gems, bool stunned, int distance) {
    const float ttk = effective_hp / std::max(1.0f, pack_dps);
    const float reward = 1.0f + static_cast<float>(gems) / 60.0f;
    return (ttk + 0.5f * static_cast<float>(distance)) / reward * (stunned ? 0.8f : 1.0f);
}

} // namespace wb::model
