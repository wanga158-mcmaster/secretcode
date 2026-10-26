#pragma once
// Static map facts and path distances. Filled from the SDK (bot) or maps/geometry.json (native tools).
#include <cstdint>
#include <vector>

namespace zero {

struct Tile
{
    std::int16_t x{}, y{};
    bool operator==(const Tile&) const = default;
};

inline int cheb(Tile a, Tile b)
{
    const int dx = a.x > b.x ? a.x - b.x : b.x - a.x, dy = a.y > b.y ? a.y - b.y : b.y - a.y;
    return dx > dy ? dx : dy;
}

enum TileFlag : std::uint8_t { tf_wall = 1, tf_hole = 2, tf_bush = 4, tf_altar = 8, tf_spawn_a = 16, tf_spawn_b = 32 };

struct Member
{
    Tile at;
    std::uint8_t camp_type{};
};

struct Group
{
    std::vector<Member> members;
    Tile center;
    std::uint32_t gold{}, gems{};
    int respawn{};
    float total_hp{}, total_strength{};
    int max_range{};
    bool harmless{};
};

struct Map
{
    int w{}, h{};
    std::vector<std::uint8_t> flags;   // TileFlag bits per tile
    std::vector<std::int16_t> group_of;  // group index of a member tile, -1 otherwise
    std::vector<Group> groups;
    std::vector<Tile> altars;
    std::vector<Tile> spawn[2];         // team 0 (A), team 1 (B)
    Tile spawn_center[2];

    int idx(Tile t) const { return t.y * w + t.x; }
    bool inside(Tile t) const { return t.x >= 0 && t.y >= 0 && t.x < w && t.y < h; }
    std::uint8_t at(Tile t) const { return flags[static_cast<std::size_t>(idx(t))]; }
    // Walkable for `team` ignoring units: not wall, hole, altar, or the other team's spawn.
    bool walkable(Tile t, int team) const
    {
        if (!inside(t)) return false;
        const auto f = at(t);
        return !(f & (tf_wall | tf_hole | tf_altar)) && !(f & (team == 0 ? tf_spawn_b : tf_spawn_a));
    }
    void finish();  // derive centers, totals, spawn centers (call after filling)
};

// Path distances (8-neighbourhood, one step per round) to a goal set, over tiles walkable for a team; living
// camp members block except the goal group's own. The bot implements it on the engine's distance_field, the
// native tools by BFS; both follow the same blocking rule.
inline constexpr int kUnreachable = 1 << 14;

class Paths
{
public:
    virtual ~Paths() = default;
    // Distance from `from` to stand within `reach` (Chebyshev) of a target tile set; kUnreachable if none.
    virtual int to_group(int group, Tile from) = 0;
    virtual int to_altar(int altar, Tile from) = 0;
    virtual int to_spawn(Tile from) = 0;
};

// Native BFS implementation (tools; also a reference for parity tests).
class BfsPaths final : public Paths
{
public:
    BfsPaths(const Map& map, int team) : map_(map), team_(team) {}
    int to_group(int group, Tile from) override;
    int to_altar(int altar, Tile from) override;
    int to_spawn(Tile from) override;

private:
    const std::vector<std::int16_t>& field(int key, const std::vector<Tile>& goals, int through_group);
    const Map& map_;
    int team_;
    std::vector<std::vector<std::int16_t>> fields_;
    std::vector<int> keys_;
};

}  // namespace zero
