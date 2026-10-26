#pragma once
// SDK-independent models: plain inputs, pure functions, unit-tested in tests/main-model.cpp.
//
// Mechanics assumed (GameRules.md, "Camps & capture"): hitting any member wakes the whole
// group; each awake monster attacks the nearest champion in range once per round, at round
// start, before champion turns; monsters do not move. Captures need capture_time rounds.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mb::model {

struct Monster
{
    float hp{};
    float strength{};
};

struct FightForecast
{
    int rounds{};       // rounds of our attacking until every monster is dead
    float damage{};     // damage the engaged side absorbs in total
    float peak_rate{};  // damage per round while every monster is alive
};

// Forecast a camp fight against `monsters` with a sustained team damage rate `dps` (per
// round). Kill order minimises total damage taken (Smith's rule: descending strength/hp).
// A monster killed during our k-th round of attacking has attacked k-1 times, because the
// group wakes on our first hit and camps act at the start of each later round.
inline FightForecast forecast_camp_fight(std::vector<Monster> monsters, float dps)
{
    FightForecast out;
    if (monsters.empty())
        return out;
    if (dps <= 0.0f)
    {
        out.rounds = 1 << 20;
        out.damage = 1e30f;
        return out;
    }
    std::sort(monsters.begin(), monsters.end(), [](const Monster& a, const Monster& b) {
        // a before b when a.strength/a.hp > b.strength/b.hp (cross-multiplied, hp > 0).
        return a.strength * std::max(b.hp, 1.0f) > b.strength * std::max(a.hp, 1.0f);
    });
    float cumulative = 0.0f;
    for (const auto& m : monsters)
    {
        out.peak_rate += m.strength;
        cumulative += std::max(m.hp, 0.0f);
        const int k = std::max(1, static_cast<int>(std::ceil(cumulative / dps - 1e-4f)));
        out.damage += m.strength * static_cast<float>(k - 1);
        out.rounds = k;
    }
    return out;
}

// Constants of a group's Smith-order fight, computed once per group (sorting is costly).
struct FightConstants
{
    float total_hp{};
    float weighted_hp{};     // sum_k strength_k * cumulative_hp_k in Smith order
    float total_strength{};
};

inline FightConstants fight_constants(std::vector<Monster> monsters)
{
    std::sort(monsters.begin(), monsters.end(), [](const Monster& a, const Monster& b) {
        return a.strength * std::max(b.hp, 1.0f) > b.strength * std::max(a.hp, 1.0f);
    });
    FightConstants c;
    for (const auto& m : monsters)
    {
        c.total_hp += std::max(m.hp, 0.0f);
        c.weighted_hp += m.strength * c.total_hp;
        c.total_strength += m.strength;
    }
    return c;
}

// O(1) forecast from precomputed constants; `hp_scale` scales every member's health
// (a partially damaged group). Exact damage lies in [W/d - S, W/d]; we take the midpoint.
inline FightForecast forecast_from_constants(const FightConstants& c, float dps, float hp_scale)
{
    FightForecast out;
    if (c.total_hp <= 0.0f) return out;
    if (dps <= 0.0f)
    {
        out.rounds = 1 << 20;
        out.damage = 1e30f;
        return out;
    }
    const float hp = c.total_hp * hp_scale;
    out.rounds = std::max(1, static_cast<int>(std::ceil(hp / dps - 1e-4f)));
    out.damage = std::max(0.0f, c.weighted_hp * hp_scale / dps - 0.5f * c.total_strength);
    out.peak_rate = c.total_strength;
    return out;
}

// Siege plan: camps keep damage while asleep (verified, mechanics.md), so a fight may take
// several trips: fight until the engaged health pool is spent, retreat, heal, return.
// `pool` is health the team can spend per trip; `overhead` is rounds lost per extra trip.
struct SiegePlan
{
    int trips{};
    int rounds{};  // fighting rounds plus extra-trip overhead
    bool feasible{};
};

inline SiegePlan plan_siege(const FightForecast& f, float pool, int overhead, float margin, int max_trips)
{
    SiegePlan p;
    if (f.damage <= 0.0f)
    {
        p.trips = 1;
        p.rounds = f.rounds;
        p.feasible = true;
        return p;
    }
    if (pool <= 0.0f) return p;
    p.trips = std::max(1, static_cast<int>(std::ceil(f.damage * margin / pool - 1e-4f)));
    p.rounds = f.rounds + (p.trips - 1) * std::max(0, overhead);
    p.feasible = p.trips <= max_trips;
    return p;
}

// Fuel model of a distance-field search: cost ~ overhead + rate * expansion steps. Returns the
// per-step rate to plan with after measuring one search: the costliest rate seen (+15%), with the
// fixed overhead taken out and searches too short to separate the two (< min_steps) ignored.
// (Before 2026-10-21 the overhead was charged to the steps: a 2-step search of a group in a
// pocket measured ~1700 per step against a real ~100, and every later field was refused.)
inline std::uint64_t updated_step_cost(std::uint64_t current, bool measured, std::uint64_t cost, unsigned steps,
                                       std::uint64_t overhead, unsigned min_steps = 10)
{
    if (steps < min_steps) return current;
    const std::uint64_t variable = cost > overhead ? cost - overhead : 0;
    const std::uint64_t rate = variable * 115 / 100 / steps + 1;
    return measured ? std::max(current, rate) : rate;
}

// Two-pool variant: the first trip can spend `first_pool` (health left above the recall rule;
// 0 means recall first), every later trip `full_pool`. A fresh champion (first == full) plans as
// plan_siege(f, full_pool, ...); a drained one pays one extra trip.
inline SiegePlan plan_siege(const FightForecast& f, float first_pool, float full_pool, int overhead, float margin, int max_trips)
{
    SiegePlan p;
    if (f.damage <= 0.0f)
    {
        p.trips = 1;
        p.rounds = f.rounds;
        p.feasible = true;
        return p;
    }
    if (full_pool <= 0.0f) return p;
    const float need = f.damage * margin;
    const float first = std::clamp(first_pool, 0.0f, full_pool);
    const int more = need > first ? std::max(1, static_cast<int>(std::ceil((need - first) / full_pool - 1e-4f))) : 0;
    p.trips = 1 + more;
    p.rounds = f.rounds + (p.trips - 1) * std::max(0, overhead);
    p.feasible = p.trips <= max_trips;
    return p;
}

// Share of the group's remaining health one trip removes: the pool lasts pool/avg_rate
// rounds of incoming damage, during which the team deals dps per round.
inline float trip_progress(const FightForecast& f, float dps, float pool, float hp_left)
{
    if (hp_left <= 0.0f) return 1.0f;
    if (f.damage <= 0.0f || f.rounds <= 0) return 1.0f;
    const float avg_rate = f.damage / static_cast<float>(f.rounds);
    const float rounds = avg_rate > 0.0f ? pool / avg_rate : static_cast<float>(f.rounds);
    return std::min(1.0f, dps * rounds / hp_left);
}

// Skirmish between visible champions: each round every living fighter's damage goes to
// the opposing side's lowest-health living fighter (focus fire; overkill carries over).
// `linked` fighters die together (Cassi & Issac). Values weigh what a death costs
// (a body plus carried gems). Cheap: at most a dozen fighters for a bounded number of rounds.
struct Fighter
{
    float hp{};
    float dps{};  // sustained damage per round (basic attacks; casts below come on top)
    bool ours{};
    float value{ 1.0f };
    int linked{ -1 };
    // Ability casts: one per round (shared by both slots), each available from round
    // ready[k] (1 = now) and again `cooldown` rounds after use. Damage 0 = no damaging cast.
    float cast[2]{};
    int ready[2]{ 1, 1 };
    int cooldown{ 8 };
    // First round it can deal and take damage: 1 = in contact now, later while still walking
    // into the fight (distance beyond its reach). Casts wait for it too.
    int arrive{ 1 };
};

struct SkirmishOutcome
{
    float our_losses{};
    float their_losses{};
    int rounds{};
    int first_target{ -1 };  // index of the enemy we focus first
    bool we_survive{};       // at least one of ours alive at the end
    bool they_survive{};
};

// Array form (at most 16 fighters). Focus = the living fighter in contact with the least health
// (a fighter still walking in can neither hit nor be hit); with everyone in contact from the
// start only the focus loses health, so that is the first living entry of the ascending order.
// `threat_focus`: our side instead focuses the most damage per remaining health (damage per
// round plus casts spread over their cooldown) - the order that minimises the damage we take.
inline SkirmishOutcome simulate_skirmish(const Fighter* f, int n, int max_rounds, bool threat_focus = false)
{
    SkirmishOutcome out;
    n = std::min(n, 16);
    float hp[16];
    bool dead[16];
    int order[2][16];
    int count[2]{};
    float rate[2]{};  // living, arrived damage per round of side 0 (ours) and 1 (theirs)
    int alive[2]{};    // living fighters per side
    int last_arrival = 1;
    for (int i = 0; i < n; ++i)
    {
        hp[i] = f[i].hp;
        dead[i] = hp[i] <= 0.0f;
        const int side = f[i].ours ? 0 : 1;
        int k = count[side]++;
        while (k > 0 && f[order[side][k - 1]].hp > f[i].hp)
        {
            order[side][k] = order[side][k - 1];
            --k;
        }
        order[side][k] = i;
        if (!dead[i]) ++alive[side];
        if (!dead[i] && f[i].arrive <= 1) rate[side] += f[i].dps;
        last_arrival = std::max(last_arrival, f[i].arrive);
    }
    int round = 1;
    auto arrived = [&](int i) { return f[i].arrive <= round; };
    float threat[16];
    if (threat_focus)
        for (int i = 0; i < n; ++i) threat[i] = f[i].dps + (f[i].cast[0] + f[i].cast[1]) / static_cast<float>(std::max(1, f[i].cooldown));
    // Focus per side, kept current: between a death and an arrival only the focus loses health,
    // so it stays the best target (least health, or most threat per health) - rescan only at
    // those events. A scan per hit cost ~17K fuel per simulation over 15 rounds of spawn-boosted
    // health and starved the turn's later phases (fixture tests/main-spawn-fixture.py).
    int focus[2] = { -1, -1 };
    auto rescan = [&](int side) {
        int best = -1;
        for (int k = 0; k < count[side]; ++k)
        {
            const int i = order[side][k];
            if (dead[i] || !arrived(i)) continue;
            // Side 1 is the side we hit: threat per health (cross-multiplied, health > 0).
            const bool better = best < 0 || (threat_focus && side == 1 ? threat[i] * hp[best] > threat[best] * hp[i] : hp[i] < hp[best]);
            if (better) best = i;
        }
        focus[side] = best;
    };
    auto any_alive = [&](int side) { return alive[side] > 0; };
    auto kill = [&](int i) {
        int sides = 0;
        for (int guard = 0; guard < 2 && i >= 0 && i < n && !dead[i]; ++guard)
        {
            dead[i] = true;
            const int side = f[i].ours ? 0 : 1;
            sides |= 1 << side;
            --alive[side];
            if (arrived(i)) rate[side] -= f[i].dps;
            (side == 0 ? out.our_losses : out.their_losses) += f[i].value;
            i = f[i].linked;  // linked partner dies too
        }
        for (int side = 0; side < 2; ++side)
            if (sides & (1 << side)) rescan(side);
    };
    rescan(0);
    rescan(1);
    // Cast events, bucketed by round: each slot fires at its ready round and every cooldown
    // after; when both slots come up together the stronger goes first and the other waits one
    // round (one cast per round). Horizon at most 32 rounds (10 events per fighter).
    constexpr int kMaxRounds = 32;
    constexpr int kMaxEvents = 160;
    max_rounds = std::min(max_rounds, kMaxRounds);
    int ev_head[kMaxRounds + 1];
    for (int r = 0; r <= kMaxRounds; ++r) ev_head[r] = -1;
    int ev_next[kMaxEvents], ev_who[kMaxEvents];
    float ev_dmg[kMaxEvents];
    int n_ev = 0;
    auto add_event = [&](int round, int who, float dmg) {
        if (round < 1 || round > max_rounds || n_ev >= kMaxEvents) return;
        ev_who[n_ev] = who;
        ev_dmg[n_ev] = dmg;
        ev_next[n_ev] = ev_head[round];
        ev_head[round] = n_ev++;
    };
    for (int i = 0; i < n; ++i)
    {
        const int cd = std::max(1, f[i].cooldown);
        const bool a = f[i].cast[0] > 0.0f, b = f[i].cast[1] > 0.0f;
        const int from = std::max(1, f[i].arrive);
        int t0 = a ? std::max(from, f[i].ready[0]) : 1 << 20;
        int t1 = b ? std::max(from, f[i].ready[1]) : 1 << 20;
        if (a && b && t0 == t1) (f[i].cast[0] >= f[i].cast[1] ? t1 : t0) += 1;
        for (int t = t0; a && t <= max_rounds; t += cd) add_event(t, i, f[i].cast[0]);
        for (int t = t1; b && t <= max_rounds; t += cd) add_event(t, i, f[i].cast[1]);
    }
    out.first_target = focus[1];
    for (int r = 0; r < max_rounds; ++r)
    {
        round = r + 1;
        if (round > 1 && round <= last_arrival)
        {
            bool joined[2]{};
            for (int i = 0; i < n; ++i)
                if (!dead[i] && f[i].arrive == round)
                {
                    rate[f[i].ours ? 0 : 1] += f[i].dps;
                    joined[f[i].ours ? 0 : 1] = true;
                }
            for (int side = 0; side < 2; ++side)
                if (joined[side]) rescan(side);
        }
        if (out.first_target < 0) out.first_target = focus[1];
        float dealt[2] = { rate[0], rate[1] };  // simultaneous: rates at round start
        for (int e = ev_head[round]; e >= 0; e = ev_next[e])
            if (!dead[ev_who[e]]) dealt[f[ev_who[e]].ours ? 0 : 1] += ev_dmg[e];
        bool landed = false;
        for (int side = 0; side < 2; ++side)
        {
            const int victim_side = 1 - side;
            float left = dealt[side];
            while (left > 0.0f)
            {
                const int t = focus[victim_side];
                if (t < 0) break;
                const float take = std::min(left, hp[t]);
                hp[t] -= take;
                left -= take;
                landed = true;
                if (hp[t] <= 0.0f) kill(t);
            }
        }
        if (!landed)
        {
            if (round >= last_arrival) break;  // nobody can hit anybody any more
            continue;                          // still walking in
        }
        out.rounds = round;
        if (!any_alive(0) || !any_alive(1)) break;
    }
    out.we_survive = any_alive(0);
    out.they_survive = any_alive(1);
    return out;
}

inline SkirmishOutcome simulate_skirmish(const std::vector<Fighter>& f, int max_rounds, bool threat_focus = false)
{
    return simulate_skirmish(f.data(), static_cast<int>(f.size()), max_rounds, threat_focus);
}

// Split a team into squads of at most `size` members (linked members together, strongest
// paired with weakest so squads are balanced). Used for pairs (size 2).
inline std::vector<int> partition_small_squads(const std::vector<float>& power, const std::vector<int>& linked, int size)
{
    const int n = static_cast<int>(power.size());
    std::vector<int> squad(static_cast<std::size_t>(n), -1);
    int next = 0;
    // Linked pairs fill a squad of two by themselves.
    for (int i = 0; i < n; ++i)
    {
        const int j = linked[static_cast<std::size_t>(i)];
        if (squad[static_cast<std::size_t>(i)] < 0 && j >= 0 && j < n && squad[static_cast<std::size_t>(j)] < 0)
        {
            squad[static_cast<std::size_t>(i)] = squad[static_cast<std::size_t>(j)] = next++;
        }
    }
    // Remaining members by descending power; pair strongest with weakest.
    std::vector<int> rest;
    for (int i = 0; i < n; ++i)
        if (squad[static_cast<std::size_t>(i)] < 0) rest.push_back(i);
    for (std::size_t a = 1; a < rest.size(); ++a)
        for (std::size_t k = a; k > 0 && power[static_cast<std::size_t>(rest[k])] > power[static_cast<std::size_t>(rest[k - 1])]; --k)
            std::swap(rest[k], rest[k - 1]);
    std::size_t lo = 0, hi = rest.size();
    while (lo < hi)
    {
        const int s = next++;
        int members = 0;
        squad[static_cast<std::size_t>(rest[lo++])] = s;
        ++members;
        while (members < size && lo < hi)
        {
            squad[static_cast<std::size_t>(rest[--hi])] = s;
            ++members;
        }
    }
    return squad;
}

// Split a team into squads that farm and fight together. Up to three members form one
// squad; larger teams form two squads of balanced power, keeping linked members together
// (linked[i] = partner index or -1) and giving each squad a melee member when possible.
inline std::vector<int> partition_squads(const std::vector<float>& power, const std::vector<bool>& melee, const std::vector<int>& linked)
{
    const int n = static_cast<int>(power.size());
    std::vector<int> squad(static_cast<std::size_t>(n), 0);
    if (n <= 3) return squad;
    struct Block { int a, b; float power; bool melee; };
    std::vector<Block> blocks;
    std::vector<bool> used(static_cast<std::size_t>(n), false);
    for (int i = 0; i < n; ++i)
    {
        if (used[static_cast<std::size_t>(i)]) continue;
        used[static_cast<std::size_t>(i)] = true;
        Block blk{ i, -1, power[static_cast<std::size_t>(i)], melee[static_cast<std::size_t>(i)] };
        const int j = linked[static_cast<std::size_t>(i)];
        if (j >= 0 && j < n && !used[static_cast<std::size_t>(j)])
        {
            used[static_cast<std::size_t>(j)] = true;
            blk.b = j;
            blk.power += power[static_cast<std::size_t>(j)];
            blk.melee = blk.melee || melee[static_cast<std::size_t>(j)];
        }
        blocks.push_back(blk);
    }
    // Strongest first (insertion sort: at most six blocks), each to the weaker squad.
    for (std::size_t i = 1; i < blocks.size(); ++i)
        for (std::size_t k = i; k > 0 && blocks[k].power > blocks[k - 1].power; --k) std::swap(blocks[k], blocks[k - 1]);
    float total[2]{};
    bool has_melee[2]{};
    std::vector<int> block_squad(blocks.size(), 0);
    for (std::size_t i = 0; i < blocks.size(); ++i)
    {
        const int s = total[0] <= total[1] ? 0 : 1;
        block_squad[i] = s;
        total[s] += blocks[i].power;
        has_melee[s] = has_melee[s] || blocks[i].melee;
    }
    // A squad without melee swaps its weakest block for the other squad's weakest melee block.
    for (int s = 0; s < 2; ++s)
    {
        if (has_melee[s]) continue;
        int give = -1, take = -1;
        for (int i = static_cast<int>(blocks.size()) - 1; i >= 0; --i)
        {
            if (block_squad[static_cast<std::size_t>(i)] == s && give < 0) give = i;
            if (block_squad[static_cast<std::size_t>(i)] != s && blocks[static_cast<std::size_t>(i)].melee && take < 0) take = i;
        }
        int other_melee = 0;
        for (std::size_t i = 0; i < blocks.size(); ++i)
            if (block_squad[i] != s && blocks[i].melee) ++other_melee;
        if (give >= 0 && take >= 0 && other_melee >= 2)
            std::swap(block_squad[static_cast<std::size_t>(give)], block_squad[static_cast<std::size_t>(take)]);
    }
    for (std::size_t i = 0; i < blocks.size(); ++i)
    {
        squad[static_cast<std::size_t>(blocks[i].a)] = block_squad[i];
        if (blocks[i].b >= 0) squad[static_cast<std::size_t>(blocks[i].b)] = block_squad[i];
    }
    return squad;
}

// Ability summary used for damage-rate estimates (decoded from SDK metadata by the caller).
enum class AbilityKind : std::uint8_t { none, attack_reset, damage, aoe, cc, heal, dash, shield };

struct AbilitySummary
{
    AbilityKind kind{ AbilityKind::none };
    float modifier{};  // focus multiplier for damage-bearing kinds
    int cooldown{ 8 };
    bool hits_camps{};
    bool hits_enemies{};
    float range{};  // target range (dash: distance covered)
};

struct ChampionSummary
{
    float strength{};
    float focus{};
    float health{};
    int attack_range{ 1 };
    AbilitySummary ability[2];
    bool splash{};          // Varg: basic attacks also hit neighbours of the target
    bool kill_resets{};     // Vendetta: kills refresh cooldowns
};

// Sustained damage per round against camps. `group_neighbours` is the average number of
// other living monsters within radius one of the attacked monster (splash/AoE extra hits).
// `dashes`: a dash with a damaging landing counts like an area hit on its cooldown (it lands on
// the target's neighbours too).
inline float camp_dps(const ChampionSummary& c, float group_neighbours, bool dashes = false)
{
    float attacks = 1.0f;
    float spell = 0.0f;
    for (const auto& a : c.ability)
    {
        const float per_round = 1.0f / static_cast<float>(std::max(1, a.cooldown));
        switch (a.kind)
        {
        case AbilityKind::attack_reset:
            attacks += per_round * (c.kill_resets ? 2.0f : 1.0f);
            break;
        case AbilityKind::damage:
        case AbilityKind::cc:
            if (a.hits_camps)
                spell += a.modifier * c.focus * per_round;
            break;
        case AbilityKind::aoe:
            if (a.hits_camps)
                spell += a.modifier * c.focus * per_round * (1.0f + group_neighbours);
            break;
        case AbilityKind::dash:
            if (dashes && a.modifier > 0.0f) spell += a.modifier * c.focus * per_round * (1.0f + group_neighbours);
            break;
        default:
            break;
        }
    }
    float basic = c.strength * attacks;
    if (c.splash)
        basic *= 1.0f + group_neighbours;
    return basic + spell;
}

// Sustained damage per round against champions: basic attacks (with resets) plus spells
// that may target enemy champions; area spells assume a single target.
// `dashes`: a dash with a damaging landing (ground-targeted, so not flagged as hitting enemies)
// counts as a damaging cast too.
inline float pvp_dps(const ChampionSummary& c, bool dashes = false)
{
    float attacks = 1.0f;
    float spell = 0.0f;
    for (const auto& a : c.ability)
    {
        const float per_round = 1.0f / static_cast<float>(std::max(1, a.cooldown));
        if (a.kind == AbilityKind::attack_reset) attacks += per_round;
        if ((a.kind == AbilityKind::damage || a.kind == AbilityKind::cc || a.kind == AbilityKind::aoe) && a.hits_enemies)
            spell += a.modifier * c.focus * per_round;
        if (dashes && a.kind == AbilityKind::dash && a.modifier > 0.0f) spell += a.modifier * c.focus * per_round;
    }
    return c.strength * attacks + spell;
}

// Skirmish profile: one basic attack per round plus each slot's damaging cast (an attack
// reset is one extra basic attack). Shields and heals are not damage.
struct PvpProfile
{
    float basic{};
    float cast[2]{};
    int cooldown{ 1 };
};

inline PvpProfile pvp_profile(const ChampionSummary& c, bool dashes = false)
{
    PvpProfile p;
    p.basic = c.strength;
    for (int k = 0; k < 2; ++k)
    {
        const auto& a = c.ability[k];
        if (a.kind == AbilityKind::attack_reset)
            p.cast[k] = c.strength;
        else if ((a.kind == AbilityKind::damage || a.kind == AbilityKind::cc || a.kind == AbilityKind::aoe) && a.hits_enemies)
            p.cast[k] = a.modifier * c.focus;
        else if (dashes && a.kind == AbilityKind::dash && a.modifier > 0.0f)
            p.cast[k] = a.modifier * c.focus;
        p.cooldown = std::max(p.cooldown, a.cooldown);
    }
    return p;
}

// Gem weight (gold per gem) from the marginal value of items: a shard of `shard_cost` gold adds
// `growth` to a main stat of `stat`, speeding the damage-limited share `dps_share` of income by
// growth / stat for the `gems_left` the race still needs, i.e. dps_share x gems_left x growth / stat
// gems per shard. Returns the inverse per gold, clamped to [floor, max(floor, cap)].
inline float item_gem_weight(float gems_left, float growth, float stat, float shard_cost, float dps_share, float floor, float cap)
{
    const float gems_per_gold = dps_share * gems_left * growth / (std::max(1.0f, shard_cost) * std::max(1.0f, stat));
    return std::clamp(1.0f / std::max(1e-3f, gems_per_gold), floor, std::max(floor, cap));
}

// Value per round of committing to a camp group: reward over the total time spent
// travelling, clearing and capturing. Returns 0 for infeasible plans.
inline float group_rate(float reward, int travel, int clear_rounds, int capture_rounds)
{
    const int total = std::max(1, travel + clear_rounds + capture_rounds);
    return reward / static_cast<float>(total);
}

} // namespace mb::model
