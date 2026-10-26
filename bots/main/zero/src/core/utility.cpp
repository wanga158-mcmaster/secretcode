#include "utility.h"
#include "action.h"
#include <algorithm>
#include <cmath>

namespace zero {
namespace {

bool contains(const std::vector<Tile>& tiles, Tile at)
{
    return std::find(tiles.begin(), tiles.end(), at) != tiles.end();
}

bool valid_radius(float radius) { return std::isfinite(radius) && radius >= 0.0f; }

void facts(UtilityAction& action, std::uint32_t inventory, std::uint32_t price, float radius, float last)
{
    action.phi[19] = static_cast<float>(inventory);
    action.phi[20] = static_cast<float>(price) / 1000.0f;
    action.phi[21] = radius / 20.0f;
    action.phi[22] = last;
}

}  // namespace

UtilityBuildContract append_ground_utilities(const Map& map, Obs& o, const UtilityRules& rules)
{
    UtilityBuildContract result;
    result.known_consume_mask = rules.known_consumable_mask & kConsumeLock;
    o.utility_actions.erase(std::remove_if(o.utility_actions.begin(), o.utility_actions.end(), [](const UtilityAction& a) {
        return a.type == a_ward || a.type == a_use_consumable;
    }), o.utility_actions.end());
    o.utility_known_mask &= ~(action_bit(a_ward) | action_bit(a_use_consumable));
    if (!o.command_facts)
    {
        result.affected_limits |= ul_command_facts;
        return result;
    }
    const bool free = o.alive && !o.self.stunned && !o.self.recalling && !o.self.tributing && !o.teleporting &&
                      !(o.state_bits & ((1u << 1) | (1u << 2) | (1u << 5) | (1u << 12)));
    const bool ward_ready = free && !(o.restriction_bits & (1u << 5)) && o.ward_charges > 0;
    const bool consume_ready = free && !(o.restriction_bits & (1u << 4));
    const bool valid_map = map.w > 0 && map.h > 0 && o.team >= 0 && o.team < 2 &&
                           map.flags.size() >= static_cast<std::size_t>(map.w) * static_cast<std::size_t>(map.h);

    result.ward_complete = !ward_ready;
    if (!ward_ready) o.utility_known_mask |= action_bit(a_ward);
    else if (!valid_map || !map.inside(o.self.at) || !valid_radius(rules.ward_radius)) result.affected_limits |= ul_rules;
    else if (!o.ally_ward_facts) result.affected_limits |= ul_ally_ward_facts;
    else
    {
        result.ward_complete = o.reservation_facts;
        if (!o.reservation_facts) result.affected_limits |= ul_reservations;
        const int radius = static_cast<int>(std::min(rules.ward_radius, static_cast<float>(std::max(map.w, map.h))));
        const int x0 = std::max(0, static_cast<int>(o.self.at.x) - radius), x1 = std::min(map.w - 1, static_cast<int>(o.self.at.x) + radius);
        const int y0 = std::max(0, static_cast<int>(o.self.at.y) - radius), y1 = std::min(map.h - 1, static_cast<int>(o.self.at.y) + radius);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
            {
                const Tile at{ static_cast<std::int16_t>(x), static_cast<std::int16_t>(y) };
                if (static_cast<float>(cheb(at, o.self.at)) > rules.ward_radius) continue;
                const auto flags = map.at(at);
                if ((flags & (tf_wall | tf_altar)) || (flags & (o.team == 0 ? tf_spawn_b : tf_spawn_a)) || contains(o.ally_ward_tiles, at)) continue;
                if (flags & tf_hole)
                {
                    result.affected_limits |= ul_ward_holes;
                    result.ward_complete = false;
                    continue;
                }
                if (contains(o.reserved_tiles, at))
                {
                    result.affected_limits |= ul_reservations;
                    result.ward_complete = false;
                    continue;
                }
                auto& action = append_utility(o, a_ward, -1, at);
                facts(action, static_cast<std::uint32_t>(o.ward_charges), 0, rules.ward_radius, static_cast<float>(o.ward_cooldown) / 40.0f);
                ++result.ward_count;
            }
        // A represented conservative subset can be used, but is never a complete-space coverage claim.
        if (result.ward_complete || result.ward_count) o.utility_known_mask |= action_bit(a_ward);
    }

    result.consume_complete = !consume_ready || (!o.consumables[0] && !o.consumables[1]);
    if (consume_ready && o.consumables[0]) result.affected_limits |= ul_tp_use;
    const bool lock_ready = consume_ready && o.consumables[1] > 0;
    if (lock_ready)
    {
        if (!(result.known_consume_mask & kConsumeLock)) result.affected_limits |= ul_lock_facts;
        else if (!valid_map || !map.inside(o.self.at) || !valid_radius(rules.lock_radius)) result.affected_limits |= ul_rules;
        else if (o.altar_lock.size() != map.altars.size()) result.affected_limits |= ul_lock_facts;
        else
        {
            std::vector<std::size_t> altars;
            altars.reserve(map.altars.size());
            for (std::size_t i = 0; i < map.altars.size(); ++i) altars.push_back(i);
            std::sort(altars.begin(), altars.end(), [&](std::size_t a, std::size_t b) {
                if (map.altars[a].y != map.altars[b].y) return map.altars[a].y < map.altars[b].y;
                return map.altars[a].x < map.altars[b].x;
            });
            for (const std::size_t i : altars)
            {
                const Tile at = map.altars[i];
                if (!map.inside(at) || static_cast<float>(cheb(at, o.self.at)) > rules.lock_radius || o.altar_lock[i] == lk_ours) continue;
                auto& action = append_utility(o, a_use_consumable, 1, at);
                facts(action, o.consumables[1], rules.lock_price, rules.lock_radius, o.altar_lock[i] == lk_theirs ? 1.0f : 0.0f);
                ++result.lock_count;
            }
            result.consume_complete = !o.consumables[0];
        }
    }
    if (result.consume_complete || result.lock_count) o.utility_known_mask |= action_bit(a_use_consumable);
    return result;
}

}  // namespace zero
