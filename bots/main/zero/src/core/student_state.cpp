#include "student.h"
#include <algorithm>
#include <cmath>

namespace zero::student {

int geometry_goal_distance(const Context& ctx, Tile from, bool* known)
{
    const auto& o = *ctx.obs;
    const auto& m = *ctx.mem;
    const auto& map = *ctx.map;
    bool available = true;
    int distance = 0;
    switch (m.goal_kind)
    {
    case g_farm:
        if (m.goal_target >= 0 && m.goal_target < static_cast<int>(map.groups.size()))
            distance = cheb(from, map.groups[static_cast<std::size_t>(m.goal_target)].center);
        else available = false;
        break;
    case g_bank:
        if (m.goal_target >= 0 && m.goal_target < static_cast<int>(map.altars.size()))
            distance = cheb(from, map.altars[static_cast<std::size_t>(m.goal_target)]);
        else available = false;
        break;
    case g_recall: case g_hold:
        distance = cheb(from, map.spawn_center[o.team]);
        break;
    case g_engage: case g_assist:
    {
        available = false;
        const auto& units = m.goal_kind == g_engage ? o.enemies : o.allies;
        for (const auto& unit : units)
            if (unit.id == m.goal_target) { distance = cheb(from, unit.at); available = true; break; }
        break;
    }
    case g_avoid:
        available = !o.enemies.empty();
        if (available)
        {
            distance = cheb(from, o.enemies.front().at);
            for (const auto& unit : o.enemies) distance = std::min(distance, cheb(from, unit.at));
            distance = -distance;
        }
        break;
    default: available = false; break;
    }
    if (known) *known = available;
    return distance;
}

void state_vector(const Context& ctx, float* x)
{
    const auto& o = *ctx.obs;
    const auto& m = *ctx.mem;
    const auto& map = *ctx.map;
    std::fill_n(x, kState, 0.0f);
    x[0] = static_cast<float>(o.round) / 1000.0f;
    x[1] = static_cast<float>(o.banked_us) / 1000.0f;
    x[2] = static_cast<float>(o.banked_them) / 1000.0f;
    x[3] = static_cast<float>(o.allies_alive) / 8.0f;
    x[4] = static_cast<float>(o.enemies_alive) / 8.0f;
    x[5] = o.alive ? 1.0f : 0.0f;
    x[6] = o.self.hp / std::max(1.0f, o.self.max_hp);
    x[7] = o.self.max_hp / 2000.0f;
    x[8] = o.self.strength / 100.0f;
    x[9] = o.self.focus / 100.0f;
    x[10] = static_cast<float>(o.self.gold) / 300.0f;
    x[11] = static_cast<float>(o.self.gems) / 100.0f;
    x[12] = static_cast<float>(o.self.at.x) / static_cast<float>(std::max(1, map.w));
    x[13] = static_cast<float>(o.self.at.y) / static_cast<float>(std::max(1, map.h));
    x[14] = o.on_spawn ? 1.0f : 0.0f;
    x[15] = o.can_move ? 1.0f : 0.0f;
    x[16] = o.can_attack ? 1.0f : 0.0f;
    x[17] = o.can_cast ? 1.0f : 0.0f;
    x[18] = static_cast<float>(o.self.cooldown[0]) / 10.0f;
    x[19] = static_cast<float>(o.self.cooldown[1]) / 10.0f;
    x[20] = static_cast<float>(o.ward_charges) / 5.0f;
    x[21] = o.command_facts ? 1.0f : 0.0f;
    x[22] = o.teleporting ? 3.0f : (o.self.tributing ? 2.0f : (o.self.recalling ? 1.0f : 0.0f));
    const float channel = static_cast<float>(std::max(0, o.channel_remaining));
    x[23] = channel / (channel + 1.0f);
    x[24] = encode_priority(o.priority);
    x[25] = static_cast<float>(o.command_index) / 8.0f;
    x[26] = m.goal_kind >= 0 ? static_cast<float>(m.goal_kind + 1) / 7.0f : 0.0f;
    bool goal_known = false;
    x[27] = static_cast<float>(geometry_goal_distance(ctx, o.self.at, &goal_known)) / 20.0f;
    x[28] = goal_known ? 1.0f : 0.0f;
    const int group = o.alive && map.inside(o.self.at)
        ? map.group_of[static_cast<std::size_t>(map.idx(o.self.at))] : -1;
    x[29] = group >= 0 && group < static_cast<int>(o.group_status.size())
        ? static_cast<float>(o.group_status[static_cast<std::size_t>(group)]) / 3.0f : -1.0f;
    x[30] = group >= 0 && m.goal_kind == g_farm && m.goal_target == group ? 1.0f : 0.0f;
    for (int bit = 0; bit < 4; ++bit) x[31 + bit] = (o.self.type >> bit) & 1;
    x[35] = static_cast<float>(o.enemies.size()) / 8.0f;
    x[36] = static_cast<float>(o.allies.size()) / 8.0f;
    x[37] = static_cast<float>(o.camps.size()) / 128.0f;
    for (const auto& unit : o.enemies) x[39] += unit.hp / 2000.0f;
    for (const auto& unit : o.allies) x[40] += unit.hp / 2000.0f;
    for (const auto& camp : o.camps) x[41] += camp.hp / 10000.0f;
    if (!o.enemies.empty())
    {
        int distance = cheb(o.self.at, o.enemies.front().at);
        for (const auto& unit : o.enemies) distance = std::min(distance, cheb(o.self.at, unit.at));
        x[38] = static_cast<float>(distance) / 20.0f;
    }
    if (!map.altars.empty())
    {
        int distance = cheb(o.self.at, map.altars.front());
        for (const auto& altar : map.altars) distance = std::min(distance, cheb(o.self.at, altar));
        x[42] = static_cast<float>(distance) / 20.0f;
    }
    x[43] = static_cast<float>(cheb(o.self.at, map.spawn_center[o.team])) / 20.0f;
    // Schema2 replaces replay-unavailable student fuel with an exact personal buff fact.
    // Source adapters must establish that the self buff query succeeded, including a known empty list.
    x[44] = o.self.shield / std::max(1.0f, o.self.max_hp);
    x[45] = static_cast<float>(o.consumables[0]) / 4.0f;
    x[46] = static_cast<float>(o.consumables[1]) / 4.0f;
    // The SDK does not currently transport a respawn timer. Expose causal goal age instead.
    x[47] = m.goal_since >= 0 ? static_cast<float>(std::max(0, o.round - m.goal_since)) / 40.0f : 0.0f;
}

}  // namespace zero::student
