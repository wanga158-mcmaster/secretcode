// Engineering probe: production EnginePaths warm/progress/fuel, then all-tile native-BFS parity.
// No gameplay actions. Every build and checker slice starts on a fresh owner turn.
#include "../../zero/src/core/catalog.cpp"
#include "../../zero/src/core/map.cpp"
#include "../../zero/src/core/moves.cpp"
#include "../../zero/src/sdk.cpp"
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using namespace zero;

struct Probe
{
    Map map;
    int stage{}, team{}, next{}, calls{}, active{ -1 }, bfs_head{}, compare_at{}, mismatches{}, checked{}, fallback_checks{};
    std::uint64_t worst{};
    std::vector<int> keys, queue;
    std::vector<std::int16_t> distances;
    std::map<int, std::uint16_t> seen_caps;
    sdk::EnginePaths* paths{};
};

Probe& probe() { static Probe p; return p; }

bool passable(const Probe& p, Tile at)
{
    if (!p.map.walkable(at, p.team)) return false;
    const int group = p.map.group_of[static_cast<std::size_t>(p.map.idx(at))];
    return group < 0 || group == p.active || p.map.groups[static_cast<std::size_t>(group)].harmless;
}

void add_root(Probe& p, Tile at)
{
    if (!passable(p, at)) return;
    auto& d = p.distances[static_cast<std::size_t>(p.map.idx(at))];
    if (d == 0) return;
    d = 0;
    p.queue.push_back(p.map.idx(at));
}

// The reference matches core/map.cpp's BfsPaths arrival sets and passability. Work is sliced so the checker
// itself does not suspend a turn or contaminate the separately measured production-field build.
void start_reference(Probe& p)
{
    p.distances.assign(static_cast<std::size_t>(p.map.w * p.map.h), static_cast<std::int16_t>(kUnreachable));
    p.queue.clear();
    p.queue.reserve(p.distances.size());
    p.bfs_head = p.compare_at = 0;
    if (p.active == 200000)
        for (auto at : p.map.spawn[p.team]) add_root(p, at);
    else
    {
        std::vector<Tile> members;
        if (p.active >= 100000) members.push_back(p.map.altars[static_cast<std::size_t>(p.active - 100000)]);
        else for (const auto& m : p.map.groups[static_cast<std::size_t>(p.active)].members) members.push_back(m.at);
        for (auto at : members)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    add_root(p, { static_cast<std::int16_t>(at.x + dx), static_cast<std::int16_t>(at.y + dy) });
    }
}

int distance(Probe& p, Tile at)
{
    if (p.active == 200000) return p.paths->to_spawn(at);
    if (p.active >= 100000) return p.paths->to_altar(p.active - 100000, at);
    return p.paths->to_group(p.active, at);
}

// Use production warm rather than duplicating a build. A small allowance deliberately creates partial fields;
// a later fresh-turn allowance must improve them without starving the other queued keys.
bool warm(Probe& p, std::uint64_t reserve, const char* phase)
{
    const auto round = cambc::round_number();
    const auto before = cambc::fuel_remaining();
    p.paths->warm(reserve);
    const auto after = cambc::fuel_remaining();
    const bool same_round = round == cambc::round_number();
    const auto cost = before >= after ? before - after : before + 100000u - after;
    if (cost > p.worst) p.worst = cost;
    int complete = 0, changed = 0;
    for (const auto& [key, field] : sdk::cache().fields)
    {
        complete += field.complete;
        if (p.seen_caps[key] == field.cap) continue;
        ++changed;
        p.seen_caps[key] = field.cap;
        std::printf("pathcheck field key%d cap%u complete%d\n", key, static_cast<unsigned>(field.cap), field.complete);
        if (!field.complete)
        {
            p.active = key;
            for (const Tile at : { Tile{ 0, 0 }, Tile{ static_cast<std::int16_t>(p.map.w - 1), static_cast<std::int16_t>(p.map.h - 1) }, p.map.spawn_center[p.team] })
            {
                const auto raw = field.distances.get(sdk::V(at));
                const Tile center = key == 200000 ? p.map.spawn_center[p.team] : key >= 100000 ? p.map.altars[static_cast<std::size_t>(key - 100000)] : p.map.groups[static_cast<std::size_t>(key)].center;
                const int expected = raw == cambc::distance_field::unreachable ? cheb(at, center) : raw;
                if (distance(p, at) != expected) { ++p.mismatches; std::printf("error: pathcheck partial lookup key%d\n", key); }
                if (raw == cambc::distance_field::unreachable) ++p.fallback_checks;
            }
        }
    }
    std::printf("pathcheck warm phase%s w%d h%d team%d r%d before%llu after%llu cost%llu worst%llu same_round%d reserve%llu built%d complete%d pending%u changed%d fallback_checks%d\n",
                phase, p.map.w, p.map.h, p.team, static_cast<int>(round),
                static_cast<unsigned long long>(before), static_cast<unsigned long long>(after),
                static_cast<unsigned long long>(cost), static_cast<unsigned long long>(p.worst), same_round,
                static_cast<unsigned long long>(reserve), p.paths->fields_built(), complete,
                static_cast<unsigned>(sdk::cache().pending.size()), changed, p.fallback_checks);
    if (!same_round) std::printf("error: pathcheck warm suspended\n");
    if (after < reserve) std::printf("error: pathcheck warm crossed reserve\n");
    return changed != 0;
}

void play()
{
    auto& p = probe();
    switch (p.stage)
    {
    case 0:
        sdk::load_terrain(p.map);
        p.team = static_cast<int>(cambc::detail::absolute_team(cambc::team::ally));
        break;
    case 1: sdk::load_groups(p.map); break;
    case 2: p.map.finish(); break;
    case 3:
        p.paths = new sdk::EnginePaths(p.map, p.team);
        p.keys.push_back(200000);
        for (std::size_t i = 0; i < p.map.altars.size(); ++i) p.keys.push_back(100000 + static_cast<int>(i));
        for (std::size_t i = 0; i < p.map.groups.size(); ++i) p.keys.push_back(static_cast<int>(i));
        break;
    case 4:
        warm(p, 5000, "bootstrap");
        if (!p.paths->fields_built()) std::printf("error: pathcheck bootstrap built no field\n");
        break;
    case 5:
        for (const int key : p.keys) { p.active = key; (void)distance(p, p.map.spawn_center[p.team]); }
        break;
    case 6:
        warm(p, 80000, "partial");
        ++p.calls;
        if (sdk::cache().fields.size() + sdk::cache().dead.size() < p.keys.size() && p.calls < static_cast<int>(p.keys.size()) * 2 + 4) return;
        p.calls = 0;
        break;
    case 7:
    {
        const bool changed = warm(p, 5000, "complete");
        if (!sdk::cache().pending.empty() && changed && ++p.calls < static_cast<int>(p.keys.size()) * 2 + 4) return;
        std::printf("pathcheck warmed fields%d pending%u fallback_checks%d\n", p.paths->fields_built(), static_cast<unsigned>(sdk::cache().pending.size()), p.fallback_checks);
        break;
    }
    case 8:
        p.active = p.keys[static_cast<std::size_t>(p.next)];
        start_reference(p);
        break;
    case 9:
        while (p.bfs_head < static_cast<int>(p.queue.size()) && cambc::fuel_remaining() > 30000)
        {
            const int i = p.queue[static_cast<std::size_t>(p.bfs_head++)];
            const Tile at{ static_cast<std::int16_t>(i % p.map.w), static_cast<std::int16_t>(i / p.map.w) };
            const auto next = static_cast<std::int16_t>(p.distances[static_cast<std::size_t>(i)] + 1);
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                {
                    if (!dx && !dy) continue;
                    const Tile to{ static_cast<std::int16_t>(at.x + dx), static_cast<std::int16_t>(at.y + dy) };
                    if (!passable(p, to)) continue;
                    auto& d = p.distances[static_cast<std::size_t>(p.map.idx(to))];
                    if (d <= next) continue;
                    d = next;
                    p.queue.push_back(p.map.idx(to));
                }
        }
        if (p.bfs_head < static_cast<int>(p.queue.size())) return;
        break;
    case 10:
        while (p.compare_at < static_cast<int>(p.distances.size()) && cambc::fuel_remaining() > 30000)
        {
            const int i = p.compare_at++;
            const Tile at{ static_cast<std::int16_t>(i % p.map.w), static_cast<std::int16_t>(i / p.map.w) };
            const int actual = distance(p, at);
            const int exact = p.distances[static_cast<std::size_t>(i)];
            const auto cached = sdk::cache().fields.find(p.active);
            const bool unknown = cached != sdk::cache().fields.end() && !cached->second.complete && exact > cached->second.cap;
            const Tile center = p.active == 200000 ? p.map.spawn_center[p.team] : p.active >= 100000 ? p.map.altars[static_cast<std::size_t>(p.active - 100000)] : p.map.groups[static_cast<std::size_t>(p.active)].center;
            const int expected = unknown ? cheb(at, center) : exact;
            if (actual != expected)
            {
                if (p.mismatches < 8) std::printf("error: pathcheck mismatch key%d x%d y%d expected%d actual%d\n", p.active, at.x, at.y, expected, actual);
                ++p.mismatches;
            }
            ++p.checked;
        }
        if (p.compare_at < static_cast<int>(p.distances.size())) return;
        std::printf("pathcheck parity key%d tiles%d mismatches%d\n", p.active, p.compare_at, p.mismatches);
        ++p.next;
        if (p.next >= static_cast<int>(p.keys.size()))
        {
            std::printf("pathcheck done w%d h%d team%d fields%d checked%d mismatches%d worst%llu\n", p.map.w, p.map.h,
                        p.team, p.next, p.checked, p.mismatches, static_cast<unsigned long long>(p.worst));
            p.stage = 11;
        }
        else p.stage = 8;
        return;
    default: return;
    }
    ++p.stage;
}

}  // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    while (true)
    {
        try
        {
            if (cambc::is_draft())
            {
                const auto ours = cambc::draft::current_draft(cambc::team::ally);
                for (int i = 0; i < 12; ++i)
                {
                    const auto pick = static_cast<cambc::champion_draft>(i);
                    if (std::find(ours.begin(), ours.end(), pick) == ours.end()) { cambc::draft::pick(pick); break; }
                }
            }
            else play();
        }
        catch (const std::exception& e) { std::printf("error: pathcheck exception %s\n", e.what()); }
        catch (...) { std::printf("error: pathcheck unknown exception\n"); }
        cambc::end_turn();
    }
}
