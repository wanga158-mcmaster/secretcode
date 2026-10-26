#include "ground_scene.h"
#include <algorithm>
#include <cmath>

namespace zero {
namespace {

bool contains(const std::vector<Tile>& tiles, Tile at)
{
    return std::find(tiles.begin(), tiles.end(), at) != tiles.end();
}

bool valid_radius(float radius) { return std::isfinite(radius) && radius >= 0.0f; }

UtilityAction& append_raw(std::vector<UtilityAction>& out, const Obs& obs,
                         ActionType type, int target, Tile at)
{
    out.emplace_back();
    auto& action = out.back();
    action.type = type;
    action.target = target;
    action.at = at;
    action.phi[15] = static_cast<float>(target) / 48.0f;
    action.phi[16] = static_cast<float>(at.x - obs.self.at.x) / 8.0f;
    action.phi[17] = static_cast<float>(at.y - obs.self.at.y) / 8.0f;
    action.phi[18] = static_cast<float>(cheb(at, obs.self.at)) / 8.0f;
    return action;
}

}  // namespace

void build_ground_utility_scene(const Map& map, const Obs& o, const UtilityRules& rules,
                                GroundUtilityScene& scene)
{
    scene.source_map = nullptr;
    scene.source_obs = nullptr;
    scene.rules = rules;
    scene.contract = UtilityBuildContract{};
    scene.known_mask = 0;
    scene.ward_tiles.clear();
    scene.lock_rows.clear();
    auto& result = scene.contract;
    result.known_consume_mask = rules.known_consumable_mask & kConsumeLock;
    const auto finish = [&] {
        scene.source_map = &map;
        scene.source_obs = &o;
        scene.source_revision = o.revision;
    };
    if (!o.command_facts)
    {
        result.affected_limits |= ul_command_facts;
        finish();
        return;
    }
    const bool free = o.alive && !o.self.stunned && !o.self.recalling && !o.self.tributing && !o.teleporting &&
                      !(o.state_bits & ((1u << 1) | (1u << 2) | (1u << 5) | (1u << 12)));
    const bool ward_ready = free && !(o.restriction_bits & (1u << 5)) && o.ward_charges > 0;
    const bool consume_ready = free && !(o.restriction_bits & (1u << 4));
    const bool valid_map = map.w > 0 && map.h > 0 && o.team >= 0 && o.team < 2 &&
                           map.flags.size() >= static_cast<std::size_t>(map.w) * static_cast<std::size_t>(map.h);

    result.ward_complete = !ward_ready;
    if (!ward_ready) scene.known_mask |= action_bit(a_ward);
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
                scene.ward_tiles.push_back(at);
                ++result.ward_count;
            }
        if (result.ward_complete || result.ward_count) scene.known_mask |= action_bit(a_ward);
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
                scene.lock_rows.push_back({ at, o.altar_lock[i] == lk_theirs });
                ++result.lock_count;
            }
            result.consume_complete = !o.consumables[0];
        }
    }
    if (result.consume_complete || result.lock_count) scene.known_mask |= action_bit(a_use_consumable);
    finish();
}

bool apply_ground_utility_contract(Obs& o, const GroundUtilityScene& scene)
{
    if (!scene.source_map || scene.source_obs != &o || scene.source_revision != o.revision) return false;
    o.utility_actions.erase(std::remove_if(o.utility_actions.begin(), o.utility_actions.end(), [](const UtilityAction& action) {
        return action.type == a_ward || action.type == a_use_consumable;
    }), o.utility_actions.end());
    o.utility_known_mask &= ~(action_bit(a_ward) | action_bit(a_use_consumable));
    o.utility_known_mask |= scene.known_mask;
    o.utility_known_consumables = scene.contract.known_consume_mask;
    o.utility_capability_limits = scene.contract.limitations;
    o.utility_affected_limits = scene.contract.affected_limits;
    o.ward_arguments_complete = scene.contract.ward_complete;
    o.consumable_arguments_complete = scene.contract.consume_complete;
    return true;
}

bool materialize_ground_utility_scene(const Map& map, const Obs& o,
                                     const GroundUtilityScene& scene, ActionType type,
                                     std::vector<UtilityAction>& out)
{
    out.clear();
    if (!scene.fresh_for(map, o) || (type != a_ward && type != a_use_consumable)) return false;
    if (type == a_ward)
    {
        out.reserve(scene.ward_tiles.size());
        for (Tile at : scene.ward_tiles)
        {
            auto& action = append_raw(out, o, a_ward, -1, at);
            action.phi[19] = static_cast<float>(static_cast<std::uint32_t>(o.ward_charges));
            action.phi[20] = static_cast<float>(0u) / 1000.0f;
            action.phi[21] = scene.rules.ward_radius / 20.0f;
            action.phi[22] = static_cast<float>(o.ward_cooldown) / 40.0f;
        }
    }
    else
    {
        out.reserve(scene.lock_rows.size());
        for (const auto& lock : scene.lock_rows)
        {
            auto& action = append_raw(out, o, a_use_consumable, 1, lock.at);
            action.phi[19] = static_cast<float>(o.consumables[1]);
            action.phi[20] = static_cast<float>(scene.rules.lock_price) / 1000.0f;
            action.phi[21] = scene.rules.lock_radius / 20.0f;
            action.phi[22] = lock.enemy ? 1.0f : 0.0f;
        }
    }
    return true;
}

}  // namespace zero
