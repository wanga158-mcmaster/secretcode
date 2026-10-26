// Deterministic actual-caller fixture. Compile only with -fno-access-control;
// the pinned SDK remains unchanged. No engine loop or native fuel claim.
#define PLAN5_BANK_EXIT 1
#if defined(__wasm__)
#undef __wasm__
#include <cambc.h>
#define __wasm__ 1
#else
#include <cambc.h>
#endif
#include "../runoff/src/policy.h"
#include "../runoff/src/native.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

extern "C" void* __cxa_allocate_exception(std::size_t size) { return std::malloc(size); }
extern "C" void __cxa_throw(void* error, void*, void(*)(void*)) {
    std::fprintf(stderr, "Unexpected Runoff SDK fixture exception: %s\n",
                 static_cast<std::exception*>(error)->what());
    std::abort();
}

namespace {
namespace r = p5::runoff;
namespace n = p5::native;
namespace e = p5::execution;
constexpr unsigned side = 64, words = 64;
unsigned checks = 0, host_round = 100, host_bag = 0, host_gold = 0, host_score = 0, host_enemy_score = 0;
unsigned round_reads = 0, flip_round_at = 0;
std::int64_t host_fuel = 1000000;
cambc::detail::champion_info host_actor{};
std::array<cambc::detail::camp_state, 4> host_camps{};
// SDK-host scenarios, not Wasm instruction fuel. Geometry and the current
// position-route cache remain deterministic immutable fixture inputs.
bool host_observed = false;
unsigned host_known = 7;
std::array<unsigned, 3> host_group_state{{1, 1, 1}};
std::array<std::uint64_t, words> host_vision{};
unsigned host_export_cost = 0, host_export_round = 0, host_exports = 0, host_field_cost = 0, host_fields = 0;
unsigned host_restrictions_cost = 0, host_restrictions = 0;
unsigned host_awake_camps = 0;
unsigned host_camp_design_queries = 0;
float host_camp_design_health = 0, host_camp_design_strength = 0;
// Optional SDK-host scheduling hook: these charges model completed quote units,
// not C++/Wasm instruction fuel. Ordinary cases leave the hook disconnected.
const int* host_preparation_cursor = nullptr;
int host_preparation_seen_cursor = 0;
unsigned host_preparation_quote_units = 0, host_preparation_quote_cost = 0, host_preparation_cross_after = 0;
unsigned host_move_fields = 0, host_reachable_queries = 0, host_reachable_round = 0;
unsigned host_point_queries = 0, host_point_charge_call = 0, host_point_cost = 0, host_cross_on_point = 0;
std::array<std::uint64_t, words> host_champion_tiles{}, host_teleport_tiles{};
bool host_move_succeeds = true;
int host_reachable_x = -1, host_reachable_y = -1;
std::array<std::uint64_t, words> host_reachable_mask{};
constexpr unsigned host_move_kind = 100; // Fixture action tag, distinct from SDK attack entity kinds.
unsigned host_cross_on_vision = 0;
struct HostAction { unsigned round = 0, kind = 0, target = 0; int x = -1, y = -1; };
std::array<HostAction, 16> host_actions{};
unsigned host_action_count = 0;
void fixture_check(bool value, int line) {
    ++checks;
    if (!value) { std::fprintf(stderr, "Runoff native check %u failed at line %d\n", checks, line); std::abort(); }
}
#define CHECK(...) ::fixture_check(bool((__VA_ARGS__)), __LINE__)
[[noreturn]] void unexpected_query(const char* name) {
    std::fprintf(stderr, "Unexpected Runoff fixture query: %s\n", name);
    CHECK(false); std::abort();
}
bool inside(int x, int y) { return x >= 0 && y >= 0 && x < int(side) && y < int(side); }
unsigned word(int x, int y) { return unsigned(y / 8) * 8 + unsigned(x / 8); }
std::uint64_t bit(int x, int y) { return std::uint64_t{1} << ((y % 8) * 8 + x % 8); }
bool host_seen(int x, int y) { return inside(x, y) && (host_vision[word(x, y)] & bit(x, y)); }
void host_charge(unsigned amount) { CHECK(host_fuel >= amount); host_fuel -= amount; }
void host_preparation_progress() {
    if (!host_preparation_cursor) return;
    const int cursor = *host_preparation_cursor;
    if (cursor < host_preparation_seen_cursor) host_preparation_seen_cursor = cursor;
    const unsigned completed = unsigned(cursor - host_preparation_seen_cursor);
    if (!completed) return;
    host_preparation_seen_cursor = cursor; host_preparation_quote_units += completed;
    host_charge(completed * host_preparation_quote_cost);
    if (host_preparation_cross_after && host_preparation_quote_units >= host_preparation_cross_after) {
        ++host_round; host_preparation_cross_after = 0;
    }
}
void record_action(unsigned kind, unsigned target, int x = -1, int y = -1) {
    CHECK(host_observed && host_action_count < host_actions.size());
    CHECK(host_actor.state & 1); CHECK(!(host_actor.state & ((1 << 1) | (1 << 2) | (1 << 5))));
    host_actions[host_action_count++] = {host_round, kind, target, x, y};
}
}

namespace cambc::detail { extern "C" {
std::int32_t sdk_get_settings(game_settings* out) {
    *out = {}; out->capture_time = 5; out->respawn_time = 50; out->recall_time = 5;
    out->submit_gem_duration_multiplier = .1; out->submit_gem_minimum_duration = 1;
    out->max_rounds = 2000; out->winning_gems = 1000;
    out->spawn_healing = 1; out->submit_gems_range = 1; out->altar_lock_hits = 5; return 0;
}
std::int32_t sdk_bc_metadata(metadata* out) { *out = {side, side, 8, 8, 8, words * 8, side * side * 2}; return 0; }
std::int32_t sdk_bc_get_last_error(char*, std::uint32_t) { return 0; }
std::int32_t sdk_shared_memory_size(std::uint32_t* out) { *out = 0; return 0; }
std::int32_t sdk_draft_memory_size(std::uint32_t* out) { *out = 0; return 0; }
std::int32_t sdk_my_champion() { return 0; }
std::int32_t sdk_my_team() { return 0; }
std::int32_t sdk_consumable_info(std::uint32_t, consumable_info* out) { *out = {}; return 0; }
std::int32_t sdk_item_info(std::uint32_t, item_info* out) { *out = {}; out->cost = 30; out->purchasable = 1; return 0; }
std::int32_t sdk_champion_design(std::int32_t, champion_design* out) { *out = {}; return 0; }
std::int32_t sdk_champion_ability_props(std::uint32_t, std::uint32_t, ability_props* out) { *out = {}; return 0; }
std::int32_t sdk_champion_ability_damage(std::uint32_t, std::uint32_t, float*) {
    unexpected_query("champion_ability_damage");
}
std::int32_t sdk_champion_cooldown(std::uint32_t, std::uint32_t, std::int32_t* out) { *out = 8; return 0; }
std::int32_t sdk_champion(std::uint32_t id, champion_info* out) { CHECK(id == 0); *out = host_actor; return 0; }
std::int32_t sdk_champion_identity(std::uint32_t id, champion_identity* out) { CHECK(id == 0); *out = {host_actor.team, host_actor.type}; return 0; }
std::int32_t sdk_champion_is_alive(std::uint32_t id, std::int32_t* out) { CHECK(id == 0); *out = host_actor.state & 1; return 0; }
std::int32_t sdk_champion_gems(std::uint32_t id, std::uint32_t* out) { CHECK(id == 0); *out = host_bag; return 0; }
std::int32_t sdk_champion_gold(std::uint32_t id, std::uint32_t* out) { CHECK(id == 0); *out = host_gold; return 0; }
std::int32_t sdk_champion_buff_data(std::uint32_t, buff_data*, std::uint32_t, std::uint32_t* count) { *count = 0; return 0; }
std::int32_t sdk_camp_buffs(std::uint32_t, camp_buff_data*, std::uint32_t, std::uint32_t* count) { *count = 0; return 0; }
std::int32_t sdk_champion_restrictions(std::uint32_t, std::uint8_t* out) {
    host_charge(host_restrictions_cost); ++host_restrictions; *out = 0; return 0;
}
std::int32_t sdk_champion_can_shop(std::uint32_t, std::int32_t, std::int32_t* out) { *out = 0; return 0; }
std::int32_t sdk_champion_items(std::uint32_t id, std::uint32_t* out) {
    CHECK(id == 0); std::fill_n(out, std::size_t(cambc::item_type::enum_type::count), 0); return 0;
}
std::int32_t sdk_camp(std::uint32_t id, camp_state* out) {
    CHECK(id >= 101 && id <= 104); const auto& value = host_camps[id - 101];
    if (host_observed && !host_seen(value.x, value.y)) return std::int32_t(cambc::error_code::api_not_in_vision);
    *out = value; return 0;
}
std::int32_t sdk_camp_state(std::uint32_t id, std::uint32_t* out) {
    CHECK(id >= 101 && id <= 104);
    *out = host_camps[id - 101].health > 0 ? ((host_awake_camps & (1u << (id - 101))) ? 1 : 2) : 0; return 0;
}
std::int32_t sdk_camp_design(std::int32_t, camp_design* out) {
    ++host_camp_design_queries; *out = {}; out->base_stats.base_attack_range = 3;
    out->base_stats.base_health = host_camp_design_health; out->base_stats.base_strength = host_camp_design_strength;
    return 0;
}
std::int32_t sdk_camp_group_state(std::uint32_t id, std::uint32_t* out) {
    CHECK(host_observed && id >= 1 && id <= 3 && (host_known & (1u << (id - 1))));
    *out = host_group_state[id - 1]; return 0;
}
std::int32_t sdk_camp_group_cooldown(std::uint32_t id, std::int32_t* out) {
    CHECK(host_observed && id >= 1 && id <= 3 && host_group_state[id - 1] == 3); *out = 50; return 0;
}
std::int32_t sdk_altar_locks(altar_lock_info*, std::uint32_t, std::uint32_t* count) { *count = 0; return 0; }
std::int32_t sdk_bc_round_number(std::uint32_t* out) {
    host_preparation_progress();
    if (++round_reads == flip_round_at && flip_round_at) ++host_round;
    *out = host_round; return 0;
}
std::int64_t sdk_fuel_remaining() { host_preparation_progress(); return host_fuel; }
std::int32_t sdk_bc_submitted_gems(std::uint32_t team, std::uint32_t* out) { CHECK(team <= 1); *out = team == 0 ? host_score : host_enemy_score; return 0; }
std::int32_t sdk_bc_map_symmetry(std::int32_t* out) { *out = 0; return 0; }
std::int32_t sdk_bc_map_mask(std::int32_t, std::uint64_t* out) { std::fill_n(out, words, 0); return 0; }
std::int32_t sdk_spawn_region(std::int32_t team, aabb* out) {
    *out = team == 0 ? aabb{8, 10, 9, 11} : aabb{60, 60, 61, 61}; return 0;
}
// General initialization/BFS are not emulated. The fixture supplies immutable
// geometry/current route caches; SDK observation still builds every turn packet.
std::int32_t sdk_map_camp_layout(std::uint32_t* out, std::uint32_t capacity, std::uint32_t* count) {
    const std::array<std::uint32_t, 31> layout{{
        1, 20, 30, 50, 2, 101, std::uint32_t(cambc::camp_type::wolf), 10, 11, 104, std::uint32_t(cambc::camp_type::wolf), 10, 12,
        2, 20, 20, 50, 1, 102, std::uint32_t(cambc::camp_type::wolf), 11, 11,
        3, 20, 10, 50, 1, 103, std::uint32_t(cambc::camp_type::wolf), 12, 11}};
    *count = unsigned(layout.size());
    if (out) std::copy_n(layout.data(), std::min<std::size_t>(capacity, layout.size()), out);
    return 0;
}
std::int32_t sdk_ally_occupancy(std::uint64_t* out) {
    for (unsigned i = 0; i < words; ++i) out[i] = host_champion_tiles[i] & host_vision[i];
    return 0;
}
std::int32_t sdk_enemy_occupancy(std::uint64_t* out) {
    CHECK(host_observed); std::fill_n(out, words, 0); return 0;
}
std::int32_t sdk_teleport_occupancy(std::uint64_t* out) {
    CHECK(host_observed);
    for (unsigned i = 0; i < words; ++i) out[i] = host_teleport_tiles[i] & host_vision[i];
    return 0;
}
std::int32_t sdk_has_champion(std::int32_t x, std::int32_t y, std::int32_t* out) {
    CHECK(host_observed && inside(x, y)); ++host_point_queries;
    if (host_point_queries == host_point_charge_call) host_charge(host_point_cost);
    if (host_cross_on_point && !--host_cross_on_point) ++host_round;
    if (!host_seen(x, y)) return std::int32_t(cambc::error_code::api_not_in_vision);
    *out = bool(host_champion_tiles[word(x, y)] & bit(x, y)); return 0;
}
std::int32_t sdk_has_camp(std::int32_t x, std::int32_t y, std::int32_t* out) {
    CHECK(host_observed && inside(x, y)); ++host_point_queries;
    if (host_point_queries == host_point_charge_call) host_charge(host_point_cost);
    if (host_cross_on_point && !--host_cross_on_point) ++host_round;
    if (!host_seen(x, y)) return std::int32_t(cambc::error_code::api_not_in_vision);
    *out = 0;
    for (const auto& camp : host_camps) if (camp.health > 0 && camp.x == x && camp.y == y) *out = 1;
    return 0;
}
std::int32_t sdk_champion_at(std::int32_t, std::int32_t, std::uint32_t*) { unexpected_query("champion_at"); }
std::int32_t sdk_vision(std::uint64_t* out) {
    CHECK(host_observed);
    if (host_export_round != host_round) { host_export_round = host_round; ++host_exports; host_charge(host_export_cost); }
    std::copy(host_vision.begin(), host_vision.end(), out);
    if (host_cross_on_vision && !--host_cross_on_vision) ++host_round;
    return 0;
}
std::int32_t sdk_camp_occupancy(std::uint64_t* out) {
    CHECK(host_observed); std::fill_n(out, words, 0);
    for (const auto& camp : host_camps) if (camp.health > 0 && host_seen(camp.x, camp.y))
        out[word(camp.x, camp.y)] |= bit(camp.x, camp.y);
    return 0;
}
std::int32_t sdk_camp_at(std::int32_t x, std::int32_t y, std::uint32_t* out) {
    CHECK(host_observed && host_seen(x, y));
    for (unsigned i = 0; i < host_camps.size(); ++i) if (host_camps[i].x == x && host_camps[i].y == y && host_camps[i].health > 0) {
        *out = 101 + i; return 0;
    }
    unexpected_query("camp_at empty");
}
std::int32_t sdk_map_camp_info(std::uint32_t id, camp_info* out) {
    CHECK(host_observed && id >= 101 && id <= 104);
    *out = {id == 104 ? 10 : int(id) - 91, id == 104 ? 12 : 11, int(cambc::camp_type::wolf)}; return 0;
}
std::int32_t sdk_map_camp_group_of(std::uint32_t id, std::uint32_t* out) {
    CHECK(host_observed && id >= 101 && id <= 104); *out = id == 104 ? 1 : id - 100; return 0;
}
std::int32_t sdk_attack(std::uint32_t kind, std::uint32_t target) {
    CHECK(kind == std::uint32_t(cambc::detail::entity_kind::camp) && target >= 101 && target <= 104);
    CHECK(host_actor.state & (1 << 4)); const auto& camp = host_camps[target - 101];
    CHECK(camp.health > 0 && host_seen(camp.x, camp.y));
    CHECK(std::max(std::abs(camp.x - host_actor.x), std::abs(camp.y - host_actor.y)) <= host_actor.attack_range);
    for (unsigned i = 0; i < host_action_count; ++i) CHECK(host_actions[i].round != host_round);
    record_action(kind, target, camp.x, camp.y);
    // Only dispatch is recorded. No damage, payout, or later engine success is inferred.
    return 0;
}
std::int32_t sdk_cast(std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::int32_t, std::int32_t*) { unexpected_query("cast"); }
std::int32_t sdk_submit_gems(std::int32_t, std::int32_t) { unexpected_query("submit_gems"); }
std::int32_t sdk_buy_item(std::uint32_t, std::uint32_t) { unexpected_query("buy_item"); }
std::int32_t sdk_recall() { unexpected_query("recall"); }
std::int32_t sdk_cancel_channel() { unexpected_query("cancel_channel"); }
std::int32_t sdk_move_to(std::int32_t x, std::int32_t y, std::int32_t* successful) {
    CHECK(host_actor.state & (1 << 3)); CHECK(inside(x, y));
    CHECK(host_reachable_round == host_round && host_reachable_x == host_actor.x && host_reachable_y == host_actor.y);
    CHECK((host_reachable_mask[word(x, y)] & bit(x, y)) && (x != host_actor.x || y != host_actor.y));
    CHECK(std::max(std::abs(x - host_actor.x), std::abs(y - host_actor.y)) == 1);
    for (unsigned i = 0; i < host_action_count; ++i) CHECK(host_actions[i].round != host_round);
    record_action(host_move_kind, unsigned(y) * side + unsigned(x), x, y);
    if (host_move_succeeds) { host_actor.x = x; host_actor.y = y; host_actor.state &= ~(1 << 3); }
    *successful = host_move_succeeds;
    return 0;
}
std::int32_t sdk_DistanceField_ComputeWithExpansionCount(const std::uint64_t* walls, const std::uint64_t* roots,
                                                       std::uint64_t* visited, std::uint16_t* field,
                                                       std::uint16_t limit, std::uint16_t* expanded) {
    CHECK(host_observed && walls && roots && visited && field && expanded);
    host_charge(host_field_cost); ++host_fields; *expanded = 0; std::fill_n(visited, words, 0);
    // Exact bounded eight-neighbour BFS over the supplied impassable mask.
    // Runtime adds the real enemy spawn to the otherwise empty fixture map;
    // those tiles stay unreachable. Host expansion layers and the declared
    // query charge are synthetic, not engine implementation/fuel evidence.
    std::fill_n(field, side * side, std::numeric_limits<std::uint16_t>::max());
    std::array<std::uint16_t, side * side> queue{}; unsigned head = 0, tail = 0;
    for (int y = 0; y < int(side); ++y) for (int x = 0; x < int(side); ++x) {
        if (!(roots[word(x, y)] & bit(x, y))) continue;
        CHECK(!(walls[word(x, y)] & bit(x, y)));
        field[y * side + x] = 0; visited[word(x, y)] |= bit(x, y);
        queue[tail++] = std::uint16_t(y * side + x);
    }
    CHECK(tail > 0);
    while (head < tail) {
        const unsigned tile = queue[head++]; const int x = int(tile % side), y = int(tile / side);
        const auto distance = field[tile]; *expanded = std::max(*expanded, distance);
        if (distance >= limit) continue;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            const int nx = x + dx, ny = y + dy;
            if ((!dx && !dy) || !inside(nx, ny) || (walls[word(nx, ny)] & bit(nx, ny))) continue;
            const unsigned next = unsigned(ny) * side + unsigned(nx);
            if (field[next] != std::numeric_limits<std::uint16_t>::max()) continue;
            CHECK(tail < queue.size()); field[next] = std::uint16_t(distance + 1);
            visited[word(nx, ny)] |= bit(nx, ny); queue[tail++] = std::uint16_t(next);
        }
    }
    CHECK(field[60 * side + 60] == std::numeric_limits<std::uint16_t>::max());
    CHECK(!(visited[word(60, 60)] & bit(60, 60)));
    return 0;
}
std::int32_t sdk_DistanceField_Get(const std::uint16_t* field, std::int32_t x, std::int32_t y, std::uint16_t* out) {
    CHECK(host_observed && inside(x, y)); *out = field[y * side + x]; return 0;
}
std::int32_t sdk_DistanceField_ComputeToWithExpansionCount(const std::uint64_t* walls, const std::uint64_t* roots,
                                                         const std::uint64_t* destinations, std::uint64_t* visited,
                                                         std::uint16_t* field, std::uint16_t limit, std::uint16_t* expanded) {
    CHECK(host_observed && walls && roots && destinations && visited && field && expanded);
    ++host_move_fields; *expanded = 0; std::fill_n(visited, words, 0);
    // Same packed bitboard and row-major field layouts as the pinned SDK.
    // Expand exact eight-neighbour layers until every requested destination is
    // reached or the bound/search ends; no native engine cost is simulated.
    std::fill_n(field, side * side, std::numeric_limits<std::uint16_t>::max());
    std::array<std::uint16_t, side * side> queue{}; unsigned head = 0, tail = 0, pending = 0;
    for (int y = 0; y < int(side); ++y) for (int x = 0; x < int(side); ++x) {
        pending += bool(destinations[word(x, y)] & bit(x, y));
        if (!(roots[word(x, y)] & bit(x, y))) continue;
        CHECK(!(walls[word(x, y)] & bit(x, y)));
        field[y * side + x] = 0; visited[word(x, y)] |= bit(x, y);
        queue[tail++] = std::uint16_t(y * side + x);
    }
    CHECK(tail > 0 && pending > 0);
    for (unsigned i = 0; i < words; ++i) pending -= std::popcount(visited[i] & destinations[i]);
    while (head < tail && pending) {
        const unsigned tile = queue[head++]; const int x = int(tile % side), y = int(tile / side);
        const auto distance = field[tile];
        if (distance >= limit) continue;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            const int nx = x + dx, ny = y + dy;
            if ((!dx && !dy) || !inside(nx, ny) || (walls[word(nx, ny)] & bit(nx, ny))) continue;
            const unsigned next = unsigned(ny) * side + unsigned(nx);
            if (field[next] != std::numeric_limits<std::uint16_t>::max()) continue;
            CHECK(tail < queue.size()); field[next] = std::uint16_t(distance + 1);
            visited[word(nx, ny)] |= bit(nx, ny); queue[tail++] = std::uint16_t(next);
            *expanded = std::max(*expanded, field[next]);
            if (destinations[word(nx, ny)] & bit(nx, ny)) --pending;
        }
    }
    return 0;
}
std::int32_t sdk_Bitboard_ReachableMask(const std::uint64_t* walls, std::int32_t x, std::int32_t y,
                                     std::uint64_t* out, std::uint16_t limit) {
    CHECK(host_observed && walls && out && inside(x, y) && !(walls[word(x, y)] & bit(x, y)));
    ++host_reachable_queries;
    std::fill_n(out, words, 0);
    std::array<std::uint16_t, side * side> queue{}, depth{}; unsigned head = 0, tail = 1;
    queue[0] = std::uint16_t(unsigned(y) * side + unsigned(x)); out[word(x, y)] |= bit(x, y);
    while (head < tail) {
        const unsigned tile = queue[head++]; const int px = int(tile % side), py = int(tile / side);
        if (depth[tile] >= limit) continue;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            const int nx = px + dx, ny = py + dy;
            if ((!dx && !dy) || !inside(nx, ny) || (walls[word(nx, ny)] & bit(nx, ny)) ||
                (out[word(nx, ny)] & bit(nx, ny))) continue;
            const unsigned next = unsigned(ny) * side + unsigned(nx);
            CHECK(tail < queue.size()); depth[next] = std::uint16_t(depth[tile] + 1);
            out[word(nx, ny)] |= bit(nx, ny); queue[tail++] = std::uint16_t(next);
        }
    }
    std::copy_n(out, words, host_reachable_mask.begin()); host_reachable_round = host_round;
    host_reachable_x = x; host_reachable_y = y;
    return 0;
}
std::int32_t sdk_Bitboard_Zero(std::uint64_t* out) { std::fill_n(out, words, 0); return 0; }
std::int32_t sdk_Bitboard_ImportRawBuffer(std::uint64_t* out, const std::uint8_t* bytes, std::size_t size) {
    CHECK(size == words * sizeof(std::uint64_t)); std::memcpy(out, bytes, size); return 0;
}
std::int32_t sdk_Bitboard_Test(const std::uint64_t* in, std::int32_t x, std::int32_t y, std::int32_t* out) {
    *out = inside(x, y) && (in[word(x, y)] & bit(x, y)); return 0;
}
std::int32_t sdk_Bitboard_Set(std::uint64_t* out, std::int32_t x, std::int32_t y, std::int32_t value) {
    CHECK(inside(x, y)); if (value) out[word(x, y)] |= bit(x, y); else out[word(x, y)] &= ~bit(x, y); return 0;
}
std::int32_t sdk_Bitboard_IsZero(const std::uint64_t* in, std::int32_t* out) {
    *out = std::all_of(in, in + words, [](auto v) { return v == 0; }); return 0;
}
std::int32_t sdk_Bitboard_Intersect(const std::uint64_t* a, const std::uint64_t* b, std::uint64_t* out) {
    for (unsigned i = 0; i < words; ++i) out[i] = a[i] & b[i]; return 0;
}
std::int32_t sdk_Bitboard_Union(const std::uint64_t* a, const std::uint64_t* b, std::uint64_t* out) {
    for (unsigned i = 0; i < words; ++i) out[i] = a[i] | b[i]; return 0;
}
std::int32_t sdk_Bitboard_Not(const std::uint64_t* in, std::uint64_t* out) {
    for (unsigned i = 0; i < words; ++i) out[i] = ~in[i]; return 0;
}
std::int32_t sdk_Bitboard_NumSetBits(const std::uint64_t* in, std::uint32_t* count) {
    *count = 0;
    for (unsigned i = 0; i < words; ++i) for (auto v = in[i]; v; v &= v - 1) ++*count;
    return 0;
}
std::int32_t sdk_Bitboard_GetSetBits(const std::uint64_t* in, void* positions, std::size_t capacity, std::uint32_t* count) {
    auto* out = static_cast<cambc::vec2*>(positions); *count = 0;
    for (int y = 0; y < int(side); ++y) for (int x = 0; x < int(side); ++x)
        if (in[word(x, y)] & bit(x, y)) { if (*count < capacity) out[*count] = {x, y}; ++*count; }
    return 0;
}
std::int32_t sdk_Bitboard_DrawChebyshev(std::uint64_t* out, std::int32_t x, std::int32_t y, float radius) {
    for (int row = 0; row < int(side); ++row) for (int col = 0; col < int(side); ++col)
        if (float(std::max(std::abs(col - x), std::abs(row - y))) <= radius) out[word(col, row)] |= bit(col, row);
    return 0;
}
}}

namespace {
using Bot = r::native_binding::Bot;
namespace rr = r::receipts;
constexpr int altar_id = 11 * int(side) + 13;
constexpr std::array<n::Tile, 5> nodes{{{10, 10}, {11, 10}, {12, 10}, {13, 10}, {8, 10}}};

void reset(Bot& bot) {
    host_round = 100; host_bag = host_gold = host_score = host_enemy_score = 0; host_fuel = 1000000;
    round_reads = flip_round_at = 0;
    host_observed = false; host_known = 7; host_group_state = {{1, 1, 1}}; host_vision.fill(0);
    host_export_cost = host_export_round = host_exports = host_action_count = host_field_cost = host_fields = 0; host_actions = {};
    host_restrictions_cost = host_restrictions = 0;
    host_awake_camps = 0;
    host_camp_design_queries = 0; host_camp_design_health = host_camp_design_strength = 0;
    host_preparation_cursor = nullptr; host_preparation_seen_cursor = 0;
    host_preparation_quote_units = host_preparation_quote_cost = host_preparation_cross_after = 0;
    host_move_fields = host_reachable_queries = host_reachable_round = 0;
    host_point_queries = host_point_charge_call = host_point_cost = host_cross_on_point = 0;
    host_champion_tiles.fill(0); host_teleport_tiles.fill(0); host_move_succeeds = true;
    host_reachable_x = host_reachable_y = -1; host_reachable_mask.fill(0);
    host_cross_on_vision = 0;
    host_actor = {0, int(cambc::champion_type::marcella), 10, 10, 1200, 1200, 50, 60, 1, 1,
                  1 | (1 << 3) | (1 << 4)};
    auto& runtime = bot.runtime_;
    runtime.cache_ = {}; auto& cache = runtime.cache_;
    cache.width = side; cache.height = side; cache.groups = 3; cache.altars = 1;
    cache.altar_tiles[0] = {13, 11};
    for (int i = 0; i < 5; ++i) cache.nodes[i] = nodes[i];
    for (auto& row : cache.distance) row.fill(n::unknown_distance);
    for (int i = 0; i < 5; ++i) for (int j = 0; j < 5; ++j)
        cache.distance[i][j] = std::uint16_t(n::cheb(n::position(nodes[i]), n::position(nodes[j])));
    runtime.groups_.clear(); runtime.camps_.clear(); runtime.enemies_.clear();
    runtime.settings_ = {5, 10, 1, 2000, 1000, 2};
    runtime.round_ = int(host_round); runtime.epoch_ = 1; runtime.phase_aborted_ = false;
    runtime.own_type_ = cambc::champion_type::marcella; runtime.own_base_.camp_damage_multiplier = 1;
    runtime.own_count_ = 1; runtime.own_ordinal_ = 0; runtime.frame_actor_valid_ = false;
    runtime.recovering_ = false; runtime.enemy_stress_ = runtime.route_stress_ = runtime.next_incoming_ = 0;
    runtime.danger_.clear(); runtime.visible_.clear(); runtime.own_receipts_ = {};
    for (int i = 0; i < 3; ++i) {
        cache.ids[i] = i + 1;
        host_camps[i] = {10 + i, 11, 50, 50, 0, 0, {0, 0}};
        auto camp = cambc::detail::handle_access::make_camp(static_cast<cambc::handle_camp_t>(101 + i),
            static_cast<cambc::handle_camp_group_t>(i + 1), cambc::camp_type::wolf, {10 + i, 11});
        std::vector<cambc::camp> members{camp};
        if (i == 0) members.push_back(cambc::detail::handle_access::make_camp(
            static_cast<cambc::handle_camp_t>(104), static_cast<cambc::handle_camp_group_t>(1), cambc::camp_type::wolf, {10, 12}));
        auto native = cambc::detail::handle_access::make_camp_group(static_cast<cambc::handle_camp_group_t>(i + 1),
            20, unsigned(i == 0 ? 30 : i == 1 ? 20 : 10), 50, std::move(members));
        n::LiveGroup g{native, cambc::bitboard{}, i + 1, i}; g.known = true; g.spawns.set({10 + i, 11});
        if (i == 0) g.spawns.set({10, 12});
        g.combat.actor_count = g.combat.monster_count = 1;
        g.combat.actors[0] = runtime.actor(); g.combat.monsters[0] = {101 + i, 50, 50, 0, true};
        runtime.groups_.push_back(std::move(g)); runtime.camps_.push_back(camp);
        runtime.visible_.set({10 + i, 10});
    }
    host_camps[3] = {10, 12, 0, 50, 0, 0, {0, 0}};
    runtime.camps_.push_back(cambc::detail::handle_access::make_camp(static_cast<cambc::handle_camp_t>(104),
        static_cast<cambc::handle_camp_group_t>(1), cambc::camp_type::wolf, {10, 12}));
    runtime.geometry_generation_ = runtime.position_field_geometry_ = 1;
    runtime.position_distances_valid_ = true; runtime.position_field_round_ = runtime.round_;
    runtime.position_field_origin_ = {10, 10};
    runtime.current_distance_.fill(-1);
    for (int i = 0; i < 5; ++i) runtime.current_distance_[i] = n::cheb({10, 10}, n::position(nodes[i]));
    bot.project_ = {}; bot.fresh_ = {}; bot.choice_ = {}; bot.shape_ = {};
    bot.preparation_.reset(); bot.preparation_.restarts = 0;
    bot.ledger_ = {}; bot.receipts_ = {}; bot.capture_ = {};
    bot.ids_ = {{-1, -1, -1}}; bot.altar_ = bot.spawn_ = bot.actor_ = -1;
    bot.fixed_ = bot.running_ = bot.closed_ = bot.done_ = bot.liquidating_ = false;
    bot.vacate_source_ = -1; bot.map_width_ = 0; for (auto& region : bot.source_spawns_) region.clear();
    bot.cash_attempt_ = 0; bot.buy_attempt_ = false; bot.supported_ = bot.supported_inventory();
    CHECK(bot.supported_);
}
void location(Bot& bot, unsigned round, int x, int y) {
    host_round = round; host_actor.x = x; host_actor.y = y;
    auto& runtime = bot.runtime_; runtime.round_ = int(round); ++runtime.epoch_;
    runtime.frame_actor_valid_ = false; runtime.position_distances_valid_ = true;
    runtime.position_field_round_ = int(round); runtime.position_field_origin_ = {x, y};
    runtime.position_field_geometry_ = runtime.geometry_generation_;
    for (int i = 0; i < 5; ++i) runtime.current_distance_[i] = n::cheb({x, y}, n::position(nodes[i]));
}
n::Snapshot packet(Bot& bot) {
    auto& runtime = bot.runtime_; n::Snapshot s;
    s.round = int(host_round); s.epoch = runtime.epoch_; s.complete = s.alive = s.settings_valid = true;
    s.type = cambc::champion_type::marcella; s.position_tile_id = host_actor.y * int(side) + host_actor.x;
    s.self = {0, true, true, true, {int(host_bag), p5::Knowledge::current, int(host_round)},
                               {int(host_gold), p5::Knowledge::current, int(host_round)}};
    s.stats = runtime.actor(); s.settings = runtime.settings_; s.receipts = runtime.own_receipts_;
    s.team_score = int(host_score); s.fuel = int(host_fuel);
    s.channel = (host_actor.state & (1 << 2)) ? e::Channel::tribute : e::Channel::none;
    s.channel_safe = true; s.windows.valid = true; s.windows.official_remaining = 2000 - int(host_round);
    s.windows.window.fill(std::max(0, 135 - int(host_round))); s.windows.deadline = s.windows.window;
    s.group_count = 3;
    for (int i = 0; i < 3; ++i) {
        auto& g = s.groups[i]; const auto& live = runtime.groups_[i];
        g.id = i + 1; g.entry_tile_id = int(nodes[i].y) * int(side) + nodes[i].x;
        g.known = live.known; g.reward = live.reward; g.respawning = live.respawning;
        g.gems = i == 0 ? 30 : i == 1 ? 20 : 10; g.gold = 20; g.combat = live.combat;
    }
    s.altar_count = 1; s.altars[0] = {altar_id, true, false, n::cheb({host_actor.x, host_actor.y}, {13, 11}) <= 1};
    return s;
}
void sample(Bot& bot, const n::Snapshot& s) { bot.capture_ = rr::observe(bot.frame(s), bot.receipts_); }
void finish_sample(Bot& bot, const n::Snapshot& s) { CHECK(rr::finalize(bot.frame(s, true), bot.receipts_)); }
void complete_component_replan(Bot& bot, const n::Snapshot& s) {
    CHECK(bot.project_.pending_replan && bot.project_.stage == r::Stage::running);
    const auto ledger = bot.ledger_;
    const auto consumed = bot.project_.executor.consumed_payout;
    const bool vacate = bot.project_.must_vacate;
    CHECK(bot.build_into(s, bot.fresh_));
    r::Preparation preparation;
    const r::PreparationDomain domain{s.position_tile_id, bot.runtime_.geometry_generation_};
    r::PreparationStatus status = r::PreparationStatus::incomplete;
    // Component cases settle a worker against one explicit packet. The actual
    // Bot::turn fixture below separately requires a later owner for each unit.
    for (unsigned unit = 0; unit < 256 && status == r::PreparationStatus::incomplete; ++unit)
        status = preparation.advance(bot.fresh_, bot.profile_, domain);
    CHECK(status == r::PreparationStatus::complete);
    r::Decision selected;
    CHECK(preparation.publish_into(bot.fresh_, selected, domain) == r::PreparationStatus::complete);
    CHECK(selected.feasible);
    r::install_replan(bot.project_, selected);
    CHECK(!bot.project_.pending_replan && !bot.project_.initialized && !bot.project_.intent);
    CHECK(bot.project_.ledger.captures == ledger.captures && bot.project_.ledger.banks == ledger.banks);
    CHECK(bot.project_.ledger.purchases == ledger.purchases && bot.project_.ledger.consumed == ledger.consumed);
    CHECK(bot.project_.must_vacate == vacate && bot.project_.executor.consumed_payout == consumed);
}
e::Decision advance(Bot& bot, const n::Snapshot& s) {
    CHECK(bot.build_into(s, bot.fresh_));
    const auto observed = bot.runtime_.execution_observation(bot.project_.executor, bot.goal());
    const auto action = bot.advance(s, observed);
    if (bot.project_.pending_replan) {
        const auto receipt_packet = bot.fresh_.execution;
        complete_component_replan(bot, s);
        bot.fresh_.execution = receipt_packet;
    }
    return action;
}
void begin(Bot& bot) {
    auto s = packet(bot); sample(bot, s); CHECK(bot.build_into(s, bot.fresh_));
    const auto selected = r::choose_job(bot.fresh_, bot.profile_);
    CHECK(selected.feasible && selected.job == r::Job::harvest);
    CHECK(bot.ids_ == (std::array<int, 3>{1, 2, 3}));
    bot.start(selected);
    const auto observations = bot.runtime_.observe_count_;
    CHECK(!bot.fresh_.execution.can_act && !bot.fresh_.execution.observation_epoch);
    const auto bind = bot.advance(s, bot.fresh_.execution);
    CHECK(bind.action.kind == e::ActionKind::refresh && bot.project_.initialized && !bot.project_.intent);
    CHECK(bot.runtime_.observe_count_ == observations && !host_action_count);
    CHECK(advance(bot, s).action.kind == e::ActionKind::attack);
    finish_sample(bot, s);
}
void cleared(Bot& bot, int index, unsigned round) {
    location(bot, round, 10 + index, 11);
    auto& g = bot.runtime_.groups_[index]; g.reward = true; g.respawning = false;
    g.combat.monsters[0].observed_health = 0; host_camps[index].health = 0;
    const auto s = packet(bot); sample(bot, s);
    CHECK(bot.build_into(s, bot.fresh_));
    const auto goal = bot.goal();
    const auto observed = bot.runtime_.execution_observation(bot.project_.executor, goal);
    const int before_phase = int(bot.project_.executor.phase), before_stage = int(bot.project_.stage);
    const auto action = bot.advance(s, observed);
    if (action.action.kind != e::ActionKind::hold)
        std::fprintf(stderr, "cleared index=%d round=%u action=%d reason=%d phase=%d:%d stage=%d:%d group=%d target=%d onspawn=%d cleared=%d known=%d safe=%d recovery=%d selected=%d step=%d initialized=%d\n",
            index, round, int(action.action.kind), int(action.reason), before_phase, int(bot.project_.executor.phase),
            before_stage, int(bot.project_.stage), observed.group_id, bot.project_.executor.target,
            int(observed.on_group_spawn), int(observed.group_cleared), int(observed.group_live_known), int(observed.safe),
            int(observed.needs_recovery), int(bot.project_.selected.job), bot.project_.index, int(bot.project_.initialized));
    CHECK(action.action.kind == e::ActionKind::hold);
    finish_sample(bot, s);
}
void basis(Bot& bot, unsigned round) {
    location(bot, round, host_actor.x, host_actor.y);
    const auto s = packet(bot); sample(bot, s); finish_sample(bot, s);
}
n::Snapshot captured(Bot& bot, int index, unsigned round, int gems, int cash, int group = -1) {
    location(bot, round, 10 + index, 11); host_bag += unsigned(gems); host_gold += unsigned(cash);
    auto& g = bot.runtime_.groups_[index]; g.reward = false; g.respawning = true;
    auto& receipt = bot.runtime_.own_receipts_; ++receipt.capture;
    receipt.capture_round = int(round); receipt.capture_group = group < 0 ? index + 1 : group; receipt.capture_gems = gems;
    auto s = packet(bot); sample(bot, s); return s;
}
void active_complete_cycle() {
    Bot bot(r::Arm::finite_harvest); reset(bot); begin(bot);
    CHECK(bot.project_.selected.selected.prefix == 2);
    cleared(bot, 0, 101); basis(bot, 105);
    auto s = captured(bot, 0, 106, 30, 20);
    CHECK(bot.capture_.exact && bot.capture_.gems == 30 && bot.capture_.gold == 20);
    std::swap(s.groups[0], s.groups[2]); // Snapshot order is not finite identity.
    auto& feedback = bot.runtime_.feedback_;
    feedback.feedback_sequence = bot.project_.executor.intent_sequence;
    feedback.feedback_kind = e::ActionKind::move; feedback.feedback_success = false; feedback.feedback_target = 17;
    const auto payoutA = advance(bot, s);
    CHECK(payoutA.action.kind == e::ActionKind::refresh);
    CHECK(bot.ledger_.captures == 1 && bot.ledger_.consumed == 1 && !bot.ledger_.banks);
    CHECK(bot.ids_ == (std::array<int, 3>{1, 2, 3}));
    CHECK(bot.goal().production && bot.goal().group_id == 2);
    CHECK(bot.project_.selected.input.groups[0].id == 1 && !bot.project_.selected.input.groups[0].available);
    // The receipt-backed replan leaves a same-owner full packet for old A.
    // Binding B may reuse it only without dispatch, and feedback stays consumed.
    CHECK(!bot.project_.initialized && bot.fresh_.execution.group_id == 1);
    const auto observations = bot.runtime_.observe_count_;
    const auto consumed = bot.project_.executor.consumed_feedback;
    CHECK(consumed == feedback.feedback_sequence && bot.project_.executor.failed_moves == 1);
    const auto nextBind = bot.advance(s, bot.fresh_.execution);
    CHECK(nextBind.action.kind == e::ActionKind::refresh && bot.project_.initialized && !bot.project_.intent);
    CHECK(bot.runtime_.observe_count_ == observations && bot.project_.executor.target == 2);
    CHECK(bot.project_.executor.consumed_feedback == consumed && bot.project_.executor.failed_moves == 1);
    CHECK(bot.ledger_.captures == 1 && bot.ledger_.consumed == 1 && !host_action_count);
    finish_sample(bot, s);
    location(bot, 110, 11, 10); s = packet(bot); sample(bot, s);
    CHECK(advance(bot, s).action.kind == e::ActionKind::attack); finish_sample(bot, s);
    cleared(bot, 1, 111); basis(bot, 115); s = captured(bot, 1, 116, 20, 20);
    CHECK(advance(bot, s).action.kind == e::ActionKind::refresh);
    CHECK(bot.ledger_.captures == 2 && bot.ledger_.consumed == 3 && host_bag == 50 && host_gold == 40);
    CHECK(bot.goal().kind == n::GoalKind::altar && !bot.goal().production); finish_sample(bot, s);
    location(bot, 120, 13, 10); s = packet(bot); sample(bot, s);
    const auto bankBind = advance(bot, s);
    CHECK(bankBind.action.kind == e::ActionKind::refresh && !bot.project_.intent);
    CHECK(bot.goal().phase == e::Phase::bank);
    const auto bank = advance(bot, s);
    CHECK(bank.action.kind == e::ActionKind::start_tribute && bank.action.whole_bag && bank.action.starting_bag == 50);
    CHECK(bank.action.channel_ticks == 5 && bot.project_.intent && bot.ledger_.banks == 0);
    host_actor.state |= 1 << 2; location(bot, 121, 13, 10); s = packet(bot); sample(bot, s);
    host_fuel = 1; s.fuel = 1;
    CHECK(advance(bot, s).action.kind == e::ActionKind::preserve_channel);
    CHECK(bot.ledger_.banks == 0 && host_bag == 50);
    host_fuel = 1000000; host_actor.state &= ~(1 << 2); host_bag = 0; host_gold = 140; host_score = 50;
    location(bot, 125, 13, 10); bot.runtime_.own_receipts_.bank = 1; bot.runtime_.own_receipts_.bank_round = 125;
    s = packet(bot); sample(bot, s);
    CHECK(advance(bot, s).action.kind == e::ActionKind::refresh);
    CHECK(bot.ledger_.banks == 1 && bot.ledger_.liquidated && bot.ledger_.captures == 2 && bot.ledger_.consumed == 3);
    CHECK(bot.goal().kind == n::GoalKind::productive_entry && bot.goal().group_id == 3);
    location(bot, 129, 12, 10); s = packet(bot); sample(bot, s);
    CHECK(advance(bot, s).action.kind == e::ActionKind::refresh);
    CHECK(advance(bot, s).action.kind == e::ActionKind::yield);
    CHECK(bot.project_.stage == r::Stage::complete && bot.done_ && !bot.running_);
    CHECK(bot.ledger_.captures == 2 && bot.ledger_.banks == 1 && bot.ledger_.purchases == 0 && host_bag == 0 && host_gold == 140);
}
void control_banks_first() {
    Bot bot(r::Arm::bank_each_control); reset(bot); begin(bot); cleared(bot, 0, 101); basis(bot, 105);
    auto s = captured(bot, 0, 106, 30, 20); CHECK(advance(bot, s).action.kind == e::ActionKind::refresh);
    CHECK(bot.goal().kind == n::GoalKind::altar && !bot.goal().production);
    CHECK(bot.ledger_.captures == 1 && bot.ledger_.consumed == 1 && bot.ledger_.banks == 0);
    location(bot, 110, 13, 10); s = packet(bot); sample(bot, s);
    CHECK(advance(bot, s).action.kind == e::ActionKind::refresh && !bot.project_.intent);
    const auto bank = advance(bot, s);
    CHECK(bank.action.kind == e::ActionKind::start_tribute && bank.action.starting_bag == 30 && bank.action.channel_ticks == 3);
}
void receipt_and_finite_guards() {
    for (int bad = 0; bad < 4; ++bad) {
        Bot bot(r::Arm::finite_harvest); reset(bot); begin(bot); cleared(bot, 0, 101); basis(bot, bad == 0 ? 104 : 105);
        if (bad == 1) ++bot.cash_attempt_;
        auto s = captured(bot, 0, 106, bad == 2 ? 6 : 30, 20, bad == 3 ? 99 : -1);
        const bool exact = bot.capture_.exact;
        const auto result = advance(bot, s);
        CHECK(bot.ledger_.captures == 0 && bot.ledger_.consumed == 0);
        CHECK(result.action.kind != e::ActionKind::start_tribute && result.action.kind != e::ActionKind::buy);
        CHECK(bot.project_.stage == r::Stage::reobserve || bot.project_.stage == r::Stage::aborted);
        if (bad == 2) CHECK(exact && bot.capture_.gems == 6 && bot.capture_.gold == 20);
        else if (bad == 3) CHECK(exact && bot.capture_.group == 99);
        else CHECK(!exact);
        bot.liquidate(s);
        CHECK(bot.liquidating_ && bot.closed_ && host_bag > 0 && bot.liquidation_goal(s).bank_requested);
        CHECK(bot.ledger_.captures == 0 && bot.ledger_.banks == 0 && !bot.done_);
    }
    Bot bot(r::Arm::finite_harvest); reset(bot); begin(bot); cleared(bot, 0, 101); basis(bot, 105);
    auto s = captured(bot, 0, 106, 30, 20); CHECK(advance(bot, s).action.kind == e::ActionKind::refresh);
    const auto owned = bot.ledger_;
    CHECK(owned.captures == 1 && owned.consumed == 1);
    CHECK(bot.build_into(s, bot.fresh_)); bot.start(r::choose_job(bot.fresh_, bot.profile_));
    CHECK(bot.ledger_.captures == 1 && bot.project_.ledger.consumed == 1);
    CHECK(bot.project_.selected.input.ledger.captures == 1 && bot.project_.selected.input.groups[0].id == 1);
    s.groups[1].respawning = true; bot.runtime_.groups_[1].respawning = true;
    CHECK(bot.build_into(s, bot.fresh_));
    CHECK(!bot.fresh_.groups[1].available && bot.fresh_.ledger.captures == 1);
    const auto liquidate = r::choose_job(bot.fresh_, bot.profile_);
    CHECK(liquidate.feasible && liquidate.selected.prefix == 0 && liquidate.job == r::Job::liquidate);
    s.windows.window.fill(1); s.windows.deadline.fill(1);
    CHECK(bot.build_into(s, bot.fresh_));
    CHECK(r::choose_job(bot.fresh_, bot.profile_).selected.prefix == 0);
}
void recovery_goal_and_actual_receipt() {
    Bot bot(r::Arm::finite_harvest); reset(bot);
    auto s = packet(bot); CHECK(bot.build_into(s, bot.fresh_));
    bot.ledger_ = {2, 1, 0, 3, true};
    bot.runtime_.own_receipts_.capture = 2;
    host_actor.health = 300;
    bot.runtime_.groups_[2].combat.monsters[0].effective_strength = 200;
    host_camps[2].strength = 200; s = packet(bot);
    CHECK(bot.build_into(s, bot.fresh_));
    const auto selected = r::choose_job(bot.fresh_, bot.profile_);
    CHECK(selected.feasible && selected.selected.count >= 2 && selected.selected.steps[0].kind == r::StepKind::recover);
    bot.start(selected);
    CHECK(bot.goal().kind == n::GoalKind::spawn && !bot.goal().production);
    CHECK(bot.runtime_.route({}, {n::NodeKind::spawn, -1}).turns() == 4);
    CHECK(advance(bot, s).action.kind == e::ActionKind::refresh);
    location(bot, 101, 8, 10); s = packet(bot); auto action = advance(bot, s);
    CHECK(action.action.kind == e::ActionKind::yield && action.reason == e::Reason::arrival_recovery);
    CHECK(host_actor.health == 300 && bot.recovery_ == 0 && bot.ledger_.captures == 2);
    location(bot, 102, 8, 10); bot.runtime_.previous_spawn_round_ = 101; s = packet(bot);
    action = advance(bot, s);
    CHECK(action.reason == e::Reason::arrival_recovery && bot.project_.selected.selected.steps[0].kind == r::StepKind::recover);
    // Current spawn health and the caller's observed later-owner receipt are
    // both required. Neither route arrival nor elapsed forecast heals a body.
    location(bot, 103, 8, 10); host_actor.health = 1200; bot.runtime_.previous_spawn_round_ = 102;
    bot.recovery_ = 1; bot.recovery_round_ = 103; s = packet(bot);
    CHECK(advance(bot, s).action.kind == e::ActionKind::refresh);
    CHECK(bot.goal().kind == n::GoalKind::productive_entry && bot.ledger_.captures == 2 && bot.ledger_.banks == 1);
}
void stale_closes_unexecuted_intent() {
    Bot bot(r::Arm::bank_each_control); reset(bot); begin(bot); cleared(bot, 0, 101); basis(bot, 105);
    auto s = captured(bot, 0, 106, 30, 20); CHECK(advance(bot, s).action.kind == e::ActionKind::refresh);
    location(bot, 110, 13, 10); s = packet(bot); sample(bot, s);
    CHECK(advance(bot, s).action.kind == e::ActionKind::refresh && !bot.project_.intent);
    CHECK(bot.build_into(s, bot.fresh_));
    CHECK(bot.fixed_ && bot.running_ && bot.goal().phase == e::Phase::bank);
    const auto observed = bot.runtime_.execution_observation(bot.project_.executor, bot.goal());
    // The first query is the post-facts guard; the second follows pure execute,
    // after it can have latched a tribute which has not reached SDK dispatch.
    round_reads = 0; flip_round_at = 2;
    const auto action = bot.advance(s, observed); flip_round_at = 0;
    CHECK(action.action.kind == e::ActionKind::refresh && host_round == 111);
    CHECK(bot.closed_ && !bot.running_ && !bot.done_ && bot.project_.stage == r::Stage::reobserve);
    CHECK(!bot.project_.intent && !bot.capture_.exact && !bot.receipts_.baseline_valid);
    CHECK(bot.ledger_.captures == 1 && bot.ledger_.consumed == 1 && bot.ledger_.banks == 0);
    CHECK(host_bag == 30 && host_gold == 20);
    // Closure cannot erase an already owned explicit liquidation obligation.
    bot.liquidating_ = true; bot.bank_turn_ = 109; bot.bank_ticks_ = 3;
    bot.bank_bag_ = 30; bot.bank_gold_ = 20; bot.bank_base_ = 7; bot.bank_altar_ = altar_id;
    bot.project_.intent = 999; bot.running_ = true; bot.done_ = true; bot.stale_episode();
    CHECK(bot.liquidating_ && bot.bank_turn_ == 109 && bot.bank_ticks_ == 3 && bot.bank_base_ == 7);
    CHECK(bot.bank_bag_ == 30 && bot.bank_gold_ == 20 && bot.bank_altar_ == altar_id);
    CHECK(!bot.project_.intent && bot.ledger_.captures == 1 && host_bag == 30);
}
void query_only_bind_rejects_changed_stock_and_owner() {
    for (int bad = 0; bad < 4; ++bad) {
        Bot bot(r::Arm::finite_harvest); reset(bot);
        const auto s = packet(bot); sample(bot, s); CHECK(bot.build_into(s, bot.fresh_));
        const auto selected = r::choose_job(bot.fresh_, bot.profile_); CHECK(selected.feasible);
        bot.start(selected);
        if (bad == 0) ++bot.fresh_.execution.owner_turn;
        if (bad == 1) ++bot.fresh_.resources.bag.value;
        if (bad == 2) ++bot.fresh_.resources.gold.value;
        const auto observations = bot.runtime_.observe_count_;
        round_reads = 0; flip_round_at = bad == 3 ? 2 : 0;
        const auto action = bot.advance(s, bot.fresh_.execution); flip_round_at = 0;
        CHECK(action.action.kind == e::ActionKind::refresh && !bot.project_.intent);
        CHECK(bot.runtime_.observe_count_ == observations && !host_action_count);
        CHECK(!bot.ledger_.captures && !bot.ledger_.banks && !bot.ledger_.purchases);
        CHECK(bot.project_.stage == (bad == 0 ? r::Stage::aborted : r::Stage::reobserve));
        if (bad == 3) CHECK(host_round == 101 && bot.closed_ && !bot.running_ && !bot.receipts_.baseline_valid);
        else CHECK(!bot.project_.initialized);
    }
}
void loaded_offplan_hold_precedes_liquidation() {
    Bot bot(r::Arm::finite_harvest); reset(bot); begin(bot); cleared(bot, 0, 101); basis(bot, 105);
    auto s = captured(bot, 0, 106, 30, 20); CHECK(advance(bot, s).action.kind == e::ActionKind::refresh);
    location(bot, 110, 11, 11);
    auto& runtime = bot.runtime_; runtime.groups_[1].reward = true;
    runtime.groups_[1].combat.monsters[0].observed_health = 0; host_camps[1].health = 0;
    s = packet(bot); s.safe_hold = runtime.fresh_unpaid_hold() != nullptr;
    CHECK(s.safe_hold && host_bag == 30);
    runtime.stage_ = n::Stage::production; runtime.plan_.group = 1;
    runtime.state_.target = 1; runtime.state_.phase = e::Phase::clear; // Deliberately old A target, actual hold is B.
    runtime.last_action_ = int(e::ActionKind::inspect);
    const auto capture = runtime.own_receipts_.capture, bank = runtime.own_receipts_.bank;
    bot.liquidate(s); bot.dispatch_liquidation(s);
    // The caller leaves this actual safe capture alone. It must not invoke
    // Reference/prepare/movement or emit an action through its old A target.
    CHECK(runtime.state_.target == 1 && runtime.state_.phase == e::Phase::clear);
    CHECK(runtime.last_action_ == int(e::ActionKind::inspect) && runtime.reference_packet_round_ == -1);
    CHECK(bot.liquidating_ && bot.closed_ && !bot.done_ && bot.bank_turn_ < 0);
    CHECK(bot.ledger_.captures == 1 && bot.ledger_.consumed == 1 && bot.ledger_.banks == 0);
    CHECK(host_bag == 30 && host_gold == 20 && runtime.own_receipts_.capture == capture && runtime.own_receipts_.bank == bank);
}
void captured_mask_controls_vacate() {
    for (const auto arm : {r::Arm::finite_harvest, r::Arm::bank_each_control}) {
        Bot bot(arm); reset(bot); begin(bot); cleared(bot, 0, 101); basis(bot, 105);
        auto s = captured(bot, 0, 106, 30, 20); CHECK(advance(bot, s).action.kind == e::ActionKind::refresh);
        CHECK(bot.vacate_source_ == 0 && bot.project_.must_vacate);
        CHECK(bot.source_spawns_[0].is_set({10, 11}) && bot.source_spawns_[0].is_set({10, 12}));
        // Another tile of A remains A even after the executor binds B/altar.
        location(bot, 107, 10, 12); s = packet(bot); sample(bot, s);
        CHECK(advance(bot, s).action.kind == e::ActionKind::refresh);
        CHECK(bot.build_into(s, bot.fresh_));
        auto observed = bot.runtime_.execution_observation(bot.project_.executor, bot.goal());
        CHECK(!observed.on_group_spawn && bot.on_captured_region(s));
        observed.move_count = 2;
        observed.moves[0] = {11 * int(side) + 10, true, true, true, true, false};
        observed.moves[1] = {12 * int(side) + 10, true, true, true, true, false};
        auto action = bot.advance(s, observed);
        CHECK(action.action.kind == e::ActionKind::refresh && action.reason == e::Reason::unknown_target);
        CHECK(bot.fresh_.execution.move_count == 0 && bot.project_.must_vacate && bot.ledger_.captures == 1);
        observed.move_count = 3;
        const int exit = 12 * int(side) + 11;
        observed.moves[2] = {exit, true, true, true, true, false};
        action = bot.advance(s, observed);
        CHECK(action.action.kind == e::ActionKind::vacate && action.action.target == exit);
        CHECK(bot.fresh_.execution.move_count == 1 && bot.ledger_.consumed == 1 && !bot.ledger_.banks);
        CHECK(host_bag == 30 && host_gold == 20 && bot.vacate_source_ == 0);
        location(bot, 108, 11, 12); s = packet(bot); sample(bot, s); (void)advance(bot, s);
        CHECK(bot.vacate_source_ == -1 && !bot.project_.must_vacate);
        CHECK(bot.ledger_.captures == 1 && bot.ledger_.consumed == 1 && host_bag == 30 && host_gold == 20);
    }
}
void observed_visibility(unsigned known) {
    host_known = known; host_vision.fill(0);
    host_vision[word(host_actor.x, host_actor.y)] |= bit(host_actor.x, host_actor.y);
    host_vision[word(13, 11)] |= bit(13, 11);
    for (int i = 0; i < 3; ++i) if (known & (1u << i)) host_vision[word(10 + i, 11)] |= bit(10 + i, 11);
    if (known & 1) host_vision[word(10, 12)] |= bit(10, 12);
}
void ordinary_reset(Bot& bot, unsigned known = 7, int x = 10, int y = 10) {
    reset(bot); host_observed = true; host_actor.x = x; host_actor.y = y; observed_visibility(known);
    auto& runtime = bot.runtime_;
    // Static map initialization is outside this fixture. Each ordinary turn
    // still enters begin_turn, refreshes SDK observations, and exports its own
    // packet; neither a Snapshot nor a selected Runoff Decision is supplied.
    runtime.initialized_ = runtime.settings_ok_ = true; runtime.cold_completed_round_ = -1;
    runtime.round_ = 99; runtime.position_field_round_ = 99; runtime.position_field_origin_ = {x, y};
    for (int i = 0; i < 5; ++i) runtime.current_distance_[i] = n::cheb({x, y}, n::position(nodes[i]));
    bot.snapshot_ = {};
}
const Bot::GateTrace* entry_trace(const Bot& bot, int gate, unsigned round) {
    for (unsigned i = 0; i < bot.gate_count_; ++i)
        if (bot.gate_queue_[i].gate == gate && bot.gate_queue_[i].round == int(round)) return &bot.gate_queue_[i];
    return nullptr;
}
bool entry_gate(const Bot& bot, int gate, unsigned round) { return entry_trace(bot, gate, round) != nullptr; }
const Bot::PrepRecord* preparation_record(const Bot& bot, unsigned round) {
    for (unsigned i = 0; i < bot.prep_count_; ++i)
        if (bot.prep_queue_[i].round == int(round)) return &bot.prep_queue_[i];
    return nullptr;
}
void preparation_units(const Bot::PrepRecord& record, int calls) {
    CHECK(record.calls == calls && calls >= 1 && calls <= int(record.units.size()));
    for (int i = 0; i < calls; ++i) {
        CHECK(record.fuel_before[i] >= 30000 && record.fuel_before[i] >= record.fuel_after[i]);
        const auto& trace = record.units[i];
        CHECK(!(trace.began && trace.quoted));
        if (i) {
            const auto& previous = record.units[i - 1];
            CHECK(trace.before_status == previous.after_status && trace.before_cursor == previous.after_cursor &&
                  trace.before_count == previous.after_count && trace.before_restarts == previous.after_restarts);
        }
    }
}
const Bot::ApplyRecord* apply_record(const Bot& bot, n::ApplyExit exit, unsigned round) {
    for (unsigned i = 0; i < bot.apply_count_; ++i)
        if (bot.apply_queue_[i].trace.exit == exit && bot.apply_queue_[i].round == int(round)) return &bot.apply_queue_[i];
    return nullptr;
}
unsigned known_count(const Bot& bot) {
    unsigned count = 0; for (int i = 0; i < bot.snapshot_.group_count; ++i) count += bot.snapshot_.groups[i].known; return count;
}
void ordinary_owner(Bot& bot, bool next = true, std::int64_t fuel = 1000000) {
    if (next) ++host_round;
    host_fuel = fuel; observed_visibility(host_known);
    bot.turn();
    CHECK(bot.snapshot_.complete && bot.snapshot_.round == int(host_round));
    CHECK(bot.runtime_.observe_count_ <= 1);
}
unsigned ordinary_prepare(Bot& bot, bool first = true, std::int64_t fuel = 1000000) {
    const auto actions = host_action_count;
    unsigned owners = 0;
    for (; owners < 256 && !bot.running_; ++owners) {
        ordinary_owner(bot, !first || owners != 0, fuel);
        CHECK(known_count(bot) == 3 && !host_action_count && !bot.project_.initialized);
        CHECK(bot.runtime_.observe_count_ == 0 && bot.runtime_.reference_packet_round_ != int(host_round));
        if (!bot.running_) CHECK(!bot.choice_.feasible);
    }
    CHECK(owners >= 1 && owners < 256 && bot.running_ && bot.fixed_ && bot.choice_.feasible);
    CHECK(host_action_count == actions && !bot.closed_);
    return owners;
}
void ordinary_preparation_batches_existing_units() {
    Bot bot(r::Arm::finite_harvest); ordinary_reset(bot);
    // One existing zero-stat shop option expands the unchanged exhaustive
    // catalogue to twelve candidates without supplying a selected Decision.
    bot.runtime_.purchase_quotes_[0] = {0, 30, int(std::uint32_t(cambc::item_type::monster_shard_of_strength)), true, 0, 0, 0};
    host_preparation_cursor = &bot.preparation_.cursor;
    for (int owner = 0; owner < 3; ++owner) {
        const auto before = host_preparation_quote_units;
        ordinary_owner(bot, owner != 0);
        CHECK(bot.preparation_.status == r::PreparationStatus::incomplete && bot.preparation_.count_ == 12);
        CHECK(bot.preparation_.cursor == 3 + owner * 4 && host_preparation_quote_units - before == (owner == 0 ? 3u : 4u));
        CHECK(!bot.running_ && !bot.fixed_ && !bot.choice_.feasible && !bot.project_.selected.feasible && !bot.project_.initialized);
        CHECK((bot.gate_seen_ & (1u << 14)) && !(bot.gate_seen_ & (1u << 10))); // Each trace reason is retained only once.
        CHECK(!host_action_count && !bot.runtime_.observe_count_ && bot.runtime_.reference_packet_round_ == -1);
        const auto* record = preparation_record(bot, host_round); CHECK(record);
        preparation_units(*record, 4);
        CHECK(record->owner == owner + 1 && record->outcome == 0 && record->cursor == bot.preparation_.cursor);
        CHECK(record->units[0].cause == (owner ? r::PreparationCause::none : r::PreparationCause::inactive));
        CHECK(record->units[0].began == (owner == 0) && record->units[0].quoted == (owner != 0));
        for (int unit = 1; unit < record->calls; ++unit)
            CHECK(record->units[unit].cause == r::PreparationCause::none && record->units[unit].quoted);
    }
    ordinary_owner(bot);
    CHECK(host_preparation_quote_units == 12 && host_exports == 4);
    CHECK(bot.running_ && bot.fixed_ && bot.choice_.feasible && !bot.project_.initialized && entry_gate(bot, 10, host_round));
    CHECK(bot.preparation_.status == r::PreparationStatus::invalid && !bot.preparation_.cursor && !host_action_count && !bot.runtime_.observe_count_);
    const auto* published = preparation_record(bot, host_round); CHECK(published); preparation_units(*published, 1);
    CHECK(published->outcome == 5 && published->cursor == 12 && published->prefix == bot.choice_.selected.prefix);
    CHECK(published->units[0].before_cursor == 11 && published->units[0].after_cursor == 12 &&
          published->units[0].after_status == r::PreparationStatus::complete && published->units[0].quoted);
    host_preparation_cursor = nullptr;
    const auto exhaustive = r::choose_job(bot.fresh_, bot.profile_);
    CHECK(exhaustive.feasible == bot.choice_.feasible && exhaustive.job == bot.choice_.job);
    const auto& actual = bot.choice_.selected; const auto& expected = exhaustive.selected;
    CHECK(actual.feasible == expected.feasible && actual.terminal == expected.terminal && actual.count == expected.count &&
          actual.prefix == expected.prefix && actual.purchase == expected.purchase && actual.buy_before == expected.buy_before);
    CHECK(actual.first_bank == expected.first_bank && actual.last_bank == expected.last_bank && actual.occupied == expected.occupied &&
          actual.capture_tick == expected.capture_tick && actual.schedule.event_count == expected.schedule.event_count);
    CHECK(bot.choice_.control.feasible == exhaustive.control.feasible && bot.choice_.control.prefix == exhaustive.control.prefix);
    for (int i = 0; i < actual.count; ++i) {
        const auto& a = actual.steps[i]; const auto& b = expected.steps[i];
        CHECK(a.kind == b.kind && a.group == b.group && a.index == b.index && a.waypoint == b.waypoint &&
              a.duration == b.duration && a.purchase == b.purchase && a.health == b.health && a.strength == b.strength &&
              a.max_health == b.max_health && a.basic_modifier == b.basic_modifier);
    }
}
void ordinary_preparation_stops_at_synthetic_quote_fuel() {
    for (const unsigned cost : {8000u, 16000u}) {
        Bot bot(r::Arm::finite_harvest); ordinary_reset(bot);
        host_preparation_cursor = &bot.preparation_.cursor; host_preparation_quote_cost = cost;
        const unsigned per_owner = cost == 8000 ? 2 : 1;
        for (unsigned owner = 0; owner < 2; ++owner) {
            ordinary_owner(bot, owner != 0, 45000);
            CHECK(host_fuel == 29000 && host_preparation_quote_units == (owner + 1) * per_owner);
            CHECK(bot.preparation_.cursor == int((owner + 1) * per_owner) && bot.preparation_.status == r::PreparationStatus::incomplete);
            CHECK(!bot.running_ && !bot.fixed_ && !bot.choice_.feasible && !bot.project_.initialized && !host_action_count);
            CHECK((bot.gate_seen_ & (1u << 14)) && !(bot.gate_seen_ & (1u << 10)) && !bot.runtime_.observe_count_);
            const auto* record = preparation_record(bot, host_round); CHECK(record);
            preparation_units(*record, int(per_owner + (owner == 0)));
            CHECK(record->outcome == 0 && record->cursor == bot.preparation_.cursor && record->fuel_after[record->calls - 1] == 29000);
        }
        // The synthetic hook charges only completed quotes, not initialization
        // or C++/Wasm work. Removing the charge lets the same worker complete.
        host_preparation_quote_cost = 0; ordinary_prepare(bot, false);
        CHECK(host_preparation_quote_units == 6 && bot.running_ && bot.choice_.feasible && !bot.project_.initialized && !host_action_count);
        host_preparation_cursor = nullptr;
    }
}
void ordinary_preparation_rejects_synthetic_unit_crossing() {
    Bot bot(r::Arm::finite_harvest); ordinary_reset(bot);
    host_preparation_cursor = &bot.preparation_.cursor; host_preparation_cross_after = 1;
    // The import hook observes the first completed quote before current(s).
    // It models owner suspension at that boundary, not native instruction fuel.
    bot.turn();
    CHECK(host_round == 101 && host_preparation_quote_units == 1 && !host_preparation_cross_after);
    CHECK(bot.snapshot_.complete && bot.snapshot_.round == 100 && !bot.running_ && !bot.fixed_ && !bot.choice_.feasible);
    CHECK(!bot.project_.selected.feasible && bot.project_.stage == r::Stage::reobserve && !bot.project_.initialized);
    CHECK(bot.preparation_.status == r::PreparationStatus::invalid && !bot.preparation_.cursor && !bot.geometry_packet_);
    CHECK(!entry_gate(bot, 10, 100) && !host_action_count && !bot.runtime_.observe_count_);
    CHECK(!preparation_record(bot, 100) && !bot.prep_count_);
    host_preparation_cursor = nullptr;
    ordinary_prepare(bot, false);
    CHECK(bot.choice_.input.round == int(host_round) && bot.running_ && !bot.project_.initialized && !host_action_count);
}
void ordinary_preparation_diagnoses_restart_inputs() {
    for (int changed = 0; changed < 5; ++changed) {
        Bot bot(r::Arm::finite_harvest); ordinary_reset(bot); ordinary_owner(bot, false);
        CHECK(bot.preparation_.cursor == 3 && !bot.preparation_.restarts && !bot.running_);
        switch (changed) {
            case 0: host_actor.strength += 1; break;
            case 1: ++host_gold; break;
            case 2: host_camps[0].health -= 1; break;
            case 3: ++bot.runtime_.geometry_generation_; break;
            case 4: host_enemy_score = 900; break;
        }
        ordinary_owner(bot);
        CHECK(bot.preparation_.cursor == 3 && bot.preparation_.restarts == 1 &&
              bot.preparation_.status == r::PreparationStatus::incomplete);
        CHECK(!bot.running_ && !bot.fixed_ && !bot.choice_.feasible && !bot.project_.initialized && !host_action_count);
        const auto* record = preparation_record(bot, host_round); CHECK(record); preparation_units(*record, 4);
        CHECK(record->owner == 2 && record->outcome == 0 && record->cursor == 3 && record->restarts == 1);
        const auto& trace = record->units[0];
        CHECK(trace.began && !trace.quoted && trace.before_cursor == 3 && trace.after_cursor == 0 &&
              trace.before_count == 6 && trace.after_count == 6 && !trace.before_restarts && trace.after_restarts == 1);
        if (changed < 3) {
            CHECK(trace.cause == r::PreparationCause::physical);
            CHECK(trace.physical == (changed == 0 ? r::PreparationPhysical::actor :
                  changed == 1 ? r::PreparationPhysical::resource_gold : r::PreparationPhysical::group_monster));
            CHECK(trace.index == (changed == 2 ? 0 : -1) && trace.detail == (changed == 2 ? 0 : -1));
        } else {
            CHECK(trace.cause == (changed == 3 ? r::PreparationCause::domain_geometry : r::PreparationCause::window_values));
            CHECK(trace.physical == r::PreparationPhysical::none && trace.index == -1 && trace.detail == -1);
        }
        for (int unit = 1; unit < record->calls; ++unit)
            CHECK(record->units[unit].cause == r::PreparationCause::none && record->units[unit].quoted);
        CHECK(!bot.runtime_.observe_count_ && bot.runtime_.reference_packet_round_ == -1);
    }
}
void same_preparation_quote(const r::Quote& a, const r::Quote& b) {
    CHECK(a.feasible == b.feasible && a.terminal == b.terminal && a.count == b.count && a.prefix == b.prefix &&
          a.purchase == b.purchase && a.buy_before == b.buy_before && a.first_bank == b.first_bank &&
          a.last_bank == b.last_bank && a.occupied == b.occupied && a.capture_tick == b.capture_tick);
    for (unsigned i = 0; i < a.steps.size(); ++i) {
        const auto& x = a.steps[i]; const auto& y = b.steps[i];
        CHECK(x.kind == y.kind && x.group == y.group && x.index == y.index && x.waypoint == y.waypoint &&
              x.duration == y.duration && x.purchase == y.purchase && x.health == y.health && x.strength == y.strength &&
              x.max_health == y.max_health && x.basic_modifier == y.basic_modifier);
    }
    const auto& x = a.schedule; const auto& y = b.schedule;
    CHECK(x.actor_count == y.actor_count && x.event_count == y.event_count && x.target_id == y.target_id &&
          x.starting_team_score == y.starting_team_score && x.win_score == y.win_score &&
          x.tribute_gems_per_tick == y.tribute_gems_per_tick && x.tribute_min_ticks == y.tribute_min_ticks &&
          x.terminal_win == y.terminal_win && x.terminal_win_tick == y.terminal_win_tick &&
          x.immediate_legal_winning_bank == y.immediate_legal_winning_bank && x.doctrine_id == y.doctrine_id &&
          x.denial_evidenced == y.denial_evidenced && x.denied_gems == y.denied_gems &&
          x.denied_bank_tick == y.denied_bank_tick && x.projected_kills == y.projected_kills);
    for (unsigned i = 0; i < x.actors.size(); ++i) {
        const auto& p = x.actors[i]; const auto& q = y.actors[i];
        CHECK(p.id == q.id && p.self == q.self && p.visible == q.visible &&
              p.bag_known == q.bag_known && p.bag == q.bag && p.gold == q.gold &&
              p.tribute_gold_per_gem == q.tribute_gold_per_gem && p.occupied_turns == q.occupied_turns);
    }
    for (unsigned i = 0; i < x.events.size(); ++i) {
        const auto& p = x.events[i]; const auto& q = y.events[i];
        CHECK(p.id == q.id && p.tick == q.tick && p.kind == q.kind && p.actor == q.actor && p.group == q.group &&
              p.payout_id == q.payout_id && p.participants == q.participants && p.holders == q.holders &&
              p.gems == q.gems && p.gold == q.gold && p.waypoint == q.waypoint && p.legal == q.legal);
    }
    const auto& p = a.rank; const auto& q = b.rank;
    CHECK(p.status == q.status && std::strcmp(p.reason, q.reason) == 0 && p.Y == q.Y && p.D == q.D && p.M == q.M &&
          p.final_bag == q.final_bag && p.final_gold == q.final_gold && p.actor_ids == q.actor_ids &&
          p.actor_count == q.actor_count && p.earned_gold == q.earned_gold && p.kills == q.kills &&
          p.occupied_turns == q.occupied_turns && p.peak_bag == q.peak_bag && p.productive_arrival == q.productive_arrival &&
          p.target_id == q.target_id && p.windows == q.windows && p.immediate_win == q.immediate_win &&
          p.official_endpoint == q.official_endpoint);
}
void ordinary_preparation_trace_does_not_change_decisions() {
    std::array<r::Decision, 2> decisions{};
    for (unsigned full = 0; full < 2; ++full) {
        Bot bot(r::Arm::finite_harvest); ordinary_reset(bot);
        bot.runtime_.purchase_quotes_[0] = {0, 30, int(std::uint32_t(cambc::item_type::monster_shard_of_strength)), true, 0, 0, 0};
        if (full) bot.prep_count_ = bot.prep_head_ = unsigned(bot.prep_queue_.size());
        host_preparation_cursor = &bot.preparation_.cursor;
        const auto owners = ordinary_prepare(bot);
        CHECK(owners == 4 && host_preparation_quote_units == 12 && host_exports == 4);
        CHECK(bot.prep_count_ == (full ? bot.prep_queue_.size() : 4u));
        CHECK(bot.prep_owners_ == 4 && bot.prep_samples_ == (full ? 0u : 4u));
        CHECK(!bot.project_.initialized && !bot.runtime_.observe_count_ && bot.runtime_.reference_packet_round_ == -1);
        decisions[full] = bot.choice_; host_preparation_cursor = nullptr;
    }
    const auto& a = decisions[0]; const auto& b = decisions[1];
    CHECK(a.feasible == b.feasible && a.job == b.job && a.profile.arm == b.profile.arm &&
          a.input.round == b.input.round && a.input.windows.window == b.input.windows.window &&
          a.input.windows.deadline == b.input.windows.deadline && a.input.windows.rate == b.input.windows.rate &&
          a.input.windows.official_remaining == b.input.windows.official_remaining &&
          r::preparation_detail::physical(a.input, b.input));
    same_preparation_quote(a.selected, b.selected); same_preparation_quote(a.control, b.control);
}
void ordinary_preparation_trace_has_reserved_bounded_records() {
    Bot bot(r::Arm::finite_harvest); ordinary_reset(bot);
    host_preparation_cursor = &bot.preparation_.cursor;
    for (unsigned owner = 0; owner < 26; ++owner) {
        // Visit the scheduled diagnostic samples without running 1,664 turns.
        // The worker still receives one new SDK owner and changed stock here.
        bot.prep_owners_ = (owner + 1) * 64 - 1; ++host_gold;
        ordinary_owner(bot, owner != 0);
        CHECK(bot.prep_count_ == std::min(owner + 1, 22u) && bot.prep_head_ <= bot.prep_count_);
        CHECK(bot.prep_samples_ == std::min(owner + 1, 22u));
        CHECK(bot.preparation_.status == r::PreparationStatus::incomplete && bot.preparation_.cursor == 3 &&
              bot.preparation_.restarts == owner && !bot.running_ && !bot.choice_.feasible && !host_action_count);
    }
    // Ordinary samples stop at 22. A completed worker below the publication
    // floor and its later publication can each use their reserved record.
    host_preparation_quote_cost = 8000; ordinary_owner(bot, true, 45000);
    CHECK(bot.prep_count_ == 22 && bot.preparation_.cursor == 5 && host_fuel == 29000);
    host_preparation_quote_cost = 30000; ordinary_owner(bot, true, 45000);
    CHECK(bot.prep_count_ == 23 && bot.preparation_.status == r::PreparationStatus::complete &&
          bot.preparation_.cursor == 6 && host_fuel == 15000 && !bot.running_ && !bot.choice_.feasible);
    const auto* complete = preparation_record(bot, host_round); CHECK(complete);
    CHECK(complete->outcome == 2 && complete->cursor == 6 && !complete->calls);
    host_preparation_quote_cost = 0; ordinary_owner(bot);
    CHECK(bot.prep_count_ == bot.prep_queue_.size() && bot.running_ && bot.choice_.feasible && !host_action_count);
    const auto* published = preparation_record(bot, host_round); CHECK(published);
    CHECK(published->outcome == 5 && published->cursor == 6 && published->prefix == bot.choice_.selected.prefix && !published->calls);
    CHECK(bot.prep_samples_ == 22);
    host_preparation_cursor = nullptr;
}
void ordinary_preparation_trace_bounds_detailed_work() {
    Bot bot(r::Arm::finite_harvest); ordinary_reset(bot);
    for (unsigned owner = 0; owner < 50; ++owner) {
        // Every32nd owner is sampled; these intervening samples introduce no
        // new category after the first stock change and do not fill the queue.
        bot.prep_owners_ = (owner + 1) * 64 - 33; ++host_gold;
        ordinary_owner(bot, owner != 0);
        CHECK(bot.prep_samples_ == std::min(owner + 1, 48u) && bot.prep_count_ == std::min(owner + 1, 2u));
        CHECK(bot.preparation_.status == r::PreparationStatus::incomplete && bot.preparation_.cursor == 3 &&
              bot.preparation_.restarts == owner && !bot.running_ && !bot.choice_.feasible && !host_action_count);
    }
    CHECK(ordinary_prepare(bot, false) == 1 && bot.prep_samples_ == 48 && bot.prep_count_ == 3);
    const auto* published = preparation_record(bot, host_round); CHECK(published);
    CHECK(published->outcome == 5 && !published->calls && published->cursor == 6 && !host_action_count);
}
void same_route(const p5::RouteLeg& a, const p5::RouteLeg& b) {
    CHECK(a.destination == b.destination && a.distance == b.distance &&
          a.terrain_and_diagonals_checked == b.terrain_and_diagonals_checked && a.safety_checked == b.safety_checked);
}
void running_matches_full(Bot& bot, const n::Snapshot& facts) {
    // Component oracle after an actual caller refresh: full construction must
    // agree on effective fields. Its preparation-only catalogue is excluded.
    const auto actual = bot.fresh_; r::Observation expected;
    CHECK(bot.build_into(facts, expected));
    CHECK(actual.round == expected.round && actual.team_score == expected.team_score && actual.actual_marcella == expected.actual_marcella);
    CHECK(actual.resources.id == expected.resources.id && actual.resources.self == expected.resources.self &&
          actual.resources.visible == expected.resources.visible && actual.resources.own_team == expected.resources.own_team);
    CHECK(actual.resources.bag.value == expected.resources.bag.value && actual.resources.gold.value == expected.resources.gold.value &&
          actual.resources.bag.current(facts.round) && actual.resources.gold.current(facts.round));
    CHECK(actual.actor.health == expected.actor.health && actual.actor.max_health == expected.actor.max_health &&
          actual.actor.strength == expected.actor.strength && actual.actor.basic_modifier == expected.actor.basic_modifier &&
          actual.actor.visible_enemy_next_phase == expected.actor.visible_enemy_next_phase);
    CHECK(actual.actor.shield == 0 && actual.actor.shield_expiry_tick == 0 && !actual.actor.spell_known_ready && !actual.actor.legal_first_cast &&
          !actual.actor.reserved_cast && !actual.actor.heal_legal_ready && actual.actor.direct_or_reset_damage == 0 && actual.actor.heal_amount == 0);
    CHECK(actual.settings.capture_rounds == expected.settings.capture_rounds && actual.settings.tribute_gems_per_tick == expected.settings.tribute_gems_per_tick &&
          actual.settings.tribute_min_ticks == expected.settings.tribute_min_ticks && actual.settings.round_limit == expected.settings.round_limit &&
          actual.settings.winning_gems == expected.settings.winning_gems && actual.settings.marcella_gold_per_gem == expected.settings.marcella_gold_per_gem);
    CHECK(actual.windows.deadline == expected.windows.deadline && actual.windows.window == expected.windows.window && actual.windows.rate == expected.windows.rate &&
          actual.windows.official_remaining == expected.windows.official_remaining && actual.windows.valid == expected.windows.valid);
    CHECK(actual.ledger.captures == expected.ledger.captures && actual.ledger.banks == expected.ledger.banks && actual.ledger.purchases == expected.ledger.purchases &&
          actual.ledger.consumed == expected.ledger.consumed && actual.ledger.liquidated == expected.ledger.liquidated);
    CHECK(actual.bank_safe == expected.bank_safe && actual.spawn_safe == expected.spawn_safe && actual.spawn_rest == expected.spawn_rest &&
          actual.spawn_heal_per_tick == expected.spawn_heal_per_tick);
    same_route(actual.current_to_altar, expected.current_to_altar); same_route(actual.current_to_spawn, expected.current_to_spawn);
    same_route(actual.altar_to_spawn, expected.altar_to_spawn);
    for (unsigned i = 0; i < actual.groups.size(); ++i) {
        const auto& a = actual.groups[i]; const auto& b = expected.groups[i];
        CHECK(a.id == b.id && a.gems == b.gems && a.gold == b.gold && a.count == b.count &&
              a.current == b.current && a.available == b.available && a.no_other_holder == b.no_other_holder);
        for (std::size_t m = 0; m < a.count; ++m)
            CHECK(a.monsters[m].id == b.monsters[m].id && a.monsters[m].public_health == b.monsters[m].public_health &&
                  a.monsters[m].observed_health == b.monsters[m].observed_health && a.monsters[m].effective_strength == b.monsters[m].effective_strength &&
                  a.monsters[m].visible == b.monsters[m].visible);
        same_route(a.from_current, b.from_current); same_route(a.from_altar, b.from_altar); same_route(a.from_spawn, b.from_spawn);
        same_route(a.to_altar, b.to_altar); same_route(a.to_spawn, b.to_spawn); same_route(a.certificate_return, b.certificate_return);
    }
    CHECK(actual.execution.owner_turn == facts.round && actual.execution.fuel == facts.fuel &&
          actual.execution.remaining_rounds == expected.execution.remaining_rounds && actual.execution.alive == expected.execution.alive &&
          actual.execution.safe == expected.execution.safe && actual.execution.channel == expected.execution.channel &&
          actual.execution.channel_valid == expected.execution.channel_valid && actual.execution.channel_safe == expected.execution.channel_safe);
    CHECK(!actual.execution.move_count && actual.execution.attack_target == -1 && !actual.execution.spells[0].ready &&
          !actual.execution.feedback_sequence && !actual.execution.payout_receipt && !actual.execution.own_payout_observed);
    CHECK(actual.purchase_count == 0 && actual.A_to_B.distance == -1 && expected.purchase_count == 1 && expected.A_to_B.distance >= 0);
    // Restore the real retained packet's cookie after the separate oracle.
    CHECK(bot.build_into(facts, bot.fresh_) && bot.refresh_running(facts, bot.fresh_));
}
void ordinary_running_refresh_replaces_live_fields() {
    Bot bot(r::Arm::finite_harvest); ordinary_reset(bot);
    // A valid zero-stat shop quote is available during preparation; ordinary
    // running packets must omit it, while geometry rebuilds must include it.
    bot.runtime_.purchase_quotes_[0] = {0, 30, int(std::uint32_t(cambc::item_type::monster_shard_of_strength)), true, 0, 0, 0};
    ordinary_prepare(bot); CHECK(bot.fresh_.purchase_count == 1);
    ordinary_owner(bot); CHECK(bot.project_.initialized && bot.fresh_.purchase_count == 0 && bot.geometry_current(bot.snapshot_, bot.fresh_));
    const auto named = bot.fresh_.groups[0].from_altar;
    host_actor.x = 8; host_actor.y = 8; host_actor.health = 900; host_actor.max_health = 1300; host_actor.strength = 61;
    host_bag = 4; host_gold = 9; host_score = 30;
    host_camps[0].health = 33; host_camps[0].max_health = 75; host_camps[0].strength = 2;
    host_camps[3].health = 40; host_camps[3].max_health = 60; host_camps[3].strength = 3;
    host_awake_camps = 9; host_group_state[1] = 3; host_known = 3;
    auto& old = bot.fresh_; old.round = old.team_score = -1; old.resources = {}; old.actor.health = -1; old.actor.shield = 77;
    old.actor.spell_known_ready = old.actor.legal_first_cast = old.actor.reserved_cast = old.actor.heal_legal_ready = true;
    old.actor.direct_or_reset_damage = old.actor.heal_amount = 88; old.settings = {}; old.windows = {}; old.ledger.captures = 99;
    old.spawn_rest = 99; old.spawn_heal_per_tick = -1; old.current_to_altar.distance = old.current_to_spawn.distance = 777;
    old.purchase_count = 3; old.A_to_B.distance = 999; old.execution.attack_target = 999; old.execution.move_count = 1;
    old.execution.spells[0].ready = old.execution.own_payout_observed = true; old.execution.feedback_sequence = old.execution.payout_receipt = 99;
    host_field_cost = 21000; bot.runtime_.position_distances_valid_ = false; ordinary_owner(bot, true, 50000);
    CHECK(bot.geometry_current(bot.snapshot_, bot.fresh_) && bot.runtime_.observe_count_ == 0 && !host_action_count && bot.running_);
    CHECK(bot.fresh_.actor.health == 900 && bot.fresh_.actor.max_health == 1300 && bot.fresh_.actor.strength == 61 && bot.fresh_.actor.visible_enemy_next_phase == 5);
    CHECK(bot.fresh_.resources.bag.value == 4 && bot.fresh_.resources.gold.value == 9 && bot.fresh_.team_score == 30 && !bot.fresh_.spawn_safe);
    CHECK(bot.fresh_.groups[0].count == 2 && bot.fresh_.groups[0].monsters[0].observed_health == 33 && bot.fresh_.groups[0].monsters[1].observed_health == 40);
    CHECK(bot.fresh_.groups[0].from_current.distance == 2 && bot.fresh_.groups[1].current && !bot.fresh_.groups[1].available && !bot.fresh_.groups[2].current);
    same_route(bot.fresh_.groups[0].from_altar, named); running_matches_full(bot, bot.snapshot_);
    host_actor.health = 850; host_bag = 2; host_gold = 11; host_score = 31;
    host_camps[0].health = 0; host_camps[3].health = 17; host_awake_camps = 0; host_group_state[1] = 2; host_known = 7;
    bot.runtime_.position_distances_valid_ = false; ordinary_owner(bot, true, 50000);
    CHECK(bot.snapshot_.groups[1].reward && bot.fresh_.groups[1].current && !bot.fresh_.groups[1].available);
    CHECK(bot.fresh_.actor.visible_enemy_next_phase == 0 && bot.fresh_.spawn_safe && bot.fresh_.groups[0].count == 1 &&
          bot.fresh_.groups[0].monsters[0].id == 104 && bot.fresh_.groups[0].monsters[0].observed_health == 17 && bot.fresh_.groups[2].available);
    running_matches_full(bot, bot.snapshot_);
    // Labeled component mutation: holder observation is compared with the full
    // builder without inventing another SDK champion or a selected Decision.
    auto held = bot.snapshot_; held.groups[0].other_holder = true;
    CHECK(bot.refresh_running(held, bot.fresh_) && !bot.fresh_.groups[0].no_other_holder && !bot.fresh_.groups[0].available);
    running_matches_full(bot, held);
    ++bot.runtime_.geometry_generation_;
    CHECK(!bot.geometry_current(bot.snapshot_, bot.fresh_) && !bot.refresh_running(bot.snapshot_, bot.fresh_) && !bot.geometry_packet_);
    bot.runtime_.position_distances_valid_ = false; ordinary_owner(bot, true, 50000);
    CHECK(bot.geometry_current(bot.snapshot_, bot.fresh_) && bot.packet_geometry_ == bot.runtime_.geometry_generation_);
    CHECK(bot.fresh_.purchase_count == 1 && bot.fresh_.A_to_B.distance >= 0 && !host_action_count && bot.runtime_.observe_count_ == 0);
}
void ordinary_turn_selects_and_dispatches() {
    Bot bot(r::Arm::finite_harvest); ordinary_reset(bot);
    const auto owners = ordinary_prepare(bot);
    CHECK(host_exports == owners);
    CHECK(bot.fixed_ && bot.running_ && bot.choice_.feasible && bot.choice_.job == r::Job::harvest);
    CHECK(bot.project_.selected.selected.prefix == 2 && bot.ids_ == (std::array<int, 3>{1, 2, 3}));
    CHECK(entry_gate(bot, 10, host_round) && !bot.project_.initialized && !host_action_count);
    // Completion publishes a current decision; the following owner binds its
    // scalar phase. Neither unit authorizes an SDK action or a typed packet.
    ordinary_owner(bot);
    CHECK(bot.project_.initialized && bot.goal().phase == e::Phase::travel);
    CHECK(bot.runtime_.observe_count_ == 0 && !host_action_count && !bot.project_.intent);
    const auto travel = bot.goal();
    ordinary_owner(bot);
    CHECK(travel.kind == n::GoalKind::camp_entry && bot.goal().kind == n::GoalKind::camp_attack);
    CHECK(bot.goal().phase == e::Phase::clear && bot.runtime_.observe_count_ == 1 && !host_action_count);
    // The changed camp goal waits for a fresh typed packet on another owner.
    ordinary_owner(bot);
    CHECK(host_action_count == 1 && host_actions[0].round == host_round && host_actions[0].kind == 2 && host_actions[0].target == 101);
    CHECK(bot.runtime_.last_action_ == int(e::ActionKind::attack));
    CHECK(bot.runtime_.observe_count_ == 1 && host_exports == owners + 3);
    CHECK(host_camps[0].health == 50 && host_bag == 0 && host_gold == 0);
    CHECK(!bot.ledger_.captures && !bot.ledger_.banks && !bot.runtime_.own_receipts_.capture);
}
void ordinary_turn_moves_to_selected_entry_then_attacks() {
    Bot bot(r::Arm::finite_harvest); ordinary_reset(bot, 7, 9, 10);
    ordinary_prepare(bot); ordinary_owner(bot);
    CHECK(!host_camp_design_queries); // Visible sleeping members need their live stats, never unused design stats.
    CHECK(bot.snapshot_.group_count == 3 && bot.snapshot_.groups[0].combat.actor_count == 1 &&
          bot.snapshot_.groups[0].combat.monster_count == 1);
    const auto& initial_member = bot.snapshot_.groups[0].combat.monsters[0];
    CHECK(initial_member.id == 101 && initial_member.public_health == 50 && initial_member.observed_health == 50 &&
          initial_member.effective_strength == 0 && initial_member.visible);
    const auto travel = bot.goal();
    CHECK(bot.project_.initialized && travel.kind == n::GoalKind::camp_entry && travel.phase == e::Phase::travel);
    CHECK(travel.group_id == 1 && !host_action_count && !host_move_fields && !host_reachable_queries);
    CHECK(!host_seen(10, 10)); // Hidden point occupancy is false through the pinned SDK wrapper.
    ordinary_owner(bot);
    CHECK(host_action_count == 1 && host_actions[0].round == host_round && host_actions[0].kind == host_move_kind);
    CHECK(host_actions[0].target == 10 * side + 10 && host_actions[0].x == 10 && host_actions[0].y == 10);
    CHECK(host_actor.x == 10 && host_actor.y == 10 && !(host_actor.state & (1 << 3)));
    CHECK(host_move_fields == 1 && host_reachable_queries == 1 && host_point_queries == 2 && bot.runtime_.observe_count_ == 1);
    CHECK(bot.runtime_.execution_quote_reuses_ == 1 && !bot.runtime_.execution_work_calls_);
    CHECK(bot.runtime_.last_action_ == int(e::ActionKind::move) && bot.goal() == travel);
    CHECK(bot.running_ && !bot.closed_ && !bot.project_.pending_replan);
    // The host has accepted the recorded SDK destination. The next engine
    // owner refreshes action availability and observes that actual arrival;
    // no selected Decision, typed packet, or controller phase is supplied.
    host_actor.state = 1 | (1 << 3) | (1 << 4);
    ordinary_owner(bot);
    CHECK(bot.snapshot_.position_tile_id == 10 * int(side) + 10 && bot.goal().kind == n::GoalKind::camp_attack);
    CHECK(bot.goal().phase == e::Phase::clear && bot.runtime_.observe_count_ == 1 && host_action_count == 1);
    CHECK(bot.runtime_.execution_quote_reuses_ == 1 && !bot.runtime_.execution_work_calls_);
    ordinary_owner(bot);
    CHECK(host_action_count == 2 && host_actions[1].round == host_round && host_actions[1].kind == 2 && host_actions[1].target == 101);
    CHECK(bot.runtime_.last_action_ == int(e::ActionKind::attack) && bot.runtime_.observe_count_ == 1);
    CHECK(bot.runtime_.execution_quote_reuses_ == 1 && !bot.runtime_.execution_work_calls_);
    CHECK(host_camps[0].health == 50 && !host_bag && !host_gold && !bot.ledger_.captures && !bot.ledger_.banks);
    CHECK(!host_camp_design_queries); // Existing actual move -> arrival -> attack path still consumes no design fallback.
}
void component_member_design_queries_after_actual_binding() {
    Bot bot(r::Arm::finite_harvest); ordinary_reset(bot, 7, 9, 10);
    ordinary_prepare(bot); ordinary_owner(bot);
    CHECK(bot.project_.initialized && bot.goal().phase == e::Phase::travel && !host_action_count && !host_camp_design_queries);
    auto& runtime = bot.runtime_;
    // Component boundaries follow actual ordinary preparation and binding.
    // Snapshot export still queries the real Runtime and current SDK host facts.
    auto export_packet = [&] {
        ++host_round; host_fuel = 1000000;
        CHECK(runtime.begin_turn() && runtime.snapshot_into(bot.snapshot_));
        CHECK(bot.snapshot_.complete && bot.snapshot_.round == int(host_round) && bot.snapshot_.group_count == 3);
        for (int i = 0; i < bot.snapshot_.group_count; ++i) {
            const auto& g = bot.snapshot_.groups[i];
            CHECK(g.id == i + 1 && g.known && g.combat.actor_count == 1 && g.combat.hold_ticks == 0);
            CHECK(g.combat.actors[0].id == 0 && g.combat.actors[0].health == host_actor.health &&
                  g.combat.actors[0].max_health == host_actor.max_health && g.combat.actors[0].strength == host_actor.strength);
        }
    };
    host_camp_design_health = 67; host_camp_design_strength = 4;
    host_camps[3].health = 17; host_camps[3].max_health = 91; host_camps[3].strength = 6;
    observed_visibility(7); host_vision[word(10, 12)] &= ~bit(10, 12);
    auto before = host_camp_design_queries; export_packet();
    CHECK(host_camp_design_queries - before == 1); // Only hidden104 needs the public design fallback.
    const auto& mixed = bot.snapshot_.groups[0].combat;
    CHECK(mixed.monster_count == 2);
    CHECK(mixed.monsters[0].id == 101 && mixed.monsters[0].public_health == 50 && mixed.monsters[0].observed_health == 50 &&
          mixed.monsters[0].effective_strength == 0 && mixed.monsters[0].visible);
    CHECK(mixed.monsters[1].id == 104 && mixed.monsters[1].public_health == 67 && mixed.monsters[1].observed_health == 67 &&
          mixed.monsters[1].effective_strength == 4 && !mixed.monsters[1].visible);
    CHECK(bot.snapshot_.groups[1].combat.monster_count == 1 && bot.snapshot_.groups[2].combat.monster_count == 1);

    observed_visibility(7); host_camps[0].health = 31; host_camps[0].max_health = 77; host_camps[0].strength = 3;
    before = host_camp_design_queries; export_packet();
    CHECK(host_camp_design_queries == before);
    const auto& visible = bot.snapshot_.groups[0].combat;
    CHECK(visible.monster_count == 2);
    CHECK(visible.monsters[0].id == 101 && visible.monsters[0].public_health == 77 && visible.monsters[0].observed_health == 31 &&
          visible.monsters[0].effective_strength == 3 && visible.monsters[0].visible);
    CHECK(visible.monsters[1].id == 104 && visible.monsters[1].public_health == 91 && visible.monsters[1].observed_health == 17 &&
          visible.monsters[1].effective_strength == 6 && visible.monsters[1].visible);

    host_camps[3].health = 0;
    before = host_camp_design_queries; export_packet();
    CHECK(host_camp_design_queries == before && bot.snapshot_.groups[0].combat.monster_count == 1);
    CHECK(bot.snapshot_.groups[0].combat.monsters[0].id == 101 && bot.snapshot_.groups[0].combat.monsters[0].observed_health == 31);

    host_group_state[0] = 2; host_group_state[1] = 3;
    host_vision[word(10, 12)] &= ~bit(10, 12);
    before = host_camp_design_queries; export_packet();
    CHECK(host_camp_design_queries == before); // Reward/respawn groups do not represent hidden members.
    CHECK(bot.snapshot_.groups[0].reward && !bot.snapshot_.groups[0].respawning && !bot.snapshot_.groups[0].combat.monster_count);
    CHECK(!bot.snapshot_.groups[1].reward && bot.snapshot_.groups[1].respawning && !bot.snapshot_.groups[1].combat.monster_count);
    CHECK(!bot.snapshot_.groups[2].reward && !bot.snapshot_.groups[2].respawning && bot.snapshot_.groups[2].combat.monster_count == 1);
    const auto& remaining = bot.snapshot_.groups[2].combat.monsters[0];
    CHECK(remaining.id == 103 && remaining.public_health == 50 && remaining.observed_health == 50 &&
          remaining.effective_strength == 0 && remaining.visible);

    host_group_state = {{1, 1, 1}}; observed_visibility(7); host_awake_camps = 1;
    host_camps[0].health = 29; host_camps[0].max_health = 79; host_camps[0].strength = 5;
    before = host_camp_design_queries; export_packet();
    CHECK(host_camp_design_queries - before == 1); // Awake101 still needs its design attack_range for danger.
    CHECK(bot.snapshot_.route_stress == 5 && bot.snapshot_.next_incoming == 5);
    const auto& awake = bot.snapshot_.groups[0].combat;
    CHECK(awake.monster_count == 1 && awake.monsters[0].id == 101 && awake.monsters[0].public_health == 79 &&
          awake.monsters[0].observed_health == 29 && awake.monsters[0].effective_strength == 5 && awake.monsters[0].visible);
    CHECK(!host_action_count); // These labeled packet cases authorize no action.
}
e::Observation original_execution_work(n::Runtime& runtime, const e::State& state, const n::Goal& goal,
                                      p5::EstimateState& work_state) {
    // Independent original full-work path on the actual current frame. No
    // quote or quote-matching predicate participates in this oracle; its
    // recovery latch and diagnostic counts are restored before the real call.
    auto out = runtime.observe_state(state, goal);
    CHECK(out.alive && out.channel == e::Channel::none && goal.production && goal.forecast_turns >= 0);
    CHECK(goal.kind != n::GoalKind::recover && !(out.group_cleared && out.on_group_spawn && out.safe));
    const auto* target = runtime.group(goal.group_id); CHECK(target && !runtime.target_combat_current(goal));
    const double threat = runtime.external_stress(target);
    const double health = cambc::self().health() - threat * goal.forecast_turns;
    const bool reserve = health > 0 && health >= std::max(.2 * cambc::self().max_health(), 2 * threat);
    work_state = runtime.work(*target, health, false, false).state;
    if (!reserve || work_state != p5::EstimateState::feasible) {
        out.safe = false; out.needs_recovery = true; runtime.recovering_ = true;
    }
    // The owner-round component case changes the round before either call;
    // neither call crosses a round internally. Apply the original phase-end
    // admission result without invalidating the frame needed by the real call.
    if (int(cambc::round_number()) != runtime.round_) out.can_act = out.safe = false;
    out.fuel = int(cambc::fuel_remaining()); return out;
}
void component_execution_quote_after_actual_binding() {
    enum class Case { changed_health, first_actor_threat, unknown, respawning, reward,
                      target_mismatch, unsafe_equal, actor_epoch, actor_round, owner_round };
    constexpr std::array cases{Case::changed_health, Case::first_actor_threat, Case::unknown, Case::respawning,
        Case::reward, Case::target_mismatch, Case::unsafe_equal, Case::actor_epoch, Case::actor_round, Case::owner_round};
    for (const auto test : cases) {
        Bot bot(r::Arm::finite_harvest); ordinary_reset(bot, 7, 9, 10);
        ordinary_prepare(bot); ordinary_owner(bot);
        // Labeled component boundary: ordinary preparation and binding precede
        // a real SDK refresh. Only the named forecast input is then perturbed.
        ++host_round; host_fuel = 60000; observed_visibility(host_known);
        auto& runtime = bot.runtime_;
        CHECK(runtime.begin_turn() && runtime.snapshot_into(bot.snapshot_) && runtime.prepare_selection());
        bot.snapshot_.fuel = int(host_fuel); CHECK(bot.refresh_running(bot.snapshot_, bot.fresh_));
        auto state = bot.project_.executor; const auto goal = bot.goal();
        CHECK(goal.kind == n::GoalKind::camp_entry && goal.group_id == 1 && state.target == 1);
        CHECK(runtime.frame_actor_valid_ && runtime.frame_actor_round_ == runtime.round_ && runtime.frame_actor_epoch_ == runtime.epoch_);
        CHECK(!runtime.execution_quote_reuses_ && !runtime.execution_work_calls_ && !runtime.observe_count_);
        auto& target = runtime.groups_[0];
        switch (test) {
        case Case::changed_health: host_actor.health = 200; break;
        case Case::first_actor_threat: runtime.frame_actor_.visible_enemy_next_phase = 1000; break;
        case Case::unknown: target.known = false; break;
        case Case::respawning: target.respawning = true; target.combat.monster_count = 0; break;
        case Case::reward: target.reward = true; target.combat.monster_count = 0; break;
        case Case::target_mismatch: state.target = 2; break;
        case Case::unsafe_equal: target.combat.monsters[0].effective_strength = 1200; break;
        case Case::actor_epoch: --runtime.frame_actor_epoch_; break;
        case Case::actor_round: --runtime.frame_actor_round_; break;
        case Case::owner_round: ++host_round; break;
        }
        const auto recovery_before = runtime.recovering_;
        const auto observes_before = runtime.observe_count_, actor_queries_before = runtime.actor_count_;
        p5::EstimateState work_state;
        const auto expected = original_execution_work(runtime, state, goal, work_state);
        const auto expected_recovery = runtime.recovering_;
        const bool expected_aborted = runtime.phase_aborted_ || int(host_round) != runtime.round_;
        runtime.recovering_ = recovery_before; runtime.observe_count_ = observes_before; runtime.actor_count_ = actor_queries_before;
        const auto actual = runtime.execution_observation(state, goal);
        CHECK(actual.safe == expected.safe && actual.needs_recovery == expected.needs_recovery);
        CHECK(actual.can_act == expected.can_act && actual.can_move == expected.can_move && actual.can_attack == expected.can_attack);
        CHECK(actual.group_id == expected.group_id && actual.group_live_known == expected.group_live_known &&
              actual.group_cleared == expected.group_cleared && actual.group_respawning == expected.group_respawning);
        CHECK(actual.owner_turn == expected.owner_turn && actual.observation_epoch == expected.observation_epoch && actual.group_count == expected.group_count);
        for (std::size_t i = 0; i < actual.group_count; ++i) {
            CHECK(actual.groups[i].id == expected.groups[i].id && actual.groups[i].assigned == expected.groups[i].assigned &&
                  actual.groups[i].solo_safe == expected.groups[i].solo_safe && actual.groups[i].unknown == expected.groups[i].unknown);
        }
        CHECK(runtime.recovering_ == expected_recovery && runtime.phase_aborted_ == expected_aborted);
        CHECK(runtime.execution_quote_reuses_ == unsigned(test == Case::unsafe_equal));
        CHECK(runtime.execution_work_calls_ == unsigned(test != Case::unsafe_equal));
        if (test == Case::unsafe_equal) CHECK(work_state == p5::EstimateState::unsafe && !actual.safe && actual.needs_recovery);
        if (test == Case::changed_health || test == Case::unknown || test == Case::respawning)
            CHECK(work_state != p5::EstimateState::feasible && !actual.safe && actual.needs_recovery);
        if (test == Case::reward || test == Case::target_mismatch || test == Case::actor_epoch || test == Case::actor_round)
            CHECK(work_state == p5::EstimateState::feasible && actual.safe && !actual.needs_recovery);
        if (test == Case::first_actor_threat) CHECK(work_state == p5::EstimateState::feasible && !actual.safe && actual.needs_recovery);
        if (test == Case::owner_round) CHECK(!actual.can_act && !actual.safe && runtime.phase_aborted_);
        CHECK(!host_action_count && !host_move_fields && !host_reachable_queries);
    }
}
void ordinary_apply_budget_defers_and_retries(bool capped = false) {
    Bot bot(r::Arm::finite_harvest); ordinary_reset(bot, 7, 9, 10);
    ordinary_prepare(bot); ordinary_owner(bot);
    CHECK(bot.project_.initialized && bot.goal().phase == e::Phase::travel && !host_reachable_queries);
    const auto phase = bot.project_.executor; const auto ledger = bot.ledger_;
    const auto index = bot.project_.index, bag = bot.project_.bag, gold = bot.project_.gold;
    const auto capture_base = bot.project_.capture_base, bank_base = bot.project_.bank_base, purchase_base = bot.project_.purchase_base;
    const auto selected_job = bot.project_.selected.job; const auto selected_prefix = bot.project_.selected.selected.prefix;
    const auto cash = bot.cash_attempt_;
    if (capped) bot.apply_attempts_ = 32;
    // Synthetic charge on the fresh landing occupancy query: planning and
    // native admission see 60k, then actual revalidation leaves 15k. This is
    // a caller-path witness, not an engine instruction/fuel qualification.
    host_point_charge_call = 1; host_point_cost = 45000;
    ordinary_owner(bot, true, 60000);
    const auto* record = apply_record(bot, n::ApplyExit::post_move_fuel, host_round);
    if (capped) CHECK(!record && !bot.apply_count_ && bot.apply_attempts_ == 32);
    else {
        CHECK(record && record->flags == 5 && bot.apply_count_ == 1 && bot.apply_attempts_ == 1);
        const auto& trace = record->trace;
        CHECK(trace.result == n::ApplyResult::stopped && trace.entry_round == int(host_round) && trace.exit_round == int(host_round));
        CHECK(trace.entry_fuel == 60000 && trace.move_fuel == 15000 && trace.exit_fuel == 15000);
        CHECK(trace.kind == e::ActionKind::move && trace.target == 10 * int(side) + 10 && trace.sequence == bot.project_.executor.intent_sequence);
        CHECK(!trace.native_called && !trace.feedback_matches && !trace.feedback_success && !trace.feedback_sequence);
    }
    CHECK(host_fuel == 15000 && !bot.runtime_.feedback_.feedback_sequence && !bot.buy_attempt_ && bot.cash_attempt_ == cash);
    CHECK(host_reachable_queries == 1 && host_point_queries == 2 && host_move_fields == 1 && !host_action_count && bot.runtime_.observe_count_ == 1);
    CHECK(host_actor.x == 9 && host_actor.y == 10 && (host_actor.state & (1 << 3)) && bot.snapshot_.epoch == bot.runtime_.epoch_);
    CHECK(!entry_gate(bot, 24, host_round) && entry_gate(bot, 11, host_round));
    CHECK(bot.running_ && !bot.closed_ && !bot.fallback_pending_ && bot.project_.initialized && !bot.project_.pending_replan);
    CHECK(bot.project_.executor.phase == phase.phase && bot.project_.executor.phase_start == phase.phase_start && bot.project_.executor.phase_budget == phase.phase_budget);
    CHECK(bot.project_.executor.consumed_payout == phase.consumed_payout && bot.project_.executor.consumed_feedback == phase.consumed_feedback);
    CHECK(bot.project_.index == index && bot.project_.bag == bag && bot.project_.gold == gold && bot.project_.selected.job == selected_job && bot.project_.selected.selected.prefix == selected_prefix);
    CHECK(bot.project_.capture_base == capture_base && bot.project_.bank_base == bank_base && bot.project_.purchase_base == purchase_base);
    CHECK(bot.ledger_.captures == ledger.captures && bot.ledger_.banks == ledger.banks && bot.ledger_.purchases == ledger.purchases && bot.ledger_.consumed == ledger.consumed && bot.ledger_.liquidated == ledger.liquidated);
    const auto deferred_sequence = bot.project_.executor.intent_sequence;
    CHECK(deferred_sequence == phase.intent_sequence + 1);
    // Query charge was one-shot. A fresh owner retries the same live project,
    // preserving its original deadline and using a later action sequence.
    ordinary_owner(bot, true, 60000);
    CHECK(host_action_count == 1 && host_actions[0].round == host_round && host_actions[0].kind == host_move_kind && host_actions[0].target == 10 * side + 10);
    CHECK(host_actor.x == 10 && host_actor.y == 10 && host_reachable_queries == 2 && host_point_queries == 4 && host_move_fields == 2);
    CHECK(bot.runtime_.feedback_.feedback_success && bot.runtime_.feedback_.feedback_sequence == bot.project_.executor.intent_sequence && bot.project_.executor.intent_sequence > deferred_sequence);
    CHECK(bot.running_ && !bot.closed_ && !bot.fallback_pending_ && bot.project_.executor.phase == phase.phase &&
          bot.project_.executor.phase_start == phase.phase_start && bot.project_.executor.phase_budget == phase.phase_budget);
    CHECK(bot.ledger_.captures == ledger.captures && bot.ledger_.banks == ledger.banks && bot.ledger_.purchases == ledger.purchases && bot.ledger_.consumed == ledger.consumed && bot.ledger_.liquidated == ledger.liquidated);
    if (capped) CHECK(!bot.apply_count_ && bot.apply_attempts_ == 32);
    else CHECK(apply_record(bot, n::ApplyExit::acted, host_round) && bot.apply_count_ == 2);
}
void ordinary_hidden_move_failure_has_matching_feedback() {
    Bot bot(r::Arm::finite_harvest); ordinary_reset(bot, 7, 9, 10);
    ordinary_prepare(bot); ordinary_owner(bot); host_move_succeeds = false;
    ordinary_owner(bot, true, 60000);
    const auto* record = apply_record(bot, n::ApplyExit::acted, host_round);
    CHECK(record && record->trace.result == n::ApplyResult::acted && record->trace.native_called);
    const auto& trace = record->trace;
    CHECK(trace.kind == e::ActionKind::move && trace.feedback_matches && !trace.feedback_success);
    CHECK(trace.feedback_sequence == trace.sequence && trace.feedback_kind == e::ActionKind::move && trace.feedback_target == trace.target);
    CHECK(trace.entry_fuel == 60000 && trace.move_fuel == 60000 && trace.exit_fuel == 60000);
    CHECK(host_action_count == 1 && host_actions[0].kind == host_move_kind && host_actions[0].target == 10 * side + 10);
    CHECK(host_actor.x == 9 && host_actor.y == 10 && (host_actor.state & (1 << 3)));
    CHECK(bot.running_ && !bot.closed_ && !bot.fallback_pending_ && bot.snapshot_.epoch + 1 == bot.runtime_.epoch_);
    CHECK(!apply_record(bot, n::ApplyExit::post_move_fuel, host_round) && !apply_record(bot, n::ApplyExit::move_binding, host_round));
}
void component_stale_binding_after_actual_decision() {
    Bot bot(r::Arm::finite_harvest); ordinary_reset(bot, 7, 9, 10);
    ordinary_prepare(bot); ordinary_owner(bot);
    // Explicit component boundary after ordinary selection and binding. SDK
    // exports and the real policy produce this movement decision; only its
    // retained binding epoch is then made stale before typed apply admission.
    ++host_round; host_fuel = 60000; observed_visibility(host_known);
    CHECK(bot.runtime_.begin_turn() && bot.runtime_.snapshot_into(bot.snapshot_) && bot.runtime_.prepare_selection());
    bot.snapshot_.fuel = int(host_fuel); CHECK(bot.refresh_running(bot.snapshot_, bot.fresh_));
    const auto goal = bot.goal(); auto observed = bot.runtime_.execution_observation(bot.project_.executor, goal);
    bot.runtime_.add_moves(observed, bot.project_.executor, goal);
    const auto action = bot.advance(bot.snapshot_, observed);
    CHECK(action.action.kind == e::ActionKind::move && action.action.target == 10 * int(side) + 10 && bot.runtime_.move_binding_.valid);
    ++bot.runtime_.move_binding_.epoch;
    n::ApplyTrace trace; auto budget = n::ApplyBudgetDeferral::entry;
    CHECK(bot.runtime_.apply(action, observed, bot.project_.executor, goal, &trace, &budget) == n::ApplyResult::stale);
    CHECK(budget == n::ApplyBudgetDeferral::none);
    CHECK(trace.exit == n::ApplyExit::move_binding && trace.result == n::ApplyResult::stale && !trace.native_called);
    CHECK(trace.entry_fuel == 60000 && trace.move_fuel == -1 && trace.exit_fuel == 60000 && !trace.feedback_matches && !trace.feedback_sequence);
    CHECK(trace.kind == e::ActionKind::move && trace.sequence == action.action.sequence && trace.target == action.action.target);
    CHECK(host_reachable_queries == 1 && !host_action_count && host_actor.x == 9 && host_actor.y == 10);
    CHECK(bot.runtime_.epoch_ == observed.observation_epoch && !bot.runtime_.feedback_.feedback_sequence);
}
void component_point_admission_after_actual_decision() {
    enum class Case { champion, camp, teleport, geometry, owner_round };
    constexpr std::array cases{Case::champion, Case::camp, Case::teleport, Case::geometry, Case::owner_round};
    for (const auto test : cases) {
        Bot bot(r::Arm::finite_harvest); ordinary_reset(bot, 7, 9, 10);
        ordinary_prepare(bot); ordinary_owner(bot);
        // Labeled component boundary: current SDK observations, typed routing
        // and the actual policy bind this move before any landing fact changes.
        ++host_round; host_fuel = 60000; observed_visibility(host_known);
        auto& runtime = bot.runtime_;
        CHECK(runtime.begin_turn() && runtime.snapshot_into(bot.snapshot_) && runtime.prepare_selection());
        bot.snapshot_.fuel = int(host_fuel); CHECK(bot.refresh_running(bot.snapshot_, bot.fresh_));
        const auto goal = bot.goal(); auto observed = runtime.execution_observation(bot.project_.executor, goal);
        runtime.add_moves(observed, bot.project_.executor, goal);
        const auto action = bot.advance(bot.snapshot_, observed);
        CHECK(action.action.kind == e::ActionKind::move && action.action.target == 10 * int(side) + 10);
        CHECK(runtime.move_binding_.valid && runtime.move_binding_.geometry == runtime.geometry_generation_);
        CHECK(host_reachable_queries == 1 && !host_point_queries && !host_action_count);
        const auto decision_round = host_round;
        host_vision[word(10, 10)] |= bit(10, 10);
        switch (test) {
        case Case::champion: host_champion_tiles[word(10, 10)] |= bit(10, 10); break;
        case Case::camp: host_camps[3].x = host_camps[3].y = 10; host_camps[3].health = 50; break;
        case Case::teleport: host_teleport_tiles[word(10, 10)] |= bit(10, 10); break;
        case Case::geometry: ++runtime.geometry_generation_; break;
        case Case::owner_round: host_cross_on_point = 1; break;
        }
        n::ApplyTrace trace; auto budget = n::ApplyBudgetDeferral::entry;
        CHECK(runtime.apply(action, observed, bot.project_.executor, goal, &trace, &budget) == n::ApplyResult::stale);
        const auto exit = test == Case::geometry ? n::ApplyExit::move_binding :
            test == Case::owner_round ? n::ApplyExit::round : n::ApplyExit::move_legality;
        CHECK(budget == n::ApplyBudgetDeferral::none && trace.exit == exit && !trace.native_called);
        CHECK(trace.kind == e::ActionKind::move && trace.target == action.action.target && trace.sequence == action.action.sequence);
        CHECK(trace.entry_round == int(decision_round) && trace.exit_round == int(host_round));
        CHECK(trace.entry_fuel == 60000 && trace.exit_fuel == 60000 && trace.move_fuel == (test == Case::owner_round ? 60000 : -1));
        CHECK(host_point_queries == (test == Case::geometry ? 0u : test == Case::champion ? 1u : 2u));
        CHECK(host_round == decision_round + unsigned(test == Case::owner_round));
        CHECK(host_reachable_queries == 1 && !host_action_count && host_actor.x == 9 && host_actor.y == 10);
        CHECK(runtime.epoch_ == observed.observation_epoch && !runtime.feedback_.feedback_sequence && !trace.feedback_matches);
        CHECK(!bot.ledger_.captures && !bot.ledger_.banks && !bot.ledger_.purchases);
    }
}
void component_entry_budget_after_actual_decision() {
    Bot bot(r::Arm::finite_harvest); ordinary_reset(bot, 7, 9, 10);
    ordinary_prepare(bot); ordinary_owner(bot);
    // Labeled Runtime-entry component boundary: actual preparation, SDK facts,
    // typed routing and policy produced the decision before the synthetic fuel
    // drop. No host query models the intervening native C++ instruction cost.
    ++host_round; host_fuel = 60000; observed_visibility(host_known);
    CHECK(bot.runtime_.begin_turn() && bot.runtime_.snapshot_into(bot.snapshot_) && bot.runtime_.prepare_selection());
    bot.snapshot_.fuel = int(host_fuel); CHECK(bot.refresh_running(bot.snapshot_, bot.fresh_));
    const auto goal = bot.goal(); auto observed = bot.runtime_.execution_observation(bot.project_.executor, goal);
    bot.runtime_.add_moves(observed, bot.project_.executor, goal);
    const auto action = bot.advance(bot.snapshot_, observed); const auto phase = bot.project_.executor;
    CHECK(action.action.kind == e::ActionKind::move && action.action.target == 10 * int(side) + 10);
    host_fuel = 15999; n::ApplyTrace trace; auto budget = n::ApplyBudgetDeferral::after_move;
    CHECK(bot.runtime_.apply(action, observed, bot.project_.executor, goal) == n::ApplyResult::stopped);
    CHECK(bot.runtime_.apply(action, observed, bot.project_.executor, goal, &trace, &budget) == n::ApplyResult::stopped);
    CHECK(budget == n::ApplyBudgetDeferral::entry && trace.exit == n::ApplyExit::entry_fuel && !trace.native_called);
    CHECK(trace.entry_fuel == 15999 && trace.move_fuel == -1 && trace.exit_fuel == 15999 && !trace.feedback_matches && !trace.feedback_sequence);
    CHECK(host_reachable_queries == 1 && !host_action_count && host_actor.x == 9 && host_actor.y == 10 && bot.runtime_.epoch_ == observed.observation_epoch);
    CHECK(bot.project_.executor.phase == phase.phase && bot.project_.executor.phase_start == phase.phase_start &&
          bot.project_.executor.phase_budget == phase.phase_budget && bot.project_.executor.intent_sequence == phase.intent_sequence);
}
std::uint64_t ordinary_capture_pending(Bot& bot) {
    ordinary_prepare(bot); ordinary_owner(bot); ordinary_owner(bot); ordinary_owner(bot);
    CHECK(host_action_count == 1 && bot.project_.executor.phase == e::Phase::clear);
    host_actor.x = 10; host_actor.y = 11;
    host_group_state[0] = 2; host_camps[0].health = host_camps[3].health = 0;
    for (unsigned hold = 0; hold < 5; ++hold) {
        ordinary_owner(bot);
        CHECK(bot.snapshot_.safe_hold && bot.running_ && !bot.project_.pending_replan);
        CHECK(!bot.runtime_.observe_count_ && host_action_count == 1 && !bot.ledger_.captures);
    }
    // Engine observations now provide an actual payout after consecutive final
    // stock samples. No receipt, decision or controller completion is supplied.
    host_group_state[0] = 3; host_bag = 30; host_gold = 20;
    const auto routes = host_move_fields, reachable = host_reachable_queries;
    ordinary_owner(bot);
    CHECK(bot.capture_.exact && bot.capture_.round == int(host_round) && bot.capture_.group == 1);
    CHECK(bot.ledger_.captures == 1 && bot.ledger_.consumed == 1 && !bot.ledger_.banks);
    CHECK(bot.project_.pending_replan && bot.project_.must_vacate && bot.vacate_source_ == 0);
    CHECK(host_move_fields == routes && host_reachable_queries == reachable && !entry_gate(bot, 22, host_round));
    const auto receipt = bot.project_.executor.consumed_payout;
    CHECK(receipt && host_action_count == 1 && bot.runtime_.observe_count_ <= 1);
    return receipt;
}
void ordinary_capture_defers_replan_and_preserves_exit() {
    Bot bot(r::Arm::finite_harvest); ordinary_reset(bot);
    const auto receipt = ordinary_capture_pending(bot);
    unsigned owners = 0;
    for (; owners < 256 && bot.project_.pending_replan; ++owners) {
        ordinary_owner(bot);
        CHECK(bot.fresh_.A_to_B.distance >= 0); // Pending replans receive the full preparation packet.
        CHECK(host_action_count == 1 && bot.running_ && !bot.closed_);
        CHECK(!bot.runtime_.observe_count_ && bot.ledger_.captures == 1 && !bot.ledger_.banks);
        CHECK(bot.project_.must_vacate && bot.project_.executor.consumed_payout == receipt);
    }
    CHECK(owners > 1 && owners < 256 && !bot.project_.pending_replan && !bot.project_.initialized);
    CHECK(bot.goal().production && bot.goal().group_id == 2);
    CHECK(bot.project_.selected.input.ledger.captures == 1 && !bot.project_.selected.input.groups[0].available);
    ordinary_owner(bot);
    CHECK(bot.project_.initialized && bot.project_.executor.target == 2 && bot.project_.must_vacate);
    CHECK(bot.project_.executor.consumed_payout == receipt && !bot.project_.intent);
    CHECK(!bot.runtime_.observe_count_ && host_action_count == 1 && host_bag == 30 && host_gold == 20);
}
void abandoned_pending_after_liquidation_uses_current_ledger() {
    Bot bot(r::Arm::finite_harvest); ordinary_reset(bot);
    ordinary_prepare(bot);
    // Explicit post-liquidation component boundary: canonical accounting has
    // accepted the bank while the abandoned Project still has its older
    // capture ledger and pending flag. No channel or bank receipt is emulated.
    // Both initial and replacement Decisions come from actual Bot::turn work.
    bot.ledger_ = {1, 1, 0, 1, true};
    bot.project_.ledger = {1, 0, 0, 1, false};
    bot.project_.pending_replan = bot.project_.initialized = bot.project_.must_vacate = true;
    bot.project_.executor.consumed_payout = 1;
    bot.running_ = false; bot.closed_ = true;
    host_bag = 0; host_gold = 80; host_score = 30;
    auto& runtime = bot.runtime_;
    CHECK(!bot.liquidating_ && !bot.running_ && !bot.done_ && bot.project_.pending_replan);
    CHECK(bot.ledger_.captures == 1 && bot.ledger_.consumed == 1 && bot.ledger_.banks == 1 && bot.ledger_.liquidated);
    CHECK(bot.project_.ledger.banks == 0 && !bot.project_.ledger.liquidated);
    unsigned owners = 0;
    for (; owners < 256 && !bot.running_; ++owners) {
        ordinary_owner(bot);
        CHECK(!host_action_count && !runtime.observe_count_ && runtime.reference_packet_round_ == -1);
        CHECK(bot.ledger_.banks == 1 && bot.ledger_.liquidated && bot.ledger_.captures == 1);
    }
    CHECK(owners >= 1 && owners < 256 && bot.running_ && !bot.project_.pending_replan && !bot.project_.initialized);
    CHECK(bot.project_.selected.input.ledger.banks == 1 && bot.project_.selected.input.ledger.liquidated);
    CHECK(bot.project_.ledger.banks == 1 && bot.project_.ledger.liquidated && bot.project_.ledger.captures == 1);
    ordinary_owner(bot);
    CHECK(bot.project_.initialized && !bot.project_.intent && !runtime.observe_count_ && !host_action_count);
    CHECK(bot.ledger_.banks == 1 && bot.ledger_.liquidated && bot.ledger_.captures == 1 && bot.ledger_.consumed == 1);
}
void ordinary_visibility_exposes_reference_entry_gate() {
    Bot bot(r::Arm::finite_harvest); ordinary_reset(bot, 0);
    // Partial-visibility samples are observed after the host marks this body
    // complete. Reference may quote a provisional production plan, but cannot
    // turn unavailable actions into SDK movement or attacks.
    host_actor.state = 1 | (1 << 5); bot.turn();
    CHECK(bot.snapshot_.complete && known_count(bot) == 0 && !bot.fixed_ && !bot.running_);
    CHECK(entry_gate(bot, 7, 100) && !host_action_count);
    host_round = 101; host_fuel = 1000000; observed_visibility(3); bot.turn();
    CHECK(bot.snapshot_.complete && known_count(bot) == 2 && !bot.fixed_ && !bot.running_);
    CHECK(bot.runtime_.stage_ == n::Stage::production && bot.runtime_.plan_.feasible);
    CHECK(!host_action_count && !bot.choice_.feasible);
    host_round = 102; host_fuel = 1000000; host_actor.state = 1 | (1 << 3) | (1 << 4); observed_visibility(7); bot.turn();
    CHECK(bot.snapshot_.complete && known_count(bot) == 3 && host_exports == 3);
    // Expected baseline failure: a provisional Reference production stage
    // prevents this ordinary caller from reaching its actual Runoff selector.
    CHECK(entry_gate(bot, 5, 102) && !bot.fixed_ && !bot.running_ && !bot.choice_.feasible);
    CHECK(host_action_count == 1 && host_actions[0].round == 102 && host_actions[0].kind == 2);
}
void ordinary_host_cost_defers_without_reference() {
    Bot bot(r::Arm::finite_harvest); ordinary_reset(bot);
    host_fuel = 100000; host_export_cost = 33000; host_field_cost = 8500;
    bot.runtime_.position_distances_valid_ = false;
    bot.turn();
    CHECK(bot.snapshot_.complete && known_count(bot) == 3 && host_exports == 1 && host_fields == 1);
    CHECK(bot.gate_current_.fuel[0] == 100000 && bot.gate_current_.fuel[1] == 67000);
    CHECK(bot.gate_current_.fuel[2] == 58500 && bot.gate_current_.fuel[3] == 58500);
    // The worker begins from this actual caller packet and yields without
    // handing provisional work to Reference. Host query charges are synthetic;
    // they do not model Wasm instruction fuel or establish the native budget.
    CHECK(!bot.fixed_ && !bot.running_ && !bot.choice_.feasible);
    CHECK(!bot.runtime_.observe_count_ && bot.runtime_.reference_packet_round_ == -1);
    CHECK(!host_action_count && host_fuel == 58500);
    ordinary_prepare(bot, false, 100000);
    CHECK(bot.choice_.feasible && !bot.project_.initialized && !host_action_count);
}
void ordinary_postbuild_fuel_defers_typed_observation() {
    Bot bot(r::Arm::finite_harvest); ordinary_reset(bot);
    ordinary_prepare(bot); ordinary_owner(bot);
    CHECK(bot.project_.initialized && bot.goal().phase == e::Phase::travel && !host_action_count);
    const auto goal = bot.goal(); const auto phase = bot.project_.executor;
    const auto fields = host_fields, exports = host_exports;
    // These host query charges reproduce a current owner return path. They
    // are synthetic costs, not Wasm instruction costs or engine fuel proof.
    host_field_cost = 21000; bot.runtime_.position_distances_valid_ = false;
    ordinary_owner(bot, true, 50000);
    const auto* trace = entry_trace(bot, 21, host_round);
    CHECK(trace && trace->phase == int(e::Phase::travel));
    CHECK(trace->phase_start == phase.phase_start && trace->phase_budget == phase.phase_budget);
    CHECK(trace->fuel == (std::array<int, 6>{{50000, 50000, 29000, 29000, 29000, 29000}}));
    CHECK((trace->flags & 7) == 5 && trace->action == -1 && trace->reason == -1 && trace->moves == -1);
    CHECK(host_fields == fields + 1 && host_exports == exports + 1 && host_fuel == 29000);
    CHECK(bot.snapshot_.epoch == bot.runtime_.epoch_ && bot.fresh_.execution.owner_turn == int(host_round));
    CHECK(bot.fresh_.execution.fuel == 29000 && bot.runtime_.observe_count_ == 0 && !host_action_count);
    CHECK(bot.goal() == goal && bot.project_.executor.phase == phase.phase);
    CHECK(bot.project_.executor.phase_start == phase.phase_start && bot.project_.executor.phase_budget == phase.phase_budget);
    CHECK(bot.running_ && !bot.closed_ && bot.project_.initialized && !bot.project_.pending_replan && !bot.project_.intent);
    host_field_cost = 0; ordinary_owner(bot);
    CHECK(bot.runtime_.observe_count_ == 1 && bot.goal().phase == e::Phase::clear && !host_action_count);
}
void ordinary_typed_fuel_defers_movement() {
    Bot bot(r::Arm::finite_harvest); ordinary_reset(bot, 7, 9, 10);
    ordinary_prepare(bot); ordinary_owner(bot);
    CHECK(bot.project_.initialized && bot.goal().phase == e::Phase::travel && !host_action_count);
    const auto goal = bot.goal(); const auto phase = bot.project_.executor;
    const auto restrictions = host_restrictions, fields = host_fields;
    // Restrictions are queried by the real typed observation. The host-only
    // cost forces the prefetch skip without changing available actions.
    host_restrictions_cost = 41000;
    ordinary_owner(bot, true, 60000);
    const auto* observed = entry_trace(bot, 17, host_round);
    const auto* advanced = entry_trace(bot, 18, host_round);
    const auto* trace = entry_trace(bot, 23, host_round);
    const auto* applied = entry_trace(bot, 11, host_round);
    CHECK(observed && advanced && trace && observed->moves == 0 && advanced->moves == 0);
    CHECK(trace->phase == int(e::Phase::travel) && trace->phase_start == phase.phase_start && trace->phase_budget == phase.phase_budget);
    CHECK(trace->fuel == (std::array<int, 6>{{60000, 60000, 60000, 60000, 19000, 19000}}));
    CHECK((trace->flags & 7) == 5 && trace->action == -1 && trace->reason == -1 && trace->moves == 0);
    CHECK(advanced->action == int(e::ActionKind::yield) && advanced->reason == int(e::Reason::low_fuel));
    CHECK(applied && applied->action == int(e::ActionKind::yield) && applied->reason == int(e::Reason::low_fuel));
    CHECK(host_restrictions == restrictions + 1 && host_fields == fields && host_fuel == 19000);
    CHECK(bot.snapshot_.epoch == bot.runtime_.epoch_ && bot.fresh_.execution.owner_turn == int(host_round));
    CHECK(bot.fresh_.execution.observation_epoch == bot.snapshot_.epoch && bot.fresh_.execution.fuel == 19000);
    CHECK(bot.runtime_.last_action_ == int(e::ActionKind::yield) && !host_move_fields && !host_reachable_queries);
    CHECK(bot.runtime_.observe_count_ == 1 && !host_action_count && !entry_gate(bot, 22, host_round));
    CHECK(bot.goal() == goal && bot.project_.executor.phase == phase.phase);
    CHECK(bot.project_.executor.phase_start == phase.phase_start && bot.project_.executor.phase_budget == phase.phase_budget);
    CHECK(bot.running_ && !bot.closed_ && bot.project_.initialized && !bot.project_.pending_replan && !bot.project_.intent);
}
void ordinary_precommit_frontier_can_change_or_fail() {
    Bot bot(r::Arm::finite_harvest); ordinary_reset(bot);
    host_actor.state = 1 | (1 << 5); // No SDK action is available during preflight.
    host_export_cost = 33000; host_field_cost = 8500; host_fuel = 78000;
    bot.runtime_.position_distances_valid_ = false; bot.turn();
    CHECK(bot.snapshot_.complete && bot.ids_ == (std::array<int, 3>{1, 2, 3}));
    CHECK(!bot.fixed_ && !bot.running_ && !bot.closed_ && !host_action_count);
    // A later owner packet may make preflight fail; the old three IDs have
    // never committed and cannot prevent a subsequent current frontier.
    host_round = 101; host_fuel = 78000; host_actor.x = 12; observed_visibility(3);
    bot.runtime_.position_distances_valid_ = false; bot.turn();
    CHECK(bot.snapshot_.complete && known_count(bot) == 2 && !bot.fixed_ && !bot.running_ && !bot.closed_);
    CHECK(!host_action_count);
    host_round = 102; host_fuel = 78000; observed_visibility(7);
    bot.runtime_.position_distances_valid_ = false; bot.turn();
    CHECK(bot.snapshot_.complete && bot.ids_ == (std::array<int, 3>{3, 2, 1}));
    CHECK(!bot.fixed_ && !bot.running_ && !bot.closed_ && !host_action_count);
    host_round = 103; host_fuel = 1000000; host_actor.state = 1 | (1 << 3) | (1 << 4); bot.turn();
    // The restarted catalogue's remaining three quotes now complete in this
    // owner; publication still binds no executor phase and issues no action.
    CHECK(bot.fixed_ && bot.running_ && !bot.project_.initialized && !host_action_count);
    CHECK(bot.choice_.feasible && bot.fixed_ && bot.running_ && bot.ids_ == (std::array<int, 3>{3, 2, 1}));
    ordinary_owner(bot); ordinary_owner(bot); ordinary_owner(bot);
    CHECK(host_action_count == 1 && host_actions[0].round == host_round && host_actions[0].target == 103);
    // Committed identity survives a later actor position with a different
    // nearest-first ordering. This query-only rebuild emits no SDK action.
    location(bot, host_round + 1, 10, 10); const auto later = packet(bot);
    CHECK(bot.build_into(later, bot.fresh_) && bot.fixed_ && bot.ids_ == (std::array<int, 3>{3, 2, 1}));
    CHECK(!bot.collecting_routes_ && host_action_count == 1);
}
void ordinary_uncommitted_stale_retries_but_started_closes() {
    Bot bot(r::Arm::finite_harvest); ordinary_reset(bot); host_cross_on_vision = 1;
    bot.turn();
    CHECK(host_round == 101 && !bot.snapshot_.complete && entry_gate(bot, 1, 100));
    CHECK(!bot.fixed_ && !bot.running_ && !bot.closed_ && !host_action_count);
    CHECK(!bot.receipts_.baseline_valid && !bot.project_.intent);
    // Subsequent ordinary turns rebuild current SDK facts, complete bounded
    // preparation, bind, and observe the two typed goals on distinct owners.
    ordinary_prepare(bot);
    CHECK(bot.snapshot_.complete && bot.choice_.feasible && bot.fixed_ && bot.running_ && !bot.closed_);
    ordinary_owner(bot); ordinary_owner(bot); ordinary_owner(bot);
    CHECK(host_action_count == 1 && host_actions[0].round == host_round);
    const auto crossing = ++host_round;
    host_fuel = 1000000; host_cross_on_vision = 1; bot.turn();
    CHECK(host_round == crossing + 1 && !bot.snapshot_.complete && bot.fixed_ && !bot.running_ && bot.closed_);
    CHECK(!bot.project_.intent && !bot.receipts_.baseline_valid && host_action_count == 1);
    CHECK(!host_bag && !bot.ledger_.captures && !bot.ledger_.banks);
}
void ordinary_obligations_keep_priority() {
    {
        Bot bot(r::Arm::finite_harvest); ordinary_reset(bot, 7, 10, 11);
        host_group_state[0] = 2; host_camps[0].health = 0;
        auto& runtime = bot.runtime_; runtime.stage_ = n::Stage::production;
        runtime.plan_.group = 2; runtime.state_.target = 2; runtime.state_.phase = e::Phase::clear;
        bot.turn();
        CHECK(bot.snapshot_.safe_hold && entry_gate(bot, 2, 100) && !host_action_count);
        CHECK(runtime.stage_ == n::Stage::production && runtime.plan_.group == 2 && runtime.state_.target == 2);
        CHECK(!bot.fixed_ && !bot.running_ && !bot.choice_.feasible && !bot.ledger_.captures);
    }
    {
        Bot bot(r::Arm::finite_harvest); ordinary_reset(bot, 7, 13, 10);
        host_bag = 30; host_actor.state = 1 | (1 << 2);
        auto& runtime = bot.runtime_; runtime.stage_ = n::Stage::output_bank; runtime.bank_intent_ = true;
        // A capture at95, finalized surviving stock through99, and a real
        // three-tick tribute started at99. Current100 is still channeling;
        // no completion receipt or converted stock is being manufactured.
        runtime.captured_bank_ = {n::Runtime::BankDisposition::reference, 0, 1, 95, 99, 30, 1, 0, 0};
        runtime.own_receipts_.capture = runtime.payout_receipt_ = 1;
        runtime.own_receipts_.capture_round = 95; runtime.own_receipts_.capture_group = 1;
        runtime.own_receipts_.capture_gems = 30;
        runtime.previous_round_ = 99; runtime.previous_alive_ = runtime.previous_gold_finalized_ = true;
        runtime.previous_submitting_ = true; runtime.previous_bag_ = 30; runtime.previous_gold_ = 0;
        runtime.bank_start_round_ = 99; runtime.bank_start_bag_ = 30; runtime.bank_start_ticks_ = 3;
        runtime.bank_baseline_ = runtime.bank_receipt_ = 0;
        runtime.state_.phase = e::Phase::bank;
        bot.turn();
        CHECK(bot.snapshot_.channel == e::Channel::tribute && bot.snapshot_.channel_safe);
        CHECK(!entry_gate(bot, 5, 100) && !host_action_count && !bot.fixed_ && !bot.running_);
        CHECK(!runtime.observe_count_ && runtime.reference_packet_round_ == -1);
        if (!(host_bag == 30 && runtime.bank_intent_ && runtime.captured_bank_.pending()))
            std::fprintf(stderr, "channel obligation bag=%u intent=%d disposition=%d actor=%d/%d observed=%d baseline=%llu bank=%llu stage=%d phase=%d action=%d\n",
                host_bag, int(runtime.bank_intent_), int(runtime.captured_bank_.disposition), runtime.captured_bank_.actor,
                runtime.cached_actor_, runtime.captured_bank_.observed_round, (unsigned long long)runtime.captured_bank_.bank_baseline,
                (unsigned long long)runtime.own_receipts_.bank, int(runtime.stage_), int(runtime.state_.phase), runtime.last_action_);
        CHECK(host_bag == 30 && runtime.bank_intent_ && runtime.captured_bank_.pending());
        CHECK(runtime.captured_bank_.actor == 0 && runtime.captured_bank_.observed_round == 100 && runtime.captured_bank_.capture == 1);
        CHECK(runtime.last_action_ == -1 && !bot.ledger_.banks);
    }
    {
        Bot bot(r::Arm::finite_harvest); ordinary_reset(bot, 7, 8, 10); host_actor.health = 300;
        auto& runtime = bot.runtime_; runtime.recovering_ = true; runtime.state_.phase = e::Phase::recover;
        bot.turn();
        CHECK(bot.snapshot_.needs_recovery && bot.snapshot_.at_spawn && entry_gate(bot, 5, 100));
        CHECK(runtime.recovering_ && !bot.fixed_ && !bot.running_ && !host_action_count);
        CHECK(host_actor.health == 300 && !bot.recovery_ && !bot.ledger_.captures);
    }
}
void reference_paid_exit_avoids_occupied_entry() {
    Bot bot(r::Arm::finite_harvest); ordinary_reset(bot, 7, 10, 11);
    auto& runtime = bot.runtime_;
    location(bot, 100, 10, 11); host_bag = 7;
    host_camps[0].health = host_camps[3].health = 0;
    runtime.groups_[0].respawning = true; runtime.groups_[0].combat.monster_count = 0;
    runtime.stage_ = n::Stage::output_bank; runtime.plan_.feasible = true;
    runtime.plan_.altar = 0; runtime.plan_.renewal = 1; runtime.plan_.duration = 50;
    runtime.state_.target = 1; runtime.state_.phase = e::Phase::allocate;
    runtime.state_.phase_start = 100; runtime.state_.phase_budget = 104;
    // Actual Road of Riches obstruction: the bootstrap entry is occupied,
    // while a different legal first step follows the committed bank route.
    host_champion_tiles[word(10, 10)] |= bit(10, 10);
    host_champion_tiles[word(13, 10)] |= bit(13, 10); // the old altar entry is occupied too
    const auto goal = runtime.reference_goal();
    CHECK(goal.kind == n::GoalKind::altar && goal.bank_requested && !goal.production);
    auto observed = runtime.observation();
    CHECK(observed.on_group_spawn && observed.own_bag == 7);
    CHECK(e::decision(observed, runtime.state_).reason == e::Reason::illegal);
    runtime.movement(observed);
    const auto action = e::decision(observed, runtime.state_);
    CHECK(action.action.kind == e::ActionKind::vacate && observed.move_count > 0);
    CHECK(action.action.target != 10 * int(side) + 10);
    CHECK(action.action.target != 10 * int(side) + 13);
    CHECK(runtime.apply(action) && host_action_count == 1);
    CHECK(!runtime.groups_[0].spawns.is_set({host_actor.x, host_actor.y}));
    CHECK(host_bag == 7 && runtime.plan_.feasible && runtime.stage_ == n::Stage::output_bank);
    CHECK(runtime.state_.phase == e::Phase::allocate);
    runtime.stage_ = n::Stage::production;
    CHECK(runtime.reference_goal().kind == n::GoalKind::camp_entry);
    observed.channel = e::Channel::tribute; observed.channel_valid = observed.channel_safe = true;
    CHECK(e::decision(observed, runtime.state_).action.kind == e::ActionKind::preserve_channel);
}
}
int main() {
    active_complete_cycle(); control_banks_first(); receipt_and_finite_guards();
    recovery_goal_and_actual_receipt(); stale_closes_unexecuted_intent(); loaded_offplan_hold_precedes_liquidation();
    query_only_bind_rejects_changed_stock_and_owner();
    captured_mask_controls_vacate();
    ordinary_turn_selects_and_dispatches(); ordinary_turn_moves_to_selected_entry_then_attacks();
    ordinary_preparation_batches_existing_units(); ordinary_preparation_stops_at_synthetic_quote_fuel();
    ordinary_preparation_rejects_synthetic_unit_crossing();
    ordinary_preparation_diagnoses_restart_inputs(); ordinary_preparation_trace_does_not_change_decisions();
    ordinary_preparation_trace_has_reserved_bounded_records();
    ordinary_preparation_trace_bounds_detailed_work();
    component_member_design_queries_after_actual_binding();
    component_execution_quote_after_actual_binding(); ordinary_capture_defers_replan_and_preserves_exit();
    abandoned_pending_after_liquidation_uses_current_ledger();
    ordinary_visibility_exposes_reference_entry_gate();
    ordinary_host_cost_defers_without_reference(); ordinary_obligations_keep_priority();
    reference_paid_exit_avoids_occupied_entry();
    ordinary_postbuild_fuel_defers_typed_observation(); ordinary_typed_fuel_defers_movement();
    ordinary_running_refresh_replaces_live_fields();
    ordinary_apply_budget_defers_and_retries(); ordinary_apply_budget_defers_and_retries(true);
    ordinary_hidden_move_failure_has_matching_feedback(); component_stale_binding_after_actual_decision();
    component_point_admission_after_actual_decision(); component_entry_budget_after_actual_decision();
    ordinary_precommit_frontier_can_change_or_fail(); ordinary_uncommitted_stale_retries_but_started_closes();
    std::printf("Runoff native caller checks passed: %u (SDK-host Bot::turn entry plus component packets; no engine/fuel qualification)\n", checks);
}
