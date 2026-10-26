#pragma once
// Static map knowledge, built once per bot instance, plus a cache of distance fields.
//
// Claims: terrain, altars, spawns and camp-group membership never change during a match
// (GameRules "Map features"); camps occupy their spawn tiles while alive, so static fields
// treat every camp tile except the target's own as a wall. Fields are computed lazily and
// only when fuel allows; their cost depends on search depth, so it is measured, not guessed.
#include <cambc.h>
#include <cstdint>
#include <memory>
#include <vector>
#include "util.h"

namespace mb {

struct CampTypeStats
{
    float health{};
    float strength{};
    int range{};
};

struct GroupInfo
{
    std::vector<vec2> tiles;            // member spawn tiles
    std::vector<std::uint8_t> types;    // member camp_type values
    vec2 center{};
    int min_x{}, min_y{}, max_x{}, max_y{};
    std::uint32_t gold{}, gems{};
    int respawn{};
    float total_hp{};
    float total_strength{};
    int max_range{};
    float neighbours{};  // mean count of other members within Chebyshev radius one
    // Fight constants with members killed in Smith order (descending strength/health):
    // weighted_hp = sum_k strength_k * cumulative_health_k. Damage taken at team damage
    // rate d is about weighted_hp/d (see model::forecast_from_constants).
    float weighted_hp{};
    bool harmless{};
    bool has_monolith{};
};

class World
{
public:
    static World& get();

    int width{}, height{};
    cambc::bitboard blocked;      // never walkable: walls, holes, altars, enemy spawn
    cambc::bitboard camp_spawns;  // every camp spawn tile
    std::vector<GroupInfo> groups;
    std::vector<cambc::map::camp_group> group_objects;  // SDK objects, same order as groups
    std::vector<vec2> altars;
    std::vector<vec2> ally_spawn, enemy_spawn;
    vec2 ally_spawn_center{}, enemy_spawn_center{};
    CampTypeStats camp_stats[6]{};

    // Group index owning a camp spawn tile (or -1), and the member index within it.
    int group_at(vec2 tile) const;
    int member_at(vec2 tile) const;
    const cambc::camp& camp(int group, int member) const { return group_objects[static_cast<std::size_t>(group)].camps()[static_cast<std::size_t>(member)]; }

    // Distance fields (nullptr when not yet computed and fuel does not allow computing now).
    // `tail_margin`: fuel kept free after the search (default cfg::field_tail_margin).
    const cambc::distance_field* group_field(int group, bool allow_compute, std::uint64_t tail_margin = 0);
    const cambc::distance_field* altar_field(bool allow_compute);
    const cambc::distance_field* spawn_field(bool allow_compute);
    // An altar whose submit range holds only one group's camp tiles (a gate): reachable by
    // standing on those tiles while that group is down. Field rooted at the gate tiles.
    std::vector<vec2> gate_tiles;
    int gate_group{ -1 };
    const cambc::distance_field* gate_field(bool allow_compute);
    // Field to walk towards a group from `from`: the full group field when present, else a
    // partial one searched only until `from` is reached (cost ~ distance, not map size; full
    // fields on 100x100 maps cost ~35K and were often unaffordable, leaving travel blocked).
    // Partial fields are for stepping only, never for travel estimates.
    const cambc::distance_field* route_field(int group, vec2 from);
    // Can a group be reached on foot from our spawn? 1 yes, 0 no, -1 unknown (spawn field not
    // computed yet). A member tile counts when any neighbour has a finite spawn distance.
    int walk_reachable(int group);
    // Path distance from `from` to a field's roots, falling back to a Chebyshev estimate.
    static int distance(const cambc::distance_field* field, vec2 from, vec2 fallback_target);
    // Fuel estimate for one field rooted near `center`: measured cost per expansion step times
    // the estimated search depth (farthest corner with a detour allowance).
    std::uint64_t field_cost_at(vec2 center) const;
    int depth_estimate(vec2 center) const;
    // Typical estimate (map centre) for callers without a specific field in mind.
    std::uint64_t field_cost() const { return field_cost_at({ width / 2, height / 2 }); }
    bool calibrated() const { return measured_; }

private:
    World();
    bool compute(std::unique_ptr<cambc::distance_field>& slot, const cambc::bitboard& roots, bool allow_compute, vec2 center,
                 std::uint64_t tail_margin = 0);

    std::vector<std::unique_ptr<cambc::distance_field>> group_fields_;
    std::vector<std::unique_ptr<cambc::distance_field>> partial_fields_;
    std::unique_ptr<cambc::distance_field> altar_field_, spawn_field_, gate_field_;
    std::vector<std::int8_t> reachable_;     // per group: -1 unknown, 0 no, 1 yes
    std::vector<std::int16_t> tile_group_;   // row-major tile -> group index
    std::vector<std::int16_t> tile_member_;  // row-major tile -> member index
    std::uint64_t step_cost_{};
    bool measured_{};
};

} // namespace mb
