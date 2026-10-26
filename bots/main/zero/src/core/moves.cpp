#include "moves.h"

namespace zero {

namespace {

// A window around the champion (radius kR covers any dash range in the catalog) holding occupancy, so each tile
// test is an array read (scanning every unit per tile cost up to ~28K fuel on a crowded spawn).
constexpr int kR = 4, kW = 2 * kR + 1;

struct Window
{
    Tile origin;
    bool occ[kW][kW]{};
    bool ally[kW][kW]{};
    bool inside(Tile t) const { return t.x - origin.x >= -kR && t.x - origin.x <= kR && t.y - origin.y >= -kR && t.y - origin.y <= kR; }
    bool& at(Tile t) { return occ[t.y - origin.y + kR][t.x - origin.x + kR]; }
    bool& ally_at(Tile t) { return ally[t.y - origin.y + kR][t.x - origin.x + kR]; }
    bool at(Tile t) const { return occ[t.y - origin.y + kR][t.x - origin.x + kR]; }
    bool ally_at(Tile t) const { return ally[t.y - origin.y + kR][t.x - origin.x + kR]; }
};

Window make_window(const Obs& o)
{
    Window w;
    w.origin = o.self.at;
    auto mark = [&](Tile t) { if (w.inside(t)) w.at(t) = true; };
    for (const auto& u : o.allies)
    {
        mark(u.at);
        if (o.command_facts && w.inside(u.at)) w.ally_at(u.at) = true;
    }
    for (const auto& u : o.enemies) mark(u.at);
    for (const auto& c : o.camps) mark(c.at);
    for (Tile t : o.reserved_tiles) mark(t);
    return w;
}

// One traversal supplies full enumeration and the short-circuit existence query. The callback returns
// true only when the caller has its answer; no alternate movement predicate is maintained.
template <class Visit>
bool visit_dash(const Map& map, const Obs& o, int dash_range, const Window& w, Visit visit)
{
    if (!o.alive || dash_range <= 0 || !o.can_cast) return false;
    if (dash_range > kR) dash_range = kR;
    const Tile at = o.self.at;
    const std::uint8_t their_spawn = o.team == 0 ? tf_spawn_b : tf_spawn_a;
    // Exact-mode engine canaries: dashes may transit allies, altars and holes; enemies, camps and walls
    // still block the path. All occupants/altars/holes block landings. Historical mode keeps its old mask.
    bool seen[kW][kW]{};
    seen[kR][kR] = true;
    Tile frontier[kW * kW], next[kW * kW];
    int nf = 1;
    frontier[0] = at;
    for (int step = 0; step < dash_range && nf > 0; ++step)
    {
        int nn = 0;
        for (int i = 0; i < nf; ++i)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                {
                    if (!dx && !dy) continue;
                    const Tile t{ static_cast<std::int16_t>(frontier[i].x + dx), static_cast<std::int16_t>(frontier[i].y + dy) };
                    if (!w.inside(t) || !map.inside(t)) continue;
                    bool& s = seen[t.y - at.y + kR][t.x - at.x + kR];
                    if (s) continue;
                    s = true;
                    const auto f = map.at(t);
                    const bool transit_blocked = w.at(t) && !(o.command_facts && w.ally_at(t));
                    const auto terrain_block = o.command_facts ? (tf_wall | their_spawn) : (tf_wall | tf_altar | their_spawn);
                    if ((f & terrain_block) || transit_blocked) continue;
                    next[nn++] = t;
                    if (!(f & (tf_hole | tf_altar)) && !w.at(t) && visit(t)) return true;
                }
        for (int i = 0; i < nn; ++i) frontier[i] = next[i];
        nf = nn;
    }
    return false;
}

int active_range(int range) { return range < 0 ? 0 : (range > kR ? kR : range); }

}  // namespace

void legal_moves(const Map& map, Obs& o, int dash_range, bool with_dash)
{
    o.step_ok.clear();
    o.dash_ok.clear();
    o.active_dash_range = active_range(dash_range);
    o.dash_destinations_known = with_dash || !o.alive || !o.can_cast || o.active_dash_range == 0;
    if (!o.alive) return;
    const Tile at = o.self.at;
    const auto w = make_window(o);
    const std::uint8_t their_spawn = o.team == 0 ? tf_spawn_b : tf_spawn_a;
    auto standable = [&](Tile t) {
        if (!map.inside(t) || !w.inside(t)) return false;
        const auto f = map.at(t);
        return !(f & (tf_wall | tf_hole | tf_altar | their_spawn)) && !w.at(t);
    };
    if (o.can_move)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
            {
                if (!dx && !dy) continue;
                const Tile t{ static_cast<std::int16_t>(at.x + dx), static_cast<std::int16_t>(at.y + dy) };
                if (standable(t)) o.step_ok.push_back(t);
            }
    if (with_dash)
        visit_dash(map, o, dash_range, w, [&](Tile t) { o.dash_ok.push_back(t); return false; });
}

bool has_dash_destination(const Map& map, const Obs& o, int dash_range)
{
    if (!o.alive || !o.can_cast || dash_range <= 0) return false;
    const auto w = make_window(o);
    return visit_dash(map, o, dash_range, w, [](Tile) { return true; });
}

void generate_dash_destinations(const Map& map, const Obs& o, int dash_range, std::vector<Tile>& out)
{
    out.clear();
    if (!o.alive || !o.can_cast || dash_range <= 0) return;
    const auto w = make_window(o);
    visit_dash(map, o, dash_range, w, [&](Tile t) { out.push_back(t); return false; });
}

void generate_dash_destinations(const Map& map, Obs& o, int dash_range)
{
    generate_dash_destinations(map, static_cast<const Obs&>(o), dash_range, o.dash_ok);
    o.active_dash_range = active_range(dash_range);
    o.dash_destinations_known = true;
}

}  // namespace zero

#include <bit>

namespace zero {
namespace {

// The original 9x9 Window occupies 81 bits. All shifts use source-border
// masks; the transit intersection also discards every bit above the window.
struct DashBits
{
    std::uint64_t lo{}, hi{};
    constexpr DashBits operator&(DashBits b) const { return {lo & b.lo, hi & b.hi}; }
    constexpr DashBits operator|(DashBits b) const { return {lo | b.lo, hi | b.hi}; }
    void set(int index)
    {
        if (index < 64) lo |= std::uint64_t{1} << index;
        else hi |= std::uint64_t{1} << (index - 64);
    }
    template <int n> constexpr DashBits left() const { return {lo << n, (hi << n) | (lo >> (64 - n))}; }
    template <int n> constexpr DashBits right() const { return {(lo >> n) | (hi << (64 - n)), hi >> n}; }
};

constexpr DashBits dash_source_mask(int excluded_x, int excluded_y)
{
    DashBits bits;
    for (int y = 0; y < kW; ++y) for (int x = 0; x < kW; ++x)
        if (x != excluded_x && y != excluded_y)
        {
            const int index = y * kW + x;
            if (index < 64) bits.lo |= std::uint64_t{1} << index;
            else bits.hi |= std::uint64_t{1} << (index - 64);
        }
    return bits;
}

DashBits dash_neighbours(DashBits source)
{
    constexpr auto west_mask = dash_source_mask(0, -1);
    constexpr auto east_mask = dash_source_mask(kW - 1, -1);
    constexpr auto north_mask = dash_source_mask(-1, 0);
    constexpr auto south_mask = dash_source_mask(-1, kW - 1);
    const auto west = source & west_mask;
    const auto east = source & east_mask;
    const auto north = source & north_mask;
    const auto south = source & south_mask;
    return west.right<1>() | east.left<1>() | north.right<kW>() | south.left<kW>() |
        (north & west_mask).right<kW + 1>() |
        (north & east_mask).right<kW - 1>() |
        (south & west_mask).left<kW - 1>() |
        (south & east_mask).left<kW + 1>();
}

}  // namespace

void generate_dash_destinations_canonical(const Map& map, const Obs& o, int dash_range, std::vector<Tile>& out)
{
    out.clear();
    if (!o.alive || dash_range <= 0 || !o.can_cast) return;
    dash_range = active_range(dash_range);
    const auto w = make_window(o);
    const Tile at = o.self.at;
    const std::uint8_t their_spawn = o.team == 0 ? tf_spawn_b : tf_spawn_a;
    const auto terrain_block = o.command_facts ? (tf_wall | their_spawn) : (tf_wall | tf_altar | their_spawn);
    DashBits transit, landing;
    // No path of <=range steps can leave this square. The relative indices are
    // still those of the original full Window, including at map-edge sources.
    for (int dy = -dash_range; dy <= dash_range; ++dy)
        for (int dx = -dash_range; dx <= dash_range; ++dx)
        {
            const Tile tile{static_cast<std::int16_t>(at.x + dx), static_cast<std::int16_t>(at.y + dy)};
            if (!w.inside(tile) || !map.inside(tile)) continue;
            const auto flags = map.at(tile);
            const bool occupied = w.at(tile);
            // Preserve the original overlap semantics: an exact-mode ally bit
            // permits transit even when another represented blocker coincides.
            const bool blocked = occupied && !(o.command_facts && w.ally_at(tile));
            if ((flags & terrain_block) || blocked) continue;
            const int index = (dy + kR) * kW + dx + kR;
            transit.set(index);
            if (!(flags & (tf_hole | tf_altar)) && !occupied) landing.set(index);
        }
    constexpr int source_index = kR * kW + kR;
    DashBits reached;
    reached.set(source_index);  // The original BFS always seeds self, even outside the map.
    for (int step = 0; step < dash_range; ++step)
        reached = reached | (dash_neighbours(reached) & transit);
    landing = landing & reached;
    landing.lo &= ~(std::uint64_t{1} << source_index);
    out.reserve(static_cast<std::size_t>(std::popcount(landing.lo) + std::popcount(landing.hi)));
    auto append = [&](int index) {
        out.push_back({static_cast<std::int16_t>(at.x + index % kW - kR),
                       static_cast<std::int16_t>(at.y + index / kW - kR)});
    };
    // Increasing bit indices are increasing signed (y,x) throughout this window.
    while (landing.lo) { append(std::countr_zero(landing.lo)); landing.lo &= landing.lo - 1; }
    while (landing.hi) { append(64 + std::countr_zero(landing.hi)); landing.hi &= landing.hi - 1; }
}

}  // namespace zero
