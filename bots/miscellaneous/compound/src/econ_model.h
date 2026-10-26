#pragma once
// The value model of contenders/compound: one currency (gems) for shopping, routing and banking.
// SDK-free and unit-tested (tests/econ_model.cpp).
//
// Derivation (NOTES.md, decision 2):
// - Gems are the objective. Gold is worth what its items add to future gems before the race
//   ends: lambda = (gems a round an item adds per gold) x (rounds of race left).
// - A champion's farming rate is the value per round of its best camps, where a camp's time
//   includes the travel, the fight (camp health / damage rate), the capture (capture_time + 1
//   phases, M14) and the share of a recall trip its damage costs (spawn heals fully, M18).
// - Survival: in a fight we cannot win we take damage until we separate. The chance that the
//   exposure kills us falls with effective health (health / Durability) against the damage a
//   losing exchange deals before separation (online O13: risk climbs with the exchange ratio).
//   A death costs its dead and walking rounds, the kill gold handed over and the bag (twice:
//   we lose it and the killer gains it, M22).
// - An item's value is the change of (farming rate - death rate x death cost) over the race.
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "model.h"

namespace cm {

struct CampSpec
{
    SmithConstants sc{};   // the group's fight constants at full health
    float gold{}, gems{};
    float travel{};        // rounds from the previous camp of the farming loop
    float home{};          // walking rounds from our spawn (the walk back after a recall)
    float cooldown{50};    // respawn after the payout (M16): a camp pays at most once per cooldown
    int members{1};
};

struct EmblemPassives
{
    float jungle{0.3f}, durability{0.1f}, treasure{0.2f}, monster{0.1f}, home{0.5f};
};

struct Profile
{
    Kit kit{};
    float hp{1000};             // max health
    float taken_camp{1.0f};     // camp damage multiplier (Jungle x Durability)
    float taken_all{1.0f};      // every damage (Durability)
    float gold_mult{1.0f};      // gold multiplier (Vesper + Treasure, M37)
    float recall{5.0f};         // recall channel in rounds (Home: 3, M41)
    float gem_gold{};           // gold per tributed gem (Marcella 2, M38)
    std::array<float, 3> growth{};   // per shard: strength, focus, health (M39)
    bool health_strength{};     // Ryth: +1% of bonus health as Strength (M43)
    std::array<bool, 7> passive{};   // family passives owned (Family order)
};

struct Econ
{
    float gold_w{0.3f};         // gems per gold, the current lambda (routing weight of gold)
    float race_left{900.0f};    // rounds of race left (the planned horizon, not our current pace)
    float margin{1.25f};        // camp damage x margin must fit in usable health
    float capture{6.0f};        // capture phases (capture_time + 1)
    int siege_trips{3};
    float usable_share{0.8f};   // share of max health spent per trip (an exit reserve stays, E4)
    int top_k{3};               // farming rate = mean of the best k camps
    float reinvest{0.5f};       // weight of gold earned back (reinvested over a shrinking horizon)
    // survival
    float threat_dps{};         // damage a round a losing exchange puts on us
    float exposure_rounds{8.0f};// rounds of that exchange before we separate
    float fights_per_round{};   // contact episodes a round
    float risk_slope{4.0f};     // steepness of the death risk in effective health / exposure
    float death_rounds{70.0f};  // rounds a death costs (respawn 60 + the walk back)
    float death_gold{100.0f};   // gold handed to the killer (kill_gold)
    float carried{};            // gems usually carried (lost and handed over on death)
};

// Income a round from farming: gems and gold separately (lambda converts only for choices).
struct Rate
{
    float gems{};
    float gold{};
    float time{};   // rounds of one cycle (camp_rate only)
    float value(float gold_w) const { return gems + gold * gold_w; }
};

// Income a round of farming camp `c` with profile `p` dealing `dps` to it; zero when it is not
// feasible. A camp pays at most once per respawn (supply cap, M16).
inline Rate camp_rate(const Profile& p, const CampSpec& c, const Econ& e, float dps)
{
    if (dps <= 0.0f || c.sc.health <= 0.0f) return {};
    const float rounds = c.sc.health / dps;
    float dmg = estimate_camp_damage(c.sc, dps, p.taken_camp);
    if (dmg > 0.0f) dmg = std::max(0.0f, dmg - shield_per_round(p.kit) * rounds);
    // Health is time: every usable health pool spent costs one heal trip (recall, the arrival
    // turn, the walk back; foundation E4, E18). A camp needing more pools than siege_trips is out.
    const float usable = std::max(1.0f, p.hp * e.usable_share);
    const float trip = p.recall + 1.0f + c.home;
    const float need = dmg * e.margin;
    if (need > usable * static_cast<float>(e.siege_trips)) return {};
    const float time = std::max(1.0f, c.travel + rounds + e.capture + dmg / usable * trip);
    return { c.gems / time, (c.gold + c.gems * p.gem_gold) * p.gold_mult / time, time };
}

inline Rate camp_rate(const Profile& p, const CampSpec& c, const Econ& e)
{
    return camp_rate(p, c, e, camp_dps(p.kit, c.members));
}

// Income of a loop over the best k camps by value (a champion farms several camps in turn); a
// loop can repeat no faster than its slowest member respawns (supply, M16/E3), and fewer than k
// feasible camps lengthen it.
inline Rate farm_rate(const Profile& p, const std::vector<CampSpec>& camps, const Econ& e)
{
    std::array<Rate, 8> best{};
    std::array<float, 8> respawn{};
    const int k = std::clamp(e.top_k, 1, 8);
    int n = 0;
    const std::array<float, 3> dps{ camp_dps(p.kit, 1), camp_dps(p.kit, 2), camp_dps(p.kit, 3) };
    for (const auto& c : camps)
    {
        const Rate r = camp_rate(p, c, e, dps[static_cast<std::size_t>(std::clamp(c.members, 1, 3) - 1)]);
        const float v = r.value(e.gold_w);
        if (v <= 0.0f) continue;
        int slot = -1;
        if (n < k) slot = n++;
        else
        {
            int lo = 0;
            for (int i = 1; i < k; ++i)
                if (best[static_cast<std::size_t>(i)].value(e.gold_w) < best[static_cast<std::size_t>(lo)].value(e.gold_w)) lo = i;
            if (v > best[static_cast<std::size_t>(lo)].value(e.gold_w)) slot = lo;
        }
        if (slot >= 0)
        {
            best[static_cast<std::size_t>(slot)] = r;
            respawn[static_cast<std::size_t>(slot)] = c.cooldown + e.capture;
        }
    }
    if (n == 0) return {};
    float gems = 0.0f, gold = 0.0f, time = 0.0f, slowest = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const auto& r = best[static_cast<std::size_t>(i)];
        gems += r.gems * r.time;
        gold += r.gold * r.time;
        time += r.time;
        slowest = std::max(slowest, respawn[static_cast<std::size_t>(i)]);
    }
    // Missing camps: the loop idles for the share of k it cannot fill (walks to farther camps).
    time *= static_cast<float>(k) / static_cast<float>(n);
    time = std::max(time, slowest);
    return { gems / time, gold / time, time };
}

// Probability that a losing contact kills us before we separate.
inline float death_risk(const Profile& p, const Econ& e)
{
    if (e.threat_dps <= 0.0f || e.exposure_rounds <= 0.0f) return 0.0f;
    const float ehp = p.hp / std::max(0.1f, p.taken_all);
    const float x = ehp / (e.threat_dps * e.exposure_rounds);
    return 1.0f / (1.0f + std::exp(e.risk_slope * (x - 1.0f)));
}

// Net income a round: farming minus the expected cost of deaths (dead rounds of income, the kill
// gold handed over and the bag twice, M22).
inline Rate net_rate(const Profile& p, const std::vector<CampSpec>& camps, const Econ& e)
{
    Rate r = farm_rate(p, camps, e);
    const float deaths = death_risk(p, e) * e.fights_per_round;
    r.gems -= deaths * (e.death_rounds * r.gems + 2.0f * e.carried);
    r.gold -= deaths * (e.death_rounds * r.gold + e.death_gold);
    return r;
}

// The value (gems by the end of the race) of a change of income: gems directly, gold through the
// items it buys later, at the current lambda and half weight (a horizon shrinking to zero).
inline float income_value(const Rate& delta, const Econ& e)
{
    return (delta.gems + e.reinvest * e.gold_w * delta.gold) * e.race_left;
}

inline Profile add_shards(Profile p, Stat s, float n)
{
    const auto i = static_cast<std::size_t>(s);
    switch (s)
    {
    case Stat::strength: p.kit.str += p.growth[i] * n; break;
    case Stat::focus: p.kit.focus += p.growth[i] * n; break;
    case Stat::health:
        p.hp += p.growth[i] * n;
        if (p.health_strength) p.kit.str += 0.01f * p.growth[i] * n;
        break;
    }
    return p;
}

// The emblem completed by the fifth shard of (f, s): its passive (only the first of a family
// counts) and its extra stats (Power +1 shard, Bruiser +1 of each other stat, M39).
inline Profile add_emblem(Profile p, Family f, Stat s, const EmblemPassives& m)
{
    const auto fi = static_cast<std::size_t>(f);
    const bool first = !p.passive[fi];
    p.passive[fi] = true;
    switch (f)
    {
    case Family::treasure: if (first) p.gold_mult += m.treasure; break;
    case Family::jungle: if (first) p.taken_camp *= 1.0f - m.jungle; break;
    case Family::durability:
        if (first)
        {
            p.taken_camp *= 1.0f - m.durability;
            p.taken_all *= 1.0f - m.durability;
        }
        break;
    case Family::monster: if (first) p.kit.camp_mult *= 1.0f + m.monster; break;
    case Family::home: if (first) p.recall = std::ceil(p.recall * m.home); break;
    case Family::power: p = add_shards(p, s, 1.0f); break;
    case Family::bruiser:
        for (int o = 0; o < 3; ++o)
            if (o != static_cast<int>(s)) p = add_shards(p, static_cast<Stat>(o), 1.0f);
        break;
    }
    return p;
}

struct Purchase
{
    Family family{Family::treasure};
    Stat stat{Stat::strength};
    int shards{};          // shards left to complete the emblem (1..5)
    float value{};         // gems over the rest of the race
    float per_gold{};      // value / cost
};

// The best bundle to work on: for every (family, stat) the shards that complete its next emblem.
// `owned[f][s]` counts shards of that pair modulo the emblems already made (0..4). Linear
// decomposition (fuel: nine farming-rate evaluations instead of twenty-one): a bundle is worth
// its shards' stat value plus its emblem's passive (first of a family only) and extra stats.
inline Purchase best_purchase(const Profile& p, const std::vector<CampSpec>& camps, const Econ& e, const EmblemPassives& m,
                              const std::array<std::array<int, 3>, 7>& owned, float shard_cost, bool allow_focus)
{
    const Rate base = net_rate(p, camps, e);
    const auto gain = [&](const Profile& q) {
        const Rate r = net_rate(q, camps, e);
        return income_value(Rate{ r.gems - base.gems, r.gold - base.gold }, e);
    };
    std::array<float, 3> stat{};   // value of one shard of each stat
    for (int si = 0; si < 3; ++si)
    {
        if (static_cast<Stat>(si) == Stat::focus && !allow_focus) continue;
        stat[static_cast<std::size_t>(si)] = gain(add_shards(p, static_cast<Stat>(si), 1.0f));
    }
    std::array<float, 7> passive{};   // value of each family's passive alone
    for (const auto f : { Family::treasure, Family::jungle, Family::durability, Family::monster, Family::home })
    {
        const auto fi = static_cast<std::size_t>(f);
        if (p.passive[fi]) continue;
        passive[fi] = gain(add_emblem(p, f, Stat::health, m));
    }
    Purchase best;
    best.per_gold = -1e30f;
    for (int fi = 0; fi < 7; ++fi)
        for (int si = 0; si < 3; ++si)
        {
            const auto f = static_cast<Family>(fi);
            const auto s = static_cast<Stat>(si);
            if (s == Stat::focus && !allow_focus) continue;
            const int have = owned[static_cast<std::size_t>(fi)][static_cast<std::size_t>(si)];
            const int n = 5 - std::clamp(have, 0, 4);
            float value = static_cast<float>(n) * stat[static_cast<std::size_t>(si)] + passive[static_cast<std::size_t>(fi)];
            if (f == Family::power) value += stat[static_cast<std::size_t>(si)];
            if (f == Family::bruiser)
                for (int o = 0; o < 3; ++o)
                    if (o != si) value += stat[static_cast<std::size_t>(o)];
            const float per_gold = value / (static_cast<float>(n) * shard_cost);
            if (per_gold > best.per_gold) best = Purchase{ f, s, n, value, per_gold };
        }
    return best;
}

// The race horizon: rounds until the faster team banks the winning gems, where our pace is the
// gem rate a gem-focused plan achieves (not our current banking, which would make a slow start
// look like a long race and gold look ever more valuable), bounded by the round limit.
inline float race_horizon(int round, int max_rounds, int winning, int our_banked, int their_banked, float our_plan_rate,
                          float their_rate)
{
    const float cap = static_cast<float>(std::max(1, max_rounds - round));
    float ours = our_plan_rate > 0.05f ? static_cast<float>(std::max(0, winning - our_banked)) / our_plan_rate : cap;
    if (their_rate > 0.05f) ours = std::min(ours, static_cast<float>(std::max(0, winning - their_banked)) / their_rate);
    return std::clamp(ours, std::min(50.0f, cap), cap);
}

// Should a champion recall now to shop? The trip costs `trip_rounds` of farming; the items
// bought arrive `advance` rounds earlier than at the next forced recall; the recall also refills
// the missing health, which is a share (1 - health fraction) of a trip we would make anyway.
inline bool shop_trip_worth(float items_gain_rate, float advance, float trip_rounds, float farm_value_rate, float health_frac)
{
    const float gain = items_gain_rate * advance;
    const float cost = std::clamp(health_frac, 0.0f, 1.0f) * trip_rounds * farm_value_rate;
    return gain > cost;
}

}  // namespace cm
