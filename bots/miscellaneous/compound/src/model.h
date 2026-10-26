#pragma once
// SDK-free models of contenders/compound: plain inputs, pure functions, unit-tested in
// tests/model.cpp. Nothing here calls the engine.
//
// Mechanics assumed (docs/reference/GameRules.md, docs/rules-notes.md, engine 1.0.2):
// - Hitting any member wakes the whole camp group; each awake monster strikes the nearest
//   champion in its range once per round at round start, before champion turns; monsters do
//   not move. A monster killed during our k-th round of attacks has struck k-1 times.
// - Camps keep their damage while asleep, so a big group can be cleared over several trips.
// - Five matching shards combine into an emblem; only the emblem carries the passive.
// - Spawn heals the whole health bar at a turn end, so a recall is a full heal plus a shop.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace cm {

// ---------------------------------------------------------------------------------------------
// Camp fights
// ---------------------------------------------------------------------------------------------
struct Monster
{
    float hp{};
    float str{};
};

struct CampForecast
{
    int rounds{};      // rounds of our attacks until every monster is dead
    float damage{};    // damage we absorb (after our mitigation)
    float peak{};      // damage per round while every harmful monster lives
};

// Smith's rule: kill the monster with the most strength per health first; it minimises the
// total strength-weighted time monsters stay alive, i.e. the damage we take. Insertion sort:
// groups are small and std::sort costs ~275 fuel an element in the guest.
inline bool smith_before(const Monster& a, const Monster& b)
{
    return a.str * std::max(b.hp, 1.0f) > b.str * std::max(a.hp, 1.0f);
}

template <class It>
inline void smith_order(It first, It last)
{
    for (It i = first; i != last; ++i)
    {
        const Monster v = *i;
        It j = i;
        while (j != first && smith_before(v, *(j - 1))) { *j = *(j - 1); --j; }
        *j = v;
    }
}

inline void smith_order(std::vector<Monster>& ms) { smith_order(ms.begin(), ms.end()); }

// A group's living monsters, without heap allocation (the largest pool group has 30).
struct MonsterSet
{
    std::array<Monster, 32> m{};
    int n{};
    void add(Monster x) { if (n < 32) m[static_cast<std::size_t>(n++)] = x; }
};

// Forecast a solo (or pooled) camp fight at a sustained damage rate `dps` per round, with
// incoming camp damage multiplied by `taken` (Jungle 0.7, Durability 0.9, ...).
inline CampForecast forecast_camp(MonsterSet ms, float dps, float taken = 1.0f)
{
    CampForecast out;
    if (ms.n == 0) return out;
    if (dps <= 0.0f)
    {
        out.rounds = 1 << 20;
        out.damage = 1e30f;
        return out;
    }
    smith_order(ms.m.begin(), ms.m.begin() + ms.n);
    float cumulative = 0.0f;
    for (int i = 0; i < ms.n; ++i)
    {
        const auto& m = ms.m[static_cast<std::size_t>(i)];
        out.peak += m.str * taken;
        cumulative += std::max(m.hp, 0.0f);
        const int k = std::max(1, static_cast<int>(std::ceil(cumulative / dps - 1e-4f)));
        out.damage += m.str * taken * static_cast<float>(k - 1);
        out.rounds = k;
    }
    return out;
}

inline CampForecast forecast_camp(const std::vector<Monster>& v, float dps, float taken = 1.0f)
{
    MonsterSet ms;
    for (const auto& x : v) ms.add(x);
    return forecast_camp(ms, dps, taken);
}

// Constants of a group's Smith-order fight at full health: W = sum_k str_k x C_k with C_k the
// cumulative health up to member k. At damage rate d the damage taken is about W / d - S / 2
// (each monster strikes ceil(C_k / d) - 1 times; the rounding averages a half strike).
struct SmithConstants
{
    float weighted{};   // W
    float strength{};   // S
    float health{};     // total health
};

inline SmithConstants smith_constants(MonsterSet ms)
{
    smith_order(ms.m.begin(), ms.m.begin() + ms.n);
    SmithConstants c;
    for (int i = 0; i < ms.n; ++i)
    {
        const auto& m = ms.m[static_cast<std::size_t>(i)];
        c.health += std::max(0.0f, m.hp);
        c.weighted += m.str * c.health;
        c.strength += m.str;
    }
    return c;
}

// Party-size law (foundation E8): with covered rotation the champion-rounds per capture are
// minimised at n* = sqrt(W x T_trip / (delta x h x walk)): solo for wolves and clusters, pairs for
// single elks, three to four for elk packs, the whole team for golems and monoliths.
inline int party_size(float weighted, float dps, float usable_hp, float trip_rounds, float walk, int team)
{
    if (weighted <= 0.0f || dps <= 0.0f || usable_hp <= 0.0f || walk <= 0.0f) return 1;
    const float n = std::sqrt(weighted * trip_rounds / (dps * usable_hp * walk));
    return std::clamp(static_cast<int>(std::lround(n)), 1, std::max(1, team));
}

// Damage taken estimate from the constants; `scale` scales every member's health (a siege in
// progress), `taken` is our camp-damage multiplier.
inline float estimate_camp_damage(const SmithConstants& c, float dps, float taken, float scale = 1.0f)
{
    if (c.strength <= 0.0f) return 0.0f;
    if (dps <= 0.0f) return 1e30f;
    return std::max(0.0f, c.weighted * scale / dps - 0.5f * c.strength) * taken;
}

// A champion's sustained damage per round on camps, from its kit (per 8-round cycle):
// basic attacks every round, an attack reset adds one attack per cooldown, damage spells
// add modifier x Focus per cooldown, a landing dash adds its damage when monsters cluster.
struct Kit
{
    float str{};
    float focus{};
    bool ranged{};
    bool attack_reset{};        // either slot
    float spell_mod{};          // single-target damage spell usable on camps (x Focus)
    float aoe_mod{};            // area damage usable on camps (x Focus), radius 1
    float dash_mod{};           // landing damage of a dash (x Focus), radius 1
    float shield_mod{};         // self shield (x Focus)
    bool kill_reset{};          // Vendetta: kills reset cooldowns
    bool splash{};              // Varg: basic attacks splash radius 1
    float camp_mult{1.0f};      // design camp damage multiplier x Monster emblem
    float missing_bonus{};      // Heart: up to this bonus at zero health
    int cooldown{8};
};

inline float camp_dps(const Kit& k, int monsters = 1)
{
    const float cd = static_cast<float>(std::max(1, k.cooldown));
    float per_cycle = k.str * cd;
    // one cast per turn: the reset and the spell share the turn budget but not the cooldowns,
    // and over a cycle both fire once.
    if (k.attack_reset) per_cycle += k.str;
    per_cycle += k.spell_mod * k.focus;
    const float spread = static_cast<float>(std::clamp(monsters, 1, 3));
    per_cycle += k.aoe_mod * k.focus * spread;
    per_cycle += k.dash_mod * k.focus * spread;
    if (k.splash && monsters > 1) per_cycle += k.str * cd * 0.5f;
    float dps = per_cycle / cd;
    if (k.kill_reset && k.attack_reset) dps += k.str * 0.05f * static_cast<float>(monsters);
    return dps * k.camp_mult;
}

// Health a shield removes from camp damage per round (it absorbs up to its value per cycle).
inline float shield_per_round(const Kit& k) { return k.shield_mod * k.focus / static_cast<float>(std::max(1, k.cooldown)); }

// ---------------------------------------------------------------------------------------------
// The race and the value of gold
// ---------------------------------------------------------------------------------------------
// Rounds the race still has to run: the leader's gems to go at the observed banking rate,
// bounded by the round limit. `rate` is gems a round for the faster team (0 = unknown).
inline int race_rounds_left(int round, int max_rounds, int our_banked, int their_banked, float our_rate, float their_rate,
                            int fallback)
{
    const int cap = std::max(1, max_rounds - round);
    const auto left = [&](int banked, float rate) -> float {
        if (rate <= 0.05f) return static_cast<float>(fallback);
        return static_cast<float>(std::max(0, 1000 - banked)) / rate;
    };
    const float est = std::min(left(our_banked, our_rate), left(their_banked, their_rate));
    return std::clamp(static_cast<int>(est), 1, cap);
}

// Gold is worth the gems its items add before the race ends. Treat the marginal item as
// adding `item_gain` gems a round per gold for the rest of the race, so a gold is worth
// item_gain x rounds_left gems, capped by the early-game exchange rate `gem_gold` (gold per gem)
// from above. Returns gems per gold.
inline float gems_per_gold(int rounds_left, float gem_gold, int reference_rounds)
{
    const float early = 1.0f / std::max(0.1f, gem_gold);
    const float frac = std::clamp(static_cast<float>(rounds_left) / static_cast<float>(std::max(1, reference_rounds)), 0.0f, 1.0f);
    return early * frac;
}

// ---------------------------------------------------------------------------------------------
// Shopping
// ---------------------------------------------------------------------------------------------
enum class Family : std::uint8_t { monster, power, durability, treasure, jungle, home, bruiser };
enum class Stat : std::uint8_t { strength, focus, health };

struct ShopStep
{
    Family family;
    Stat stat;
    int shards;  // shards of this (family, stat) the step asks for in total
};

// Item index in cambc::item_type::enum_type order: family-major, (strength, focus, health),
// shard then emblem. Shards are even indices.
inline int shard_index(Family f, Stat s) { return static_cast<int>(f) * 6 + static_cast<int>(s) * 2; }
inline int emblem_index(Family f, Stat s) { return shard_index(f, s) + 1; }

enum class Role : std::uint8_t { tank, vesper, heart, other };

// Shop plans. Plan 0 (Health first, the #1 online team's order): Treasure, Jungle, Bruiser, then
// Durability and Bruiser forever; Vesper takes Strength in its first two emblems; Heart takes
// Focus for its Jungle emblem (its dash and shield scale with Focus).
inline std::vector<ShopStep> shop_plan(int plan, Role role)
{
    using F = Family;
    using S = Stat;
    std::vector<ShopStep> p;
    const auto add = [&](F f, S s, int n) { p.push_back({ f, s, n }); };
    if (plan == 1)
    {
        // Strength first (the main bot's order), for comparison.
        const S main = role == Role::heart ? S::focus : S::strength;
        add(F::treasure, main, 5);
        add(F::jungle, main, 5);
        add(F::durability, S::health, 5);
        add(F::bruiser, main, 60);
        return p;
    }
    switch (role)
    {
    case Role::vesper:
        add(F::treasure, S::strength, 5);
        add(F::jungle, S::strength, 5);
        add(F::bruiser, S::health, 5);
        add(F::durability, S::health, 5);
        add(F::bruiser, S::strength, 60);
        break;
    case Role::heart:
        add(F::treasure, S::health, 5);
        add(F::jungle, S::focus, 5);
        add(F::bruiser, S::health, 5);
        add(F::durability, S::health, 5);
        add(F::bruiser, S::focus, 60);
        break;
    default:
        add(F::treasure, S::health, 5);
        add(F::jungle, S::strength, 5);
        add(F::bruiser, S::health, 5);
        add(F::durability, S::health, 5);
        add(F::bruiser, S::health, 60);
        break;
    }
    return p;
}

// Owned shards of each (family, stat) counting emblems as five shards. `items` is indexed by
// item_type (shards even, emblems odd).
inline int owned_shards(const std::array<std::uint32_t, 42>& items, Family f, Stat s)
{
    return static_cast<int>(items[static_cast<std::size_t>(shard_index(f, s))])
         + 5 * static_cast<int>(items[static_cast<std::size_t>(emblem_index(f, s))]);
}

// The next shard to buy under a plan (index into item_type), or -1 when the plan is done.
inline int next_shard(const std::vector<ShopStep>& plan, const std::array<std::uint32_t, 42>& items)
{
    // Steps are cumulative per (family, stat): a later step of the same pair asks for more.
    std::array<int, 42> asked{};
    for (const auto& st : plan)
    {
        const auto i = static_cast<std::size_t>(shard_index(st.family, st.stat));
        asked[i] += st.shards;
        if (owned_shards(items, st.family, st.stat) < asked[i]) return static_cast<int>(i);
    }
    return -1;
}

// ---------------------------------------------------------------------------------------------
// Skirmish forecast (champion fights)
// ---------------------------------------------------------------------------------------------
struct Fighter
{
    float hp{};
    float dps{};        // damage per round dealt to champions
    float value{1.0f};  // loss weight (carried gems raise it)
    int join{1};        // first round it is in contact
    bool ours{};
};

struct SkirmishResult
{
    float our_loss{};
    float their_loss{};
    int our_deaths{};
    int their_deaths{};
    int first_kill_round{-1};   // round of the first enemy death (-1 none)
    int first_loss_round{-1};   // round of our first death (-1 none)
};

// Each round every fighter in contact hits the opposing fighter in contact with the least
// health (focus fire). Deaths happen at the end of the round. Runs `horizon` rounds.
inline SkirmishResult simulate_skirmish(std::vector<Fighter> fs, int horizon)
{
    SkirmishResult r;
    const int n = static_cast<int>(fs.size());
    std::vector<float> incoming(static_cast<std::size_t>(n));
    for (int round = 1; round <= horizon; ++round)
    {
        std::fill(incoming.begin(), incoming.end(), 0.0f);
        int focus_ours = -1, focus_theirs = -1;
        for (int i = 0; i < n; ++i)
        {
            const auto& f = fs[static_cast<std::size_t>(i)];
            if (f.hp <= 0.0f || f.join > round) continue;
            int& focus = f.ours ? focus_ours : focus_theirs;
            if (focus < 0 || f.hp < fs[static_cast<std::size_t>(focus)].hp) focus = i;
        }
        if (focus_ours < 0 || focus_theirs < 0)
        {
            // nobody of one side in contact yet: skip to the next round
            bool anyone_left_ours = false, anyone_left_theirs = false;
            for (const auto& f : fs)
                if (f.hp > 0.0f) (f.ours ? anyone_left_ours : anyone_left_theirs) = true;
            if (!anyone_left_ours || !anyone_left_theirs) break;
            continue;
        }
        for (int i = 0; i < n; ++i)
        {
            const auto& f = fs[static_cast<std::size_t>(i)];
            if (f.hp <= 0.0f || f.join > round) continue;
            incoming[static_cast<std::size_t>(f.ours ? focus_theirs : focus_ours)] += f.dps;
        }
        for (int i = 0; i < n; ++i)
        {
            auto& f = fs[static_cast<std::size_t>(i)];
            if (f.hp <= 0.0f || incoming[static_cast<std::size_t>(i)] <= 0.0f) continue;
            f.hp -= incoming[static_cast<std::size_t>(i)];
            if (f.hp <= 0.0f)
            {
                if (f.ours)
                {
                    r.our_loss += f.value;
                    ++r.our_deaths;
                    if (r.first_loss_round < 0) r.first_loss_round = round;
                }
                else
                {
                    r.their_loss += f.value;
                    ++r.their_deaths;
                    if (r.first_kill_round < 0) r.first_kill_round = round;
                }
            }
        }
    }
    return r;
}

// Rounds until one side kills the other alone (a duel), for quick checks.
inline float rounds_to_kill(float hp, float dps) { return dps > 0.0f ? hp / dps : 1e9f; }

// ---------------------------------------------------------------------------------------------
// Shared memory protocol (8 bytes per team)
// ---------------------------------------------------------------------------------------------
// Bytes 0..4: one per champion slot (draft order): bits 0-5 target group + 1 (0 none) or, in
//             mode `converge`, the rounds to arrival; bits 6-7 the mode.
// Bytes 5..7: the convergence beacon: x + 1 (7 bits, 0 = no beacon, so zeroed memory is
//             empty), y (7 bits), round mod 64 (6 bits), and the number of allies asked for
//             (4 bits).
enum class Mode : std::uint8_t { farm = 0, converge = 1, fight = 2, home = 3 };

inline std::uint8_t encode_slot(Mode m, int value)
{
    return static_cast<std::uint8_t>((static_cast<unsigned>(m) << 6) | (static_cast<unsigned>(std::clamp(value, 0, 63))));
}
inline Mode slot_mode(std::uint8_t b) { return static_cast<Mode>(b >> 6); }
inline int slot_value(std::uint8_t b) { return b & 63; }

struct Beacon
{
    int x{-1}, y{-1};
    int round_mod{};
    int asks{};
    bool valid() const { return x >= 0; }
};

inline std::array<std::uint8_t, 3> encode_beacon(const Beacon& b)
{
    const std::uint32_t x = b.valid() ? static_cast<std::uint32_t>(std::clamp(b.x, 0, 126) + 1) : 0u;
    const std::uint32_t y = static_cast<std::uint32_t>(std::clamp(b.y, 0, 127));
    const std::uint32_t r = static_cast<std::uint32_t>(b.round_mod & 63);
    const std::uint32_t a = static_cast<std::uint32_t>(std::clamp(b.asks, 0, 15));
    const std::uint32_t w = x | (y << 7) | (r << 14) | (a << 20);
    return { static_cast<std::uint8_t>(w & 0xFF), static_cast<std::uint8_t>((w >> 8) & 0xFF), static_cast<std::uint8_t>((w >> 16) & 0xFF) };
}

inline Beacon decode_beacon(const std::array<std::uint8_t, 3>& bytes)
{
    const std::uint32_t w = bytes[0] | (static_cast<std::uint32_t>(bytes[1]) << 8) | (static_cast<std::uint32_t>(bytes[2]) << 16);
    Beacon b;
    const int x = static_cast<int>(w & 127u);
    if (x == 0) return b;
    b.x = x - 1;
    b.y = static_cast<int>((w >> 7) & 127u);
    b.round_mod = static_cast<int>((w >> 14) & 63u);
    b.asks = static_cast<int>((w >> 20) & 15u);
    return b;
}

// Age in rounds of a beacon stamped with round_mod, seen at `round` (0..63).
inline int beacon_age(int round, int round_mod) { return ((round & 63) - round_mod + 64) & 63; }

}  // namespace cm
