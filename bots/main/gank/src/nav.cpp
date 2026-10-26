#include "nav.h"
#include <algorithm>
#include <array>
#include <vector>
#include "world.h"

namespace mb {

namespace {

// Obstacle masks cached for one turn: other units only move on their own turns.
struct Obstacles
{
    int stamp{ -1 };
    int revealed_turn{ -1 };
    std::vector<vec2> revealed;  // tiles where a step failed this turn (hidden occupants)
    cambc::bitboard units;       // visible champions, camps and teleport reservations
    cambc::bitboard blocked;     // static blocked tiles plus units
    cambc::bitboard dash_block;  // dash path obstacles: walls, altars, enemy spawn, units
};

Obstacles& obstacles()
{
    static Obstacles o;
    static int turn_stamp = 0;
    (void)turn_stamp;
    return o;
}

int g_turn = 0;

void refresh_obstacles()
{
    auto& o = obstacles();
    if (o.stamp == g_turn) return;
    o.units = cambc::vision::champion_tiles();
    o.units |= cambc::vision::camp_tiles();
    o.units |= cambc::vision::teleport_tiles();
    if (o.revealed_turn != g_turn)
    {
        o.revealed.clear();
        o.revealed_turn = g_turn;
    }
    for (auto t : o.revealed) o.units.set(t);
    o.blocked = World::get().blocked | o.units;
    static const cambc::bitboard dash_static =
        cambc::map::walls() | cambc::map::altars() | cambc::map::spawn_region(cambc::team::enemy);
    o.dash_block = dash_static | o.units;
    o.stamp = g_turn;
}

} // namespace

void new_turn_obstacles() { ++g_turn; }

void invalidate_obstacles() { obstacles().stamp = -1; }

void remember_revealed(vec2 tile)
{
    auto& o = obstacles();
    if (o.revealed_turn != g_turn)
    {
        o.revealed.clear();
        o.revealed_turn = g_turn;
    }
    o.revealed.push_back(tile);
}

const cambc::bitboard& blocked_now()
{
    refresh_obstacles();
    return obstacles().blocked;
}

std::vector<vec2> step_options(const cambc::bitboard& blocked, vec2 from)
{
    std::vector<vec2> out;
    const auto& world = World::get();
    for (auto d : kDirs)
    {
        const vec2 n = from + d;
        if (n.x < 0 || n.y < 0 || n.x >= world.width || n.y >= world.height) continue;
        if (blocked.is_set(n)) continue;
        out.push_back(n);
    }
    return out;
}

namespace {

// Try candidates in order; a false return means a hidden occupant was revealed.
bool try_moves(const std::vector<vec2>& ordered)
{
    auto& me = cambc::self();
    for (auto tile : ordered)
    {
        if (!me.can_move()) return false;
        // The cached mask can miss units that became visible mid-turn: check the tile itself.
        if (cambc::vision::has_champion(tile) || cambc::vision::has_camp(tile)) continue;
        try
        {
            const bool ok = me.move_absolute(tile);
            invalidate_obstacles();  // moved (new vision) or revealed a hidden occupant
            if (ok) return true;
            remember_revealed(tile);
        }
        catch (const cambc::error& e)
        {
            // A destination we believed legal was rejected; try the next one.
            remember_revealed(tile);
            invalidate_obstacles();
            MB_LOG("move rejected %d,%d (champ %d camp %d tele %d ward %d inmask %d): %s\n", tile.x, tile.y,
                   cambc::vision::has_champion(tile), cambc::vision::has_camp(tile), cambc::vision::has_teleport(tile),
                   cambc::vision::has_ward(tile), obstacles().units.is_set(tile), e.what());
        }
    }
    return false;
}

} // namespace

bool step_along(const cambc::distance_field& field, bool allow_sideways, const std::function<int(vec2)>& penalty)
{
    auto& me = cambc::self();
    if (!me.can_move()) return false;
    const vec2 here = me.position();
    const int base = field.get(here);
    const auto& blocked = blocked_now();
    struct Option { vec2 tile; int cost; };
    std::vector<Option> options;
    for (auto n : step_options(blocked, here))
    {
        const int d = field.get(n);
        if (d == cambc::distance_field::unreachable) continue;
        const int cost = d * 16 + (penalty ? penalty(n) : 0);
        if (d < base || (allow_sideways && d == base)) options.push_back({ n, cost });
    }
    std::sort(options.begin(), options.end(), [](const Option& a, const Option& b) { return a.cost < b.cost; });
    std::vector<vec2> ordered;
    for (auto& o : options) ordered.push_back(o.tile);
    return try_moves(ordered);
}

bool step_best(const std::function<int(vec2)>& score, int stay_score)
{
    auto& me = cambc::self();
    if (!me.can_move()) return false;
    const vec2 here = me.position();
    const auto& blocked = blocked_now();
    struct Option { vec2 tile; int score; };
    std::vector<Option> options;
    for (auto n : step_options(blocked, here))
    {
        const int s = score(n);
        if (s > stay_score) options.push_back({ n, s });
    }
    std::sort(options.begin(), options.end(), [](const Option& a, const Option& b) { return a.score > b.score; });
    std::vector<vec2> ordered;
    for (auto& o : options) ordered.push_back(o.tile);
    return try_moves(ordered);
}

std::vector<vec2> dash_landings(vec2 from, int range)
{
    refresh_obstacles();
    auto& o = obstacles();
    const bool was = o.dash_block.is_set(from);
    if (was) o.dash_block.set(from, false);
    auto out = (o.dash_block.reachable_tiles(from, static_cast<std::uint16_t>(range)) & ~o.blocked).positions();
    if (was) o.dash_block.set(from, true);
    std::erase(out, from);
    return out;
}

bool step_greedy(vec2 target)
{
    auto& me = cambc::self();
    const vec2 here = me.position();
    const int d0 = cheb(here, target);
    return step_best([&](vec2 n) {
        const int d = cheb(n, target);
        // Prefer reducing Chebyshev distance, then Euclidean-ish tie break.
        const int dx = n.x - target.x, dy = n.y - target.y;
        return -(d * 1000 + dx * dx + dy * dy);
    }, -(d0 * 1000 + (here.x - target.x) * (here.x - target.x) + (here.y - target.y) * (here.y - target.y)));
}

} // namespace mb
