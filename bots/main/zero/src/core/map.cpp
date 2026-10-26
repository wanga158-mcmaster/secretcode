#include "map.h"
#include "catalog.h"

namespace zero {

void Map::finish()
{
    group_of.assign(static_cast<std::size_t>(w * h), -1);
    const auto& cat = catalog();
    for (std::size_t g = 0; g < groups.size(); ++g)
    {
        auto& gr = groups[g];
        long sx = 0, sy = 0;
        gr.total_hp = gr.total_strength = 0.0f;
        gr.max_range = 0;
        for (const auto& m : gr.members)
        {
            group_of[static_cast<std::size_t>(idx(m.at))] = static_cast<std::int16_t>(g);
            sx += m.at.x;
            sy += m.at.y;
            const auto& d = cat.camp(m.camp_type);
            gr.total_hp += d.health;
            gr.total_strength += d.strength;
            if (d.strength > 0.0f && static_cast<int>(d.attack_range) > gr.max_range) gr.max_range = static_cast<int>(d.attack_range);
        }
        const auto n = static_cast<long>(gr.members.empty() ? 1 : gr.members.size());
        gr.center = { static_cast<std::int16_t>(sx / n), static_cast<std::int16_t>(sy / n) };
        gr.harmless = gr.total_strength <= 0.0f;
    }
    for (int t = 0; t < 2; ++t)
    {
        long sx = 0, sy = 0;
        for (auto s : spawn[t]) { sx += s.x; sy += s.y; }
        const auto n = static_cast<long>(spawn[t].empty() ? 1 : spawn[t].size());
        spawn_center[t] = { static_cast<std::int16_t>(sx / n), static_cast<std::int16_t>(sy / n) };
    }
}

const std::vector<std::int16_t>& BfsPaths::field(int key, const std::vector<Tile>& goals, int through_group)
{
    for (std::size_t i = 0; i < keys_.size(); ++i)
        if (keys_[i] == key) return fields_[i];
    std::vector<std::int16_t> d(static_cast<std::size_t>(map_.w * map_.h), static_cast<std::int16_t>(kUnreachable));
    std::vector<int> queue;
    queue.reserve(d.size());
    auto passable = [&](Tile t) {
        if (!map_.walkable(t, team_)) return false;
        const int g = map_.group_of[static_cast<std::size_t>(map_.idx(t))];
        return g < 0 || g == through_group || map_.groups[static_cast<std::size_t>(g)].harmless;
    };
    for (auto t : goals)
        if (map_.inside(t) && passable(t) && d[static_cast<std::size_t>(map_.idx(t))] != 0)
        {
            d[static_cast<std::size_t>(map_.idx(t))] = 0;
            queue.push_back(map_.idx(t));
        }
    for (std::size_t head = 0; head < queue.size(); ++head)
    {
        const int i = queue[head];
        const Tile t{ static_cast<std::int16_t>(i % map_.w), static_cast<std::int16_t>(i / map_.w) };
        const auto nd = static_cast<std::int16_t>(d[static_cast<std::size_t>(i)] + 1);
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
            {
                if (!dx && !dy) continue;
                const Tile u{ static_cast<std::int16_t>(t.x + dx), static_cast<std::int16_t>(t.y + dy) };
                if (!map_.inside(u) || !passable(u)) continue;
                auto& du = d[static_cast<std::size_t>(map_.idx(u))];
                if (du <= nd) continue;
                du = nd;
                queue.push_back(map_.idx(u));
            }
    }
    keys_.push_back(key);
    fields_.push_back(std::move(d));
    return fields_.back();
}

namespace {

// Callers query their actual position, or a movement/dash destination conditional on successfully landing
// there. Both use a vacant origin even when the static map still marks a camp member there; the conditional
// query does not assert engine acceptance of that move. Permit departure from that one tile only: the cached
// field and every other member remain blocked. Arbitrary occupied camp tiles are not valid origins under this
// contract. Terrain and enemy spawn never receive this exception.
int origin_distance(const Map& map, int team, const std::vector<std::int16_t>& field, Tile from, const std::vector<Tile>& goals)
{
    if (!map.walkable(from, team)) return kUnreachable;
    const int direct = field[static_cast<std::size_t>(map.idx(from))];
    if (direct < kUnreachable) return direct;
    const int group = map.group_of[static_cast<std::size_t>(map.idx(from))];
    if (group < 0 || map.groups[static_cast<std::size_t>(group)].harmless) return kUnreachable;
    for (const Tile goal : goals)
        if (goal == from) return 0;
    int best = kUnreachable;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
        {
            if (!dx && !dy) continue;
            const Tile to{ static_cast<std::int16_t>(from.x + dx), static_cast<std::int16_t>(from.y + dy) };
            if (!map.walkable(to, team)) continue;
            const int distance = field[static_cast<std::size_t>(map.idx(to))];
            if (distance < best) best = distance;
        }
    return best < kUnreachable - 1 ? best + 1 : kUnreachable;
}

}  // namespace

// Arrival set for a group: its member tiles and every walkable tile beside one (melee reach, capture).
int BfsPaths::to_group(int group, Tile from)
{
    const auto& gr = map_.groups[static_cast<std::size_t>(group)];
    std::vector<Tile> goals;
    for (const auto& m : gr.members)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) goals.push_back({ static_cast<std::int16_t>(m.at.x + dx), static_cast<std::int16_t>(m.at.y + dy) });
    const auto& f = field(group, goals, group);
    return origin_distance(map_, team_, f, from, goals);
}

int BfsPaths::to_altar(int altar, Tile from)
{
    const Tile a = map_.altars[static_cast<std::size_t>(altar)];
    std::vector<Tile> goals;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) goals.push_back({ static_cast<std::int16_t>(a.x + dx), static_cast<std::int16_t>(a.y + dy) });
    const auto& f = field(100000 + altar, goals, -1);
    return origin_distance(map_, team_, f, from, goals);
}

int BfsPaths::to_spawn(Tile from)
{
    const auto& f = field(200000, map_.spawn[team_], -1);
    return origin_distance(map_, team_, f, from, map_.spawn[team_]);
}

}  // namespace zero
