#include "world.h"
#include <algorithm>
#include <array>
#include <cstring>

namespace tt {

namespace {
std::array<MonsterStats, 8> g_monsters{};
std::array<bool, 8> g_monster_known{};
} // namespace

MonsterStats monster_stats(cambc::camp_type type)
{
    const auto i = static_cast<std::size_t>(type);
    if (i < g_monsters.size() && g_monster_known[i]) return g_monsters[i];
    return {};
}

World& World::get()
{
    // Built in place: members and groups point into the engine's group list, never moved.
    static World w;
    static const bool ready = (w.init(), true);
    (void)ready;
    return w;
}

void World::init()
{
    init_fuel[0] = fuel_left();
    W = cambc::map::width();
    H = cambc::map::height();
    area = W * H;
    // All -1, written eight bytes at a time (memset is a byte loop here, about 3 fuel a byte).
    {
        const std::size_t n_words = static_cast<std::size_t>((area + 3) / 4);
        auto* words = new std::uint64_t[n_words];
        for (std::size_t i = 0; i < n_words; ++i) words[i] = ~std::uint64_t{ 0 };
        tile_member.reset(reinterpret_cast<short*>(words));
    }
    my_spawn = cambc::map::spawn_region(cambc::team::ally);
    enemy_spawn = cambc::map::spawn_region(cambc::team::enemy);
    walls_static = cambc::map::walls() | cambc::map::holes() | cambc::map::altars() | enemy_spawn;
    camp_tiles_all.clear();

    my_spawn_tiles = my_spawn.positions();
    {
        long sx = 0, sy = 0;
        for (auto p : my_spawn_tiles) { sx += p.x; sy += p.y; }
        const long n = std::max<long>(1, static_cast<long>(my_spawn_tiles.size()));
        my_spawn_centre = { static_cast<int>(sx / n), static_cast<int>(sy / n) };
        const auto e = enemy_spawn.positions();
        long ex = 0, ey = 0;
        for (auto p : e) { ex += p.x; ey += p.y; }
        const long m = std::max<long>(1, static_cast<long>(e.size()));
        enemy_spawn_centre = { static_cast<int>(ex / m), static_cast<int>(ey / m) };
    }

    init_fuel[1] = fuel_left();
    loaded_ = cambc::map::camp_groups();
    auto& loaded = loaded_;
    init_fuel[2] = fuel_left();
    groups.reserve(loaded.size());
    {
        std::size_t total = 0;
        for (const auto& cg : loaded) total += cg.camps().size();
        members.reserve(total);
    }
    groups.resize(loaded.size());
    for (std::size_t gi = 0; gi < loaded.size(); ++gi) {
        const auto& cg = loaded[gi];
        Group& g = groups[gi];
        g.index = static_cast<int>(gi);
        g.members.reserve(cg.camps().size());
        g.gems = static_cast<int>(cg.gem_value());
        g.gold = static_cast<int>(cg.gold_value());
        g.cooldown = cg.max_respawn_timer();
        long cx = 0, cy = 0;
        const long q0 = fuel_left();
        for (const auto& c : cg.camps()) {
            Member& m = members.emplace_back();
            m.pos = c.spawn_position();
            m.tile = tile(m.pos);
            m.group = g.index;
            m.type = c.kind();
            m.camp = &c;
            const auto ti = static_cast<std::size_t>(m.type);
            if (ti < g_monsters.size() && !g_monster_known[ti]) {
                const auto st = c.base_stats();
                g_monsters[ti] = { st.base_health, st.base_strength, st.base_attack_range };
                g_monster_known[ti] = true;
            }
            m.stats = g_monsters[ti];
            g.members.push_back(static_cast<int>(members.size()) - 1);
            tile_member[static_cast<std::size_t>(m.tile)] = static_cast<short>(members.size() - 1);
            camp_tiles_all.set(m.pos);
            cx += m.pos.x;
            cy += m.pos.y;
            g.hp_total += m.stats.hp;
            g.str_total += m.stats.str;
            if (m.stats.str > 0) {
                g.harmless = false;
                g.max_range = std::max(g.max_range, m.stats.range);
            }
            g.spec.add(m.stats.hp, m.stats.str, m.stats.range);
        }
        const long q1 = fuel_left();
        const long n = std::max<long>(1, static_cast<long>(g.members.size()));
        g.centre = { static_cast<int>(cx / n), static_cast<int>(cy / n) };
        g.spec.gems = g.gems;
        g.spec.gold = g.gold;
        g.spec.cooldown = g.cooldown;
        g.spec.sort_members();
        const long q2 = fuel_left();
        g.handle = &cg;
        const long q3 = fuel_left();
        init_parts[0] += q0 - q1;
        init_parts[1] += q1 - q2;
        init_parts[2] += q2 - q3;
    }

    init_fuel[3] = fuel_left();
    altars = cambc::map::altars().positions();
    cambc::bitboard open = ~(walls_static | camp_tiles_all);
    cambc::bitboard walk = ~walls_static;
    cambc::bitboard near;
    for (auto a : altars) {
        near.clear();
        near.draw_chebyshev(a, static_cast<float>(cambc::constants::submit_gems_range));
        cambc::bitboard free_tiles = near;
        free_tiles &= open;
        if (free_tiles.is_empty()) { // gated altar: camp tiles only
            free_tiles = near;
            free_tiles &= walk;
        }
        altar_submit.push_back(std::move(free_tiles));
    }
    group_fields_.resize(groups.size());
    init_fuel[4] = fuel_left();
}

bool World::compute_field(Field& f, const cambc::bitboard& roots, const cambc::bitboard& walls)
{
    if (roots.is_empty()) return false;
    cambc::bitboard visited;
    f.df.compute(walls & ~roots, roots, visited);
    f.ready = true;
    ++fields_this_turn;
    return true;
}

const Field* World::group_field(int g, bool compute)
{
    if (g < 0 || g >= static_cast<int>(groups.size())) return nullptr;
    auto& slot = group_fields_[static_cast<std::size_t>(g)];
    if (slot && slot->ready) return slot.get();
    if (!compute || fields_this_turn >= field_budget || !fuel_ok(field_cost() + 25000)) return nullptr;
    if (!slot) slot = std::make_unique<Field>();
    cambc::bitboard roots;
    roots.clear();
    for (int m : groups[static_cast<std::size_t>(g)].members) roots.set(members[static_cast<std::size_t>(m)].pos);
    const cambc::bitboard walls = walls_static | (camp_tiles_all & ~roots);
    compute_field(*slot, roots, walls);
    return slot->ready ? slot.get() : nullptr;
}

const Field* World::altar_field(bool compute)
{
    if (altar_field_ && altar_field_->ready) return altar_field_.get();
    if (!compute || fields_this_turn >= field_budget || !fuel_ok(field_cost() + 25000)) return nullptr;
    if (!altar_field_) altar_field_ = std::make_unique<Field>();
    cambc::bitboard roots;
    roots.clear();
    for (const auto& s : altar_submit) roots |= s;
    compute_field(*altar_field_, roots, walls_static | camp_tiles_all);
    return altar_field_->ready ? altar_field_.get() : nullptr;
}

const Field* World::spawn_field(bool compute)
{
    if (spawn_field_ && spawn_field_->ready) return spawn_field_.get();
    if (!compute || fields_this_turn >= field_budget || !fuel_ok(field_cost() + 25000)) return nullptr;
    if (!spawn_field_) spawn_field_ = std::make_unique<Field>();
    compute_field(*spawn_field_, my_spawn, walls_static | camp_tiles_all);
    return spawn_field_->ready ? spawn_field_.get() : nullptr;
}

} // namespace tt
