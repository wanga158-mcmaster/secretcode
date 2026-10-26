#include "world.h"
#include <algorithm>
#include "config.h"
#include "model.h"

namespace mb {

World& World::get()
{
    static World world;
    return world;
}

namespace {

vec2 centre(const std::vector<vec2>& tiles)
{
    vec2 sum{};
    for (auto t : tiles) sum += t;
    if (tiles.empty()) return sum;
    const int n = static_cast<int>(tiles.size());
    return { sum.x / n, sum.y / n };
}

// Build a mask from tiles with one host call (packed row-major bits, LSB first).
// Verified against is_set() once; falls back to per-tile writes if the layout differs.
void import_tiles(cambc::bitboard& out, const std::vector<vec2>& tiles, int width)
{
    std::vector<std::uint8_t> bytes(cambc::bitboard::linear_buffer_size(), 0);
    for (auto t : tiles)
    {
        const std::size_t i = static_cast<std::size_t>(t.y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(t.x);
        bytes[i / 8] |= static_cast<std::uint8_t>(1u << (i % 8));
    }
    out.import_linear_buffer(bytes);
    if (!tiles.empty() && !out.is_set(tiles.front()))
    {
        out.clear();
        for (auto t : tiles) out.set(t);
    }
}

} // namespace

World::World()
{
    namespace map = cambc::map;
    width = map::width();
    height = map::height();
    // Conservative initial field estimate (observed 2.5-20 fuel/tile depending on search
    // depth); the costliest measured field replaces it once calibration has run.
    step_cost_ = 260;  // conservative until measured (observed ~150-250 per expansion step)
    blocked = map::walls() | map::holes() | map::altars() | map::spawn_region(cambc::team::enemy);
    altars = map::altars().positions();
    ally_spawn = map::spawn_region(cambc::team::ally).positions();
    enemy_spawn = map::spawn_region(cambc::team::enemy).positions();
    ally_spawn_center = centre(ally_spawn);
    enemy_spawn_center = centre(enemy_spawn);

    const std::size_t area = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    tile_group_.assign(area, -1);
    tile_member_.assign(area, -1);
    group_objects = map::camp_groups();
    groups.resize(group_objects.size());
    std::size_t total_camps = 0;
    for (const auto& g : group_objects) total_camps += g.camps().size();
    std::vector<vec2> all_tiles;
    all_tiles.reserve(total_camps);
    bool type_known[6]{};
    for (std::size_t gi = 0; gi < group_objects.size(); ++gi)
    {
        const auto& g = group_objects[gi];
        auto& info = groups[gi];
        info.gold = g.gold_value();
        info.gems = g.gem_value();
        info.respawn = g.max_respawn_timer();
        info.min_x = width;
        info.min_y = height;
        info.max_x = -1;
        info.max_y = -1;
        const auto camps = g.camps();
        info.tiles.reserve(camps.size());
        info.types.reserve(camps.size());
        for (std::size_t m = 0; m < camps.size(); ++m)
        {
            const auto& camp = camps[m];
            const auto type = static_cast<int>(camp.kind());
            if (type >= 0 && type < 6 && !type_known[type])
            {
                const auto stats = camp.base_stats();
                camp_stats[type] = { stats.base_health, stats.base_strength, stats.base_attack_range };
                type_known[type] = true;
            }
            const vec2 p = camp.spawn_position();
            info.tiles.push_back(p);
            info.types.push_back(static_cast<std::uint8_t>(type));
            info.min_x = std::min(info.min_x, p.x);
            info.min_y = std::min(info.min_y, p.y);
            info.max_x = std::max(info.max_x, p.x);
            info.max_y = std::max(info.max_y, p.y);
            const std::size_t idx = static_cast<std::size_t>(p.y) * width + p.x;
            tile_group_[idx] = static_cast<std::int16_t>(gi);
            tile_member_[idx] = static_cast<std::int16_t>(m);
            all_tiles.push_back(p);
        }
        info.center = centre(info.tiles);
    }
    // Smith order over camp types (fixed health/strength per type): descending strength/health.
    int order[6] = { 0, 1, 2, 3, 4, 5 };
    for (int i = 1; i < 6; ++i)
        for (int j = i; j > 0; --j)
        {
            const auto& a = camp_stats[order[j]];
            const auto& b = camp_stats[order[j - 1]];
            if (a.strength * std::max(b.health, 1.0f) > b.strength * std::max(a.health, 1.0f)) std::swap(order[j], order[j - 1]);
            else break;
        }
    for (std::size_t gi = 0; gi < groups.size(); ++gi)
    {
        auto& info = groups[gi];
        int count[6]{};
        for (const auto t : info.types) ++count[t];
        float cumulative = 0.0f;
        for (const int t : order)
        {
            const auto& s = camp_stats[t];
            const float c = static_cast<float>(count[t]);
            if (count[t] == 0) continue;
            // Members of one type die one after another: sum_j s * (C + j*h), j = 1..c.
            info.weighted_hp += s.strength * (c * cumulative + s.health * c * (c + 1.0f) * 0.5f);
            cumulative += c * s.health;
            info.total_strength += c * s.strength;
            info.max_range = std::max(info.max_range, s.range);
        }
        info.total_hp = cumulative;
        info.has_monolith = count[static_cast<int>(cambc::camp_type::monolith)] > 0;
        info.harmless = info.total_strength <= 0.0f;
        // Splash/area neighbours: members are cardinally connected, so a group of n has at
        // least one neighbour each; dense blobs saturate near 2.5 (exact counting is costly).
        const float n = static_cast<float>(info.tiles.size());
        info.neighbours = n <= 1.0f ? 0.0f : std::min(n - 1.0f, 2.5f);
    }
    import_tiles(camp_spawns, all_tiles, width);
    group_fields_.resize(groups.size());
    reachable_.assign(groups.size(), -1);
    // Gated altars: no free floor within submit range, only camp tiles (01boss-battle's altar is
    // ringed by holes, walls and a monolith). Those tiles open while their group is down.
    const int reach = static_cast<int>(cambc::constants::submit_gems_range);
    for (auto a : altars)
    {
        bool free = false;
        std::vector<vec2> camp_tiles;
        for (int dx = -reach; dx <= reach; ++dx)
            for (int dy = -reach; dy <= reach; ++dy)
            {
                const vec2 t{ a.x + dx, a.y + dy };
                if ((dx == 0 && dy == 0) || t.x < 0 || t.y < 0 || t.x >= width || t.y >= height || blocked.is_set(t)) continue;
                if (group_at(t) >= 0) camp_tiles.push_back(t);
                else free = true;
            }
        if (free || camp_tiles.empty() || gate_group >= 0) continue;
        gate_group = group_at(camp_tiles.front());
        for (auto t : camp_tiles)
            if (group_at(t) == gate_group) gate_tiles.push_back(t);
    }
}

int World::walk_reachable(int group)
{
    if (group < 0 || group >= static_cast<int>(groups.size())) return 0;
    auto& r = reachable_[static_cast<std::size_t>(group)];
    if (r >= 0) return r;
    if (!spawn_field_) return -1;
    r = 0;
    constexpr vec2 cardinal[4] = { { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 } };
    for (auto t : groups[static_cast<std::size_t>(group)].tiles)
    {
        for (auto d : cardinal)
        {
            const vec2 n = t + d;
            if (n.x < 0 || n.y < 0 || n.x >= width || n.y >= height) continue;
            if (spawn_field_->get(n) != cambc::distance_field::unreachable) { r = 1; return r; }
        }
    }
    return r;
}

int World::group_at(vec2 tile) const
{
    if (tile.x < 0 || tile.y < 0 || tile.x >= width || tile.y >= height) return -1;
    return tile_group_[static_cast<std::size_t>(tile.y) * width + tile.x];
}

int World::member_at(vec2 tile) const
{
    if (tile.x < 0 || tile.y < 0 || tile.x >= width || tile.y >= height) return -1;
    return tile_member_[static_cast<std::size_t>(tile.y) * width + tile.x];
}

int World::depth_estimate(vec2 center) const
{
    // Search depth ~ distance to the farthest map corner, with a detour allowance.
    int far = 0;
    const vec2 corners[4] = { { 0, 0 }, { width - 1, 0 }, { 0, height - 1 }, { width - 1, height - 1 } };
    for (auto c : corners) far = std::max(far, cheb(center, c));
    return far * 7 / 5 + 5;
}

std::uint64_t World::field_cost_at(vec2 center) const
{
    return step_cost_ * static_cast<std::uint64_t>(depth_estimate(center)) + 3000;
}

bool World::compute(std::unique_ptr<cambc::distance_field>& slot, const cambc::bitboard& roots, bool allow_compute, vec2 center,
                    std::uint64_t tail_margin)
{
    if (slot) return true;
    // Leave room for the rest of the turn after the search, whoever asked for it.
    const std::uint64_t margin = tail_margin > 0 ? tail_margin : cfg::field_tail_margin;
    if (!allow_compute || !fuel_ok(field_cost_at(center) + margin)) return false;
    const auto before = fuel();
    // Other camps' tiles stay walls: living camps block movement and capture targets are roots.
    cambc::bitboard walls = (blocked | camp_spawns) & ~roots;
    cambc::bitboard visited;
    auto field = std::make_unique<cambc::distance_field>();
    const auto steps = field->compute_with_expansion_count(walls, roots, visited);
    slot = std::move(field);
    const auto after = fuel();
    if (before > after && steps > 0)
    {
        if (measured_ && before - after > field_cost_at(center))
            MB_LOG("field cost %llu exceeded estimate %llu (steps %u)\n", static_cast<unsigned long long>(before - after),
                   static_cast<unsigned long long>(field_cost_at(center)), static_cast<unsigned>(steps));
        if (cfg::field_overhead > 0)
        {
            // Fuel ~ overhead + rate * steps: short searches (pockets) would inflate the rate.
            step_cost_ = model::updated_step_cost(step_cost_, measured_, before - after, steps, cfg::field_overhead);
            if (steps >= 10) measured_ = true;
        }
        else
        {
            // Pre-v22 rule: the costliest per-step rate seen (+15%), overhead included.
            const auto per_step = (before - after) * 115 / 100 / steps + 1;
            step_cost_ = measured_ ? std::max(step_cost_, per_step) : per_step;
            measured_ = true;
        }
    }
    return true;
}

const cambc::distance_field* World::group_field(int group, bool allow_compute, std::uint64_t tail_margin)
{
    if (group < 0 || group >= static_cast<int>(groups.size())) return nullptr;
    auto& slot = group_fields_[static_cast<std::size_t>(group)];
    if (!slot)
    {
        const vec2 center = groups[static_cast<std::size_t>(group)].center;
        const std::uint64_t margin = tail_margin > 0 ? tail_margin : cfg::field_tail_margin;
        if (!allow_compute || !fuel_ok(field_cost_at(center) + margin)) return nullptr;
        cambc::bitboard roots;
        import_tiles(roots, groups[static_cast<std::size_t>(group)].tiles, width);
        if (!compute(slot, roots, allow_compute, center, margin)) return nullptr;
    }
    return slot.get();
}

const cambc::distance_field* World::altar_field(bool allow_compute)
{
    if (!altar_field_ && !altars.empty())
    {
        if (!compute(altar_field_, cambc::map::altars(), allow_compute, { width / 2, height / 2 })) return nullptr;
    }
    return altar_field_.get();
}

const cambc::distance_field* World::gate_field(bool allow_compute)
{
    if (!gate_field_ && !gate_tiles.empty())
    {
        cambc::bitboard roots;
        import_tiles(roots, gate_tiles, width);
        if (!compute(gate_field_, roots, allow_compute, gate_tiles.front())) return nullptr;
    }
    return gate_field_.get();
}

const cambc::distance_field* World::spawn_field(bool allow_compute)
{
    if (!spawn_field_)
    {
        if (!compute(spawn_field_, cambc::map::spawn_region(cambc::team::ally), allow_compute, ally_spawn_center)) return nullptr;
    }
    return spawn_field_.get();
}

const cambc::distance_field* World::route_field(int group, vec2 from)
{
    if (group < 0 || group >= static_cast<int>(groups.size())) return nullptr;
    if (const auto* full = group_field(group, false)) return full;
    if (partial_fields_.size() != groups.size()) partial_fields_.resize(groups.size());
    auto& slot = partial_fields_[static_cast<std::size_t>(group)];
    if (slot && slot->get(from) != cambc::distance_field::unreachable) return slot.get();
    // Layers ~ path length: Chebyshev distance with a detour allowance.
    const auto& info = groups[static_cast<std::size_t>(group)];
    const auto layers = static_cast<std::uint64_t>(cheb(from, info.center) * 2 + 8);
    if (!fuel_ok(step_cost_ * layers + 3000 + 6000)) return nullptr;
    cambc::bitboard roots;
    import_tiles(roots, info.tiles, width);
    const cambc::bitboard walls = (blocked | camp_spawns) & ~roots;
    cambc::bitboard dest;
    dest.set(from);
    cambc::bitboard visited;
    auto field = std::make_unique<cambc::distance_field>();
    // Bounded by the layer estimate so an unreachable destination cannot cost a whole-map search.
    field->compute_to_with_expansion_count(walls, roots, dest, visited, static_cast<std::uint16_t>(std::min<std::uint64_t>(layers, 60000)));
    slot = std::move(field);
    return slot->get(from) != cambc::distance_field::unreachable ? slot.get() : nullptr;
}

int World::distance(const cambc::distance_field* field, vec2 from, vec2 fallback_target)
{
    if (field)
    {
        const auto d = field->get(from);
        if (d != cambc::distance_field::unreachable) return d;
        return 1 << 14;
    }
    // Uncomputed: a mild detour factor over the straight-line Chebyshev distance.
    return cheb(from, fallback_target) * 5 / 4;
}

} // namespace mb
