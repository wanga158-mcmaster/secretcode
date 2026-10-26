#pragma once
// Capital is useful only where an affordable item changes the productive bottleneck.
// Pure bounded workload model; runtime translation and the real callers live in shop.cpp.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

namespace mb::capital {

struct Work
{
    float hp{}, weighted_hp{}, strength{}, gems{}, gold{};
    float camp_str{}, camp_focus{};
    float travel{}, walk_back{}, capture{}, respawn{};
};

struct Build
{
    float strength{}, focus{}, health{};
    float camp_damage{1}, damage_taken{1}, gold_mult{1};
    float recall{5}, usable_share{0.5f}, safety{1.6f}, max_trips{4};
};

struct Flow { float gems{}, gold{}; int tasks{}; };

struct CreditKey { int round{}, item_version{}, purse_shards{}; bool ready{}; };
inline bool credit_current(const CreditKey& cached, int round, int version, int purse)
{
    return cached.ready && round >= cached.round && round - cached.round <= 40 && cached.item_version == version && cached.purse_shards == purse;
}

inline Flow flow(std::span<const Work> work, const Build& b)
{
    struct Task { float gems{}, gold{}, cycle{}, period{}; };
    std::array<Task, 8> ordered{};
    int count = 0;
    for (const auto& w : work)
    {
        if (count == 8 || w.gems <= 0) continue;
        const float dps = std::max(1.0f, (w.camp_str * b.strength + w.camp_focus * b.focus) * b.camp_damage);
        const float fight = std::max(1.0f, std::ceil(w.hp / dps));
        const float damage = std::max(0.0f, w.weighted_hp / dps - w.strength * 0.5f) * b.damage_taken;
        const float usable = std::max(1.0f, b.health * b.usable_share);
        if (damage * b.safety > usable * b.max_trips) continue;
        const float healing = damage / usable * (b.recall + 1.0f + w.walk_back);
        Task task{ w.gems, w.gold * b.gold_mult, w.travel + fight + w.capture + healing,
                   w.respawn + fight + w.capture };
        int at = count++;
        while (at > 0 && ordered[static_cast<std::size_t>(at - 1)].gems / ordered[static_cast<std::size_t>(at - 1)].cycle < task.gems / task.cycle)
        {
            ordered[static_cast<std::size_t>(at)] = ordered[static_cast<std::size_t>(at - 1)];
            --at;
        }
        ordered[static_cast<std::size_t>(at)] = task;
    }
    Flow best;
    float gems = 0, gold = 0, cycle = 0, period = 0;
    for (int i = 0; i < count; ++i)
    {
        const auto& task = ordered[static_cast<std::size_t>(i)];
        gems += task.gems;
        gold += task.gold;
        cycle += task.cycle;
        period = std::max(period, task.period);
        const float rounds = std::max({ 1.0f, cycle, period });
        if (gems / rounds > best.gems) best = { gems / rounds, gold / rounds, i + 1 };
    }
    return best;
}

// Survival is priced separately from camp throughput so damage-limited farming is not charged
// twice. The runtime supplies the inherited measured PvP death/heal term, including its prior.
inline float bundle_value(const Flow& before, const Flow& after, float survival_rounds_saved, float horizon)
{
    const float productive = std::max(0.0f, after.gems - before.gems);
    const float survival = before.gems * std::max(0.0f, survival_rounds_saved);
    return (productive + survival) * std::max(0.0f, horizon);
}

inline float group_reward(float gems, float gold, float gold_price, float gold_mult, float tribute_gold)
{
    return gems * (1.0f + tribute_gold * gold_price) + gold * gold_mult * gold_price;
}

inline float shop_benefit(const Flow& before, const Flow& after, float survival_rounds_saved, float advance, float heal_credit)
{
    const float speed = before.gems > 0.0001f ? std::max(0.0f, after.gems / before.gems - 1.0f) : 0.0f;
    return (speed + std::max(0.0f, survival_rounds_saved)) * std::max(0.0f, advance) + std::max(0.0f, heal_credit);
}

inline bool shop_worth(float benefit, int extra_route_rounds) { return benefit >= static_cast<float>(std::max(0, extra_route_rounds) + 2); }

struct Bank
{
    int gems{}, remaining_score{}, team_size{1}, walk{}, channel{}, rounds_left{};
    int extra_route{-1}; // unknown is not zero; only a real route may supply an opportunistic detour
    float gem_flow{}, investment_credit{};
    bool contact{}, at_altar{}, gated{}, win_now{};
    int known_team_carried{}; // visible bags are a lower bound; never invent unseen carry
};

// Route geometry is static, but an answer applies only to the exact origin and productive
// target used to compute it. Unknown/incomplete searches never become a zero-cost detour.
struct RouteKey { int x{}, y{}, route_x{}, route_y{}, target{-1}; bool operator==(const RouteKey&) const = default; };
struct RouteMemo
{
    struct Entry { RouteKey key{}; int extra{-1}; };
    std::array<Entry, 8> entries{};
    unsigned next{};
    int get(RouteKey key) const
    {
        for (const auto& entry : entries) if (entry.extra >= 0 && entry.key == key) return entry.extra;
        return -1;
    }
    void put(RouteKey key, int extra)
    {
        if (extra >= 0) entries[next++ % entries.size()] = { key, extra };
    }
};

inline bool route_budget(std::uint64_t start, std::uint64_t remaining, std::uint64_t tail, std::uint64_t limit)
{
    return remaining > tail && start >= remaining && start - remaining < limit;
}

inline bool bank_now(const Bank& b)
{
    if (b.gems <= 0) return false;
    if (b.at_altar || b.gated || b.win_now || b.contact || b.known_team_carried >= b.remaining_score ||
        b.rounds_left <= b.walk + b.channel + 15) return true;
    // A derived team share prevents invisible teammates' bags from delaying every cash-out.
    const int quota = (std::max(1, b.remaining_score) + std::max(1, b.team_size) - 1) / std::max(1, b.team_size);
    if (b.gems >= quota) return true;
    if (b.extra_route < 0) return false;
    // Do not call distance-to-altar a detour. Only extra distance through the chosen altar and
    // back to the productive route can be paid by bringing a concrete investment forward.
    return static_cast<float>(b.extra_route + b.channel) <= b.investment_credit;
}

inline bool bank_needs_route(const Bank& b)
{
    // Every possible route has nonnegative extra cost. Do not spend executor fuel proving a
    // detour which cannot change the decision, or checking an already decisive override.
    return !bank_now(b) && b.investment_credit >= static_cast<float>(b.channel);
}

} // namespace mb::capital
