#include "world.h"
#include <array>
#include <variant>
#include "config.h"

namespace wb {

void World::init() {
    w = cambc::map::width();
    h = cambc::map::height();
    my_spawn = cambc::map::spawn_region(team::ally);
    enemy_spawn = cambc::map::spawn_region(team::enemy);
    terrain = cambc::map::walls() | cambc::map::holes() | cambc::map::altars() | enemy_spawn;
    walkable = w * h - static_cast<int>(terrain.count());
    {
        const auto mine = my_spawn.positions();
        long sx = 0, sy = 0;
        for (auto p : mine) { sx += p.x; sy += p.y; }
        if (!mine.empty()) my_spawn_c = {static_cast<int>(sx / static_cast<long>(mine.size())), static_cast<int>(sy / static_cast<long>(mine.size()))};
        const auto theirs = enemy_spawn.positions();
        sx = sy = 0;
        for (auto p : theirs) { sx += p.x; sy += p.y; }
        if (!theirs.empty()) enemy_spawn_c = {static_cast<int>(sx / static_cast<long>(theirs.size())), static_cast<int>(sy / static_cast<long>(theirs.size()))};
    }
    group_at.assign(static_cast<std::size_t>(w * h), -1);

    sdk_groups = cambc::map::camp_groups();
    groups.resize(sdk_groups.size());
    tiles_.resize(sdk_groups.size());
    gfields_.resize(sdk_groups.size());
    // Camp base stats per type, queried once per type.
    std::array<bool, 8> have{};
    std::array<cambc::camp_stats, 8> stats{};
    for (std::size_t gi = 0; gi < sdk_groups.size(); ++gi) {
        const auto& s = sdk_groups[gi];
        Group& g = groups[gi];
        g.sdk = static_cast<int>(gi);
        g.gold = static_cast<int>(s.gold_value());
        g.gems = static_cast<int>(s.gem_value());
        g.respawn = s.max_respawn_timer();
        const auto camps = s.camps();
        g.members.resize(camps.size());
        long sx = 0, sy = 0;
        for (std::size_t mi = 0; mi < camps.size(); ++mi) {
            const auto& c = camps[mi];
            Monster& m = g.members[mi];
            m.tile = c.spawn_position();
            m.type = c.kind();
            m.sdk_member = static_cast<int>(mi);
            const auto t = static_cast<std::size_t>(m.type);
            if (t < stats.size()) {
                if (!have[t]) { stats[t] = c.base_stats(); have[t] = true; }
                m.hp = stats[t].base_health;
                m.str = stats[t].base_strength;
                m.range = m.str > 0 ? stats[t].base_attack_range : 0;
            }
            m.cur = m.hp;
            g.hp += m.hp;
            g.str += m.str;
            g.range = std::max(g.range, m.range);
            if (m.str > 0) g.harmless = false;
            if (m.type == cambc::camp_type::monolith) g.monolith = true;
            sx += m.tile.x;
            sy += m.tile.y;
            camp_tiles.set(m.tile);
            group_at[static_cast<std::size_t>(idx(m.tile))] = static_cast<std::int16_t>(gi);
        }
        // Insertion sort into Smith order (groups are small).
        for (std::size_t i = 1; i < g.members.size(); ++i)
            for (std::size_t j = i; j > 0 && model::smith_before({g.members[j].hp, g.members[j].str},
                                                                {g.members[j - 1].hp, g.members[j - 1].str}); --j)
                std::swap(g.members[j], g.members[j - 1]);
        if (!g.members.empty()) {
            const auto n = static_cast<long>(g.members.size());
            const vec2 c{static_cast<int>(sx / n), static_cast<int>(sy / n)};
            g.center = g.members.front().tile;
            for (const auto& m : g.members)
                if (cheb(m.tile, c) < cheb(g.center, c)) g.center = m.tile;
        }
        g.seen_hp = g.hp;
        g.seen_str = g.str;
        // Pack job: a base champion (solo_dps) would take more than solo_spare damage clearing it.
        if (!g.harmless) {
            model::Monster buf[32];
            int n = 0;
            for (const auto& m : g.members)
                if (n < 32) buf[n++] = {m.hp, m.str};
            const auto f = model::forecast_sorted(buf, n, cfg::solo_dps);
            g.pack_job = f.damage * cfg::camp_safety > cfg::solo_spare;
        }
    }

    altars = cambc::map::altars().positions();
    for (auto a : altars) altar_ring.draw_chebyshev(a, static_cast<float>(cambc::constants::submit_gems_range));
    altar_ring &= ~(cambc::map::walls() | cambc::map::holes() | cambc::map::altars() | enemy_spawn);
    ready_ = true;
}

const bitboard& World::tiles(int g) {
    auto& slot = tiles_[static_cast<std::size_t>(g)];
    if (!slot) {
        slot = std::make_unique<bitboard>();
        for (const auto& m : groups[static_cast<std::size_t>(g)].members) slot->set(m.tile);
    }
    return *slot;
}

std::unique_ptr<cambc::distance_field> World::compute_field(const bitboard& walls, const bitboard& roots) {
    if (fuel() < field_cost() + cfg::fuel_reserve) return nullptr;
    auto f = std::make_unique<cambc::distance_field>();
    bitboard visited;
    f->compute(walls, roots, visited);
    return f;
}

const cambc::distance_field* World::group_field(int g, bool compute) {
    if (g < 0 || g >= static_cast<int>(groups.size())) return nullptr;
    auto& slot = gfields_[static_cast<std::size_t>(g)];
    if (!slot && compute && fuel() >= field_cost() + cfg::fuel_reserve) {
        const auto& t = tiles(g);
        slot = compute_field(terrain | (camp_tiles & ~t), t);
    }
    return slot.get();
}

const cambc::distance_field* World::altar_field(bool compute) {
    if (!altar_field_ && compute && !altar_ring.is_empty())
        altar_field_ = compute_field(terrain | (camp_tiles & ~altar_ring), altar_ring);
    return altar_field_.get();
}

const cambc::distance_field* World::home_field(bool compute) {
    if (!home_field_ && compute) home_field_ = compute_field(terrain | camp_tiles, my_spawn);
    return home_field_.get();
}

namespace {
// Field value at p; a tile that is a wall in this field (we stand on a camp tile) reads its
// best neighbour plus one.
int read_field(const cambc::distance_field& f, vec2 p) {
    const auto v = f.get(p);
    if (v != cambc::distance_field::unreachable) return v;
    int best = 1 << 20;
    for (auto d : kDirs) {
        const vec2 q{p.x + d.x, p.y + d.y};
        if (!in_bounds(q)) continue;
        const auto u = f.get(q);
        if (u != cambc::distance_field::unreachable) best = std::min(best, static_cast<int>(u) + 1);
    }
    return best;
}
} // namespace

float World::remaining_hp(int g, int round) const {
    const auto& grp = groups[static_cast<std::size_t>(g)];
    if (grp.state == GState::respawning && grp.ready_round <= round) return grp.hp;
    if (grp.state == GState::reward || grp.state == GState::respawning) return 0;
    float hp = 0;
    for (const auto& m : grp.members)
        if (!m.dead) hp += m.cur;
    return hp;
}

int World::believed_monsters(int g, int round, model::Monster* out) const {
    const auto& grp = groups[static_cast<std::size_t>(g)];
    const bool fresh = (grp.state == GState::respawning && grp.ready_round <= round) || grp.state == GState::unknown;
    int n = 0;
    for (const auto& m : grp.members) {   // members are stored in Smith order
        if (n >= 32) break;
        if (fresh) out[n++] = {m.hp, m.str};
        else if (!m.dead) out[n++] = {m.cur, m.str};
    }
    return n;
}

int World::dist_to_group(vec2 p, int g) {
    if (const auto* f = group_field(g, false)) return read_field(*f, p);
    return static_cast<int>(static_cast<float>(cheb(p, groups[static_cast<std::size_t>(g)].center)) * cfg::detour);
}

int World::dist_to_altar(vec2 p) {
    if (const auto* f = altar_field(false)) return read_field(*f, p);
    int best = 1 << 20;
    for (auto a : altars) best = std::min(best, cheb(p, a));
    return static_cast<int>(static_cast<float>(best) * cfg::detour);
}

int World::dist_home(vec2 p) {
    if (const auto* f = home_field(false)) return read_field(*f, p);
    return static_cast<int>(static_cast<float>(cheb(p, my_spawn_c)) * cfg::detour);
}

void World::observe(int round) {
    const auto vis = cambc::vision::visible_tiles();
    const auto living = cambc::vision::camp_tiles();
    const auto seen_tiles = (vis & camp_tiles).positions();
    if (seen_tiles.empty()) return;
    int seen[64];
    int n_seen = 0;
    for (auto t : seen_tiles) {
        const int g = group_of(t);
        if (g < 0) continue;
        bool dup = false;
        for (int i = 0; i < n_seen; ++i) dup |= seen[i] == g;
        if (!dup && n_seen < 64) seen[n_seen++] = g;
    }
    for (int k = 0; k < n_seen; ++k) {
        const int gi = seen[k];
        auto& g = groups[static_cast<std::size_t>(gi)];
        g.seen_round = round;
        float hp = 0, str = 0;
        int alive = 0;
        for (auto& m : g.members) {
            if (!living.is_set(m.tile)) {
                if (vis.is_set(m.tile)) m.dead = true;
                continue;
            }
            ++alive;
            str += m.str;
            m.dead = false;
            if (vis.is_set(m.tile)) {
                try { m.cur = camp_of(gi, m).health(); } catch (const cambc::error&) { m.cur = m.hp; }
            } else if (g.state != GState::alive) m.cur = m.hp;  // hidden member: assume full health
            hp += m.cur;
        }
        if (alive > 0) {
            g.state = GState::alive;
            g.seen_hp = hp;
            g.seen_str = str;
            continue;
        }
        if (g.state == GState::respawning && g.ready_round > round) continue;
        for (auto& m : g.members) { m.dead = true; m.cur = 0; }
        try {
            const auto st = sdk_groups[static_cast<std::size_t>(g.sdk)].state();
            if (std::holds_alternative<cambc::map::camp_group_reward_available>(st)) {
                g.state = GState::reward;
            } else if (const auto* r = std::get_if<cambc::map::camp_group_respawning>(&st)) {
                g.state = GState::respawning;
                g.ready_round = round + std::max(1, static_cast<int>(r->timer));
            } else {
                g.state = GState::alive;  // alive per the engine but no member tile visible
                for (auto& m : g.members) { m.dead = false; m.cur = m.hp; }
            }
        } catch (const cambc::error&) {
        }
    }
}

} // namespace wb
