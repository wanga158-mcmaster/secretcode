#include "world.h"

#include <algorithm>
#include <limits>

#include "config.h"
#include "model.h"

namespace cw {

World& World::get()
{
    static World w;
    return w;
}

const World::TileRef* World::find_tile(vec2 t) const
{
    if (!in_bounds(t) || !camp_all.is_set(t)) return nullptr;
    const int key = t.y * width + t.x;
    for (const auto& r : camp_index_)
        if (r.index == key) return &r;
    return nullptr;
}

int World::group_at(vec2 t) const
{
    const auto* r = find_tile(t);
    return r ? r->group : -1;
}

int World::member_at(vec2 t) const
{
    const auto* r = find_tile(t);
    return r ? r->member : -1;
}

bool World::walkable_static(vec2 t) const
{
    if (!in_bounds(t)) return false;
    return !impassable.is_set(t) && group_at(t) < 0;
}

void World::init()
{
    if constexpr (cfg::bounded_turns) { init_step(); return; }
    if (ready) return;
    width = cambc::map::width();
    height = cambc::map::height();

    ally_spawn = cambc::map::spawn_region(cambc::team::ally);
    enemy_spawn = cambc::map::spawn_region(cambc::team::enemy);
    impassable = cambc::map::walls() | cambc::map::holes() | cambc::map::altars() | enemy_spawn;

    spawn_tiles = ally_spawn.positions();
    const auto centre_of = [](const std::vector<vec2>& ts) {
        if (ts.empty()) return vec2{};
        long sx = 0, sy = 0;
        for (auto t : ts) { sx += t.x; sy += t.y; }
        const vec2 c{ static_cast<int>(sx / static_cast<long>(ts.size())), static_cast<int>(sy / static_cast<long>(ts.size())) };
        vec2 best = ts.front();
        for (auto t : ts) if (cheb(t, c) < cheb(best, c)) best = t;
        return best;
    };
    spawn_center = centre_of(spawn_tiles);
    enemy_center = centre_of(enemy_spawn.positions());
    altars = cambc::map::altars().positions();

    // Camp type stats, queried once per type.
    struct TypeStats { float hp{}; float str{}; int range{}; bool known{}; };
    std::array<TypeStats, 6> stats{};

    camp_all.clear();
    sdk_groups = cambc::map::camp_groups();
    if constexpr (cfg::debug_level == 4)
        std::printf("W layout r%u fuel%llu groups%u\n", cambc::round_number(), static_cast<unsigned long long>(cambc::fuel_remaining()), static_cast<unsigned>(sdk_groups.size()));
    groups.reserve(sdk_groups.size());
    std::size_t total = 0;
    for (const auto& sg : sdk_groups) total += sg.camps().size();
    camp_index_.reserve(total);
    all_members.reserve(total);
    for (const auto& sg : sdk_groups)
    {
        Group g;
        g.id = static_cast<int>(groups.size());
        g.first = static_cast<int>(all_members.size());
        g.gold = static_cast<int>(sg.gold_value());
        g.gems = static_cast<int>(sg.gem_value());
        g.cooldown = sg.max_respawn_timer();
        long sx = 0, sy = 0;
        for (const auto& c : sg.camps())
        {
            const auto type_index = static_cast<std::size_t>(c.kind());
            if (type_index < stats.size() && !stats[type_index].known)
            {
                const auto s = c.base_stats();
                stats[type_index] = { s.base_health, s.base_strength, s.base_attack_range, true };
            }
            Member m;
            m.tile = c.spawn_position();
            m.type = c.kind();
            if (type_index < stats.size())
            {
                m.hp = stats[type_index].hp;
                m.str = stats[type_index].str;
                m.range = stats[type_index].range;
            }
            all_members.push_back(m);
            sx += m.tile.x;
            sy += m.tile.y;
            g.total_hp += m.hp;
            g.total_str += m.str;
            g.max_range = std::max(g.max_range, m.str > 0.0f ? m.range : 0);
            if (m.str > 0.0f) g.harmful = true;
            if (m.type == cambc::camp_type::golem || m.type == cambc::camp_type::monolith) g.big = true;
            camp_all.set(m.tile);
            camp_index_.push_back({ m.tile.y * width + m.tile.x, static_cast<std::int16_t>(g.id),
                                    static_cast<std::int16_t>(static_cast<int>(all_members.size()) - 1 - g.first) });
        }
        const std::size_t count = all_members.size() - static_cast<std::size_t>(g.first);
        {
            cm::MonsterSet ms;
            for (std::size_t i = 0; i < count; ++i)
            {
                const auto& m = all_members[static_cast<std::size_t>(g.first) + i];
                ms.add({ m.hp, m.str });
            }
            g.smith_w = cm::smith_constants(ms).weighted;
            // Base kits deal ~65 a round on camps (foundation E2), usable health ~900, a heal trip
            // ~21 rounds, walking ~10 between camps.
            g.party_n = cm::party_size(g.smith_w, 65.0f, 900.0f, 21.0f, 10.0f, 5);
        }
        g.members = std::span<const Member>{ all_members.data() + g.first, count };
        g.camps = sg.camps();
        if (count > 0)
        {
            const vec2 c{ static_cast<int>(sx / static_cast<long>(count)), static_cast<int>(sy / static_cast<long>(count)) };
            g.anchor = g.members.front().tile;
            for (const auto& m : g.members)
                if (cheb(m.tile, c) < cheb(g.anchor, c)) g.anchor = m.tile;
        }
        groups.push_back(std::move(g));
    }
    if constexpr (cfg::debug_level == 4)
        std::printf("W groups r%u fuel%llu members%u\n", cambc::round_number(), static_cast<unsigned long long>(cambc::fuel_remaining()), static_cast<unsigned>(all_members.size()));
    gem_supply = 0.0f;
    for (const auto& g : groups)
        gem_supply += static_cast<float>(g.gems) / static_cast<float>(std::max(1, g.cooldown) + cambc::constants::capture_time + 1);
    if (stats[static_cast<std::size_t>(cambc::camp_type::wolf)].known) wolf_hp = stats[static_cast<std::size_t>(cambc::camp_type::wolf)].hp;
    if (stats[static_cast<std::size_t>(cambc::camp_type::elk)].known) elk_hp = stats[static_cast<std::size_t>(cambc::camp_type::elk)].hp;

    // Tribute tiles: free tiles in submit range of each altar; a gated altar (only camp tiles
    // in range) is reached by standing on its camp tiles while their group is down.
    const int reach = std::max(1, cambc::constants::submit_gems_range);
    altar_tiles_.resize(altars.size());
    for (std::size_t a = 0; a < altars.size(); ++a)
    {
        std::vector<vec2> free_tiles, camp_tiles;
        free_tiles.reserve(8);
        camp_tiles.reserve(8);
        for (int dy = -reach; dy <= reach; ++dy)
            for (int dx = -reach; dx <= reach; ++dx)
            {
                const vec2 t{ altars[a].x + dx, altars[a].y + dy };
                if (!in_bounds(t) || (dx == 0 && dy == 0)) continue;
                if (impassable.is_set(t)) continue;
                if (group_at(t) >= 0) camp_tiles.push_back(t);
                else free_tiles.push_back(t);
            }
        altar_tiles_[a] = free_tiles.empty() ? camp_tiles : free_tiles;
    }

    group_fields_.resize(groups.size());
    altar_fields_.resize(altars.size());
    ready = true;
}

void World::init_step()
{
    if (ready) return;
    // Only static metadata crosses this explicit owner-turn boundary. No observation or action
    // from an old round survives: Brain returns until ready, then starts from a fresh snapshot.
    struct TypeStats { float hp{}, str{}; int range{}; bool known{}; };
    struct Init
    {
        bool layout{};
        bool groups{};
        std::size_t next_group{}, next_altar{};
        std::array<TypeStats, 6> stats{};
    };
    static Init state;
    if (!state.layout)
    {
        width = cambc::map::width();
        height = cambc::map::height();
        ally_spawn = cambc::map::spawn_region(cambc::team::ally);
        enemy_spawn = cambc::map::spawn_region(cambc::team::enemy);
        impassable = cambc::map::walls() | cambc::map::holes() | cambc::map::altars() | enemy_spawn;
        spawn_tiles = ally_spawn.positions();
        const auto centre = [](const std::vector<vec2>& tiles) {
            if (tiles.empty()) return vec2{};
            long x = 0, y = 0;
            for (auto p : tiles) { x += p.x; y += p.y; }
            vec2 mean{ static_cast<int>(x / static_cast<long>(tiles.size())), static_cast<int>(y / static_cast<long>(tiles.size())) };
            vec2 best = tiles.front();
            for (auto p : tiles) if (cheb(p, mean) < cheb(best, mean)) best = p;
            return best;
        };
        spawn_center = centre(spawn_tiles);
        enemy_center = centre(enemy_spawn.positions());
        altars = cambc::map::altars().positions();
        sdk_groups = cambc::map::camp_groups();
        groups.reserve(sdk_groups.size());
        std::size_t total = 0;
        for (const auto& group : sdk_groups) total += group.camps().size();
        camp_index_.reserve(total);
        all_members.reserve(total); // member spans must never be invalidated by later batches
        camp_all.clear();
        state.layout = true;
    }
    for (; state.next_group < sdk_groups.size(); ++state.next_group)
    {
        if (cambc::fuel_remaining() < 55000) return;
        const auto& sg = sdk_groups[state.next_group];
        Group g;
        g.id = static_cast<int>(groups.size());
        g.first = static_cast<int>(all_members.size());
        g.gold = static_cast<int>(sg.gold_value());
        g.gems = static_cast<int>(sg.gem_value());
        g.cooldown = sg.max_respawn_timer();
        long x = 0, y = 0;
        cm::MonsterSet ms;
        for (const auto& camp : sg.camps())
        {
            const auto kind = static_cast<std::size_t>(camp.kind());
            if (kind < state.stats.size() && !state.stats[kind].known)
            {
                const auto stats = camp.base_stats();
                state.stats[kind] = { stats.base_health, stats.base_strength, stats.base_attack_range, true };
            }
            Member m;
            m.tile = camp.spawn_position();
            m.type = camp.kind();
            if (kind < state.stats.size())
            {
                m.hp = state.stats[kind].hp;
                m.str = state.stats[kind].str;
                m.range = state.stats[kind].range;
            }
            all_members.push_back(m);
            ms.add({ m.hp, m.str });
            x += m.tile.x;
            y += m.tile.y;
            g.total_hp += m.hp;
            g.total_str += m.str;
            g.max_range = std::max(g.max_range, m.str > 0.0f ? m.range : 0);
            g.harmful |= m.str > 0.0f;
            g.big |= m.type == cambc::camp_type::golem || m.type == cambc::camp_type::monolith;
            camp_all.set(m.tile);
            camp_index_.push_back({ m.tile.y * width + m.tile.x, static_cast<std::int16_t>(g.id),
                                    static_cast<std::int16_t>(static_cast<int>(all_members.size()) - 1 - g.first) });
        }
        const auto count = all_members.size() - static_cast<std::size_t>(g.first);
        g.members = std::span<const Member>{ all_members.data() + g.first, count };
        g.camps = sg.camps();
        g.smith_w = cm::smith_constants(ms).weighted;
        g.party_n = cm::party_size(g.smith_w, 65.0f, 900.0f, 21.0f, 10.0f, 5);
        if (count)
        {
            const vec2 mean{ static_cast<int>(x / static_cast<long>(count)), static_cast<int>(y / static_cast<long>(count)) };
            g.anchor = g.members.front().tile;
            for (const auto& m : g.members) if (cheb(m.tile, mean) < cheb(g.anchor, mean)) g.anchor = m.tile;
        }
        groups.push_back(std::move(g));
    }
    if (!state.groups)
    {
        gem_supply = 0.0f;
        for (const auto& g : groups)
            gem_supply += static_cast<float>(g.gems) / static_cast<float>(std::max(1, g.cooldown) + cambc::constants::capture_time + 1);
        if (state.stats[static_cast<std::size_t>(cambc::camp_type::wolf)].known) wolf_hp = state.stats[static_cast<std::size_t>(cambc::camp_type::wolf)].hp;
        if (state.stats[static_cast<std::size_t>(cambc::camp_type::elk)].known) elk_hp = state.stats[static_cast<std::size_t>(cambc::camp_type::elk)].hp;
        altar_tiles_.resize(altars.size());
        state.groups = true;
    }
    const int reach = std::max(1, cambc::constants::submit_gems_range);
    for (; state.next_altar < altars.size(); ++state.next_altar)
    {
        if (cambc::fuel_remaining() < 55000) return;
        std::vector<vec2> free_tiles, camp_tiles;
        free_tiles.reserve(8);
        camp_tiles.reserve(8);
        for (int dy = -reach; dy <= reach; ++dy)
            for (int dx = -reach; dx <= reach; ++dx)
            {
                const vec2 p{ altars[state.next_altar].x + dx, altars[state.next_altar].y + dy };
                if (!in_bounds(p) || (dx == 0 && dy == 0) || impassable.is_set(p)) continue;
                if (group_at(p) >= 0) camp_tiles.push_back(p);
                else free_tiles.push_back(p);
            }
        altar_tiles_[state.next_altar] = free_tiles.empty() ? camp_tiles : free_tiles;
    }
    if (cambc::fuel_remaining() < 55000) return;
    group_fields_.resize(groups.size());
    altar_fields_.resize(altars.size());
    ready = true;
}

std::uint64_t World::field_cost() const
{
    const std::uint64_t estimate = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 3u + 3000u;
    return std::max(estimate, worst_cost_);
}

void World::note_field_cost(std::uint64_t spent)
{
    worst_cost_ = std::max(worst_cost_, spent + spent / 8);
}

bool World::may_compute()
{
    const int round = static_cast<int>(cambc::round_number());
    if (round != fields_round_)
    {
        fields_round_ = round;
        fields_this_turn_ = 0;
    }
    return fields_this_turn_ < field_limit
        && cambc::fuel_remaining() >= field_cost() + static_cast<std::uint64_t>(cfg::fuel_reserve);
}

bool World::compute(cambc::distance_field& f, const cambc::bitboard& walls, const cambc::bitboard& roots, std::uint16_t max_distance)
{
    ++fields_this_turn_;
    const auto before = cambc::fuel_remaining();
    cambc::bitboard visited;
    f.compute(walls, roots, visited, max_distance);
    const auto after = cambc::fuel_remaining();
    if (before > after) note_field_cost(before - after);
    return true;
}

const cambc::distance_field* World::group_field(int g, bool allow_compute)
{
    if (g < 0 || g >= static_cast<int>(groups.size())) return nullptr;
    auto& slot = group_fields_[static_cast<std::size_t>(g)];
    if (slot) return slot.get();
    if (!allow_compute || !may_compute()) return nullptr;
    cambc::bitboard roots;
    for (const auto& m : groups[static_cast<std::size_t>(g)].members) roots.set(m.tile);
    cambc::bitboard walls = camp_all;
    walls ^= roots;          // other groups' camp tiles stay walls
    walls |= impassable;
    auto f = std::make_unique<cambc::distance_field>();
    compute(*f, walls, roots, cambc::distance_field::unreachable);
    slot = std::move(f);
    return slot.get();
}

const cambc::distance_field* World::spawn_field(bool allow_compute)
{
    if (spawn_field_) return spawn_field_.get();
    if (!allow_compute || !may_compute()) return nullptr;
    auto f = std::make_unique<cambc::distance_field>();
    compute(*f, impassable, ally_spawn, cambc::distance_field::unreachable);
    spawn_field_ = std::move(f);
    // Walking distances of every group from our spawn (camp tiles are passable in this field).
    for (auto& g : groups)
    {
        const auto d = spawn_field_->get(g.anchor);
        g.spawn_dist = d == cambc::distance_field::unreachable ? -1 : static_cast<int>(d);
    }
    return spawn_field_.get();
}

const cambc::distance_field* World::altar_field(int a, bool allow_compute)
{
    if (a < 0 || a >= static_cast<int>(altars.size())) return nullptr;
    auto& slot = altar_fields_[static_cast<std::size_t>(a)];
    if (slot) return slot.get();
    if (!allow_compute || !may_compute()) return nullptr;
    cambc::bitboard roots;
    for (auto t : altar_tiles_[static_cast<std::size_t>(a)]) roots.set(t);
    if (roots.is_empty()) return nullptr;
    cambc::bitboard walls = camp_all;
    walls &= ~roots;
    walls |= impassable;
    auto f = std::make_unique<cambc::distance_field>();
    compute(*f, walls, roots, cambc::distance_field::unreachable);
    slot = std::move(f);
    return slot.get();
}

const cambc::distance_field* World::from_field(vec2 origin, int max_distance)
{
    const int round = static_cast<int>(cambc::round_number());
    if (from_field_ && from_origin_ == origin && from_round_ >= round - 2) return from_field_.get();
    if (!may_compute()) return nullptr;
    if (!from_field_) from_field_ = std::make_unique<cambc::distance_field>();
    cambc::bitboard roots;
    roots.set(origin);
    cambc::bitboard walls = impassable;
    walls.set(origin, false);
    compute(*from_field_, walls, roots, static_cast<std::uint16_t>(std::clamp(max_distance, 1, 60000)));
    from_origin_ = origin;
    from_round_ = round;
    return from_field_.get();
}

void World::warm(std::uint64_t keep)
{
    const auto affordable = [&] { return may_compute() && cambc::fuel_remaining() > field_cost() + keep; };
    if (!spawn_field_ && affordable()) spawn_field(true);
    for (std::size_t a = 0; a < altars.size(); ++a)
        if (!altar_fields_[a] && affordable()) altar_field(static_cast<int>(a), true);
    // Altar distance of each group: the altar field at the anchor's neighbours.
    if (spawn_field_)
    {
        for (; warm_index_ < static_cast<int>(groups.size()); ++warm_index_)
        {
            if (cambc::fuel_remaining() < keep + 4000) return;
            auto& g = groups[static_cast<std::size_t>(warm_index_)];
            int best = -1;
            for (std::size_t a = 0; a < altars.size(); ++a)
            {
                const auto* f = altar_fields_[a].get();
                if (!f) continue;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        const vec2 t{ g.anchor.x + dx, g.anchor.y + dy };
                        if (!in_bounds(t)) continue;
                        const auto d = f->get(t);
                        if (d != cambc::distance_field::unreachable && (best < 0 || d < best)) best = d;
                    }
            }
            g.altar_dist = best;
        }
    }
}

}  // namespace cw
