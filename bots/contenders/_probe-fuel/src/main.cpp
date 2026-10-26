// Probe: fuel cost of SDK calls (release build), turn budgets, static init, exhaustion.
// Team A's first champion and team B's first champion run the measurement suite; the others idle.
// Output lines:
//   B <side> r<round> <who> fuel=<remaining at turn start> [first]   (budget evidence)
//   F <side> <map>x<h> <op> raw=<median> net=<median - noop> min=<> max=<> n=<>
//   X ...                                                            (exhaustion test)
#include <cambc.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <array>
#include <algorithm>
#include <optional>
#include <string_view>

using namespace cambc;

static volatile std::uint64_t sink;
static std::uint64_t main_entry_fuel;
static bool first_turn_logged = false;
static int side() { return detail::sdk_my_team(); }
static const char* side_name() { return side() == 0 ? "A" : "B"; }

// ---------------------------------------------------------------- shared measurement state
static std::optional<camp> g_camp;          // a visible camp (any state)
static std::optional<champion> g_enemy;      // a visible enemy champion
static std::optional<map::camp_group> g_group;
static vec2 g_far{0, 0}, g_near{0, 0};      // far corner / nearby tile
static bitboard* g_a = nullptr;              // walls|holes
static bitboard* g_b = nullptr;              // a few bits
static bitboard* g_many = nullptr;           // ~ many set bits
static bitboard* g_roots = nullptr;
static bitboard* g_dest = nullptr;
static bitboard* g_visited = nullptr;
static distance_field* g_df = nullptr;
static std::array<vec2, 1024> g_pos_buf;
static std::optional<camp> g_hidden_camp;          // a camp whose tile is not in vision
static std::optional<map::camp_group> g_hidden_group;
static std::optional<champion> g_hidden_enemy;     // an enemy champion not in vision

struct Task {
    const char* name;
    int n;        // samples wanted
    int est;      // rough fuel per sample (gating)
    void (*fn)();
    bool needs_enemy = false;
    bool needs_camp = false;
};

#define T(name, n, est, ...) Task{name, n, est, [] { __VA_ARGS__; }}
#define TE(name, n, est, ...) Task{name, n, est, [] { __VA_ARGS__; }, true, false}
#define TC(name, n, est, ...) Task{name, n, est, [] { __VA_ARGS__; }, false, true}

static champion& me() { return self(); }

static void observe_typical() {
    auto& s = self();
    auto p = s.position();
    auto h = s.health();
    auto st = s.state();
    auto ab = s.abilities();
    auto c0 = ab[0].cooldown(), c1 = ab[1].cooldown();
    std::uint64_t acc = p.x + p.y + static_cast<std::uint64_t>(h) + st.bits() + c0 + c1;
    for (auto& e : vision::champions(team::enemy)) acc += static_cast<std::uint64_t>(e.health()) + e.position().x;
    for (auto q : vision::camp_tiles().positions()) {
        auto c = vision::camp_at(q);
        acc += static_cast<std::uint64_t>(c.health()) + static_cast<std::uint64_t>(c.state());
    }
    std::array<std::byte, 8> mem{};
    shared_memory.read(0, std::span<std::byte>{mem});
    acc += static_cast<std::uint64_t>(mem[0]);
    sink = acc;
}

// Same information through fewer host calls: one raw champion record, occupancy masks into
// preallocated boards, camps read only for health via the raw record.
static bitboard* g_tmp1 = nullptr;
static bitboard* g_tmp2 = nullptr;
static void observe_lean() {
    detail::champion_info info{};
    const auto me_handle = static_cast<std::uint32_t>(static_cast<handle_champion_t>(self()));
    detail::check(detail::sdk_champion(me_handle, &info));
    std::int32_t c0{}, c1{};
    detail::check(detail::sdk_champion_cooldown(me_handle, 0, &c0));
    detail::check(detail::sdk_champion_cooldown(me_handle, 1, &c1));
    std::uint64_t acc = info.x + info.y + static_cast<std::uint64_t>(info.health) + info.state + c0 + c1;
    vision::champion_tiles_into(team::enemy, *g_tmp1);
    auto n = g_tmp1->positions_into(std::span<vec2>{g_pos_buf});
    for (std::uint32_t i = 0; i < n && i < g_pos_buf.size(); ++i) {
        std::uint32_t handle{};
        detail::check(detail::sdk_champion_at(g_pos_buf[i].x, g_pos_buf[i].y, &handle));
        detail::champion_info e{};
        detail::check(detail::sdk_champion(handle, &e));
        acc += static_cast<std::uint64_t>(e.health) + e.x;
    }
    vision::camp_tiles_into(*g_tmp2);
    n = g_tmp2->positions_into(std::span<vec2>{g_pos_buf});
    for (std::uint32_t i = 0; i < n && i < g_pos_buf.size(); ++i) {
        std::uint32_t handle{};
        detail::check(detail::sdk_camp_at(g_pos_buf[i].x, g_pos_buf[i].y, &handle));
        detail::camp_state cs{};
        detail::check(detail::sdk_camp(handle, &cs));
        std::uint32_t state{};
        detail::check(detail::sdk_camp_state(handle, &state));
        acc += static_cast<std::uint64_t>(cs.health) + state;
    }
    std::array<std::byte, 8> mem{};
    shared_memory.read(0, std::span<std::byte>{mem});
    acc += static_cast<std::uint64_t>(mem[0]);
    sink = acc;
}

static std::vector<Task> make_tasks() {
    return {
        T("noop", 31, 50, sink = sink + 1),
        T("fuel_remaining()", 31, 50, sink = fuel_remaining()),
        T("round_number()", 21, 100, sink = round_number()),
        T("self()", 21, 50, sink = static_cast<std::uint32_t>(static_cast<handle_champion_t>(self()))),
        T("self().position()", 21, 150, sink = me().position().x),
        T("self().health()", 21, 150, sink = static_cast<std::uint64_t>(me().health())),
        T("self().max_health()", 15, 150, sink = static_cast<std::uint64_t>(me().max_health())),
        T("self().state()", 15, 150, sink = me().state().bits()),
        T("self().can_move()", 15, 150, sink = me().can_move()),
        T("self().is_alive()", 15, 150, sink = me().is_alive()),
        T("self().strength()", 15, 150, sink = static_cast<std::uint64_t>(me().strength())),
        T("self().champion_name()", 15, 150, sink = static_cast<std::uint64_t>(me().champion_name())),
        T("self().team()", 15, 150, sink = static_cast<std::uint64_t>(me().team())),
        T("detail::sdk_champion raw record", 15, 150,
          detail::champion_info i{}; detail::check(detail::sdk_champion(static_cast<std::uint32_t>(static_cast<handle_champion_t>(me())), &i)); sink = i.x),
        T("self().abilities() copy", 15, 400, auto a = me().abilities(); sink = a[0].slot()),
        T("ability.cooldown()", 15, 200, sink = me().abilities()[0].cooldown()),
        T("ability.cooldown() (held copy)", 15, 200, static const auto a = me().abilities(); sink = a[1].cooldown()),
        T("ability.is_ready() (held copy)", 15, 3000, static const auto a = me().abilities(); sink = a[0].is_ready()),
        T("ability.info() (held copy)", 15, 100, static const auto a = me().abilities(); sink = a[0].info().has_value()),
        T("ability.valid_targets() (held copy)", 15, 200, static const auto a = me().abilities(); sink = a[0].valid_targets().bits()),
        T("self().gold_count()", 15, 150, sink = me().gold_count()),
        T("self().gem_count()", 15, 150, sink = me().gem_count()),
        T("self().item_count()", 15, 600, sink = me().item_count()[item_type::monster_shard_of_health]),
        T("self().buffs()", 15, 800, sink = me().buffs().size()),
        T("self().restrictions()", 15, 150, sink = me().restrictions().bits()),
        T("self().ward_cooldown()", 15, 150, sink = static_cast<std::uint64_t>(me().ward_cooldown())),
        T("self().ward_positions()", 15, 400, sink = me().ward_positions().size()),
        T("self().priority()", 15, 150, sink = static_cast<std::uint64_t>(me().priority())),
        T("self().can_buy(item)", 15, 600, sink = me().can_buy(item_type::power_shard_of_health)),
        T("self().visible_tiles()", 15, 1500, sink = me().visible_tiles().count()),
        T("self().visible_tiles_from(p)", 15, 2000, sink = me().visible_tiles_from(g_near).count()),
        T("self().consumables()[tp].count()", 15, 200, sink = me().consumables()[consumable_type::tp_crystal].count()),
        T("vision::visible_tiles()", 15, 1500, sink = vision::visible_tiles().count()),
        T("vision::visible_tiles_into(b)", 15, 800, vision::visible_tiles_into(*g_tmp1); sink = g_tmp1->ptr()[0]),
        T("vision::in_vision(p)", 21, 150, sink = vision::in_vision(g_near)),
        T("vision::champion_tiles(enemy)", 15, 1500, sink = vision::champion_tiles(team::enemy).ptr()[0]),
        T("vision::champion_tiles_into(enemy,b)", 15, 800, vision::champion_tiles_into(team::enemy, *g_tmp1); sink = g_tmp1->ptr()[0]),
        T("vision::champion_tiles() both", 15, 2500, sink = vision::champion_tiles().ptr()[0]),
        T("vision::camp_tiles()", 15, 1500, sink = vision::camp_tiles().ptr()[0]),
        T("vision::camp_tiles_into(b)", 15, 800, vision::camp_tiles_into(*g_tmp1); sink = g_tmp1->ptr()[0]),
        T("vision::awake_camp_tiles()", 15, 1500, sink = vision::awake_camp_tiles().ptr()[0]),
        T("vision::asleep_camp_tiles()", 15, 1500, sink = vision::asleep_camp_tiles().ptr()[0]),
        T("vision::ward_tiles(enemy)", 15, 1500, sink = vision::ward_tiles(team::enemy).ptr()[0]),
        T("vision::teleport_tiles()", 15, 1500, sink = vision::teleport_tiles().ptr()[0]),
        T("vision::altar_lock_tiles(enemy)", 15, 1500, sink = vision::altar_lock_tiles(team::enemy).ptr()[0]),
        T("vision::has_camp(p)", 15, 200, sink = vision::has_camp(g_near)),
        T("vision::has_champion(p)", 15, 200, sink = vision::has_champion(g_near)),
        T("vision::wards()", 15, 600, sink = vision::wards().size()),
        T("vision::altar_locks()", 15, 600, sink = vision::altar_locks().size()),
        T("vision::teleports()", 15, 600, sink = vision::teleports().size()),
        T("vision::champions(enemy)", 9, 9000, sink = vision::champions(team::enemy).size()),
        T("vision::champions(ally)", 9, 9000, sink = vision::champions(team::ally).size()),
        T("vision::camps()", 9, 12000, sink = vision::camps().size()),
        TE("vision::champion_at(p)", 15, 3000, sink = static_cast<std::uint32_t>(static_cast<handle_champion_t>(vision::champion_at(g_enemy->position())))),
        TE("enemy.health()", 15, 200, sink = static_cast<std::uint64_t>(g_enemy->health())),
        TE("enemy.position()", 15, 200, sink = g_enemy->position().x),
        TE("enemy.state()", 15, 200, sink = g_enemy->state().bits()),
        TE("enemy.is_visible()", 15, 200, sink = g_enemy->is_visible()),
        TE("enemy.is_alive()", 15, 200, sink = g_enemy->is_alive()),
        TE("enemy.gem_count()", 15, 200, sink = g_enemy->gem_count()),
        TE("enemy.buffs()", 15, 800, sink = g_enemy->buffs().size()),
        TE("enemy.abilities()[0].cooldown()", 15, 600, sink = g_enemy->abilities()[0].cooldown()),
        TE("enemy.item_count()", 15, 600, sink = g_enemy->item_count()[item_type::monster_shard_of_health]),
        TC("vision::camp_at(p)", 15, 1500, sink = static_cast<std::uint32_t>(static_cast<handle_camp_t>(vision::camp_at(g_camp->position())))),
        TC("camp.health()", 15, 200, sink = static_cast<std::uint64_t>(g_camp->health())),
        TC("camp.position()", 15, 200, sink = g_camp->position().x),
        TC("camp.state()", 15, 200, sink = static_cast<std::uint64_t>(g_camp->state())),
        TC("camp.is_visible()", 15, 200, sink = g_camp->is_visible()),
        TC("camp.base_stats()", 15, 400, sink = g_camp->base_stats().base_attack_range),
        TC("camp.attack_range()", 15, 400, sink = static_cast<std::uint64_t>(g_camp->attack_range())),
        TC("camp.buffs()", 15, 800, sink = g_camp->buffs().size()),
        TC("camp.camp_group()", 9, 3000, sink = g_camp->camp_group().gem_value()),
        TC("camp_group.state() (held)", 15, 1500, sink = g_group->state().index()),
        T("map::camp(p) (spawn tile)", 9, 2000, sink = static_cast<std::uint32_t>(static_cast<handle_camp_t>(map::camp(g_group->camps()[0].spawn_position())))),
        T("map::camp_groups()", 5, 40000, sink = map::camp_groups().size()),
        T("vision::camp_groups()", 5, 40000, sink = vision::camp_groups().size()),
        T("cambc::champions(enemy)", 9, 8000, sink = cambc::champions(team::enemy).size()),
        T("cambc::champions(ally)", 9, 8000, sink = cambc::champions(team::ally).size()),
        T("global_submitted_gems(enemy)", 15, 150, sink = global_submitted_gems(team::enemy)),
        T("constants::capture_time", 15, 50, sink = static_cast<std::uint64_t>(constants::capture_time)),
        T("map::walls() ref", 15, 50, sink = map::walls().ptr()[0]),
        T("map::width()", 15, 50, sink = static_cast<std::uint64_t>(map::width())),
        T("shared_memory[0] read", 21, 150, sink = static_cast<std::uint8_t>(shared_memory[0])),
        T("shared_memory[0] write", 21, 150, shared_memory[0] = static_cast<std::uint8_t>(sink & 0x7f)),
        T("shared_memory.read(0, 8 bytes)", 15, 200, std::array<std::byte, 8> m{}; shared_memory.read(0, std::span<std::byte>{m}); sink = static_cast<std::uint64_t>(m[3])),
        T("shared_memory.write(0, 8 bytes)", 15, 200, std::array<std::byte, 8> m{}; m[0] = std::byte{1}; shared_memory.write(0, std::span<const std::byte>{m})),
        T("draft_memory[0] (snapshot)", 15, 50, sink = static_cast<std::uint64_t>(draft_memory[0])),
        T("draft_memory_buffer{} (fresh read)", 9, 2000, draft_memory_buffer d; sink = d.size()),
        T("bitboard{} construct", 15, 800, bitboard b; sink = b.ptr()[0]),
        T("bitboard copy", 15, 800, bitboard b{*g_a}; sink = b.ptr()[0]),
        T("bitboard a | b", 15, 900, auto c = *g_a | *g_b; sink = c.ptr()[0]),
        T("bitboard a |= b", 15, 600, *g_tmp1 |= *g_b; sink = g_tmp1->ptr()[0]),
        T("bitboard a & b", 15, 900, auto c = *g_a & *g_b; sink = c.ptr()[0]),
        T("bitboard ~a", 15, 900, auto c = ~*g_a; sink = c.ptr()[0]),
        T("bitboard shift_right(1)", 15, 900, auto c = g_a->shift_right(1); sink = c.ptr()[0]),
        T("bitboard shift_up_into(1, out)", 15, 900, g_a->shift_up_into(1, *g_tmp1); sink = g_tmp1->ptr()[0]),
        T("bitboard count()", 15, 300, sink = g_a->count()),
        T("bitboard is_empty()", 15, 300, sink = g_a->is_empty()),
        T("bitboard is_set(p)", 21, 150, sink = g_a->is_set(g_near)),
        T("bitboard set(p)", 21, 150, g_tmp1->set(g_near)),
        T("bitboard == ", 15, 300, sink = (*g_a == *g_b)),
        T("bitboard positions() few bits", 15, 1500, sink = g_b->positions().size()),
        T("bitboard positions() many bits", 9, 15000, sink = g_many->positions().size()),
        T("bitboard positions_into(span) many bits", 9, 12000, sink = g_many->positions_into(std::span<vec2>{g_pos_buf})),
        T("bitboard draw_chebyshev(p,3)", 15, 400, g_tmp1->draw_chebyshev(g_near, 3.0f); sink = g_tmp1->ptr()[0]),
        T("bitboard draw_circle(p,3)", 15, 400, g_tmp1->draw_circle(g_near, 3.0f); sink = g_tmp1->ptr()[0]),
        T("bitboard reachable_tiles(p,1)", 15, 1500, sink = g_a->reachable_tiles(me().position(), 1).ptr()[0]),
        T("bitboard reachable_tiles(p,8)", 9, 4000, sink = g_a->reachable_tiles(me().position(), 8).ptr()[0]),
        T("bitboard shadow_cast(p,8)", 9, 4000, sink = map::walls().shadow_cast(me().position(), 8).ptr()[0]),
        T("bitboard reflect(symmetry)", 15, 900, sink = g_a->reflect(map::symmetry()).ptr()[0]),
        T("distance_field{} construct", 9, 3000, distance_field d; sink = d.width()),
        T("distance_field.compute whole map", 5, 60000, sink = g_df->compute_with_expansion_count(*g_a, *g_roots, *g_visited)),
        T("distance_field.compute max_distance 10", 7, 15000, sink = g_df->compute_with_expansion_count(*g_a, *g_roots, *g_visited, 10)),
        T("distance_field.compute_to near tile", 7, 15000, sink = g_df->compute_to_with_expansion_count(*g_a, *g_roots, *g_dest, *g_visited)),
        T("distance_field.get(p)", 21, 150, sink = g_df->get(g_near)),
        T("distance_field.next_move_from(p)", 15, 1500, sink = g_df->next_move_from(g_near).has_value()),
        T("distance_field.copy_from", 7, 5000, static distance_field d2; d2.copy_from(*g_df); sink = d2.get(g_near)),
        T("printf one formatted line", 9, 3000, std::printf("Z %u %d,%d %.2f %s\n", round_number(), g_near.x, g_near.y, 123.456, "abcdef")),
        T("vector<int> 100 push_back", 9, 9000, std::vector<int> v; for (int i = 0; i < 100; ++i) v.push_back(i * 7 % 13); sink = v.size()),
        T("std::sort 100 ints", 9, 40000, static std::vector<int> v; v.clear(); for (int i = 0; i < 100; ++i) v.push_back((i * 7919) % 101); std::sort(v.begin(), v.end()); sink = v[50]),
        T("int64 chebyshev x100", 9, 5000, std::int64_t s = 0; for (int i = 0; i < 100; ++i) s += vec2{i, 3}.chebyshev_distance(g_near); sink = s),
        T("printf int-only line", 9, 3000, std::printf("Z %u %d %d %d\n", round_number(), g_near.x, g_near.y, 42)),
        T("printf 8 fields 2 floats", 9, 9000, std::printf("Z %u %d,%d hp=%.2f/%.2f st=%u cd=%d,%d\n", round_number(), g_near.x, g_near.y, 812.5, 1000.0, 7u, 3, 0)),
        T("puts literal", 9, 2000, std::puts("Z literal line of thirty chars")),
        T("printf no conversion", 9, 3000, std::printf("Z literal line of thirty chars\n")),
        T("printf 1 int", 9, 3000, std::printf("Z %d\n", g_near.x)),
        T("printf 2 ints", 9, 3000, std::printf("Z %d %d\n", g_near.x, g_near.y)),
        T("printf 1 %.2f", 9, 5000, std::printf("Z %.2f\n", 812.5)),
        T("printf 1 %s", 9, 3000, std::printf("Z %s\n", "abcdefgh")),
        T("fputs + fputc", 9, 2000, std::fputs("Z literal line of thirty chars", stdout); std::fputc('\n', stdout)),
        T("fflush(nullptr) after a line", 9, 5000, std::puts("Z x"); std::fflush(nullptr)),
        T("std::to_string + concat", 9, 3000, std::string t = "r" + std::to_string(round_number()) + " x" + std::to_string(g_near.x); sink = t.size()),
        T("try/catch, no throw", 15, 100, try { sink = sink + 1; } catch (...) { sink = 0; }),
        T("throw+catch std::runtime_error", 7, 20000, try { if (sink != 12345) throw std::runtime_error("x"); } catch (const std::exception& e) { sink = e.what()[0]; }),
        Task{"camp.state() hidden (throws, caught)", 7, 20000, [] { try { sink = static_cast<std::uint64_t>(g_hidden_camp->state()); } catch (const std::exception& e) { sink = e.what()[0]; } }, false, false},
        Task{"camp.is_visible() hidden", 15, 300, [] { sink = g_hidden_camp->is_visible(); }, false, false},
        Task{"camp_group.state() hidden (throws, caught)", 7, 20000, [] { try { sink = g_hidden_group->state().index(); } catch (const std::exception& e) { sink = e.what()[0]; } }, false, false},
        Task{"vision::camp_at(hidden tile) (throws, caught)", 7, 20000, [] { try { sink = static_cast<std::uint32_t>(static_cast<handle_camp_t>(vision::camp_at(g_hidden_camp->spawn_position()))); } catch (const std::exception& e) { sink = e.what()[0]; } }, false, false},
        Task{"hidden enemy.health() (throws, caught)", 7, 20000, [] { try { sink = static_cast<std::uint64_t>(g_hidden_enemy->health()); } catch (const std::exception& e) { sink = e.what()[0]; } }, false, false},
        Task{"hidden enemy.is_visible()", 15, 300, [] { sink = g_hidden_enemy->is_visible(); }, false, false},
        Task{"hidden enemy.is_alive()", 15, 300, [] { sink = g_hidden_enemy->is_alive(); }, false, false},
        Task{"hidden enemy.champion_name()", 15, 300, [] { sink = static_cast<std::uint64_t>(g_hidden_enemy->champion_name()); }, false, false},
        T("champion{type} design construct", 7, 3000, champion d{champion_type::ryth}; sink = static_cast<std::uint64_t>(d.base_stats().base_health)),
        T("item_type.info()", 15, 300, sink = item_type::power_shard_of_health.info().cost),
        T("consumable_type.cost()", 15, 300, sink = consumable_type::altar_lock.cost()),
        T("observe_typical()", 7, 40000, observe_typical()),
        T("observe_lean()", 7, 30000, observe_lean()),
    };
}

// ---------------------------------------------------------------- suite runner
static std::vector<Task> tasks;
static std::size_t task_index = 0;
static std::vector<std::int64_t> samples;
static std::int64_t noop_median = 0;
static int setup_round = -1;

static void setup() {
    if (!g_a) {
        g_a = new bitboard(map::walls() | map::holes());
        g_b = new bitboard();
        g_many = new bitboard(~map::walls());
        g_roots = new bitboard();
        g_dest = new bitboard();
        g_visited = new bitboard();
        g_df = new distance_field();
        g_tmp1 = new bitboard();
        g_tmp2 = new bitboard();
    }
    if (setup_round == static_cast<int>(round_number())) return;
    setup_round = static_cast<int>(round_number());
    auto here = self().position();
    g_near = {std::min(here.x + 2, map::width() - 1), here.y};
    g_far = {here.x < map::width() / 2 ? map::width() - 1 : 0, here.y < map::height() / 2 ? map::height() - 1 : 0};
    g_b->clear();
    for (int i = 0; i < 10; ++i) g_b->set({(here.x + i) % map::width(), here.y});
    g_roots->clear();
    g_roots->set(here);
    g_dest->clear();
    g_dest->set({std::min(here.x + 6, map::width() - 1), here.y});
    g_enemy.reset();
    for (auto p : vision::champion_tiles(team::enemy).positions()) { g_enemy = vision::champion_at(p); break; }
    g_camp.reset();
    for (auto p : vision::camp_tiles().positions()) { g_camp = vision::camp_at(p); break; }
    g_hidden_enemy.reset();
    for (auto& c : cambc::champions(team::enemy)) if (!c.is_visible()) { g_hidden_enemy = c; break; }
    if (!g_hidden_camp) {
        auto vis = vision::visible_tiles();
        for (auto& g : map::camp_groups()) {
            bool any = false;
            for (auto& m : g) any = any || vis.is_set(m.spawn_position());
            if (!any) { g_hidden_group = g; g_hidden_camp = g.camps()[0]; break; }
        }
    }
    if (!g_group) {
        auto groups = map::camp_groups();
        if (!groups.empty()) g_group = groups[0];
        if (g_camp) g_group = g_camp->camp_group();
    }
    static bool df_done = false;
    if (!df_done) {
        const auto e1 = g_df->compute_with_expansion_count(*g_a, *g_roots, *g_visited);
        const auto e2 = g_df->compute_with_expansion_count(*g_a, *g_roots, *g_visited, 10);
        const auto e3 = g_df->compute_to_with_expansion_count(*g_a, *g_roots, *g_dest, *g_visited);
        g_df->compute(*g_a, *g_roots, *g_visited);
        const auto reached = g_visited->count();
        std::printf("V %s %dx%d expansions whole=%u bounded10=%u to_near=%u reached=%u walls|holes=%u visible enemies=%zu camps=%u\n", side_name(),
                    map::width(), map::height(), e1, e2, e3, reached, g_a->count(), vision::champions(team::enemy).size(), vision::camp_tiles().count());
        df_done = true;
    }
}

static std::int64_t median(std::vector<std::int64_t> v) {
    std::sort(v.begin(), v.end());
    return v.empty() ? 0 : v[v.size() / 2];
}

static void run_suite() {
    if (tasks.empty()) tasks = make_tasks();
    setup();
    while (task_index < tasks.size()) {
        const auto& t = tasks[task_index];
        if ((t.needs_enemy && !g_enemy) || (t.needs_camp && (!g_camp || !g_group))) {
            std::printf("F %s %dx%d %s skipped (nothing visible)\n", side_name(), map::width(), map::height(), t.name);
            ++task_index;
            continue;
        }
        if (t.needs_camp && !g_camp->is_visible()) { ++task_index; continue; }
        const std::string_view nm{t.name};
        if ((nm.find("camp") != std::string_view::npos && nm.find("hidden") != std::string_view::npos && (!g_hidden_camp || !g_hidden_group)) ||
            (nm.find("hidden enemy") != std::string_view::npos && !g_hidden_enemy)) {
            std::printf("F %s %dx%d %s skipped (nothing hidden)\n", side_name(), map::width(), map::height(), t.name);
            ++task_index;
            continue;
        }
        if (fuel_remaining() < static_cast<std::uint64_t>(t.est) + 15000) return; // next turn
        const auto a = fuel_remaining();
        t.fn();
        const auto b = fuel_remaining();
        samples.push_back(static_cast<std::int64_t>(a) - static_cast<std::int64_t>(b));
        if (static_cast<int>(samples.size()) >= t.n) {
            auto med = median(samples);
            if (task_index == 0) noop_median = med;
            auto mn = *std::min_element(samples.begin(), samples.end());
            auto mx = *std::max_element(samples.begin(), samples.end());
            std::printf("F %s %dx%d %s raw=%lld net=%lld min=%lld max=%lld n=%d\n", side_name(), map::width(), map::height(), t.name,
                        static_cast<long long>(med), static_cast<long long>(med - noop_median), static_cast<long long>(mn),
                        static_cast<long long>(mx), static_cast<int>(samples.size()));
            samples.clear();
            ++task_index;
        }
    }
}

// ---------------------------------------------------------------- actions measured once per turn
static int action_phase = 0;
static void measure_action(const char* name, auto&& fn) {
    const auto a = fuel_remaining();
    bool ok = false;
    try { ok = fn(); } catch (const std::exception& e) { std::printf("F %s action %s exception %s\n", side_name(), name, e.what()); }
    const auto b = fuel_remaining();
    std::printf("F %s %dx%d action %s raw=%lld ok=%d\n", side_name(), map::width(), map::height(), name,
                static_cast<long long>(a) - static_cast<long long>(b), ok ? 1 : 0);
}

static std::optional<vec2> free_delta() {
    auto& s = self();
    const auto here = s.position();
    bitboard blocked = map::walls() | map::holes() | map::altars() | map::spawn_region(team::enemy) | vision::champion_tiles() |
                       vision::camp_tiles();
    static const vec2 dirs[8] = {vec2::north, vec2::east, vec2::south, vec2::west, vec2::north_east, vec2::south_east, vec2::south_west, vec2::north_west};
    for (auto d : dirs) {
        const auto p = here + d;
        if (map::is_in_bounds(p) && !blocked.is_set(p)) return d;
    }
    return std::nullopt;
}

static std::optional<camp> nearest_cluster() {
    std::optional<camp> best;
    std::int64_t bd = 1 << 30;
    const auto here = self().position();
    for (auto p : vision::camp_tiles().positions()) {
        auto c = vision::camp_at(p);
        auto d = p.chebyshev_distance(here) + (c.is_gem_cluster() || c.is_gold_cluster() ? 0 : 100);
        if (d < bd) { bd = d; best = c; }
    }
    return best;
}

static void run_actions() {
    auto& s = self();
    switch (action_phase) {
    case 0: case 1: {
        auto d = free_delta();
        if (d) measure_action(action_phase == 0 ? "move(delta) free tile" : "move(delta) free tile 2", [&] { return s.move(*d); });
        ++action_phase;
        break;
    }
    case 2: measure_action("move_towards(far corner)", [&] { return s.move_towards(g_far); }); ++action_phase; break;
    case 3: {
        const auto h = s.position();
        const vec2 t{std::clamp(h.x + 3, 0, map::width() - 1), h.y};
        measure_action("move_towards(3 tiles away)", [&] { return s.move_towards(t); });
        ++action_phase;
        break;
    }
    case 4: {
        static std::optional<vec2> target;
        static int tries = 0;
        if (!target) { if (auto c0 = nearest_cluster()) target = c0->position(); }
        if (!target || ++tries > 40) { std::printf("F %s action approach: gave up\n", side_name()); action_phase = 9; break; }
        if (target->chebyshev_distance(s.position()) > s.attack_range() || !vision::has_camp(*target)) { s.move_towards(*target); break; }
        auto c = std::optional<camp>{vision::camp_at(*target)};
        measure_action("attack(camp)", [&] { s.attack(*c); return true; });
        auto ab = s.abilities();
        for (auto& a : ab)
            if (a.is_attack_reset()) { measure_action("ability.use() attack reset", [&] { return a.use(); }); break; }
        if (s.can_attack()) measure_action("attack(camp) after reset", [&] { s.attack(*c); return true; });
        ++action_phase;
        break;
    }
    case 5: {
        auto ab = s.abilities();
        for (auto& a : ab)
            if (a.is_dash()) {
                auto d = free_delta();
                if (d) { const auto t = s.position() + *d; measure_action("ability.use(tile) dash 1 step", [&] { return a.use(t); }); }
                break;
            }
        ++action_phase;
        break;
    }
    case 6: {
        std::optional<camp> c;
        for (auto q : vision::camp_tiles().positions())
            if (q.chebyshev_distance(s.position()) <= s.attack_range()) { c = vision::camp_at(q); break; }
        if (c)
            measure_action("attack(camp) twice (second throws)", [&] { s.attack(*c); s.attack(*c); return true; });
        ++action_phase;
        break;
    }
    default: action_phase = 9; break;
    }
}

// ---------------------------------------------------------------- exhaustion test
static std::uint64_t iter_cost_x1000 = 0; // fuel per 1000 iterations
static void busy(std::uint64_t iters) {
    for (std::uint64_t i = 0; i < iters; ++i) sink = sink * 3 + i;
}
static void exhaustion_test() {
    const auto local_round = round_number();
    auto& s = self();
    const auto before = s.position();
    bool moved1 = false;
    if (auto d = free_delta()) { try { moved1 = s.move(*d); } catch (...) {} }
    const auto f0 = fuel_remaining();
    const std::uint64_t iters = iter_cost_x1000 ? (150000ull * 1000ull) / iter_cost_x1000 : 30000;
    std::printf("X %s begin local_round=%u fuel=%llu pos=%d,%d moved_before=%d iters=%llu\n", side_name(), local_round,
                static_cast<unsigned long long>(f0), before.x, before.y, moved1 ? 1 : 0, static_cast<unsigned long long>(iters));
    std::fflush(nullptr);
    busy(iters);
    const auto f1 = fuel_remaining();
    const auto live = round_number();
    auto p = s.position();
    bool moved2 = false;
    if (auto d = free_delta()) { try { moved2 = s.move(*d); } catch (const std::exception& e) { std::printf("X %s move after exception %s\n", side_name(), e.what()); } }
    auto p2 = s.position();
    std::printf("X %s after local_round=%u live_round=%u fuel=%llu pos=%d,%d moved_after=%d pos_after=%d,%d state=%u\n", side_name(), local_round, live,
                static_cast<unsigned long long>(f1), p.x, p.y, moved2 ? 1 : 0, p2.x, p2.y, s.state().bits());
}

// ---------------------------------------------------------------- turn
static int my_index() {
    // Draft order index among our champions: compare with the sorted handles of allies.
    auto mine = static_cast<std::uint32_t>(static_cast<handle_champion_t>(self()));
    auto all = cambc::champions(team::ally);
    std::vector<std::uint32_t> ids;
    for (auto& c : all) ids.push_back(static_cast<std::uint32_t>(static_cast<handle_champion_t>(c)));
    std::sort(ids.begin(), ids.end());
    return static_cast<int>(std::find(ids.begin(), ids.end(), mine) - ids.begin());
}

static int g_index = -1;
static void play_turn(std::uint64_t start_fuel) {
    const auto r = round_number();
    if (g_index < 0) g_index = my_index();
    std::printf("B %s r%u c%d fuel=%llu%s\n", side_name(), r, g_index, static_cast<unsigned long long>(start_fuel), first_turn_logged ? "" : " first");
    if (!first_turn_logged) {
        std::printf("B %s r%u c%d main_entry_fuel=%llu\n", side_name(), r, g_index, static_cast<unsigned long long>(main_entry_fuel));
        first_turn_logged = true;
    }
    if (g_index != 0) return;
    if (r >= 3 && r < 200 && task_index < (tasks.empty() ? 1 : tasks.size())) { run_suite(); return; }
    if (r >= 3 && iter_cost_x1000 == 0) {
        const auto a = fuel_remaining();
        busy(1000);
        const auto b = fuel_remaining();
        iter_cost_x1000 = a - b;
        std::printf("X %s busy(1000) fuel=%llu\n", side_name(), static_cast<unsigned long long>(iter_cost_x1000));
        return;
    }
    if (action_phase < 9) { run_actions(); return; }
    static bool exhausted_once = false;
    if (!exhausted_once && side() == 0) { exhausted_once = true; exhaustion_test(); return; }
    static int tail = 0;
    if (side() == 0 && ++tail > 4) { std::printf("X %s resign r%u\n", side_name(), r); std::fflush(nullptr); resign(); }
}

static void draft_turn(std::uint64_t start_fuel, int draft_turn_no) {
    std::printf("B %s draft turn%d fuel=%llu%s\n", side_name(), draft_turn_no, static_cast<unsigned long long>(start_fuel),
                draft_turn_no == 0 ? " first" : "");
    if (draft_turn_no == 0)
        std::printf("B %s draft main_entry_fuel=%llu\n", side_name(), static_cast<unsigned long long>(main_entry_fuel));
    const champion_draft order[] = {champion_draft::vesper_vale, champion_draft::ryth, champion_draft::stellar,
                                    champion_draft::varg, champion_draft::tavi};
    {
        auto a = fuel_remaining();
        auto x = draft::current_draft(team::ally);
        auto b = fuel_remaining();
        auto y = draft::current_draft(team::enemy);
        auto c = fuel_remaining();
        auto n = draft::team_size();
        auto d = fuel_remaining();
        std::printf("F %s %dx%d draft current_draft(ally)=%lld current_draft(enemy)=%lld team_size()=%lld (sizes %zu %zu %u)\n", side_name(),
                    map::width(), map::height(), static_cast<long long>(a - b), static_cast<long long>(b - c), static_cast<long long>(c - d),
                    x.size(), y.size(), n);
        if (draft_turn_no == 0) {
            auto e = fuel_remaining();
            auto g = map::camp_groups();
            auto f = fuel_remaining();
            std::printf("F %s %dx%d draft map::camp_groups()=%lld groups=%zu\n", side_name(), map::width(), map::height(),
                        static_cast<long long>(e - f), g.size());
        }
    }
    auto mine = draft::current_draft(team::ally);
    for (auto c : order)
        if (std::find(mine.begin(), mine.end(), c) == mine.end()) { draft::pick(c); return; }
}

int main() {
    main_entry_fuel = fuel_remaining();
    int draft_turns = 0;
    std::uint64_t start = main_entry_fuel;
    while (true) {
        try {
            if (is_draft()) draft_turn(start, draft_turns++);
            else play_turn(start);
        } catch (const std::exception& e) {
            std::printf("probe error: %s\n", e.what());
        }
        end_turn();
        start = fuel_remaining();
    }
}
