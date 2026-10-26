#include "student.h"
#include <algorithm>

namespace zero::student {
namespace {

// Student goal schema1 (zero-based phi; distinct from every diagnostic legacy/plan descriptor):
//   0..6 GoalKind one-hot; 7 obstacle-path available=0 (no path fact is supplied).
//   8 dx/20; 9 dy/20; 10 Chebyshev geometry/40 to the row's reference point.
//   11 gold/300; 12 gems/100; 13 public/full HP/10000; 14 observed HP/10000;
//   15 strength/200; 16 public member count/64.
//   17 Farm GroupStatus/3; Bank observed lock/2; unit/self observed recall+2*tribute bits/3.
//   18 Farm current status known; Bank positive lock presence observed; unit/self status supplied.
//   19 Farm distinct observed-living members/public members; unit/self=1, Bank=0.
//   20 Farm max(0, remembered respawn estimate-round)/40, otherwise0.
//   21 Farm remembered estimate present (NOT an exact native timer); Bank observed lock hits/5;
//      unit/self Focus/200. Unavailable values remain0.
//   22 Farm full current HP known only if every public member is observed alive, the group is empty,
//      or current reward/respawn status proves no living members; unit/self=1, Bank=0.
//   23 exact current-goal kind/target equality.
// Farm public full HP and observed HP stay separate: absent camps never acquire assumed full HP or death.
// Observations have one member per static tile; duplicates use the last view, then sum in input order.
// Bank lock0 is unobserved-or-none, NOT proved absence;18 is0 without positive observed lock evidence.
// Schema2 defines11 only for Farm and self rows. It is an unused zero for Engage/Assist, not a gold fact.
// Recall/Hold reference the public own-spawn center; Avoid references our current tile. Their unit facts
// refer to self. All singleton intents remain present: choosing an intent does not execute its primitive.

struct TileView
{
    Tile at;
    int last{ -1 };
};

std::uint32_t bucket(Tile at, std::size_t count)
{
    return (static_cast<std::uint32_t>(at.x) * 0x9e3779b9u ^ static_cast<std::uint32_t>(at.y) * 0x85ebca6bu) &
           static_cast<std::uint32_t>(count - 1);
}

int public_group(const Map& map, Tile at)
{
    if (!map.inside(at)) return -1;
    const auto index = static_cast<std::size_t>(map.idx(at));
    if (index >= map.group_of.size()) return -1;
    const int group = map.group_of[index];
    return group >= 0 && static_cast<std::size_t>(group) < map.groups.size() ? group : -1;
}

Cand& add(const Context& ctx, std::vector<Cand>& out, GoalKind kind, int target, Tile at)
{
    out.emplace_back();
    auto& c = out.back();
    c.kind = kind; c.target = target; c.at = at;
    c.phi[kind] = 1.0f;
    c.phi[8] = static_cast<float>(static_cast<int>(at.x) - ctx.obs->self.at.x) / 20.0f;
    c.phi[9] = static_cast<float>(static_cast<int>(at.y) - ctx.obs->self.at.y) / 20.0f;
    c.phi[10] = static_cast<float>(cheb(at, ctx.obs->self.at)) / 40.0f;
    c.phi[23] = ctx.mem && ctx.mem->goal_kind == kind && ctx.mem->goal_target == target ? 1.0f : 0.0f;
    return c;
}

void unit_facts(const UnitView& unit, bool self, Cand& c)
{
    c.phi[11] = self ? static_cast<float>(unit.gold) / 300.0f : 0.0f;
    c.phi[12] = static_cast<float>(unit.gems) / 100.0f;
    c.phi[13] = unit.max_hp / 10000.0f;
    c.phi[14] = unit.hp / 10000.0f;
    c.phi[15] = unit.strength / 200.0f;
    c.phi[16] = 1.0f / 64.0f;
    c.phi[17] = static_cast<float>((unit.recalling ? 1 : 0) + (unit.tributing ? 2 : 0)) / 3.0f;
    c.phi[18] = c.phi[19] = c.phi[22] = 1.0f;
    c.phi[21] = unit.focus / 200.0f;
}

void visible_units(const Context& ctx, const std::vector<UnitView>& units, GoalKind kind, std::vector<Cand>& out)
{
    const auto begin = out.size();
    for (const auto& unit : units) unit_facts(unit, false, add(ctx, out, kind, unit.id, unit.at));
    // Stable physical IDs, independent of engine bitboard enumeration order. No candidate is removed.
    std::sort(out.begin() + static_cast<std::ptrdiff_t>(begin), out.end(), [](const Cand& a, const Cand& b) {
        return a.target < b.target;
    });
}

}  // namespace

void goal_candidates(const Context& ctx, std::vector<Cand>& out)
{
    const auto& map = *ctx.map;
    const auto& o = *ctx.obs;
    out.clear();
    out.reserve(map.groups.size() + map.altars.size() + o.enemies.size() + o.allies.size() + 3);
    // Farm rows occupy exactly their public group index, which also provides the one-pass HP accumulator.
    for (std::size_t g = 0; g < map.groups.size(); ++g)
    {
        const auto& group = map.groups[g];
        auto& c = add(ctx, out, g_farm, static_cast<int>(g), group.center);
        c.phi[11] = static_cast<float>(group.gold) / 300.0f;
        c.phi[12] = static_cast<float>(group.gems) / 100.0f;
        c.phi[13] = group.total_hp / 10000.0f;
        c.phi[15] = group.total_strength / 200.0f;
        c.phi[16] = static_cast<float>(group.members.size()) / 64.0f;
        const int status = g < o.group_status.size() ? o.group_status[g] : gs_unknown;
        c.phi[17] = static_cast<float>(status) / 3.0f;
        c.phi[18] = status != gs_unknown ? 1.0f : 0.0f;
        if (ctx.mem && g < ctx.mem->respawn_round.size() && ctx.mem->respawn_round[g] >= 0)
        {
            const int estimate = ctx.mem->respawn_round[g];
            c.phi[20] = static_cast<float>(estimate > o.round ? estimate - o.round : 0) / 40.0f;
            c.phi[21] = 1.0f;
        }
    }
    if (!o.camps.empty() && !map.groups.empty())
    {
        std::size_t count = 1;
        while (count < o.camps.size() * 2) count *= 2;
        std::vector<TileView> seen(count);
        for (std::size_t i = 0; i < o.camps.size(); ++i)
        {
            const auto at = o.camps[i].at;
            if (public_group(map, at) < 0) continue;
            auto slot = bucket(at, count);
            while (seen[slot].last >= 0 && seen[slot].at != at) slot = (slot + 1) & (count - 1);
            seen[slot].at = at; seen[slot].last = static_cast<int>(i);
        }
        for (std::size_t i = 0; i < o.camps.size(); ++i)
        {
            const auto& camp = o.camps[i];
            const int group = public_group(map, camp.at);
            if (group < 0 || camp.hp <= 0.0f) continue;
            auto slot = bucket(camp.at, count);
            while (seen[slot].last >= 0 && seen[slot].at != camp.at) slot = (slot + 1) & (count - 1);
            if (seen[slot].last != static_cast<int>(i)) continue;
            out[static_cast<std::size_t>(group)].phi[14] += camp.hp;
            out[static_cast<std::size_t>(group)].phi[19] += 1.0f;
        }
    }
    for (std::size_t g = 0; g < map.groups.size(); ++g)
    {
        auto& c = out[g];
        const auto members = map.groups[g].members.size();
        const float observed = c.phi[19];
        const int status = g < o.group_status.size() ? o.group_status[g] : gs_unknown;
        c.phi[14] /= 10000.0f;
        c.phi[19] = members ? observed / static_cast<float>(members) : 0.0f;
        c.phi[22] = !members || observed == static_cast<float>(members) || status == gs_reward || status == gs_respawning ? 1.0f : 0.0f;
    }
    for (std::size_t a = 0; a < map.altars.size(); ++a)
    {
        auto& c = add(ctx, out, g_bank, static_cast<int>(a), map.altars[a]);
        c.phi[12] = static_cast<float>(o.self.gems) / 100.0f;
        const auto lock = a < o.altar_lock.size() ? o.altar_lock[a] : lk_none;
        c.phi[17] = static_cast<float>(lock) / 2.0f;
        c.phi[18] = lock != lk_none ? 1.0f : 0.0f;
        c.phi[21] = a < o.altar_lock_hits.size() && o.altar_lock_hits[a] >= 0 ? static_cast<float>(o.altar_lock_hits[a]) / 5.0f : 0.0f;
    }
    const Tile spawn = map.spawn_center[o.team];
    unit_facts(o.self, true, add(ctx, out, g_recall, -1, spawn));
    visible_units(ctx, o.enemies, g_engage, out);
    unit_facts(o.self, true, add(ctx, out, g_avoid, -1, o.self.at));
    unit_facts(o.self, true, add(ctx, out, g_hold, -1, spawn));
    visible_units(ctx, o.allies, g_assist, out);
}

}  // namespace zero::student
