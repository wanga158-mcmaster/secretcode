#pragma once
// Bounded planning-only use of complete turn-start camp occupancy. No SDK queries or cache.
#include <array>
#include <cstddef>
#include <cstdint>
#include "config.h"
#include "model.h"

namespace mb::survivors {

inline constexpr std::size_t max_members = 32;
inline constexpr std::size_t max_cached_camps = 64;

template<class Context, class Actions>
struct TurnFacts
{
    const Context& ctx;
    const Actions& actions;
    int round;
    bool complete_occupancy;

    bool fresh(const Context& current) const
    {
        return complete_occupancy && &ctx == &current && round == current.round &&
               !actions.moved && !actions.attacked && !actions.cast;
    }
};

// A borrowed pointer exists only while its stack owner lives. It retains no observation facts
// between calls/turns, and neither this storage nor the scope is used with the feature off.
template<class Context, class Actions>
const TurnFacts<Context, Actions>*& borrowed_turn()
{
    static const TurnFacts<Context, Actions>* facts = nullptr;
    return facts;
}

template<bool Enabled, class Context, class Actions>
class TurnBorrow
{
public:
    TurnBorrow(const Context&, const Actions&, bool) {}
};

template<class Context, class Actions>
class TurnBorrow<true, Context, Actions>
{
public:
    TurnBorrow(const Context& ctx, const Actions& actions, bool complete_occupancy)
        : facts_{ ctx, actions, ctx.round, complete_occupancy }, previous_(borrowed_turn<Context, Actions>())
    {
        borrowed_turn<Context, Actions>() = &facts_;
    }
    ~TurnBorrow() { borrowed_turn<Context, Actions>() = previous_; }
    TurnBorrow(const TurnBorrow&) = delete;
    TurnBorrow& operator=(const TurnBorrow&) = delete;

private:
    TurnFacts<Context, Actions> facts_;
    const TurnFacts<Context, Actions>* previous_;
};

template<class Actions, class Context, class Group, class Belief>
bool constants(const Context& ctx, const Group& info, const Belief& belief, int group,
               model::FightConstants& out)
{
    const auto* facts = borrowed_turn<Context, Actions>();
    // The caller's alive status is required even when a respawn/reward belief has old live views.
    if (!facts || !facts->fresh(ctx) || belief.status != decltype(belief.status)::alive ||
        belief.seen_round != ctx.round) return false;
    const std::size_t original = info.tiles.size();
    if (original == 0 || original > max_members || ctx.camps.size() > max_cached_camps) return false;
    for (const auto tile : info.tiles)
        if (!ctx.visible.is_set(tile)) return false;

    std::array<model::Monster, max_members> members{};
    std::uint32_t occupied = 0;
    std::size_t living = 0;
    for (const auto& camp : ctx.camps)
    {
        if (camp.group != group) continue;
        if (camp.member < 0 || static_cast<std::size_t>(camp.member) >= original ||
            camp.pos != info.tiles[static_cast<std::size_t>(camp.member)] ||
            !(camp.hp > 0.0f) || !std::isfinite(camp.hp) ||
            !(camp.strength >= 0.0f) || !std::isfinite(camp.strength)) return false;
        const std::uint32_t bit = std::uint32_t{ 1 } << camp.member;
        if (occupied & bit) return false;
        occupied |= bit;
        members[static_cast<std::size_t>(camp.member)] = { camp.hp, camp.strength };
        ++living;
    }
    // Complete authoritative awake/asleep enumeration plus full spawn visibility proves these
    // missing nonmoving members dead. A partial list alone never licenses this inference.
    if (living == 0 || living == original) return false;
    std::vector<model::Monster> monsters;
    monsters.reserve(living);
    // Canonical original-member order makes extraction independent of occupancy iteration order.
    for (std::size_t member = 0; member < original; ++member)
        if (occupied & (std::uint32_t{ 1 } << member)) monsters.push_back(members[member]);
    out = model::fight_constants(monsters);
    return true;
}

// A call boundary keeps both complete forecasts materialized before the volatile selector read.
// Shadow/use differ only in this nonzero Data word; it never controls eligibility or observation.
[[gnu::noinline]] inline model::FightForecast select(const model::FightForecast& original,
                                                   const model::FightForecast& surviving)
{
    static volatile std::uint32_t apply = cfg::visible_survivors_apply ? 0x4d425401u : 0x4d425400u;
    return (apply & 1u) ? surviving : original;
}

} // namespace mb::survivors
