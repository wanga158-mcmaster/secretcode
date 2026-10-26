#pragma once
// Static map knowledge, built once per bot instance on the first gameplay turn, and a cache
// of distance fields computed lazily under a fuel gate.
//
// Static facts (GameRules "Map features", "Camps & capture"): terrain, altars, spawns and camp
// groups never change; a camp stands on its spawn tile while alive and never moves. Fields
// therefore treat every camp tile as a wall except the tiles a field is rooted at.
#include <cambc.h>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace cw {

using cambc::vec2;

inline int cheb(vec2 a, vec2 b)
{
    const int dx = a.x > b.x ? a.x - b.x : b.x - a.x;
    const int dy = a.y > b.y ? a.y - b.y : b.y - a.y;
    return dx > dy ? dx : dy;
}

struct Member
{
    vec2 tile{};
    cambc::camp_type type{};
    float hp{};
    float str{};
    int range{};
};

struct Group
{
    int id{};
    int first{};                       // index of the first member in World::all_members
    std::span<const Member> members;   // view into World::all_members
    std::span<const cambc::camp> camps;   // SDK camp views (World::sdk_groups), same order
    vec2 anchor{};                     // member tile nearest the group's centre
    int gold{}, gems{}, cooldown{};
    float total_hp{}, total_str{};
    int max_range{};
    bool harmful{};
    bool big{};                        // golem or monolith
    float smith_w{};                   // Smith constants at full health (model.h)
    int party_n{1};                    // E8 party size at base stats (1 solo .. 5 team)
    int spawn_dist{-1};                // walking steps from our spawn (-1 unknown/unreachable)
    int enemy_spawn_dist{-1};
    int altar_dist{-1};                // walking steps to the nearest altar tile
};

class World
{
public:
    static World& get();

    bool ready{};
    int width{}, height{};
    cambc::bitboard impassable;   // walls, holes, altars, enemy spawn
    cambc::bitboard camp_all;     // every camp spawn tile
    cambc::bitboard ally_spawn;
    cambc::bitboard enemy_spawn;
    vec2 spawn_center{}, enemy_center{};
    std::vector<vec2> spawn_tiles;
    std::vector<vec2> altars;
    std::vector<Group> groups;
    std::vector<cambc::map::camp_group> sdk_groups;   // same order as groups
    std::vector<Member> all_members;                   // every group's members, group order
    float gem_supply{};                                // gems a round if every group paid each respawn (E12)
    float wolf_hp{1350}, elk_hp{2400};

    // Build static data; costs ~20-60K fuel (first gameplay turn has a double budget).
    void init();
    void init_step();             // bounded_turns: resumable static construction; ready on completion

    // Group / member owning a camp spawn tile, or -1: a bitboard test (~38 fuel), then a binary
    // search over the sorted camp tiles.
    int group_at(vec2 t) const;
    int member_at(vec2 t) const;
    bool in_bounds(vec2 t) const { return t.x >= 0 && t.y >= 0 && t.x < width && t.y < height; }
    bool walkable_static(vec2 t) const;   // not impassable and not a camp tile

    // Distance fields; nullptr when not computed and the fuel gate refuses to compute now.
    const cambc::distance_field* group_field(int g, bool allow_compute);
    const cambc::distance_field* spawn_field(bool allow_compute);
    const cambc::distance_field* altar_field(int a, bool allow_compute);
    // Tiles from which altar `a` accepts a tribute (submit range, walkable or gate tiles).
    const std::vector<vec2>& altar_tiles(int a) const { return altar_tiles_[static_cast<std::size_t>(a)]; }

    // A field from `origin` over static terrain (camp tiles passable, as a cheap estimate of
    // travel to every group at once); recomputed on demand. Returns nullptr when unaffordable.
    const cambc::distance_field* from_field(vec2 origin, int max_distance);
    vec2 from_origin() const { return from_origin_; }
    int from_round() const { return from_round_; }

    // Fuel a full-map field is expected to cost.
    std::uint64_t field_cost() const;
    // At most `field_limit` field computations per turn (fields cost ~3 fuel per tile).
    bool may_compute();
    int field_limit{1};
    void note_field_cost(std::uint64_t spent);

    // Precompute spawn/altar fields and spawn distances while the turn has fuel to spare.
    void warm(std::uint64_t keep);

private:
    struct TileRef { int index; std::int16_t group; std::int16_t member; };
    std::vector<TileRef> camp_index_;   // every camp spawn tile (group order)
    const TileRef* find_tile(vec2 t) const;
    std::vector<std::unique_ptr<cambc::distance_field>> group_fields_;
    std::unique_ptr<cambc::distance_field> spawn_field_;
    std::vector<std::unique_ptr<cambc::distance_field>> altar_fields_;
    std::vector<std::vector<vec2>> altar_tiles_;
    std::unique_ptr<cambc::distance_field> from_field_;
    vec2 from_origin_{-1, -1};
    int from_round_{-1000};
    std::uint64_t worst_cost_{};
    int warm_index_{};
    int fields_round_{-1};
    int fields_this_turn_{};
    bool compute(cambc::distance_field& f, const cambc::bitboard& walls, const cambc::bitboard& roots, std::uint16_t max_distance);
};

}  // namespace cw
