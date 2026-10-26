#pragma once
// Pure economic models (no SDK): camp clears, body throughput, rates. Unit-tested in tests/.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace tt {

inline constexpr int kMaxMembers = 32;
inline constexpr int kMaxGroups = 64;
inline constexpr int kMaxBodies = 6;
inline constexpr int kMaxAltars = 24;
inline constexpr int kInf = 1 << 20;

// A camp group as the planner sees it. Members are kept in Smith order (most strength per
// health first): killing in that order minimises the damage a fixed team takes.
struct GroupSpec {
    GroupSpec() {}
    int gems = 0, gold = 0, cooldown = 60;
    int n = 0;
    // Only the first n entries are meaningful. No zero-initialisation: memset is a byte loop in
    // this toolchain (about 3 fuel a byte), and these specs are built per group per instance.
    float hp[kMaxMembers];
    float str[kMaxMembers];
    int range[kMaxMembers];
    float hp_total = 0, str_total = 0;
    int max_range = 0;
    bool harmless = true;
    // Closed form of the Smith-order clear at damage d per round (members killed in order, each
    // taking h_i / d rounds plus half a round of rounding on average):
    //   rounds(d) = hp_total / d + n / 2,  damage(d) = smith_a / d + smith_b,
    // with S_i the strength still alive while member i dies: smith_a = sum S_i h_i, smith_b = sum S_i / 2.
    float smith_a = 0, smith_b = 0;

    void add(float h, float s, int r)
    {
        if (n >= kMaxMembers) return;
        hp[n] = h; str[n] = s; range[n] = r; ++n;
        hp_total += h; str_total += s;
        max_range = std::max(max_range, r);
        if (s > 0) harmless = false;
    }
    // Smith order: highest strength per health first (harmless members last, lightest first).
    void sort_members()
    {
        for (int i = 1; i < n; ++i)
            for (int j = i; j > 0 && before(j, j - 1); --j) {
                std::swap(hp[j], hp[j - 1]);
                std::swap(str[j], str[j - 1]);
                std::swap(range[j], range[j - 1]);
            }
        float alive = str_total;
        smith_a = smith_b = 0;
        for (int i = 0; i < n; ++i) {
            smith_a += alive * hp[i];
            smith_b += alive * 0.5f;
            alive -= str[i];
        }
    }
    bool before(int a, int b) const
    {
        const float ka = str[a] / std::max(1.0f, hp[a]), kb = str[b] / std::max(1.0f, hp[b]);
        if (ka != kb) return ka > kb;
        return hp[a] < hp[b];
    }
};

// Rounds to kill the members (Smith order) at `dps`, and the damage a single tank takes while
// they live. `left` may hold remaining health per member (partial sieges); nullptr = full.
struct Clear {
    float rounds = 0;
    float damage = 0;
};

inline Clear smith_clear(const GroupSpec& g, float dps, const float* left = nullptr)
{
    Clear c;
    if (dps <= 0.0f) { c.rounds = kInf; c.damage = kInf; return c; }
    float alive_str = 0;
    for (int i = 0; i < g.n; ++i)
        if ((left ? left[i] : g.hp[i]) > 0) alive_str += g.str[i];
    for (int i = 0; i < g.n; ++i) {
        const float h = left ? left[i] : g.hp[i];
        if (h <= 0) continue;
        // Whole rounds per member: a kill rarely lands exactly on the boundary.
        const float k = std::ceil(h / dps - 1e-4f);
        c.damage += alive_str * k;
        c.rounds += k;
        alive_str -= g.str[i];
    }
    return c;
}

// O(1) Smith clear from the precomputed constants (planning and target rates; within about one
// round per member of the exact simulation above).
inline Clear smith_fast(const GroupSpec& g, float dps)
{
    Clear c;
    if (dps <= 0.0f) { c.rounds = kInf; c.damage = kInf; return c; }
    c.rounds = g.hp_total / dps + 0.5f * static_cast<float>(g.n);
    c.damage = g.smith_a / dps + g.smith_b;
    return c;
}

// A body (one champion) as the planner sees it.
struct BodySpec {
    int type = -1;          // cambc::champion_type value
    float hp = 1000;        // maximum health
    float dps = 50;         // damage per round against camps (single target, sustained)
    int range = 1;          // basic attack range
    float gold_mult = 1.0f; // passive gold bonus
    float taken = 1.0f;     // camp damage multiplier (Jungle 0.7)
    float shield = 0.0f;    // shield absorbed per round (cast every cooldown)
    bool dash = false;      // can dash while travelling
    bool fragile = false;   // linked fate (Cassi and Issac)
};

// Damage a body takes clearing g alone, after its passives.
inline float solo_damage(const GroupSpec& g, const BodySpec& b, const Clear& c)
{
    if (g.harmless) return 0.0f;
    return std::max(0.0f, c.damage * b.taken - b.shield * c.rounds);
}

// Rounds of walking for `tiles` of distance, counting dashes (2 extra tiles every 8 rounds).
inline float walk_rounds(int tiles, bool dash)
{
    if (tiles <= 0) return 0.0f;
    return dash ? tiles * 0.8f : static_cast<float>(tiles);
}

// Throughput of one task: value per round, where every spent health bar costs a heal trip.
struct Rate {
    float value = 0;   // gems + weighted gold
    float rounds = 0;  // total rounds the body spends
    float rate() const { return rounds > 0 ? value / rounds : 0.0f; }
};

} // namespace tt
