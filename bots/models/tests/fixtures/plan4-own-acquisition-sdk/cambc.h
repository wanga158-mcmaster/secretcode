#pragma once
// Narrow, fault-injecting SDK double. Actual SDK/ABI compatibility is checked
// separately by the selected production Release builds.
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <vector>
#include <variant>

namespace cambc {
struct vec2 { int x = 0, y = 0; friend bool operator==(vec2, vec2) = default; };
enum class team { ally, enemy };
enum class handle_champion_t : std::uint32_t {};
enum class handle_camp_group_t : std::uint32_t {};
class champion_state {
    unsigned bits_ = 0;
public:
    constexpr champion_state() = default;
    constexpr explicit champion_state(unsigned bits) : bits_(bits) {}
    constexpr bool contains(champion_state value) const { return (bits_ & value.bits_) == value.bits_; }
    static const champion_state recalling, submitting_gems, teleporting;
};
inline constexpr champion_state champion_state::recalling{1};
inline constexpr champion_state champion_state::submitting_gems{2};
inline constexpr champion_state champion_state::teleporting{4};
namespace constants { inline int respawn_time = 60; }
namespace mock {
enum class Query {
    self, own_alive, own_position, own_bag, own_state, roster, enemy_alive,
    enemy_position, enemy_bag, enemy_state, enemy_visible, vision, visible_tile,
    spawn_tile, group_value, group_timer, group_state, count
};
inline constexpr std::size_t query_count = static_cast<std::size_t>(Query::count);
inline std::array<int, query_count> calls{};
inline int round = 1, fuel = 100000, query_cost = 20, queries = 0, unauthorized = 0;
struct Hook {
    Query query = Query::count;
    int occurrence = -1, fuel_after = -1;
    bool advance_round = false;
};
inline Hook hook{};
inline int count(Query q) { return calls[static_cast<std::size_t>(q)]; }
inline void query(Query q) {
    ++queries;
    ++calls[static_cast<std::size_t>(q)];
    fuel -= query_cost;
    if (hook.query == q && count(q) == hook.occurrence) {
        if (hook.fuel_after >= 0) fuel = hook.fuel_after;
        if (hook.advance_round) ++round;
    }
    assert(fuel > 0);
}
inline void arm(Query q, bool drift = false, int fuel_after = -1, int after = 1) {
    hook = {q, count(q) + after, fuel_after, drift};
}
inline constexpr int board_width = 12, board_height = 12;
inline int tile(vec2 p) {
    assert(p.x >= 0 && p.y >= 0 && p.x < board_width && p.y < board_height);
    return p.y * board_width + p.x;
}
struct Actor {
    int id = -1;
    vec2 position{};
    unsigned bag = 0;
    bool alive = true, shown = false;
    champion_state state{};
};
inline Actor own{7, {3, 4}, 0, true, true, {}};
inline std::vector<Actor> enemies;
inline std::vector<int> roster;
inline std::array<bool, board_width * board_height> visible{};
struct Group {
    int id = -1, state = 1, max_timer = 30, timer = 30;
    unsigned gems = 24;
    std::vector<vec2> spawns;
};
inline std::vector<Group> groups;
inline Actor& actor(int id) {
    if (id == own.id) return own;
    auto it = std::find_if(enemies.begin(), enemies.end(), [id](const auto& a) { return a.id == id; });
    assert(it != enemies.end());
    return *it;
}
inline Group& group(int id) {
    auto it = std::find_if(groups.begin(), groups.end(), [id](const auto& g) { return g.id == id; });
    assert(it != groups.end());
    return *it;
}
inline void reset() {
    calls.fill(0);
    round = 1; fuel = 100000; query_cost = 20; queries = unauthorized = 0; hook = {};
    constants::respawn_time = 60;
    own = {7, {3, 4}, 0, true, true, {}};
    enemies = {{9, {8, 8}, 0, true, false, {}}, {3, {10, 9}, 0, true, false, {}}};
    roster = {9, 3};
    visible.fill(false);
    visible[static_cast<std::size_t>(tile(own.position))] = true;
    groups = {{42, 1, 30, 30, 24, {{3, 4}}}};
}
}
inline unsigned round_number() { return static_cast<unsigned>(mock::round); }
inline unsigned fuel_remaining() { return static_cast<unsigned>(mock::fuel); }
struct bitboard {
    std::array<bool, mock::board_width * mock::board_height> cells{};
    bool is_vision = false;
    void set(vec2 p, bool value = true) { cells[static_cast<std::size_t>(mock::tile(p))] = value; }
    bool is_set(vec2 p) const {
        mock::query(is_vision ? mock::Query::visible_tile : mock::Query::spawn_tile);
        return cells[static_cast<std::size_t>(mock::tile(p))];
    }
};
class champion {
    int id_;
    const mock::Actor& body(mock::Query own, mock::Query enemy) const {
        mock::query(id_ == mock::own.id ? own : enemy);
        const auto& a = mock::actor(id_);
        if (id_ != mock::own.id && !a.shown) { ++mock::unauthorized; assert(false); }
        return a;
    }
public:
    explicit champion(int id) : id_(id) {}
    operator handle_champion_t() const { return static_cast<handle_champion_t>(id_); }
    bool is_alive() const {
        mock::query(id_ == mock::own.id ? mock::Query::own_alive : mock::Query::enemy_alive);
        return mock::actor(id_).alive;
    }
    vec2 position() const { return body(mock::Query::own_position, mock::Query::enemy_position).position; }
    unsigned gem_count() const { return body(mock::Query::own_bag, mock::Query::enemy_bag).bag; }
    champion_state state() const { return body(mock::Query::own_state, mock::Query::enemy_state).state; }
    bool is_visible() const { mock::query(mock::Query::enemy_visible); return mock::actor(id_).shown; }
};
inline champion& self() { mock::query(mock::Query::self); static champion actor{7}; return actor; }
inline std::vector<champion> champions(team side) {
    mock::query(mock::Query::roster);
    assert(side == team::enemy);
    std::vector<champion> out;
    for (int id : mock::roster) out.emplace_back(id);
    return out;
}
namespace vision {
inline bitboard visible_tiles() {
    mock::query(mock::Query::vision);
    bitboard result;
    result.cells = mock::visible;
    result.is_vision = true;
    return result;
}
}
namespace map {
struct camp_group_alive {};
struct camp_group_reward_available {};
struct camp_group_respawning { std::int32_t timer; };
using camp_group_state = std::variant<camp_group_alive, camp_group_reward_available, camp_group_respawning>;
class camp_group {
    int id_;
public:
    explicit camp_group(int id) : id_(id) {}
    operator handle_camp_group_t() const { return static_cast<handle_camp_group_t>(id_); }
    unsigned gem_value() const { mock::query(mock::Query::group_value); return mock::group(id_).gems; }
    int max_respawn_timer() const { mock::query(mock::Query::group_timer); return mock::group(id_).max_timer; }
    camp_group_state state() const {
        mock::query(mock::Query::group_state);
        const auto& group = mock::group(id_);
        const bool personally_visible = std::any_of(group.spawns.begin(), group.spawns.end(), [](vec2 p) {
            return mock::visible[static_cast<std::size_t>(mock::tile(p))];
        });
        if (!personally_visible) { ++mock::unauthorized; assert(false); }
        if (group.state == 0) return camp_group_alive{};
        if (group.state == 1) return camp_group_reward_available{};
        return camp_group_respawning{group.timer};
    }
};
}
}
