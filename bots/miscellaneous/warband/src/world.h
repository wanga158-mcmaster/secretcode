#pragma once
// Static map knowledge (terrain, camp groups, altars, spawns) and this champion's beliefs about
// camp groups, plus lazily cached distance fields. One World per champion instance.
// Fuel: map::camp_groups() is ~750 per group; everything else here is plain data built once,
// and per-group bitboards are built only for groups we path to.
#include <cambc.h>
#include <memory>
#include <vector>
#include "model.h"
#include "util.h"

namespace wb {

struct Monster {
    vec2 tile{};
    float hp = 0, str = 0;
    int range = 0;
    cambc::camp_type type{};
    float cur = 0;            // last seen health (persists: camps keep damage while asleep)
    bool dead = false;        // last seen dead
    int sdk_member = 0;       // index into the SDK group's camps()
};

enum class GState : std::uint8_t { unknown, alive, reward, respawning };

struct Group {
    int sdk = 0;              // index into World::sdk_groups
    std::vector<Monster> members;   // Smith order: strength per health descending
    vec2 center{};
    int gold = 0, gems = 0, respawn = 0;
    float hp = 0, str = 0;
    int range = 0;            // max attack range of members (0: harmless)
    bool harmless = true, monolith = false;
    bool pack_job = false;    // no single base champion clears it safely: the pack converges on it
    // Beliefs, refreshed whenever a member tile is in view.
    GState state = GState::unknown;
    int ready_round = 0;      // respawning: round it should be alive again
    int seen_round = -1;
    float seen_hp = 0, seen_str = 0;
    int banned_until = 0;
};

class World {
public:
    void init();
    bool ready() const { return ready_; }

    int w = 0, h = 0;
    bitboard terrain;         // walls | holes | altars | enemy spawn
    bitboard camp_tiles;      // every camp spawn tile
    bitboard my_spawn, enemy_spawn;
    vec2 my_spawn_c{}, enemy_spawn_c{};
    std::vector<cambc::map::camp_group> sdk_groups;
    std::vector<Group> groups;
    std::vector<std::int16_t> group_at; // tile -> group index or -1
    std::vector<vec2> altars;
    bitboard altar_ring;      // free tiles within tribute range of an altar
    int walkable = 0;

    int idx(vec2 p) const { return p.y * w + p.x; }
    int group_of(vec2 p) const { return in_bounds(p) ? group_at[static_cast<std::size_t>(idx(p))] : -1; }
    const cambc::camp& camp_of(int g, const Monster& m) const {
        return sdk_groups[static_cast<std::size_t>(groups[static_cast<std::size_t>(g)].sdk)].camps()[static_cast<std::size_t>(m.sdk_member)];
    }
    const bitboard& tiles(int g);   // the group's spawn tiles (built on first use)

    // Fields return nullptr when not yet computed and fuel does not allow it now.
    const cambc::distance_field* group_field(int g, bool compute = true);
    const cambc::distance_field* altar_field(bool compute = true);
    const cambc::distance_field* home_field(bool compute = true);
    // Believed remaining monsters of a group (respawned groups read full health).
    float remaining_hp(int g, int round) const;
    // Believed living monsters in kill order into out (at most 32); returns the count.
    int believed_monsters(int g, int round, model::Monster* out) const;
    // Walking distance from p to group g (field if cached, else Chebyshev x detour).
    int dist_to_group(vec2 p, int g);
    int dist_to_altar(vec2 p);
    int dist_home(vec2 p);

    // Refresh beliefs from current vision (cheap masks plus the visible groups' states).
    void observe(int round);
    long field_cost() const { return 3000 + 3L * walkable; }

private:
    bool ready_ = false;
    std::vector<std::unique_ptr<bitboard>> tiles_;
    std::vector<std::unique_ptr<cambc::distance_field>> gfields_;
    std::unique_ptr<cambc::distance_field> altar_field_, home_field_;
    std::unique_ptr<cambc::distance_field> compute_field(const bitboard& walls, const bitboard& roots);
};

} // namespace wb
