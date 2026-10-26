#pragma once
// Static map knowledge (camps, altars, spawns, terrain) and cached distance fields.
#include <cambc.h>
#include <memory>
#include <optional>
#include <vector>
#include "model.h"
#include "util.h"

namespace tt {

struct MonsterStats {
    float hp = 0, str = 0;
    int range = 0;
};

struct Member {
    vec2 pos;
    int tile = 0;
    int group = -1;
    cambc::camp_type type = cambc::camp_type::gem_cluster;
    MonsterStats stats;
    const cambc::camp* camp = nullptr;          // into World::loaded_ (stable)
};

struct Group {
    Group() {}
    int index = 0;
    std::vector<int> members;     // indices into World::members
    int gems = 0, gold = 0, cooldown = 60;
    bool harmless = true;
    int max_range = 0;
    float hp_total = 0, str_total = 0;
    vec2 centre;
    GroupSpec spec;               // Smith-ordered planning view at full health
    const cambc::map::camp_group* handle = nullptr; // into World::loaded_ (stable)
};

// A lazily computed distance field.
struct Field {
    cambc::distance_field df;
    bool ready = false;
    int get(vec2 p) const
    {
        const int v = df.get(p);
        return v >= cambc::distance_field::unreachable ? kInf : v;
    }
};

class World {
public:
    static World& get();

    int W = 0, H = 0, area = 0;
    std::vector<Member> members;
    std::vector<Group> groups;
    std::unique_ptr<short[]> tile_member; // tile -> member index, -1 if none (raw: no zeroing)
    std::vector<vec2> altars;
    std::vector<cambc::bitboard> altar_submit; // free tiles within submit range of each altar
    cambc::bitboard walls_static;        // walls, holes, altars, enemy spawn
    cambc::bitboard camp_tiles_all;      // every camp spawn tile
    cambc::bitboard my_spawn, enemy_spawn;
    std::vector<vec2> my_spawn_tiles;
    vec2 my_spawn_centre, enemy_spawn_centre;

    int tile(vec2 p) const { return p.y * W + p.x; }
    bool in_bounds(vec2 p) const { return p.x >= 0 && p.y >= 0 && p.x < W && p.y < H; }
    int member_at(vec2 p) const { return in_bounds(p) ? tile_member[static_cast<std::size_t>(tile(p))] : -1; }
    int group_at(vec2 p) const { const int m = member_at(p); return m < 0 ? -1 : members[static_cast<std::size_t>(m)].group; }
    bool blocked_static(vec2 p) const { return !in_bounds(p) || walls_static.is_set(p); }

    // Estimated fuel for one full-map field.
    long field_cost() const { return static_cast<long>(area) * 3 + 4000; }

    // Navigation fields. Group fields are rooted at the member tiles (other camps are walls);
    // nullptr when not yet computed and `compute` is false or fuel is short.
    const Field* group_field(int g, bool compute);
    const Field* altar_field(bool compute);
    const Field* spawn_field(bool compute);
    int fields_this_turn = 0;
    int field_budget = 1;
    long init_fuel[5] = {0, 0, 0, 0, 0}; // fuel left at init checkpoints (diagnosis)
    long init_parts[3] = {0, 0, 0};      // members, sort, group move (diagnosis)

private:
    std::vector<cambc::map::camp_group> loaded_; // the engine's groups, kept: camps and groups point here
    void init();
    bool compute_field(Field& f, const cambc::bitboard& roots, const cambc::bitboard& walls);
    std::vector<std::unique_ptr<Field>> group_fields_;
    std::unique_ptr<Field> altar_field_, spawn_field_;
};

// Camp design stats per camp type, read from the engine once.
MonsterStats monster_stats(cambc::camp_type type);

} // namespace tt
