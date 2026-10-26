#pragma once
// Finite, conditional next-investment comparison. The runtime supplies observed task state,
// complete purchase lists and executable schedules; this helper invents no rates or gold price.
// Gem-round area concerns one capture payout, not banked score or full economic dominance.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace mb::capital::episode {

enum class Kind : std::uint8_t { unknown, travel, combat, capture, respawn_wait, recall, healing, shop };
struct Phase
{
    Kind kind{Kind::unknown};
    int rounds{-1};
    float damage{}, healing{};
    int attacks{}, casts{}; // witnesses within combat duration, not additional serial time
    bool known{};
};
struct Schedule
{
    std::array<Phase, 12> phases{};
    unsigned count{};
    bool known{};
};
struct Task
{
    int id{-1};
    float gems{-1};
    int gold{-1}; // concrete recipient payout, after queried passives/sharing/rounding
    Schedule remaining; // live remaining work/capture progress, not a fresh static camp cycle
    bool known{};        // includes authoritative availability, reservation and cast readiness
};
struct Buy
{
    int item{-1}, count{}, unit_cost{-1};
};
struct Purchases
{
    std::array<Buy, 8> lines{};
    unsigned count{};
    bool known{}, complete{}; // actual sequencer's entire bounded visit, including partial blocks
    std::uint64_t resulting_build{}; // runtime signature of resulting item/stat/passive state
    float health_gain{}, max_health_after{-1}; // explicit queried item health effect/cap
};
struct Plan
{
    Schedule to_shop; // real recall/walk/heal/shop duration and damage, from this branch's origin
    Purchases purchases;
    Schedule post_shop{{}, 0, true}; // purchase first, then explicit spawn turn-end recovery
    Task gem;        // exactly one specified gem payout, after those purchases
    bool bound{};    // runtime has a concrete route/task/purchase execution commitment
};
struct Input
{
    int purse{-1}, horizon{-1};
    float health{-1};
    Task displaced; // the actual current productive task, including a zero-gem gold task
    Plan now, defer;
    bool known{};
};
enum class Decision : std::uint8_t { unknown, keep_working, return_now };
enum class Reason : std::uint8_t
{
    unknown_input, unbound_plan, unknown_schedule, invalid_schedule, infeasible_health,
    unknown_purchase, unaffordable, no_investment, unequal_target, purchase_not_prefix,
    inconsistent_build, inconsistent_execution, outside_horizon, no_gain, earlier_payout
};
struct Branch
{
    int capture_round{}, spent{}, cash_after_shop{}, cash_end{};
    float health_end{}, minimum_health{}, gem_rounds{};
};
struct Result
{
    Decision decision{Decision::unknown};
    Reason reason{Reason::unknown_input};
    Branch now, defer;
    int displaced_capture_round{}, added_items{}, added_spend{}, extra_gold_required{};
    float advantage{};
    bool completion_time_heuristic{true}, terminal_inventory_differs{}, terminal_gold_unvalued{true};
};

namespace detail {
inline bool nonnegative(float value) { return std::isfinite(value) && value >= 0; }
struct Progress { int rounds{}; float hp{}, minimum{}; };

inline Reason run(const Schedule& schedule, int horizon, Progress& progress)
{
    if (!schedule.known || schedule.count > schedule.phases.size()) return Reason::unknown_schedule;
    for (unsigned i = 0; i < schedule.count; ++i)
    {
        const auto& phase = schedule.phases[i];
        if (!phase.known || phase.kind == Kind::unknown) return Reason::unknown_schedule;
        if (phase.rounds < 0 || !nonnegative(phase.damage) || !nonnegative(phase.healing) ||
            phase.attacks < 0 || phase.casts < 0) return Reason::invalid_schedule;
        if (phase.rounds > horizon - progress.rounds) return Reason::outside_horizon;
        // A whole combat phase must survive before its terminal heal. Runtime may split
        // interleaved heals into phases; an aggregate heal never rescues a lethal prefix.
        progress.hp -= phase.damage;
        progress.minimum = std::min(progress.minimum, progress.hp);
        if (!(progress.hp > 0)) return Reason::infeasible_health;
        progress.hp += phase.healing;
        if (!std::isfinite(progress.hp)) return Reason::invalid_schedule;
        progress.rounds += phase.rounds;
    }
    return Reason::no_gain;
}

struct PurchaseSummary { int units{}, cost{}; };
inline Reason purchases(const Purchases& list, int budget, PurchaseSummary& summary)
{
    if (!list.known || !list.complete || list.count > list.lines.size() ||
        !nonnegative(list.health_gain) || !std::isfinite(list.max_health_after) || list.max_health_after <= 0)
        return Reason::unknown_purchase;
    for (unsigned i = 0; i < list.count; ++i)
    {
        const auto& buy = list.lines[i];
        if (buy.item < 0 || buy.count <= 0 || buy.unit_cost <= 0 || buy.count > 32 - summary.units)
            return Reason::unknown_purchase;
        // Divide before multiplication: arbitrary caller integers cannot overflow cost.
        if (buy.count > (budget - summary.cost) / buy.unit_cost) return Reason::unaffordable;
        summary.units += buy.count;
        summary.cost += buy.count * buy.unit_cost;
    }
    return summary.units > 0 ? Reason::no_gain : Reason::no_investment;
}

inline bool prefix(const Purchases& small, const Purchases& large, int units)
{
    unsigned a = 0, b = 0;
    int ai = 0, bi = 0;
    for (int i = 0; i < units; ++i)
    {
        if (a >= small.count || b >= large.count || small.lines[a].item != large.lines[b].item ||
            small.lines[a].unit_cost != large.lines[b].unit_cost) return false;
        if (++ai == small.lines[a].count) { ai = 0; ++a; }
        if (++bi == large.lines[b].count) { bi = 0; ++b; }
    }
    return true;
}

// Arrival-dependent respawn waits may change with identical items. All intrinsic execution
// must remain identical: extra unspent gold cannot secretly improve combat or movement.
inline bool same_execution(const Schedule& a, const Schedule& b)
{
    if (a.count > a.phases.size() || b.count > b.phases.size()) return false;
    unsigned i = 0, j = 0;
    for (;;)
    {
        while (i < a.count && a.phases[i].kind == Kind::respawn_wait) ++i;
        while (j < b.count && b.phases[j].kind == Kind::respawn_wait) ++j;
        if (i == a.count || j == b.count) return i == a.count && j == b.count;
        const auto& x = a.phases[i++];
        const auto& y = b.phases[j++];
        if (x.kind != y.kind || x.rounds != y.rounds || x.damage != y.damage || x.healing != y.healing ||
            x.attacks != y.attacks || x.casts != y.casts) return false;
    }
}
inline bool task_known(const Task& task)
{
    return task.known && task.id >= 0 && nonnegative(task.gems) && task.gold >= 0;
}
inline bool capture_witness(const Schedule& schedule)
{
    if (!schedule.known || schedule.count == 0 || schedule.count > schedule.phases.size()) return false;
    unsigned captures = 0;
    for (unsigned i = 0; i < schedule.count; ++i) captures += schedule.phases[i].kind == Kind::capture;
    const auto& terminal = schedule.phases[schedule.count - 1];
    // These are still-unpaid tasks. Missing work must not become an instantaneous reward;
    // even a nearly completed live capture needs an explicit positive completion phase.
    return captures == 1 && terminal.known && terminal.kind == Kind::capture && terminal.rounds > 0;
}
} // namespace detail

inline Result evaluate(const Input& input)
{
    Result result;
    const auto fail = [&](Reason reason) { result.reason = reason; return result; };
    if (!input.known || input.purse < 0 || input.horizon <= 0 || !std::isfinite(input.health) || input.health <= 0 ||
        !detail::task_known(input.displaced) || input.displaced.gold <= 0 ||
        !detail::task_known(input.now.gem) || !detail::task_known(input.defer.gem)) return result;
    if (!input.now.bound || !input.defer.bound) return fail(Reason::unbound_plan);
    if (!detail::capture_witness(input.displaced.remaining) || !detail::capture_witness(input.now.gem.remaining) ||
        !detail::capture_witness(input.defer.gem.remaining)) return fail(Reason::unknown_schedule);
    if (input.now.gem.id != input.defer.gem.id || input.now.gem.gems <= 0 ||
        input.now.gem.gems != input.defer.gem.gems)
        return fail(Reason::unequal_target);
    if (input.displaced.gold > INT32_MAX - input.purse) return result;
    detail::PurchaseSummary a, b;
    auto reason = detail::purchases(input.now.purchases, input.purse, a);
    if (reason != Reason::no_gain) return fail(reason);
    reason = detail::purchases(input.defer.purchases, input.purse + input.displaced.gold, b);
    if (reason != Reason::no_gain) return fail(reason);
    if (b.units < a.units || !detail::prefix(input.now.purchases, input.defer.purchases, a.units))
        return fail(Reason::purchase_not_prefix);
    if (a.units == b.units)
    {
        if (input.now.purchases.resulting_build != input.defer.purchases.resulting_build ||
            input.now.purchases.health_gain != input.defer.purchases.health_gain ||
            input.now.purchases.max_health_after != input.defer.purchases.max_health_after)
            return fail(Reason::inconsistent_build);
        if (!detail::same_execution(input.now.gem.remaining, input.defer.gem.remaining))
            return fail(Reason::inconsistent_execution);
    }
    detail::Progress now{0, input.health, input.health}, later = now;
    reason = detail::run(input.displaced.remaining, input.horizon, later);
    if (reason != Reason::no_gain) return fail(reason);
    result.displaced_capture_round = later.rounds;
    reason = detail::run(input.now.to_shop, input.horizon, now);
    if (reason != Reason::no_gain) return fail(reason);
    reason = detail::run(input.defer.to_shop, input.horizon, later);
    if (reason != Reason::no_gain) return fail(reason);
    now.hp = std::min(input.now.purchases.max_health_after, now.hp + input.now.purchases.health_gain);
    later.hp = std::min(input.defer.purchases.max_health_after, later.hp + input.defer.purchases.health_gain);
    reason = detail::run(input.now.post_shop, input.horizon, now);
    if (reason != Reason::no_gain) return fail(reason);
    reason = detail::run(input.defer.post_shop, input.horizon, later);
    if (reason != Reason::no_gain) return fail(reason);
    reason = detail::run(input.now.gem.remaining, input.horizon, now);
    if (reason != Reason::no_gain) return fail(reason);
    reason = detail::run(input.defer.gem.remaining, input.horizon, later);
    if (reason != Reason::no_gain) return fail(reason);
    result.now = {now.rounds, a.cost, input.purse - a.cost, 0, now.hp, now.minimum,
                  input.now.gem.gems * static_cast<float>(input.horizon - now.rounds)};
    result.defer = {later.rounds, b.cost, input.purse + input.displaced.gold - b.cost, 0, later.hp, later.minimum,
                    input.defer.gem.gems * static_cast<float>(input.horizon - later.rounds) +
                    input.displaced.gems * static_cast<float>(input.horizon - result.displaced_capture_round)};
    // Terminal gold is reported, never assigned a lambda or reused for a fictitious cycle.
    if (input.now.gem.gold > INT32_MAX - result.now.cash_after_shop ||
        input.defer.gem.gold > INT32_MAX - result.defer.cash_after_shop) return fail(Reason::unknown_input);
    result.now.cash_end = result.now.cash_after_shop + input.now.gem.gold;
    result.defer.cash_end = result.defer.cash_after_shop + input.defer.gem.gold;
    result.added_items = b.units - a.units;
    result.terminal_inventory_differs = result.added_items != 0;
    result.added_spend = b.cost - a.cost;
    result.extra_gold_required = std::max(0, b.cost - input.purse);
    result.advantage = result.now.gem_rounds - result.defer.gem_rounds;
    if (!std::isfinite(result.now.gem_rounds) || !std::isfinite(result.defer.gem_rounds) || !std::isfinite(result.advantage))
        return fail(Reason::unknown_input);
    result.decision = result.advantage > 0 ? Decision::return_now : Decision::keep_working;
    result.reason = result.advantage > 0 ? Reason::earlier_payout : Reason::no_gain;
    return result;
}

} // namespace mb::capital::episode
