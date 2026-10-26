#pragma once
#include <cambc.h>
#include "../../common/src/observe.h"
#include "../../common/src/accounting.h"
#include "../../common/src/execute.h"
#include "../../common/src/card-life.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#ifndef PLAN5_BANK_EXIT
#define PLAN5_BANK_EXIT 0
#endif
#ifndef PLAN5_WORKSHOP_TRACE
#define PLAN5_WORKSHOP_TRACE 0
#endif
#ifndef PLAN5_WORKSHOP_BOOTSTRAP
#define PLAN5_WORKSHOP_BOOTSTRAP 0
#endif

// Native binding only. Decisions use the Plan5 cycle/accounting and stage
// executor; no Fundamentals policy or live team-memory protocol is imported.
namespace p5::native {
namespace e = p5::execution;
using cambc::bitboard;
using cambc::vec2;
using cambc::team;
constexpr int groups_cap = 8, nodes_cap = 11;
constexpr std::uint16_t unknown_distance = 65535;
inline bitboard at(vec2 p) { bitboard b; b.set(p); return b; }
inline int cheb(vec2 a, vec2 b) { return int(a.chebyshev_distance(b)); }
inline int gid(const cambc::map::camp_group& g) { return int(static_cast<cambc::handle_camp_group_t>(g)); }
inline int cid(const cambc::camp& c) { return int(static_cast<cambc::handle_camp_t>(c)); }
inline int own_id() { return int(static_cast<cambc::handle_champion_t>(cambc::self())); }
inline bitboard static_walls() {
    return cambc::map::walls() | cambc::map::holes() | cambc::map::altars() |
        cambc::map::spawn_region(team::enemy);
}
struct Tile { std::int16_t x = -1, y = -1; };
inline vec2 position(Tile p) { return {p.x, p.y}; }
inline Tile tile(vec2 p) { return {std::int16_t(p.x), std::int16_t(p.y)}; }
struct Cache {
    std::uint32_t magic = 0;
    std::uint16_t width = 0, height = 0, rows = 0;
    std::uint8_t groups = 0, altars = 0, pick_count = 0, team_size = 0;
    std::array<int, groups_cap> ids{};
    std::array<Tile, nodes_cap> nodes{};
    std::array<Tile, 2> altar_tiles{};
    std::array<std::array<std::uint16_t, nodes_cap>, nodes_cap> distance{};
    std::array<std::uint8_t, 5> picks{};
};
static_assert(sizeof(Cache) < 1024);
constexpr std::uint32_t cache_magic = 0x50355231;
inline bool valid(const Cache& c) {
    return c.magic == cache_magic && c.width == cambc::map::width() &&
        c.height == cambc::map::height() && c.groups <= 8 && c.altars <= 2 &&
        c.pick_count <= 5 && c.team_size >= 3 && c.team_size <= 5;
}
inline int node_count(const Cache& c) { return c.groups + c.altars + 1; }
inline Cache metadata() {
    Cache c;
    c.magic = cache_magic; c.width = cambc::map::width(); c.height = cambc::map::height();
    c.team_size = 3;
    for (auto& row : c.distance) row.fill(unknown_distance);
    const auto walls = static_walls();
    const auto spawn = cambc::map::spawn_region(team::ally).positions().front();
    auto all = cambc::map::camp_groups();
    bitboard camp_spawns;
    for (const auto& g : all) camp_spawns |= g.spawn_tiles();
    std::sort(all.begin(), all.end(), [&](const auto& a, const auto& b) {
        // Public bootstrap opportunity ordering, not a claim about unseen life.
        auto work = [&](const auto& g) {
            double hp = 0, incoming = 0;
            for (const auto& m : g) { hp += m.base_stats().base_health; incoming += m.base_stats().base_strength; }
            return hp * std::max(1.0, incoming) + cheb(spawn, g.camps().front().spawn_position());
        };
        const auto wa = work(a), wb = work(b);
        return wa != wb ? wa < wb : gid(a) < gid(b);
    });
    for (const auto& g : all) {
        if (!g.gem_value() || c.groups == groups_cap || g.camps().size() > 8) continue;
        bitboard near;
        for (const auto& member : g) near.draw_chebyshev(member.spawn_position(), 1);
        near &= ~(walls | camp_spawns);
        if (near.is_empty()) continue;
        auto places = near.positions();
        std::sort(places.begin(), places.end(), [&](vec2 a, vec2 b) {
            return cheb(a, spawn) != cheb(b, spawn) ? cheb(a, spawn) < cheb(b, spawn) :
                a.y != b.y ? a.y < b.y : a.x < b.x;
        });
        c.ids[c.groups] = gid(g); c.nodes[c.groups++] = tile(places.front());
    }
    auto altars = cambc::map::altars().positions();
    std::sort(altars.begin(), altars.end(), [&](vec2 a, vec2 b) {
        return cheb(a, spawn) != cheb(b, spawn) ? cheb(a, spawn) < cheb(b, spawn) :
            a.y != b.y ? a.y < b.y : a.x < b.x;
    });
    for (vec2 altar : altars) {
        if (c.altars == 2) break;
        bitboard adjacent; adjacent.draw_chebyshev(altar, 1);
        auto places = (adjacent & ~(walls | camp_spawns)).positions();
        if (places.empty()) continue;
        c.altar_tiles[c.altars] = tile(altar);
        c.nodes[c.groups + c.altars++] = tile(places.front());
    }
    c.nodes[c.groups + c.altars] = tile(spawn);
    return c;
}
inline Cache read_cache() {
    Cache c;
    if (cambc::draft_memory.size() >= sizeof(c)) std::memcpy(&c, cambc::draft_memory.data(), sizeof(c));
    return valid(c) ? c : metadata();
}
inline void field_row(Cache& c, int row) {
    cambc::distance_field field; bitboard visited;
    const auto before = cambc::fuel_remaining();
    const auto expanded = field.compute_with_expansion_count(static_walls(), at(position(c.nodes[row])), visited, 256);
    for (int j = 0; j < node_count(c); ++j) c.distance[row][j] = field.get(position(c.nodes[j]));
    c.rows |= std::uint16_t(1u << row);
    std::printf("P5 field=draft row=%d expansions=%u fuel=%u\n", row, unsigned(expanded), unsigned(before - cambc::fuel_remaining()));
}
inline cambc::champion_type design_type(int pick) {
    return static_cast<cambc::champion_type>(pick < 4 ? pick : pick + 1);
}
inline void draft_impl(const p5::DraftRequest& request) {
    static Cache c = read_cache();
    c.team_size = std::uint8_t(cambc::draft::team_size());
    const auto selected = cambc::draft::current_draft(team::ally);
    p5::DraftContext context;
    context.team_size = c.team_size; context.pick_index = int(selected.size()); context.own_count = selected.size();
    context.independent_cells = c.groups;
    for (std::size_t i = 0; i < selected.size(); ++i) {
        c.picks[i] = std::uint8_t(selected[i]); context.own_picks[i] = p5::draft_aliases[c.picks[i]];
    }
    context.pool_count = 12;
    for (int i = 0; i < 12; ++i) {
        const auto s = cambc::champion_designs[int(design_type(i))].base_stats();
        auto& t = context.pool[i]; t.alias = p5::draft_aliases[i]; t.known = true;
        t.actors = p5::draft_actor_count(t.alias);
        t.productivity = s.base_strength * (s.base_attack_range > 1 ? .5 : 1);
        t.health = s.base_health; t.mobility = s.base_move_speed;
        if (i == 3) { const auto extra = cambc::champion_designs[int(cambc::champion_type::issac)].base_stats();
            t.productivity += extra.base_strength; t.health = std::min(t.health, double(extra.base_health)); }
    }
    const auto choice = p5::choose_draft(context, request);
    if (!choice.feasible) throw std::runtime_error("P5 full-pool draft has no legal choice");
    for (int row = 0; row < node_count(c) && cambc::fuel_remaining() > 150000; ++row)
        if (!(c.rows & (1u << row))) field_row(c, row);
    int pick = 0; while (pick < 12 && p5::draft_aliases[pick] != choice.alias) ++pick;
    if (pick == 12) throw std::runtime_error("P5 invalid draft alias");
    c.picks[selected.size()] = std::uint8_t(pick); c.pick_count = std::uint8_t(selected.size() + 1);
    if (cambc::draft_memory.size() < sizeof(c)) throw std::runtime_error("P5 draft memory too small");
    cambc::draft_memory = std::span<const Cache>{&c, 1};
    cambc::draft::pick(static_cast<cambc::champion_draft>(pick));
}

struct LiveGroup {
    cambc::map::camp_group native;
    bitboard spawns;
    int id, index;
    bool known = false, reward = false, respawning = false;
    p5::CombatInput combat{};
};
enum class NodeKind { current, group_entry, altar_entry, spawn };
struct Node { NodeKind kind = NodeKind::current; int id = -1; };
enum class GoalKind { none, camp_entry, camp_attack, camp_spawn, altar, spawn, productive_entry, recover };
// A card supplies its fresh full-tail proof for this named target. These modes
// do not relax external exposure, point safety, channels or SDK action legality.
enum class TargetCombatMode { basic_only, prism_one_primary, ruin_one_self_shield, ruin_passive_only };
constexpr bool target_mode_matches_type(TargetCombatMode mode, cambc::champion_type type) {
    return (mode == TargetCombatMode::prism_one_primary && type == cambc::champion_type::prism) ||
        ((mode == TargetCombatMode::ruin_one_self_shield || mode == TargetCombatMode::ruin_passive_only) &&
         type == cambc::champion_type::heart_of_ruin);
}
constexpr bool target_event_continues(int now, int observed, bool alive, bool known, bool respawning,
                                      std::uint64_t captured_before, std::uint64_t captured_now) {
    return alive && known && !respawning && observed >= 0 && now >= observed && now - observed <= 1 &&
        captured_before == captured_now;
}
static_assert(target_mode_matches_type(TargetCombatMode::prism_one_primary, cambc::champion_type::prism));
static_assert(!target_mode_matches_type(TargetCombatMode::prism_one_primary, cambc::champion_type::heart_of_ruin));
static_assert(target_mode_matches_type(TargetCombatMode::ruin_one_self_shield, cambc::champion_type::heart_of_ruin));
static_assert(target_mode_matches_type(TargetCombatMode::ruin_passive_only, cambc::champion_type::heart_of_ruin));
static_assert(!target_mode_matches_type(TargetCombatMode::basic_only, cambc::champion_type::prism));
static_assert(target_event_continues(6, 5, true, true, false, 2, 2));
static_assert(!target_event_continues(7, 5, true, true, false, 2, 2));
static_assert(!target_event_continues(6, 5, false, true, false, 2, 2));
static_assert(!target_event_continues(6, 5, true, false, false, 2, 2));
static_assert(!target_event_continues(6, 5, true, true, true, 2, 2));
static_assert(!target_event_continues(6, 5, true, true, false, 2, 3));
struct Goal {
    GoalKind kind = GoalKind::none;
    int group_id = -1, altar_tile_id = -1;
    int controller_stage = -1, forecast_turns = 8, purchase_family = -1;
    e::Phase phase = e::Phase::bootstrap;
    bool production = false, bank_requested = false;
    TargetCombatMode target_combat_mode = TargetCombatMode::basic_only;
    int primary_cast_slot = -1, primary_cast_target = -1;
    std::uint64_t target_combat_stamp = 0;
    bool operator==(const Goal&) const = default;
};
struct GroupFacts {
    int id = -1, entry_tile_id = -1, gems = 0, gold = 0;
    bool known = false, reward = false, respawning = false, other_holder = false;
    p5::CombatInput combat{};
    double external_stress = 0;
};
struct AltarFacts { int tile_id = -1; bool visible = false, hostile_lock = false, adjacent = false; };
struct PurchaseQuote {
    int family = -1, cost = 0, item_id = -1;
    bool purchasable = false;
    double strength_gain = 0, health_gain = 0, spell_damage_gain = 0;
};
struct OwnReceipts {
    std::uint64_t capture = 0, bank = 0, purchase = 0;
    int capture_round = -1, capture_group = -1, capture_gems = 0, bank_round = -1;
    int purchase_round = -1, purchase_family = -1, purchase_item = -1, purchase_cost = 0, purchase_count = 0;
    bool capture_gold_known = false;
    int capture_gold = 0;
};
struct Snapshot {
    int round = -1, body_ordinal = 0, team_score = 0, fuel = 0, position_tile_id = -1;
    std::uint64_t epoch = 0;
    bool complete = false, alive = false, settings_valid = false;
    e::Channel channel = e::Channel::none;
    bool channel_safe = false, safe_hold = false, needs_recovery = false, at_spawn = false;
    cambc::champion_type type = cambc::champion_type::ryth;
    p5::ActorObservation self{};
    p5::CombatActor stats{};
    p5::EconomicSettings settings{};
    p5::FinishWindows windows{};
    OwnReceipts receipts{};
    std::array<GroupFacts, 8> groups{};
    int group_count = 0;
    std::array<AltarFacts, 2> altars{};
    int altar_count = 0;
    std::array<PurchaseQuote, 3> purchases{};
    double route_stress = 0, next_incoming = 0;
};
struct CardBuildTrace {
    int stage = 0, reason = 0, group = -1, profile = -1;
    int cache_misses = 0, kernel_calls = 0, prepared_cells = 0, profile_count = 0;
    // Existing caller guard/MISS samples only; -1 means not sampled.
    int prepare_fuel = -1;
    int first_candidate_group = -1, first_candidate_entry_fuel = -1, first_candidate_quote_fuel = -1;
    int candidate_group = -1, candidate_entry_fuel = -1, candidate_quote_fuel = -1;
    int miss_fuel_total = 0, miss_fuel_max = 0, quote_peak = 0;
};
static_assert(sizeof(CardBuildTrace) == 72);
enum class ApplyResult { acted, stopped, stale };
// Both values certify an unchanged admission guard returned before apply_native.
// Kept separate from ApplyResult so existing callers retain their semantics.
enum class ApplyBudgetDeferral { none, entry, after_move };
// Optional bounded diagnostics. A pre-native matching feedback packet may be
// old. native_called means apply_native was entered, not that an SDK call ran.
// Physical success requires acted plus matching successful mutating feedback.
enum class ApplyExit { none, authority, target_combat, entry_fuel, move_binding,
    move_legality, round, post_move_fuel, native_phase, native_rejected, acted };
struct ApplyTrace {
    ApplyExit exit = ApplyExit::none;
    ApplyResult result = ApplyResult::stopped;
    int entry_round = -1, exit_round = -1, entry_fuel = -1, move_fuel = -1, exit_fuel = -1;
    bool native_called = false, feedback_matches = false, feedback_success = false;
    std::uint64_t sequence = 0, feedback_sequence = 0;
    e::ActionKind kind = e::ActionKind::yield, feedback_kind = e::ActionKind::yield;
    int target = -1, feedback_target = -1;
};
enum class UnexpectedErrorKind { sdk, standard, unknown };
constexpr std::array<char, 97> bounded_error_text(const char* source) noexcept {
    std::array<char, 97> out{};
    if (source) for (std::size_t i = 0; i < 96 && source[i]; ++i)
        out[i] = source[i] == '\n' || source[i] == '\r' || source[i] == '\t' ? ' ' : source[i];
    return out;
}
constexpr unsigned diagnostic_increment(unsigned value) noexcept {
    return value == std::numeric_limits<unsigned>::max() ? value : value + 1;
}
constexpr bool reference_action_mutates(e::ActionKind kind, bool successful) noexcept {
    return successful && (kind == e::ActionKind::move || kind == e::ActionKind::vacate ||
        kind == e::ActionKind::withdraw || kind == e::ActionKind::attack || kind == e::ActionKind::cast ||
        kind == e::ActionKind::buy || kind == e::ActionKind::start_tribute ||
        kind == e::ActionKind::start_recall || kind == e::ActionKind::cancel_channel);
}
constexpr bool capture_offer_serial_ok(int now, int received, int group, int expected_group,
    std::uint64_t serial, std::uint64_t own_serial, std::uint64_t consumed, bool accepted) noexcept {
    return now >= 0 && received == now && group >= 0 && group == expected_group && serial > 0 &&
        serial == own_serial && (serial > consumed || (accepted && serial == consumed));
}
constexpr bool terminal_bank_replan(bool alive, bool no_channel, bool already_winning,
    std::int64_t score, std::int64_t bag, std::int64_t winning) noexcept {
    return alive && no_channel && !already_winning && score >= 0 && bag > 0 && winning > 0 &&
        (score >= winning || bag >= winning - score);
}
constexpr bool captured_stock_continues(int now, int observed, bool alive, int bag, int held) noexcept {
    return alive && held > 0 && bag >= held && observed >= 0 && now >= observed && now - observed <= 1;
}
constexpr bool captured_bank_completed(int now, bool alive, int bag, std::uint64_t receipt,
    std::uint64_t baseline, int receipt_round, int starting_bag, int held) noexcept {
    return alive && bag == 0 && held > 0 && starting_bag > 0 && receipt > baseline && receipt_round == now;
}
static_assert(captured_stock_continues(208, 207, true, 6, 6));
static_assert(captured_stock_continues(217, 216, true, 6, 6)); // Recovery changes HP, not this obligation.
static_assert(!captured_stock_continues(209, 207, true, 6, 6));
static_assert(!captured_stock_continues(208, 207, false, 6, 6));
static_assert(!captured_stock_continues(208, 207, true, 5, 6));
static_assert(captured_bank_completed(230, true, 0, 2, 1, 230, 6, 6));
static_assert(captured_bank_completed(230, true, 0, 2, 1, 230, 6, 12)); // Capture can increase an active tribute's whole bag.
static_assert(!captured_bank_completed(230, true, 0, 1, 1, 230, 6, 6));
static_assert(!captured_bank_completed(230, true, 0, 2, 1, 229, 6, 6));
static_assert(!captured_bank_completed(230, true, 0, 2, 1, 230, 0, 6));
static_assert(!captured_bank_completed(230, true, 6, 2, 1, 230, 6, 6));
static_assert(!captured_bank_completed(230, false, 0, 2, 1, 230, 6, 6));
static_assert(bounded_error_text("bad\nquery")[3] == ' ');
static_assert(bounded_error_text(nullptr)[0] == 0);
static_assert(bounded_error_text("12345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890")[96] == 0);
static_assert(diagnostic_increment(std::numeric_limits<unsigned>::max()) == std::numeric_limits<unsigned>::max());
static_assert(!reference_action_mutates(e::ActionKind::inspect, true));
static_assert(!reference_action_mutates(e::ActionKind::move, false));
static_assert(reference_action_mutates(e::ActionKind::start_tribute, true));
static_assert(capture_offer_serial_ok(8, 8, 4, 4, 2, 2, 1, false));
static_assert(capture_offer_serial_ok(8, 8, 4, 4, 2, 2, 2, true));
static_assert(!capture_offer_serial_ok(8, 7, 4, 4, 2, 2, 1, false));
static_assert(!capture_offer_serial_ok(8, 8, 5, 4, 2, 2, 1, false));
static_assert(!capture_offer_serial_ok(8, 8, 4, 4, 2, 3, 1, false));
static_assert(!capture_offer_serial_ok(8, 8, 4, 4, 2, 2, 2, false));
static_assert(terminal_bank_replan(true, true, false, 991, 9, 1000));
static_assert(!terminal_bank_replan(true, true, false, 991, 8, 1000));
static_assert(!terminal_bank_replan(true, false, false, 991, 9, 1000));
static_assert(!terminal_bank_replan(true, true, true, 991, 9, 1000));
static_assert(!terminal_bank_replan(false, true, false, 991, 9, 1000));
struct SpawnRecoveryQuote { bool valid = false; int heal_ticks = 0; };
struct CaptureGoldDelta { bool known = false; int amount = 0; };
constexpr bool equipment_shop_location_allowed(bool at_spawn, bool tavi, bool equipment) {
    return at_spawn || (tavi && equipment);
}
constexpr bool production_row_needs_quote(bool production, bool assigned, bool known, bool respawning) {
    return production && assigned && known && !respawning;
}
constexpr bool target_quote_reusable(int observed_target, int row, bool quoted) {
    return quoted && observed_target >= 0 && observed_target == row;
}
constexpr int production_selection_delay(bool has_production, bool bank_first) {
    return has_production && !bank_first ? 1 : 0;
}
static_assert(production_row_needs_quote(true, true, true, false));
static_assert(!production_row_needs_quote(true, false, true, false));
static_assert(!production_row_needs_quote(true, true, false, false));
static_assert(!production_row_needs_quote(true, true, true, true));
static_assert(!production_row_needs_quote(false, true, true, false));
static_assert(target_quote_reusable(7, 7, true));
static_assert(!target_quote_reusable(7, 8, true));
static_assert(!target_quote_reusable(7, 7, false));
static_assert(production_selection_delay(true, false) == 1);
static_assert(production_selection_delay(true, true) == 0);
static_assert(production_selection_delay(false, false) == 0);
static_assert(equipment_shop_location_allowed(false, true, true));
static_assert(!equipment_shop_location_allowed(false, false, true));
static_assert(!equipment_shop_location_allowed(false, true, false));
static_assert(equipment_shop_location_allowed(true, false, true));
constexpr CaptureGoldDelta capture_gold_delta(std::int64_t before, std::int64_t after,
                                               std::uint64_t passive, bool clean_interval) {
    if (!clean_interval || before < 0 || after < before) return {};
    const auto delta = std::uint64_t(after - before);
    if (delta < passive || delta - passive > std::uint64_t(std::numeric_limits<int>::max())) return {};
    return {true, int(delta - passive)};
}
static_assert(capture_gold_delta(10, 50, 0, true).known && capture_gold_delta(10, 50, 0, true).amount == 40);
static_assert(capture_gold_delta(10, 52, 2, true).amount == 40);
static_assert(!capture_gold_delta(10, 50, 0, false).known);
static_assert(!capture_gold_delta(10, 9, 0, true).known);
static_assert(!capture_gold_delta(10, 11, 2, true).known);
// A30-gold intervening buy must not turn a40-gold payout into a10-gold receipt.
static_assert(!capture_gold_delta(10, 20, 0, false).known);
// The prefix must survive before any healing is credited. Healing is the
// queried fraction of maximum health, observed at later owner-turn starts.
constexpr SpawnRecoveryQuote spawn_recovery_quote(double health, double maximum,
                                                  double fraction, int available_ticks) {
    if (!(maximum > 0 && health >= .2 * maximum && health <= maximum &&
          fraction > 0 && fraction <= 1) || available_ticks < 0) return {};
    const double healing = maximum * fraction, missing = maximum - health;
    if (!(healing > 0 && healing <= maximum)) return {};
    const double exact = missing / healing;
    if (!(exact >= 0 && exact <= available_ticks)) return {};
    int ticks = int(exact);
    if (double(ticks) * healing < missing) ++ticks;
    return ticks <= available_ticks ? SpawnRecoveryQuote{true, ticks} : SpawnRecoveryQuote{};
}
constexpr bool spawn_recovery_confirmed(int now, int arrival, int previous_spawn,
                                        bool alive, bool at_spawn, double health, double maximum) {
    return alive && at_spawn && arrival >= 0 && now > arrival && previous_spawn == now - 1 &&
        maximum > 0 && maximum <= std::numeric_limits<double>::max() && health == maximum;
}
constexpr bool tail_health_safe(double health, double maximum, double damage,
                                 double threat, int ticks) {
    if (!(health > 0 && health <= maximum && maximum <= std::numeric_limits<double>::max() &&
          damage >= 0 && threat >= 0) || ticks < 0) return false;
    const double after = health - damage - threat * ticks;
    return after > 0 && after >= .2 * maximum && after >= 2 * threat;
}
constexpr int liquidation_ticks(int distance, int locks, int channel, bool adjacent_open) noexcept {
    if ((!adjacent_open && distance < 0) || distance > 1000000 || locks < 0 || locks > 1000000 || channel <= 0 || channel > 1000000) return 0;
    return (adjacent_open ? 0 : distance + 2 + locks) + channel;
}
static_assert(liquidation_ticks(20, 5, 1, false) == 28);
static_assert(liquidation_ticks(20, 5, 1, true) == 1);
static_assert(liquidation_ticks(-1, 5, 1, true) == 1); // A legal adjacent bank needs no path to the cached entry.
static_assert(!liquidation_ticks(-1, 5, 1, false));
static_assert(!liquidation_ticks(20, 5, 0, false));
// Safe conversion does not require crediting an unsafe productive successor.
static_assert(tail_health_safe(900, 1200, 0, 0, 60));
static_assert(!tail_health_safe(900, 1200, 900, 0, 60));
static_assert(!tail_health_safe(900, 1200, 0, 20, 60));
static_assert(spawn_recovery_quote(320, 800, .1, 6).valid &&
              spawn_recovery_quote(320, 800, .1, 6).heal_ticks == 6);
static_assert(800 - 480 == 320 && 800 - 480 >= .2 * 800);
static_assert(800 - 480 - 480 < .2 * 800);
static_assert(!spawn_recovery_quote(320, 800, .1, 5).valid);
static_assert(!spawn_recovery_quote(159, 800, .1, 100).valid);
static_assert(!spawn_recovery_quote(320, 800, 0, 100).valid);
static_assert(!spawn_recovery_quote(320, 800, std::numeric_limits<double>::quiet_NaN(), 100).valid);
static_assert(!spawn_recovery_confirmed(10, 10, 9, true, true, 800, 800));
static_assert(!spawn_recovery_confirmed(16, 10, 15, true, true, 720, 800));
static_assert(!spawn_recovery_confirmed(17, 10, 15, true, true, 800, 800));
static_assert(!spawn_recovery_confirmed(17, 10, 16, false, true, 800, 800));
static_assert(!spawn_recovery_confirmed(17, 10, 16, true, false, 800, 800));
static_assert(spawn_recovery_confirmed(17, 10, 16, true, true, 800, 800));
// An unexpectedly costly first fight invalidates a previously safe direct
// successor, but it cannot prevent a separately timed, actually gated heal.
static_assert(tail_health_safe(600, 800, 400, 0, 10));
static_assert(!tail_health_safe(400, 800, 400, 0, 10));
static_assert(spawn_recovery_quote(400, 800, .1, 5).valid &&
              spawn_recovery_quote(400, 800, .1, 5).heal_ticks == 5);
static_assert(tail_health_safe(800, 800, 400, 0, 10));
// A forced startup turn can cross either the actual reserve or the last
// available tick. A current-turn bank prefix has neither added delay.
static_assert(tail_health_safe(200, 1000, 0, 10, production_selection_delay(false, false)));
static_assert(!tail_health_safe(200, 1000, 0, 10, production_selection_delay(true, false)));
static_assert(5 + production_selection_delay(true, false) > 5);
static_assert(5 + production_selection_delay(true, true) == 5);
enum class Stage { none, initial_bank, production, output_bank, recovery, renewal, liquidation_bank };
struct Plan {
    bool feasible = false, bank_first = false, bank_only = false, winning = false, via_spawn = false;
    int group = -1, altar = -1, renewal = -1, duration = 8, renewal_tick = 0;
    double renewal_damage = 0;
    int renewal_work_ticks = 0, recovery_ticks = 0, recovery_heal_ticks = 0;
    p5::AccountingSchedule schedule{};
    p5::AccountingResult rank{};
};
class Runtime {
    Cache cache_{};
    std::vector<LiveGroup> groups_;
    p5::EconomicSettings settings_{};
    p5::ScoreHistory history_{};
    e::State state_{};
    Plan plan_{};
    Plan candidate_scratch_{}, best_scratch_{};
    cambc::champion_type own_type_ = cambc::champion_type::ryth;
    cambc::champion_stats own_base_{};
    std::array<PurchaseQuote, 3> purchase_quotes_{};
    p5::CombatActor frame_actor_{};
    bool frame_actor_valid_ = false;
    int frame_actor_round_ = -1;
    std::uint64_t frame_actor_epoch_ = 0;
    int plan_start_round_ = -1;
    double plan_external_stress_ = 0;
    bool residual_invalid_ = false;
    Stage stage_ = Stage::none;
    bool initialized_ = false, settings_ok_ = false, recovering_ = false;
    int cold_completed_round_ = -1;
    int reference_packet_round_ = -1, packet_position_tile_ = -1;
    int round_ = -1, previous_round_ = -1, previous_bag_ = 0, previous_gold_ = 0;
    int previous_group_ = -1, previous_spawn_round_ = -1;
    bool previous_submitting_ = false, previous_alive_ = false, previous_group_reward_ = false;
    bool previous_gold_finalized_ = false;
    std::uint64_t purchase_attempt_sequence_ = 0, previous_purchase_sequence_ = 0;
    bool moved_ = false, cast_ = false, reset_after_move_ = false;
    bool bank_intent_ = false; int bank_start_round_ = -1, bank_start_bag_ = 0, bank_start_ticks_ = 0;
    int last_attack_round_ = -1;
    std::uint64_t payout_receipt_ = 0, bank_receipt_ = 0, bank_baseline_ = 0, epoch_ = 0;
    e::Observation feedback_{};
    OwnReceipts own_receipts_{};
    enum class BankDisposition { none, reference, card, converted, unknown };
    struct CapturedBank {
        BankDisposition disposition = BankDisposition::none;
        int actor = -1, group = -1, captured_round = -1, observed_round = -1, bag = 0;
        std::uint64_t capture = 0, bank_baseline = 0;
        int altar = -1;
        bool pending() const noexcept {
            return disposition == BankDisposition::reference || disposition == BankDisposition::card;
        }
    } captured_bank_{};
    struct TargetBinding {
        Goal goal{};
        int actor = -1, round = -1, position = -1, bag = 0, gold = 0;
        double health = 0, maximum = 0, strength = 0, focus = 0;
        std::uint64_t epoch = 0, action = 0;
    } target_binding_{};
    struct TargetEvent {
        TargetCombatMode mode = TargetCombatMode::basic_only;
        int actor = -1, group = -1, slot = -1, target = -1, round = -1, observed_round = -1;
        std::uint64_t capture = 0;
    } target_event_{};
    std::uint64_t target_stamp_ = 0, native_action_sequence_ = 0;
    struct MoveBinding {
        bool valid = false;
        Goal goal{};
        e::Phase phase = e::Phase::bootstrap;
        int target = -1, round = -1;
        std::uint64_t epoch = 0;
        std::array<int, 4> destinations{};
        std::size_t count = 0;
        int origin = -1;
        std::uint64_t geometry = 0;
    } move_binding_{};
    bitboard visible_{}, danger_{};
    std::vector<cambc::camp> camps_;
    std::vector<cambc::champion> enemies_;
    std::array<int, nodes_cap> current_distance_{};
    int position_field_round_ = -1;
    vec2 position_field_origin_{};
    // The initialized geometry/node generation never changes during a match.
    // These distances contain no units, reservations, vision or combat facts.
    std::uint64_t geometry_generation_ = 0, position_field_geometry_ = 0;
    bool position_distances_valid_ = false;
    bool snapshot_reusable_ = false;
    int reusable_round_ = -1;
    std::uint64_t reusable_epoch_ = 0;
    vec2 reusable_origin_{};
    std::optional<cambc::distance_field> move_field_{};
    std::vector<vec2> move_roots_{};
    double enemy_stress_ = 0, route_stress_ = 0, next_incoming_ = 0;
    int own_ordinal_ = 0, own_count_ = 1;
    enum class FuelPhase { snapshot_refresh, snapshot_export, reference_refresh, reference_observe, execution_observe, select, apply, position_field };
    struct FuelStart { int round = -1; unsigned fuel = 0; };
    unsigned callback_count_ = 0;
    unsigned snapshot_count_ = 0, refresh_count_ = 0, observe_count_ = 0, forecast_count_ = 0;
    unsigned execution_quote_reuses_ = 0, execution_work_calls_ = 0;
    mutable unsigned actor_count_ = 0;
    std::array<unsigned, 8> phase_cost_{};
    unsigned crossed_phases_ = 0, entry_fuel_ = 0, feasible_count_ = 0;
    unsigned field_compute_fuel_ = 0, field_expansions_ = 0, field_reuses_ = 0;
    unsigned move_plan_fuel_ = 0, move_compute_fuel_ = 0, move_expansions_ = 0, move_destroy_fuel_ = 0;
    unsigned precheck_skips_ = 0, precheck_mask_ = 0, selection_budget_stops_ = 0;
    // Saved Build10 Fundamentals-A: largest completed select chunk excluding
    // its position field was37804 fuel; first reference observation was5503.
    // Retain the existing16000 action/finalization reserve after each chunk.
    unsigned selection_chunk_peak_ = 37804, observe_chunk_peak_ = 5503;
    unsigned selection_finalize_fuel_ = 0, selection_finalize_peak_ = 0, selection_deferrals_ = 0;
    std::array<unsigned, 3> card_cost_{};
    unsigned card_opportunities_ = 0;
    bool card_certified_ = false;
    int last_action_ = -1, last_reason_ = -1;
    std::uint64_t gap_count_ = 0, crossing_count_ = 0;
    bool phase_aborted_ = false;
    struct FuelReport {
        bool pending = false, cold = false;
        int round = -1, end_round = -1, action = -1, reason = -1;
        unsigned callback = 0, entry = 0, end = 0, cross_mask = 0;
        unsigned compute = 0, expansions = 0, reuses = 0, candidates = 0, feasible = 0;
        std::array<unsigned, 8> phase{};
        std::array<unsigned, 3> card{};
        unsigned opportunities = 0;
        bool certified = false;
        std::array<unsigned, 4> queries{};
        std::array<unsigned, 4> move{};
        std::array<unsigned, 3> skips{};
        // Selected target plus the last fresh packet's position/live facts.
        std::array<int, 9> target{};
        std::array<unsigned, 3> selection{};
        bool card_attempt = false, menu_complete = true, has_build_trace = false;
        CardBuildTrace build_trace{};
    } report_{};
    unsigned report_cost_ = 0, max_report_cost_ = 0, reported_callback_ = 0;
    bool traced_report_measured_ = false;
    std::uint64_t reported_crossings_ = 0;
    struct UnexpectedError {
        bool pending = false;
        UnexpectedErrorKind kind = UnexpectedErrorKind::unknown;
        int actor = -1, cached_round = -1, reference_stage = -1, reference_phase = -1;
        int controller_stage = -1, controller_phase = -1, action = -1, target = -1, code = -1;
        std::array<char, 97> text{};
    };
    std::array<UnexpectedError, 2> unexpected_{};
    unsigned unexpected_count_ = 0, unexpected_dropped_ = 0, error_report_peak_ = 0;
    int cached_actor_ = -1, cached_error_round_ = -1;
    int cached_controller_stage_ = -1, cached_controller_phase_ = -1;
    int cached_error_action_ = -1, cached_error_target_ = -1;
    bool flushing_diagnostics_ = false, diagnostics_blocked_ = false;
    int diagnostic_emit_round_ = -1;
    struct CardReportRequest {
        bool pending = false, certified = false, menu_complete = true, has_build_trace = false;
        int round = -1, end_round = -1;
        unsigned callback = 0, entry = 0, end = 0, opportunities = 0;
        unsigned cross_mask = 0;
        std::array<unsigned, 3> costs{};
        CardBuildTrace build_trace{};
    };
    std::array<CardReportRequest, 3> card_reports_{};
    unsigned card_report_count_ = 0;
    struct CardCompletion {
        bool pending = false;
        int actor = -1, round = -1, job = -1, group = -1, family = -1, renewal = -1;
    };
    std::array<CardCompletion, 2> card_completions_{};
    unsigned card_completion_count_ = 0, card_completion_dropped_ = 0, completion_report_peak_ = 0;
    p5::diagnostics::CardLifeQueue card_life_{};
    unsigned card_life_report_peak_ = 0;
    void cache_goal_context(const Goal& goal) noexcept {
        cached_controller_stage_ = goal.controller_stage; cached_controller_phase_ = int(goal.phase);
    }
    void invalidate_latches() {
        if (captured_bank_.pending()) captured_bank_.disposition = BankDisposition::unknown;
        previous_round_ = previous_group_ = previous_spawn_round_ = -1;
        previous_alive_ = previous_submitting_ = previous_group_reward_ = false;
        previous_gold_finalized_ = false;
        reset_target_combat();
        bank_intent_ = false; move_binding_.valid = false; snapshot_reusable_ = false; frame_actor_valid_ = false;
        stage_ = Stage::none; state_ = {}; phase_aborted_ = true; ++epoch_;
    }
    FuelStart phase_begin(FuelPhase) {
        return {int(cambc::round_number()), unsigned(cambc::fuel_remaining())};
    }
    bool phase_end(FuelPhase phase, const FuelStart& start) {
        const unsigned fuel = unsigned(cambc::fuel_remaining()); const int now = int(cambc::round_number());
        const bool same = start.round == round_ && now == round_;
        const bool new_crossing = !same && !phase_aborted_;
        if (new_crossing) ++crossing_count_;
        if (same && start.fuel >= fuel) phase_cost_[unsigned(phase)] += start.fuel - fuel;
        if (same && start.fuel >= fuel && phase == FuelPhase::reference_observe)
            observe_chunk_peak_ = std::max(observe_chunk_peak_, start.fuel - fuel);
        if (!same) { crossed_phases_ |= 1u << unsigned(phase); invalidate_latches(); }
        return same;
    }
    bool promote_card_report() noexcept {
        if (report_.pending) return false;
        for (auto& request : card_reports_) if (request.pending) {
            report_ = {}; report_.pending = report_.card_attempt = true;
            report_.round = request.round; report_.end_round = request.end_round;
            report_.callback = request.callback; report_.entry = request.entry; report_.end = request.end;
            report_.card = request.costs; report_.opportunities = request.opportunities;
            report_.certified = request.certified; report_.cross_mask = request.cross_mask; report_.target.fill(-1);
            report_.menu_complete = request.menu_complete;
            report_.has_build_trace = request.has_build_trace;
            if (request.has_build_trace) report_.build_trace = request.build_trace;
            request.pending = false; return true;
        }
        return false;
    }
    void queue_report() {
        if (report_.pending || promote_card_report()) return;
        // First two warm callbacks plus callback four cover both startup and
        // steady work. At most four later crossing reports are retained.
        const bool sampled = callback_count_ <= 2 || callback_count_ == 4;
        const bool crossing = crossing_count_ > reported_crossings_ && crossing_count_ <= 4;
        if (report_.pending || reported_callback_ == callback_count_ || (!sampled && !crossing)) return;
        const auto* target = group(state_.target);
        const int entry = target ? tile_id(position(cache_.nodes[target->index])) : -1;
        const vec2 p{packet_position_tile_ >= 0 ? packet_position_tile_ % cache_.width : -1,
            packet_position_tile_ >= 0 ? packet_position_tile_ / cache_.width : -1};
        const bool at_target = target && packet_position_tile_ >= 0 &&
            (packet_position_tile_ == entry || target->spawns.is_set(p));
        const std::array<int, 9> trace{state_.target, target ? target->index : -1, entry, packet_position_tile_,
            at_target, target && target->known, target ? int(target->combat.monster_count) : 0, state_.failed_moves,
            feedback_.feedback_kind == e::ActionKind::move ? int(feedback_.feedback_success) : -1};
        const unsigned end_fuel = unsigned(cambc::fuel_remaining());
        const int end_round = int(cambc::round_number());
        if (end_round != round_) {
            if (!phase_aborted_) ++crossing_count_;
            crossed_phases_ |= 1u << 13; invalidate_latches();
        }
        report_ = {true, false, round_, end_round, last_action_, last_reason_,
            callback_count_, entry_fuel_, end_fuel, crossed_phases_,
            field_compute_fuel_, field_expansions_, field_reuses_, forecast_count_, feasible_count_, phase_cost_};
        report_.card = card_cost_; report_.opportunities = card_opportunities_; report_.certified = card_certified_;
        report_.queries = {snapshot_count_, refresh_count_, observe_count_, actor_count_};
        report_.move = {move_plan_fuel_, move_compute_fuel_, move_expansions_, move_destroy_fuel_};
        report_.skips = {precheck_skips_, precheck_mask_, selection_budget_stops_};
        report_.target = trace;
        report_.selection = {selection_chunk_peak_, selection_finalize_fuel_, selection_deferrals_};
        reported_callback_ = callback_count_; reported_crossings_ = crossing_count_;
    }
    void flush_report(bool fresh_entry = false) {
        if (!report_.pending || diagnostics_blocked_) return;
        // Learn each format's first cost only at a fresh callback entry. Later
        // reports use the measured maximum plus the existing action reserve.
        const bool traced = report_.card_attempt && report_.has_build_trace;
        if ((!max_report_cost_ || (traced && !traced_report_measured_)) && !fresh_entry) return;
        flushing_diagnostics_ = true;
        if (max_report_cost_ && cambc::fuel_remaining() < max_report_cost_ + 16000) { flushing_diagnostics_ = false; return; }
        const unsigned before = unsigned(cambc::fuel_remaining());
        const int start_round = int(cambc::round_number());
        if (diagnostic_emit_round_ == start_round) { flushing_diagnostics_ = false; return; }
        diagnostic_emit_round_ = start_round;
        const auto& build = report_.build_trace;
        // Exactly one branch formats a record under the shared deferred gate.
        // Card attempts omit unrelated full-perf fields to bound formatting.
        int written;
        if (traced) {
            written = std::printf("P5 card a=%d r=%d:%d cb=%u fuel=%u:%u card=%u/%u/%u count=%u cert=%d menu=%d x=%u build=%d/%d/%d/%d/%d/%d/%d/%d phase=%d/%d/%d/%d/%d/%d/%d/%d/%d/%d log=%u\n",
                cached_actor_, report_.round, report_.end_round, report_.callback, report_.entry, report_.end,
                report_.card[0], report_.card[1], report_.card[2], report_.opportunities, report_.certified,
                report_.menu_complete, report_.cross_mask,
                build.stage, build.reason, build.group, build.profile,
                build.cache_misses, build.kernel_calls, build.prepared_cells, build.profile_count,
                build.prepare_fuel, build.first_candidate_group, build.first_candidate_entry_fuel, build.first_candidate_quote_fuel,
                build.candidate_group, build.candidate_entry_fuel, build.candidate_quote_fuel,
                build.miss_fuel_total, build.miss_fuel_max, build.quote_peak, report_cost_);
        } else {
            const auto& p = report_.phase;
            written = std::printf("P5 perf a=%d r=%d:%d cb=%u coldSDKRuntime=%d fuel=%u:%u p=%u,%u,%u,%u,%u,%u,%u,%u field=%u/%u/%u move=%u/%u/%u/%u n=%u/%u skip=%u/%u/%u act=%d/%d card=%u/%u/%u/%u/%d q=%u/%u/%u/%u target=%d/%d/%d/%d/%d/%d/%d/%d/%d selection=%u/%u/%u attempt=%d menu=%d log=%u x=%u\n",
                cached_actor_, report_.round, report_.end_round, report_.callback, report_.cold,
                report_.entry, report_.end, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
                report_.compute, report_.expansions, report_.reuses,
                report_.move[0], report_.move[1], report_.move[2], report_.move[3], report_.candidates, report_.feasible,
                report_.skips[0], report_.skips[1], report_.skips[2],
                report_.action, report_.reason, report_.card[0], report_.card[1], report_.card[2], report_.opportunities, report_.certified,
                report_.queries[0], report_.queries[1], report_.queries[2], report_.queries[3],
                report_.target[0], report_.target[1], report_.target[2], report_.target[3], report_.target[4],
                report_.target[5], report_.target[6], report_.target[7], report_.target[8],
                report_.selection[0], report_.selection[1], report_.selection[2], report_.card_attempt, report_.menu_complete, report_cost_, report_.cross_mask);
        }
        const unsigned after = unsigned(cambc::fuel_remaining());
        const int end_round = int(cambc::round_number());
        flushing_diagnostics_ = false;
        if (written < 0) { diagnostics_blocked_ = true; return; }
        report_.pending = false;
        if (start_round == end_round && before >= after) {
            report_cost_ = before - after; max_report_cost_ = std::max(max_report_cost_, report_cost_);
            if (traced) traced_report_measured_ = true;
        } else { ++crossing_count_; invalidate_latches(); }
    }
    void flush_unexpected_error(bool fresh_entry) {
        if (diagnostics_blocked_) return;
        UnexpectedError* pending = nullptr;
        for (auto& error : unexpected_) if (error.pending) { pending = &error; break; }
        if (!pending || (!error_report_peak_ && !fresh_entry)) return;
        flushing_diagnostics_ = true;
        if (error_report_peak_ && cambc::fuel_remaining() < error_report_peak_ + 16000) { flushing_diagnostics_ = false; return; }
        const unsigned before = unsigned(cambc::fuel_remaining());
        const int flush_round = int(cambc::round_number());
        if (diagnostic_emit_round_ == flush_round) { flushing_diagnostics_ = false; return; }
        diagnostic_emit_round_ = flush_round;
        const auto& error = *pending;
        const int written = std::printf("P5 UNEXPECTED_ENGINE_ERROR a=%d cached_r=%d throw_r=unknown ref=%d/%d controller=%d/%d action=%d target=%d kind=%d code=%d dropped=%u flush_start=%d text=%s\n",
            error.actor, error.cached_round, error.reference_stage, error.reference_phase,
            error.controller_stage, error.controller_phase, error.action, error.target,
            int(error.kind), error.code, unexpected_dropped_, flush_round, error.text.data());
        const unsigned after = unsigned(cambc::fuel_remaining());
        const int end_round = int(cambc::round_number());
        flushing_diagnostics_ = false;
        if (written < 0) { diagnostics_blocked_ = true; return; }
        pending->pending = false;
        if (flush_round == end_round && before >= after) error_report_peak_ = std::max(error_report_peak_, before - after);
        else { ++crossing_count_; invalidate_latches(); }
    }
    void flush_card_completion(bool fresh_entry) {
        if (diagnostics_blocked_) return;
        CardCompletion* pending = nullptr;
        for (auto& item : card_completions_) if (item.pending) { pending = &item; break; }
        if (!pending || (!completion_report_peak_ && !fresh_entry)) return;
        flushing_diagnostics_ = true;
        if (completion_report_peak_ && cambc::fuel_remaining() < completion_report_peak_ + 16000) {
            flushing_diagnostics_ = false; return;
        }
        const unsigned before = unsigned(cambc::fuel_remaining());
        const int flush_round = int(cambc::round_number());
        if (diagnostic_emit_round_ == flush_round) { flushing_diagnostics_ = false; return; }
        diagnostic_emit_round_ = flush_round;
        const int written = std::printf("P5 card-complete a=%d r=%d job=%d group=%d kit=%d return=%d\n",
            pending->actor, pending->round, pending->job, pending->group, pending->family, pending->renewal);
        const unsigned after = unsigned(cambc::fuel_remaining());
        const int end_round = int(cambc::round_number());
        flushing_diagnostics_ = false;
        if (written < 0) { diagnostics_blocked_ = true; return; }
        pending->pending = false;
        if (flush_round == end_round && before >= after)
            completion_report_peak_ = std::max(completion_report_peak_, before - after);
        else { ++crossing_count_; invalidate_latches(); }
    }
    void flush_card_life(bool fresh_entry) {
        if (diagnostics_blocked_) return;
        const auto* item = card_life_.front();
        if (!item || (!card_life_report_peak_ && !fresh_entry)) return;
        flushing_diagnostics_ = true;
        if (card_life_report_peak_ && cambc::fuel_remaining() < card_life_report_peak_ + 16000) {
            flushing_diagnostics_ = false; return;
        }
        const unsigned before = unsigned(cambc::fuel_remaining());
        const int flush_round = int(cambc::round_number());
        if (diagnostic_emit_round_ == flush_round) { flushing_diagnostics_ = false; return; }
        diagnostic_emit_round_ = flush_round;
        const int written = std::printf("P5 card-life a=%d p=%d r_cached=%d event=%d job=%d g=%d next=%d altar=%d kit=%d nentry=%d stage=%d:%d phase=%d reason=%d action=%d outcome=%d flags=%u pos=%d observed=%d budget=%d gold=%d bag=%d receipts=%llu/%llu/%llu print_peak=%u dropped=%u flush=%d\n",
            item->actor, item->project, item->round, int(item->event), item->job, item->group, item->renewal,
            item->altar, item->family, item->renewal_entry, item->from_stage, item->stage, item->phase,
            item->reason, item->action, item->outcome, unsigned(item->flags), item->position, item->observed_group,
            item->budget, item->gold, item->bag, (unsigned long long)item->purchase,
            (unsigned long long)item->capture, (unsigned long long)item->bank,
            card_life_report_peak_, card_life_.dropped(), flush_round);
        const unsigned after = unsigned(cambc::fuel_remaining());
        const int end_round = int(cambc::round_number());
        flushing_diagnostics_ = false;
        if (written < 0) { diagnostics_blocked_ = true; return; }
        card_life_.consume();
        if (flush_round == end_round && before >= after)
            card_life_report_peak_ = std::max(card_life_report_peak_, before - after);
        else { ++crossing_count_; invalidate_latches(); }
    }

    bool tribute_exact(int bag) const {
        if (bag <= 0 || !settings_ok_) return false;
        const double value = cambc::constants::submit_gem_base_duration +
            std::ceil(double(bag) * cambc::constants::submit_gem_duration_multiplier);
        return std::isfinite(value) && std::max(double(cambc::constants::submit_gem_minimum_duration), value) == settings_.channel_ticks(bag);
    }
    void initialize() {
        cache_ = read_cache();
        ++geometry_generation_; position_distances_valid_ = false; position_field_round_ = -1;
        for (const auto& g : cambc::map::camp_groups())
            for (int i = 0; i < cache_.groups; ++i) if (gid(g) == cache_.ids[i])
                groups_.push_back({g, g.spawn_tiles(), gid(g), i});
        std::sort(groups_.begin(), groups_.end(), [](const auto& a, const auto& b) { return a.index < b.index; });
        const double multiplier = cambc::constants::submit_gem_duration_multiplier;
        const double reciprocal = multiplier > 0 ? 1.0 / multiplier : 0;
        const int divisor = std::isfinite(reciprocal) && reciprocal >= 1 && reciprocal <= 1000000 ? int(std::round(reciprocal)) : 0;
        settings_ = {cambc::constants::capture_time, divisor, cambc::constants::submit_gem_minimum_duration,
            int(cambc::constants::max_rounds), int(cambc::constants::winning_gems),
            int(cambc::constants::passive_abilities::marcella_tribute_gold_per_gem)};
        settings_ok_ = settings_.valid() && divisor > 0 && multiplier == 1.0 / divisor &&
            cambc::constants::submit_gem_base_duration == 0 && cambc::constants::submit_gems_range >= 1;
        own_type_ = cambc::self().champion_name();
        // SDK header: bot-facing metadata is immutable; champion designs are
        // explicitly immutable (cambc.h:3981). Read actual item-type metadata;
        // GameRules also specifies the fixed shard families. Actual
        // purchases still re-query can_buy/cost and verify the exact debit.
        own_base_ = cambc::self().base_stats();
        for (int family = 0; family < 3; ++family) {
            const auto item = *purchase_item(family); const auto info = item.info(); auto& q = purchase_quotes_[family];
            q.family = family; q.item_id = int(static_cast<std::uint32_t>(item)); q.cost = int(info.cost);
            q.purchasable = info.purchasable && info.cost > 0;
            q.strength_gain = info.strength_points * own_base_.strength_growth;
            q.health_gain = info.health_points * own_base_.health_growth; q.spell_damage_gain = 0;
        }
        const auto type = own_type_;
        int ordinal = 0;
        for (int i = 0; i < cache_.pick_count; ++i) {
            const int pick = cache_.picks[i];
            if (type == design_type(pick)) own_ordinal_ = ordinal;
            if (pick == 3 && type == cambc::champion_type::issac) own_ordinal_ = ordinal + 1;
            ordinal += pick == 3 ? 2 : 1;
        }
        own_count_ = std::max(1, ordinal);
        initialized_ = true;
    }
    LiveGroup* group(int id) {
        for (auto& g : groups_) if (g.id == id) return &g;
        return nullptr;
    }
    const LiveGroup* group(int id) const {
        for (const auto& g : groups_) if (g.id == id) return &g;
        return nullptr;
    }
    static bool production_goal(const Goal& goal) {
        return goal.production && (goal.kind == GoalKind::camp_entry ||
            goal.kind == GoalKind::camp_attack || goal.kind == GoalKind::camp_spawn);
    }
    bool target_event_matches(const Goal& goal) const {
        return target_event_.mode == goal.target_combat_mode && target_event_.actor == own_id() &&
            target_event_.group == goal.group_id && target_event_.capture == own_receipts_.capture &&
            target_event_.round >= 0 && target_event_.observed_round == round_;
    }
    bool target_combat_current(const Goal& goal) const {
        if (goal.target_combat_mode == TargetCombatMode::basic_only || !goal.target_combat_stamp ||
            target_binding_.goal != goal || !production_goal(goal) || phase_aborted_ ||
            target_binding_.round != round_ || target_binding_.epoch != epoch_ ||
            target_binding_.action != native_action_sequence_ || int(cambc::round_number()) != round_) return false;
        const auto* g = group(goal.group_id);
        if (!g || !g->known || g->respawning) return false;
        const auto& me = cambc::self();
        // Every native action/refresh/round change invalidates this binding.
        // Within that no-action packet lifetime, kit and buffs cannot change;
        // these actual self-only stocks/stats also reject a mutated caller.
        return target_binding_.actor == own_id() && me.is_alive() &&
            target_binding_.position == tile_id(me.position()) &&
            target_binding_.bag == int(me.gem_count()) && target_binding_.gold == int(me.gold_count()) &&
            target_binding_.health == me.health() && target_binding_.maximum == me.max_health() &&
            target_binding_.strength == me.strength() && target_binding_.focus == me.focus() &&
            int(cambc::round_number()) == round_;
    }
    bool target_cast_legal(const Goal& goal, int slot, int target) const {
        if (slot < 0 || slot > 1 || cast_ || !production_goal(goal)) return false;
        const auto* g = group(goal.group_id); auto& me = cambc::self();
        if (!g || !g->known || g->respawning || g->reward || !me.is_alive() || me.has_completed_turn() ||
            me.is_submitting_gems() || me.is_recalling() || me.is_teleporting() || !me.can_cast() ||
            me.buffs().has_stun() || me.restrictions().contains(cambc::buff_restrictions::cast)) return false;
        const auto ability = me.abilities()[slot];
        if (!ability.info() || !ability.is_ready() ||
            (moved_ && std::visit([](const auto& p) { return p.exclusive_with_movement; }, *ability.info()))) return false;
        if (goal.target_combat_mode == TargetCombatMode::prism_one_primary) {
            if (own_type_ != cambc::champion_type::prism || slot != 0 || !ability.is_cc() || !ability.can_target_camps()) return false;
            for (const auto& m : camps_) if (cid(m) == target && g->spawns.is_set(m.spawn_position()) &&
                m.is_visible() && m.is_alive() && ability.valid_tiles().is_set(m.position()))
                return int(cambc::round_number()) == round_;
        } else if (goal.target_combat_mode == TargetCombatMode::ruin_one_self_shield) {
            return own_type_ == cambc::champion_type::heart_of_ruin && slot == 0 && target == own_id() &&
                ability.is_shield() && ability.can_target_self() && int(cambc::round_number()) == round_;
        }
        return false;
    }
    void refresh_target_event() {
        if (target_event_.mode == TargetCombatMode::basic_only) return;
        const auto* g = group(target_event_.group);
        if (!g || target_event_.actor != own_id() ||
            !target_event_continues(round_, target_event_.observed_round, cambc::self().is_alive(),
                g->known, g->respawning, target_event_.capture, own_receipts_.capture) ||
            int(cambc::round_number()) != round_) {
            reset_target_combat(); return;
        }
        target_event_.observed_round = round_;
    }
    int tile_id(vec2 p) const { return p.y * cache_.width + p.x; }
    int node(Node n) const {
        if (n.kind == NodeKind::spawn) return cache_.groups + cache_.altars;
        if (n.kind == NodeKind::group_entry) { const auto* g = group(n.id); return g ? g->index : -1; }
        if (n.kind == NodeKind::altar_entry)
            for (int i = 0; i < cache_.altars; ++i) if (tile_id(position(cache_.altar_tiles[i])) == n.id) return altar_node(i);
        return -1;
    }
    static std::optional<cambc::item_type> purchase_item(int family) {
        if (family == 0) return cambc::item_type::monster_shard_of_strength;
        if (family == 1) return cambc::item_type::jungle_shard_of_health;
        if (family == 2) return cambc::item_type::monster_shard_of_focus;
        return std::nullopt;
    }
    bool capture_gold_inventory_supported() const {
        const auto items = cambc::self().item_count();
        for (std::uint32_t raw = 0; raw < items.size(); ++raw) {
            const cambc::item_type item{raw};
            if (!items[item]) continue;
            if (item != cambc::item_type::monster_shard_of_strength && item != cambc::item_type::monster_emblem_of_strength &&
                item != cambc::item_type::monster_shard_of_focus && item != cambc::item_type::monster_emblem_of_focus &&
                item != cambc::item_type::jungle_shard_of_health && item != cambc::item_type::jungle_emblem_of_health) return false;
        }
        return true;
    }
    int static_distance(int a, int b) const {
        if (a < 0 || b < 0 || a >= node_count(cache_) || b >= node_count(cache_)) return -1;
        auto d = cache_.distance[a][b];
        if (d == unknown_distance) d = cache_.distance[b][a];
        return d == unknown_distance ? -1 : int(d);
    }
    int altar_node(int a) const { return cache_.groups + a; }
    p5::CombatActor query_actor() const {
        ++actor_count_;
        auto& me = cambc::self(); p5::CombatActor a;
        a.id = own_id(); a.self = a.visible = true; a.ranged = me.attack_range() > 1;
        a.health = me.health(); a.max_health = me.max_health(); a.strength = me.strength();
        a.basic_modifier = std::min(1.0, double(own_base_.camp_damage_multiplier));
        a.visible_enemy_next_phase = enemy_stress_; a.enemy_cast_bound_known = true;
        // No unproved future cast, passive, item, shield or healing benefit.
        return a;
    }
    p5::CombatActor actor() const {
        if (frame_actor_valid_ && frame_actor_round_ == round_ && frame_actor_epoch_ == epoch_ &&
            int(cambc::round_number()) == round_) return frame_actor_;
        return query_actor();
    }
    static void copy_combat_prefix(p5::CombatInput& to, const p5::CombatInput& from) {
        to.actor_count = from.actor_count; to.monster_count = from.monster_count; to.hold_ticks = from.hold_ticks;
        for (std::size_t i = 0; i < from.actor_count; ++i) to.actors[i] = from.actors[i];
        for (std::size_t i = 0; i < from.monster_count; ++i) to.monsters[i] = from.monsters[i];
    }
    struct OwnFlags {
        vec2 position{};
        double health = 0, max_health = 0;
        bool safe = false, in_threat = false, at_spawn = false, spawn_recovered = false;
        e::Channel channel = e::Channel::none;
        bool channel_safe = false;
    };
    OwnFlags own_flags() const {
        auto& me = cambc::self(); const auto a = actor(); OwnFlags out;
        out.position = me.position(); out.health = a.health; out.max_health = a.max_health;
        out.at_spawn = cambc::map::spawn_region(team::ally).is_set(out.position);
        out.spawn_recovered = out.at_spawn && previous_spawn_round_ >= 0 && previous_spawn_round_ < round_;
        const double reserve = std::max(.2 * a.max_health, 2 * next_incoming_);
        out.safe = a.health > 0 && a.health >= reserve; out.in_threat = danger_.is_set(out.position);
        out.channel = me.is_submitting_gems() ? e::Channel::tribute : me.is_recalling() ? e::Channel::recall :
            me.is_teleporting() ? e::Channel::teleport : e::Channel::none;
        out.channel_safe = out.channel == e::Channel::tribute ? a.health > next_incoming_ + reserve : !out.in_threat;
        return out;
    }
    void update_recovery(const OwnFlags& own, bool safe) {
        if (!safe) recovering_ = true;
        if (recovering_ && own.at_spawn && own.spawn_recovered && own.health >= own.max_health) recovering_ = false;
    }
    void refresh() {
        snapshot_reusable_ = false; frame_actor_valid_ = false;
        target_binding_.goal.target_combat_stamp = 0;
        ++refresh_count_;
        ++epoch_; visible_ = cambc::vision::visible_tiles(); camps_ = cambc::vision::camps();
        enemies_ = cambc::vision::champions(team::enemy); danger_.clear();
        enemy_stress_ = route_stress_ = next_incoming_ = 0;
        const auto origin = cambc::self().position();
        for (const auto& m : camps_) if (m.is_alive() && m.is_awake() && m.strength() > 0) {
            bitboard reach; reach.draw_chebyshev(m.position(), m.attack_range()); danger_ |= reach;
            route_stress_ += m.strength();
            if (reach.is_set(origin)) next_incoming_ += m.strength();
        }
        for (const auto& enemy : enemies_) {
            double spell = 0;
            for (const auto& a : enemy.abilities())
                if (a.is_damage() || a.is_cc() || a.is_aoe_damage() || a.is_dash()) spell = std::max(spell, double(a.damage_amount()));
            // Use raw caster damage: the target overload subtracts our current
            // shield and caps at current HP, which cannot bound later casts.
            // Unknown enemy cooldowns receive the full-cast stress case.
            enemy_stress_ += enemy.strength() + spell;
            bitboard reach; reach.draw_chebyshev(enemy.position(), enemy.attack_range() + 1); danger_ |= reach;
            if (reach.is_set(origin)) next_incoming_ += enemy.strength() + spell;
        }
        // Treat every visible enemy and awake camp as exposure for every route
        // tick, even when its current range does not reach the selected path.
        route_stress_ += enemy_stress_;
        frame_actor_ = query_actor(); frame_actor_round_ = round_; frame_actor_epoch_ = epoch_;
        frame_actor_valid_ = int(cambc::round_number()) == round_;
        for (auto& g : groups_) {
            g.known = !(g.spawns & visible_).is_empty(); g.reward = g.respawning = false;
            g.combat.actor_count = 1; g.combat.actors[0] = frame_actor_;
            g.combat.monster_count = 0; g.combat.hold_ticks = 0;
            if (g.known) {
                const auto state = g.native.state();
                g.reward = std::holds_alternative<cambc::map::camp_group_reward_available>(state);
                g.respawning = std::holds_alternative<cambc::map::camp_group_respawning>(state);
            }
            if (g.reward || g.respawning) continue;
            for (const auto& m : g.native) {
                const bool seen = m.is_visible();
                if (seen && !m.is_alive()) continue;
                if (g.combat.monster_count == g.combat.monsters.size()) {
                    g.known = false; g.combat.monster_count = 0; break;
                }
                if (seen) {
                    auto& out = g.combat.monsters[g.combat.monster_count++];
                    out = {cid(m), m.max_health(), m.health(), m.strength(), true};
                }
                else {
                    const auto stats = m.base_stats();
                    auto& out = g.combat.monsters[g.combat.monster_count++];
                    out = {cid(m), stats.base_health, stats.base_health, stats.base_strength, false};
                }
            }
        }
        refresh_target_event();
    }
    p5::CombatForecast work(const LiveGroup& g, double health, bool full_respawn, bool include_threat) const {
        p5::CombatForecast out;
        if (!g.known || (!full_respawn && g.respawning) || health <= 0) return out;
        auto input = g.combat;
        input.actors[0] = actor(); input.actors[0].health = health;
        input.actors[0].visible_enemy_next_phase = include_threat ? route_stress_ : 0;
        input.hold_ticks = settings_.capture_rounds + 1;
        if (full_respawn) {
            input.monster_count = 0;
            for (const auto& m : g.native) {
                if (input.monster_count == input.monsters.size()) return out;
                const auto stats = m.base_stats();
                input.monsters[input.monster_count++] = {cid(m), stats.base_health, stats.base_health, stats.base_strength, false};
            }
        }
        if (input.monster_count) return p5::forecast_combat(input);
        if (!g.reward || full_respawn) return out;
        const double threat = input.actors[0].visible_enemy_next_phase;
        out.remaining_health[0] = health - threat * input.hold_ticks;
        out.state = out.remaining_health[0] > 0 && out.remaining_health[0] >= std::max(.2 * input.actors[0].max_health, 2 * threat)
            ? p5::EstimateState::feasible : p5::EstimateState::unsafe;
        return out;
    }
    double external_stress(const LiveGroup* unfinished_target) const {
        double threat = enemy_stress_;
        for (const auto& m : camps_) if (m.is_alive() && m.is_awake() &&
            (!unfinished_target || !unfinished_target->spawns.is_set(m.spawn_position()))) threat += m.strength();
        return threat;
    }
    bool residual_safe() {
        const bool unfinished = stage_ == Stage::production ||
            (stage_ == Stage::initial_bank && !plan_.bank_only && !plan_.winning);
        auto* target = unfinished ? group(plan_.group) : nullptr;
        const double external = external_stress(target);
        if (!unfinished && !plan_.winning) {
            const int remaining = std::max(0, plan_.duration - (round_ - plan_start_round_)) + plan_.renewal_work_ticks;
            const bool healing_ahead = plan_.via_spawn && stage_ != Stage::renewal;
            if (healing_ahead) return external <= plan_external_stress_ &&
                tail_health_safe(cambc::self().health(), cambc::self().max_health(), 0, external, remaining);
            // Actual injury matters even with the same zero outside threat.
            return tail_health_safe(cambc::self().health(), cambc::self().max_health(),
                plan_.renewal_damage, external, remaining);
        }
        if (external <= plan_external_stress_) return true;
        // Spawn recovery was admitted with no outside exposure. A new threat
        // invalidates that premise instead of borrowing future healing early.
        if (plan_.via_spawn) return false;
        int milestone = plan_.duration;
        for (int i = 0; i < plan_.schedule.event_count; ++i) {
            const auto& v = plan_.schedule.events[i];
            if (stage_ == Stage::production && v.kind == p5::AccountingEventKind::capture) milestone = v.tick;
            if (stage_ == Stage::initial_bank && v.kind == p5::AccountingEventKind::tribute_complete) { milestone = v.tick; break; }
            if (stage_ == Stage::output_bank && v.kind == p5::AccountingEventKind::tribute_complete) milestone = v.tick;
        }
        // The current phase may take its entire bounded allowance. Add the
        // still-unstarted schedule tail, rather than trusting elapsed travel.
        const int phase_left = std::max(0, state_.phase_budget - (round_ - state_.phase_start));
        const int remaining = std::max({1, plan_.duration - (round_ - plan_start_round_),
            phase_left + std::max(0, plan_.duration - milestone)});
        const double health = cambc::self().health() - external * (remaining + plan_.renewal_work_ticks) - plan_.renewal_damage;
        if (health <= 0 || health < std::max(.2 * cambc::self().max_health(), 2 * external)) return false;
        // The target's remaining camps are paid once by the combat certificate;
        // awakening that target never makes it an all-route external threat.
        return !unfinished || (target && work(*target, health, false, false).state == p5::EstimateState::feasible);
    }
    bool bind_position_distances() {
        if (position_distances_valid_ && position_field_geometry_ == geometry_generation_ &&
            position_field_origin_ == cambc::self().position()) {
            if (position_field_round_ != round_) ++field_reuses_;
            position_field_round_ = round_; return true;
        }
        position_distances_valid_ = false; position_field_round_ = -1;
        current_distance_.fill(-1); return false;
    }
    void position_field() {
        if (bind_position_distances()) return;
        if (cambc::fuel_remaining() < 45000) return;
        const auto sample = phase_begin(FuelPhase::position_field);
        const auto origin = cambc::self().position();
        {
            // The phase includes allocation, setup, extraction and destruction.
            cambc::distance_field field; bitboard visited;
            const auto before = cambc::fuel_remaining();
            const auto expanded = field.compute_with_expansion_count(static_walls(), at(origin), visited, 256);
            const auto after = unsigned(cambc::fuel_remaining());
            const int computed_round = int(cambc::round_number());
            if (computed_round == sample.round && before >= after) field_compute_fuel_ += unsigned(before) - after;
            field_expansions_ += unsigned(expanded);
            if (computed_round == round_ && cambc::self().position() == origin) {
                for (int i = 0; i < node_count(cache_); ++i) {
                    const auto d = field.get(position(cache_.nodes[i]));
                    if (d != cambc::distance_field::unreachable) current_distance_[i] = d;
                }
                for (int i = 0; i < node_count(cache_); ++i) if (position(cache_.nodes[i]) == origin) {
                    for (int j = 0; j < node_count(cache_); ++j)
                        cache_.distance[i][j] = current_distance_[j] < 0 ? unknown_distance : std::uint16_t(current_distance_[j]);
                }
            }
        }
        const bool same = phase_end(FuelPhase::position_field, sample);
        position_distances_valid_ = same && cambc::self().position() == origin;
        if (position_distances_valid_) {
            position_field_origin_ = origin; position_field_geometry_ = geometry_generation_; position_field_round_ = round_;
        } else { current_distance_.fill(-1); position_field_round_ = -1; }
    }
    void initial_into(p5::AccountingSchedule& s, int target) const {
        auto& me = cambc::self();
        s.actor_count = 1; s.event_count = 0; s.target_id = target;
        s.starting_team_score = cambc::global_submitted_gems(team::ally);
        s.win_score = settings_.winning_gems; s.tribute_gems_per_tick = settings_.tribute_gems_per_tick;
        s.tribute_min_ticks = settings_.tribute_min_ticks;
        s.terminal_win = false; s.terminal_win_tick = -1; s.immediate_legal_winning_bank = false;
        s.doctrine_id = 0; s.denial_evidenced = false; s.denied_gems = 0; s.denied_bank_tick = -1; s.projected_kills = 0;
        s.actors[0] = {own_id(), true, true, true, me.gem_count(), me.gold_count(),
            own_type_ == cambc::champion_type::marcella ? settings_.marcella_gold_per_gem : 0, 0};
    }
    static void event(p5::AccountingSchedule& s, p5::AccountingEventKind kind, int tick, int gems = 0,
                      int gold = 0, int group_id = -1, int waypoint = -1) {
        auto& v = s.events[s.event_count]; v = {}; v.id = s.event_count++; v.kind = kind; v.tick = tick;
        v.actor = 0; v.gems = gems; v.gold = gold; v.group = group_id; v.waypoint = waypoint;
        if (kind == p5::AccountingEventKind::capture) v.participants = v.holders = 1;
    }
    void bank_events(p5::AccountingSchedule& s, int tick, int bag) const {
        event(s, p5::AccountingEventKind::tribute_start, tick, -1);
        event(s, p5::AccountingEventKind::tribute_complete, tick + settings_.channel_ticks(bag), -1);
    }
    bool renewal_after_bank(Plan& out, const LiveGroup* first, int bank_done,
                            int capture, double first_loss) {
        const auto own = actor();
        const int altar = altar_node(out.altar), spawn = cache_.groups + cache_.altars;
        const int horizon = settings_.round_limit - round_;
        const int walk = static_distance(altar, spawn);
        const int arrival = bank_done + std::max(0, walk) + 2;
        // Only this zero-exposure alternative can credit spawn healing. The
        // direct alternative below keeps its existing whole-route charge.
        const auto recovery = route_stress_ == 0 && walk >= 0
            ? spawn_recovery_quote(own.health - first_loss, own.max_health,
                cambc::constants::spawn_healing, horizon - arrival - 1)
            : SpawnRecoveryQuote{};
        const int recovery_ticks = recovery.valid ? walk + 2 + recovery.heal_ticks + 1 : 0;
        auto consider = [&](const LiveGroup& other, int tick, bool respawn, double health, bool via_spawn) {
            if (tick > horizon) return;
            const auto successor = work(other, health, respawn, true);
            if (successor.state != p5::EstimateState::feasible) return;
            if (out.renewal >= 0 && (tick > out.renewal_tick ||
                (tick == out.renewal_tick && other.index >= out.renewal))) return;
            out.renewal = other.index; out.renewal_tick = tick;
            out.renewal_damage = successor.worst_camp_damage;
            out.renewal_work_ticks = successor.clear_ticks + settings_.capture_rounds + 1;
            out.via_spawn = via_spawn;
            out.recovery_ticks = via_spawn ? recovery_ticks : 0;
            out.recovery_heal_ticks = via_spawn ? recovery.heal_ticks : 0;
        };
        for (const auto& other : groups_) {
            const bool respawn = first && other.index == first->index;
            const int due = respawn ? capture + first->native.max_respawn_timer() + 1 : 0;
            const int direct = static_distance(altar, other.index);
            if (direct >= 0) {
                const int tick = std::max(bank_done + direct + 2, due);
                consider(other, tick, respawn, own.health - first_loss - route_stress_ * tick, false);
            }
            const int back = recovery.valid ? static_distance(spawn, other.index) : -1;
            if (back >= 0) {
                const int tick = std::max(bank_done + recovery_ticks + back + 2, due);
                consider(other, tick, respawn, own.max_health, true);
            }
        }
        return out.renewal >= 0;
    }
    bool refresh_finished_tail() {
        if (plan_.renewal < 0 || plan_.renewal >= cache_.groups) return false;
        if (!bind_position_distances()) position_field();
        if (phase_aborted_ || int(cambc::round_number()) != round_ || position_field_round_ != round_) return false;
        const auto own = actor();
        const int horizon = settings_.round_limit - round_, altar = altar_node(plan_.altar);
        const int spawn = cache_.groups + cache_.altars;
        const bool before_bank = stage_ == Stage::output_bank || (stage_ == Stage::initial_bank && plan_.bank_only);
        int bank = 0;
        if (before_bank) {
            const int bag = int(cambc::self().gem_count());
            if (!tribute_exact(bag) || current_distance_[altar] < 0) return false;
            int start = current_distance_[altar] + 2 + int(cambc::constants::altar_lock_hits);
            bool hostile = false;
            for (const auto& lock : cambc::vision::altar_locks(team::enemy))
                hostile |= lock.position() == position(cache_.altar_tiles[plan_.altar]);
            if (!hostile && visible_.is_set(position(cache_.altar_tiles[plan_.altar])) &&
                cheb(cambc::self().position(), position(cache_.altar_tiles[plan_.altar])) <= cambc::constants::submit_gems_range) start = 0;
            bank = start + settings_.channel_ticks(bag);
        }
        int due = 0;
        const auto* first = group(plan_.group);
        if (first && first->index == plan_.renewal && own_receipts_.capture_group == first->id &&
            own_receipts_.capture_round >= plan_start_round_)
            due = std::max(0, own_receipts_.capture_round + first->native.max_respawn_timer() + 1 - round_);
        const int direct = before_bank ? static_distance(altar, plan_.renewal) : current_distance_[plan_.renewal];
        int tick = direct >= 0 ? std::max(bank + direct + 2, due) : horizon + 1;
        bool via_spawn = false; int recovery_ticks = 0, healing_ticks = 0;
        if (tick > horizon || !tail_health_safe(own.health, own.max_health, plan_.renewal_damage,
                route_stress_, tick + plan_.renewal_work_ticks)) {
            const int walk = before_bank ? static_distance(altar, spawn) : current_distance_[spawn];
            const int back = static_distance(spawn, plan_.renewal);
            if (route_stress_ != 0 || walk < 0 || back < 0) return false;
            const auto recovery = spawn_recovery_quote(own.health, own.max_health,
                cambc::constants::spawn_healing, horizon - bank - walk - 3);
            if (!recovery.valid || !tail_health_safe(own.max_health, own.max_health,
                plan_.renewal_damage, 0, 0)) return false;
            recovery_ticks = walk + 2 + recovery.heal_ticks + 1;
            tick = std::max(bank + recovery_ticks + back + 2, due);
            if (tick > horizon) return false;
            via_spawn = true; healing_ticks = recovery.heal_ticks;
        }
        // Keep the named, previously certified successor. Hidden life is not
        // refreshed here; actual entry still requires a fresh work certificate.
        plan_.via_spawn = via_spawn; plan_.recovery_ticks = recovery_ticks;
        plan_.recovery_heal_ticks = healing_ticks;
        // This is an execution horizon repair. The original selection's
        // accounting/rank remains its historical quote, not a new comparison.
        plan_.duration = plan_.renewal_tick = round_ - plan_start_round_ + tick;
        return true;
    }
    struct LiquidationQuote { int altar = -1, ticks = 0, budget = 0; };
    LiquidationQuote liquidation_quote(int selected = -1, int remaining_budget = -1) {
        LiquidationQuote best;
        const int bag = int(cambc::self().gem_count());
        if (!captured_bank_.pending() || captured_bank_.actor != cached_actor_ || !tribute_exact(bag)) return best;
        if (!bind_position_distances()) position_field();
        if (phase_aborted_ || position_field_round_ != round_ || int(cambc::round_number()) != round_) return best;
        const auto own = actor(); const auto locks = cambc::vision::altar_locks(team::enemy);
        const int horizon = settings_.round_limit - round_;
        for (int a = 0; a < cache_.altars; ++a) {
            if (selected >= 0 && a != selected) continue;
            const int distance = current_distance_[altar_node(a)];
            const auto altar = position(cache_.altar_tiles[a]);
            bool hostile = false; for (const auto& lock : locks) hostile |= lock.position() == altar;
            const bool adjacent_open = !hostile && visible_.is_set(altar) &&
                cheb(cambc::self().position(), altar) <= cambc::constants::submit_gems_range;
            const int ticks = liquidation_ticks(distance, int(cambc::constants::altar_lock_hits),
                settings_.channel_ticks(bag), adjacent_open);
            if (!ticks || ticks > horizon) continue;
            const int budget = remaining_budget >= 0 ? std::min(horizon, remaining_budget) :
                std::min(horizon, std::max(8, 2 * ticks + 4));
            // Pay exposure for the entire bounded bank phase, including lock
            // removal, whole-bag channel and movement retries. No healing or
            // productive successor is credited by this liquidation quote.
            if (ticks > budget || !tail_health_safe(own.health, own.max_health, 0, route_stress_, budget)) continue;
            if (best.altar < 0 || ticks < best.ticks) best = {a, ticks, budget};
        }
        if (int(cambc::round_number()) != round_) { invalidate_latches(); return {}; }
        return best;
    }
    bool enter_liquidation(const e::Observation& observed) {
        const auto quote = liquidation_quote();
        if (quote.altar < 0) return false;
        captured_bank_.altar = quote.altar;
        stage_ = Stage::liquidation_bank; state_ = {};
        auto point = observed; point.forecast_turns = quote.ticks;
        e::enter(state_, e::Phase::bank, point); state_.phase_budget = quote.budget;
        state_.consumed_payout = captured_bank_.capture;
        // A disposition of real stock is not a complete economic schedule.
        plan_.feasible = plan_.winning = false; plan_.renewal = -1;
        bank_baseline_ = bank_receipt_; bank_intent_ = false; residual_invalid_ = false;
        move_binding_.valid = false;
        return true;
    }
    const LiveGroup* fresh_unpaid_hold() const {
        const auto p = cambc::self().position();
        for (const auto& g : groups_) if (g.known && g.reward && g.spawns.is_set(p)) {
            bool contested = false;
            for (const auto& enemy : enemies_) contested |= g.spawns.is_set(enemy.position());
            const auto own = actor();
            if (!contested && tail_health_safe(own.health, own.max_health, 0, next_incoming_, 0) &&
                int(cambc::round_number()) == round_) return &g;
        }
        return nullptr;
    }
    void enter_spawn_recovery(const e::Observation& o) {
        stage_ = Stage::recovery; e::enter(state_, e::Phase::recover, o);
        state_.recovery_arrival = -1;
        state_.phase_budget = std::min(o.remaining_rounds, std::max(8, 2 * plan_.recovery_ticks + 4));
        move_binding_.valid = false;
    }
    bool forecast_into(LiveGroup* g, int altar, bool bank_first, const p5::FinishWindows& windows, Plan& out) {
        ++forecast_count_;
        out.feasible = out.winning = false; out.bank_first = bank_first; out.bank_only = g == nullptr;
        out.group = out.renewal = -1; out.altar = altar; out.duration = 8; out.renewal_tick = 0;
        out.renewal_damage = 0; out.renewal_work_ticks = out.recovery_ticks = out.recovery_heal_ticks = 0;
        out.via_spawn = false; out.rank = {};
        auto& me = cambc::self(); const int bag = int(me.gem_count());
        if (altar < 0 || !settings_ok_) return false;
        const int a = altar_node(altar), current_altar = current_distance_[a];
        initial_into(out.schedule, g ? g->id : -1);
        // Direct production is committed now and dispatched on the next fresh
        // callback. All its payout/bank/return ticks and occupied time include
        // that owner turn. Bank prefixes retain their current-turn dispatch.
        const int startup = production_selection_delay(g != nullptr, bank_first);
        int prefix = startup;
        // An unknown future lock is a queried full-lock removal stress cost,
        // never an assertion that an unseen altar is currently unlocked.
        const int lock_stress = int(cambc::constants::altar_lock_hits);
        if (bank_first || !g) {
            if (!tribute_exact(bag) || current_altar < 0) return false;
            prefix = current_altar + 2 + lock_stress;
            const auto locks = cambc::vision::altar_locks(team::enemy);
            bool hostile = false; for (const auto& l : locks) hostile |= l.position() == position(cache_.altar_tiles[altar]);
            if (cheb(me.position(), position(cache_.altar_tiles[altar])) <= cambc::constants::submit_gems_range && !hostile &&
                visible_.is_set(position(cache_.altar_tiles[altar]))) prefix = 0;
            bank_events(out.schedule, prefix, bag); prefix += settings_.channel_ticks(bag);
            if (out.schedule.starting_team_score + bag >= settings_.winning_gems) {
                if (me.health() - route_stress_ * prefix < std::max(.2 * me.max_health(), 2 * route_stress_)) return false;
                out.winning = true; out.schedule.terminal_win = true; out.schedule.terminal_win_tick = prefix;
                out.schedule.immediate_legal_winning_bank = out.schedule.events[0].tick == 0;
                out.schedule.actors[0].occupied_turns = prefix;
                out.duration = prefix;
                out.rank = p5::forecast_schedule(out.schedule, windows); out.feasible = out.rank.status == p5::AccountingStatus::feasible;
                return out.feasible;
            }
        }
        if (!g) {
            if (!renewal_after_bank(out, nullptr, prefix, 0, 0)) return false;
        } else {
            if (!g->known || g->respawning) return false;
            // The caller checked current visible holders before this frame.
            const int to = bank_first ? static_distance(a, g->index) : current_distance_[g->index];
            const int conversion = static_distance(g->index, a);
            if (to < 0 || conversion < 0) return false;
            const int final_bag = (bank_first ? 0 : bag) + int(g->native.gem_value());
            if (!tribute_exact(final_bag)) return false;
            const auto fight = work(*g, me.health() - route_stress_ * startup, false, false);
            if (fight.state != p5::EstimateState::feasible) return false;
            const int capture = prefix + to + 2 + fight.clear_ticks + settings_.capture_rounds + 1;
            event(out.schedule, p5::AccountingEventKind::capture, capture, int(g->native.gem_value()), int(g->native.gold_value()), g->id);
            const int bank_start = capture + conversion + 2 + lock_stress;
            bank_events(out.schedule, bank_start, final_bag);
            const int bank_done = bank_start + settings_.channel_ticks(final_bag);
            out.group = g->id;
            // Returning to the same source includes its configured respawn;
            // entry waypoints lie outside spawn tiles and cannot block respawn.
            if (!renewal_after_bank(out, g, bank_done, capture, fight.worst_camp_damage)) return false;
        }
        if (out.renewal_tick > settings_.round_limit - round_) return false;
        event(out.schedule, p5::AccountingEventKind::return_to_work, out.renewal_tick, 0, 0, -1, out.renewal);
        out.schedule.actors[0].occupied_turns = out.renewal_tick;
        out.rank = p5::forecast_schedule(out.schedule, windows);
        out.feasible = out.rank.status == p5::AccountingStatus::feasible; out.duration = out.renewal_tick;
        return out.feasible;
    }
    bool candidate_precheck(const LiveGroup* g, int altar, bool bank_first, int bag, const bitboard& holders) {
        auto reject = [&](unsigned mask) { ++precheck_skips_; precheck_mask_ |= mask; return false; };
        if (!settings_ok_ || altar < 0 || altar >= cache_.altars) return reject(1);
        const int a = altar_node(altar);
        if ((bank_first || !g) && !tribute_exact(bag)) return reject(2);
        if ((bank_first || !g) && current_distance_[a] < 0) return reject(8);
        if (!g) return true;
        if (!g->known || g->respawning) return reject(4);
        if (!(g->spawns & holders).is_empty()) return reject(16);
        const int to = bank_first ? static_distance(a, g->index) : current_distance_[g->index];
        if (to < 0 || static_distance(g->index, a) < 0) return reject(8);
        if (!tribute_exact((bank_first ? 0 : bag) + int(g->native.gem_value()))) return reject(32);
        return true;
    }
    void select() {
        const auto sample = phase_begin(FuelPhase::select);
        if (!bind_position_distances()) position_field();
        if (position_field_round_ != round_ || position_field_origin_ != cambc::self().position()) { phase_end(FuelPhase::select, sample); return; }
        auto windows = history_.windows(round_, settings_.round_limit, settings_.winning_gems, cambc::global_submitted_gems(team::enemy));
        const int bag = int(cambc::self().gem_count());
        bitboard holders;
        for (const auto& ally : cambc::vision::champions(team::ally))
            if (int(static_cast<cambc::handle_champion_t>(ally)) != own_id()) holders.set(ally.position());
        auto& best = best_scratch_; best.feasible = false;
        auto consider = [&](LiveGroup* g, int altar, bool bank_first) {
            if (!candidate_precheck(g, altar, bank_first, bag, holders)) return true;
            if (cambc::fuel_remaining() < std::max(45000u, selection_chunk_peak_ + selection_finalize_peak_ + 16000u)) {
                ++selection_budget_stops_; return false;
            }
            if (int(cambc::round_number()) != round_) { invalidate_latches(); return false; }
            const unsigned chunk_start = unsigned(cambc::fuel_remaining());
            if (forecast_into(g, altar, bank_first, windows, candidate_scratch_)) {
                ++feasible_count_;
                if (!best.feasible || p5::compare_results(candidate_scratch_.rank, best.rank) > 0) best = candidate_scratch_;
            }
            const unsigned chunk_end = unsigned(cambc::fuel_remaining());
            if (int(cambc::round_number()) != round_) { invalidate_latches(); return false; }
            if (chunk_start >= chunk_end) selection_chunk_peak_ = std::max(selection_chunk_peak_, chunk_start - chunk_end);
            return !phase_aborted_;
        };
        bool more = true;
        for (int a = 0; a < cache_.altars; ++a) {
            if (!consider(nullptr, a, true)) break;
            if (best.feasible && best.rank.immediate_win) break;
            for (auto& g : groups_) {
                if (!consider(&g, a, false) || !consider(&g, a, true)) { more = false; break; }
            }
            if (!more) break;
        }
        if (phase_aborted_ || int(cambc::round_number()) != round_ || !best.feasible ||
            cambc::fuel_remaining() < selection_finalize_peak_ + 16000u) {
            phase_end(FuelPhase::select, sample); return;
        }
        const unsigned finalize_start = unsigned(cambc::fuel_remaining());
        plan_ = best; bank_baseline_ = bank_receipt_; bank_intent_ = false;
        plan_start_round_ = round_; plan_external_stress_ = external_stress(group(best.group)); residual_invalid_ = false;
        stage_ = best.bank_first || best.bank_only || best.winning ? Stage::initial_bank : Stage::production;
        state_ = {}; state_.target = best.group;
        e::Observation o; o.owner_turn = round_; o.remaining_rounds = settings_.round_limit - round_; o.forecast_turns = best.duration;
        e::enter(state_, stage_ == Stage::initial_bank ? e::Phase::bank : e::Phase::bootstrap, o);
        const unsigned finalize_end = unsigned(cambc::fuel_remaining());
        if (int(cambc::round_number()) == round_ && finalize_start >= finalize_end) {
            selection_finalize_fuel_ = finalize_start - finalize_end;
            selection_finalize_peak_ = std::max(selection_finalize_peak_, selection_finalize_fuel_);
        }
        // Commit/enter is inside the measured phase. The selected target is
        // already in the bounded report; formatting here used to suspend the
        // callback after an otherwise completed forecast.
        phase_end(FuelPhase::select, sample);
    }

    bitboard blocked() const {
        auto b = static_walls() | cambc::vision::champion_tiles() | cambc::vision::camp_tiles() | cambc::vision::teleport_tiles();
        b.set(cambc::self().position(), false); return b;
    }
    Goal reference_goal() const {
        Goal goal; goal.phase = state_.phase; goal.controller_stage = int(stage_);
        if (stage_ == Stage::liquidation_bank) {
            goal.kind = GoalKind::altar; goal.bank_requested = true;
            goal.forecast_turns = std::max(0, state_.phase_budget - (round_ - state_.phase_start));
            if (captured_bank_.altar >= 0) goal.altar_tile_id = tile_id(position(cache_.altar_tiles[captured_bank_.altar]));
            return goal;
        }
        goal.forecast_turns = plan_.duration; goal.group_id = state_.target;
        goal.production = stage_ == Stage::production;
        goal.bank_requested = stage_ == Stage::initial_bank || stage_ == Stage::output_bank;
        if (plan_.altar >= 0) goal.altar_tile_id = tile_id(position(cache_.altar_tiles[plan_.altar]));
        if (stage_ == Stage::recovery) goal.kind = GoalKind::spawn;
        else if (state_.phase == e::Phase::recover) goal.kind = GoalKind::recover;
        else if (stage_ == Stage::renewal && plan_.renewal >= 0) {
            goal.kind = GoalKind::productive_entry; goal.group_id = cache_.ids[plan_.renewal];
#if PLAN5_BANK_EXIT
        // ALLOCATE owns a completed payout. Its committed bank route can
        // vacate directly; an occupied bootstrap entry tile must not strand
        // the bag until the complete plan's phase deadline expires.
        } else if (state_.phase == e::Phase::allocate && goal.bank_requested) {
            goal.kind = GoalKind::altar;
#endif
        } else if (state_.phase == e::Phase::bank) goal.kind = GoalKind::altar;
        else if (state_.phase == e::Phase::shop) goal.kind = GoalKind::spawn;
        else if (state_.phase == e::Phase::hold) goal.kind = GoalKind::camp_spawn;
        else if (state_.phase == e::Phase::clear) goal.kind = GoalKind::camp_attack;
        else goal.kind = GoalKind::camp_entry;
        return goal;
    }
    bool movement_work_current() {
        if (phase_aborted_) return false;
        const unsigned fuel = unsigned(cambc::fuel_remaining());
        if (int(cambc::round_number()) != round_) {
            ++crossing_count_; crossed_phases_ |= 1u << 11; invalidate_latches(); return false;
        }
        return fuel >= 16000; // Existing action/finalization reserve, not a higher entry floor.
    }
    bitboard goals(const e::Observation& o, const Goal& goal, bool& complete) {
        auto& me = cambc::self();
        if (goal.kind == GoalKind::recover && o.in_threat_range) return {};
        if (goal.kind == GoalKind::spawn) return cambc::map::spawn_region(team::ally);
        if (goal.kind == GoalKind::altar) {
            const int n = node({NodeKind::altar_entry, goal.altar_tile_id});
#if PLAN5_BANK_EXIT
            if (n < 0) return {};
            bitboard adjacent;
            adjacent.draw_chebyshev({goal.altar_tile_id % cache_.width, goal.altar_tile_id / cache_.width},
                cambc::constants::submit_gems_range);
            if (goal.phase == e::Phase::allocate)
                if (const auto* paid = group(goal.group_id)) adjacent &= ~paid->spawns;
            return adjacent; // current blockers are removed by movement_impl
#else
            return n >= 0 ? at(position(cache_.nodes[n])) : bitboard{};
#endif
        }
        auto* g = group(goal.group_id);
        if (!g) return {};
        if (goal.kind == GoalKind::camp_spawn) return g->spawns;
        if (goal.kind == GoalKind::camp_attack) {
            const cambc::camp* target = nullptr;
            double target_health = 0;
            for (const auto& m : camps_) {
                if (!movement_work_current()) { complete = false; return {}; }
                if (!g->spawns.is_set(m.spawn_position()) || !m.is_alive()) continue;
                const double health = m.health();
                if (!target || health < target_health || (health == target_health && cid(m) < cid(*target))) {
                    target = &m; target_health = health;
                }
            }
            if (!movement_work_current()) { complete = false; return {}; }
            if (target) { bitboard b; b.draw_chebyshev(target->position(), me.attack_range()); return b; }
        }
        if (goal.kind == GoalKind::camp_entry || goal.kind == GoalKind::productive_entry || goal.kind == GoalKind::camp_attack)
            return at(position(cache_.nodes[g->index]));
        return {};
    }
    void movement_impl(e::Observation& o, const e::State& state, const Goal& goal) {
        o.move_count = 0; move_binding_.valid = false;
        if (!o.can_move || o.owner_turn != round_ || o.observation_epoch != epoch_ || goal.phase != state.phase ||
            cambc::fuel_remaining() < 30000 || !movement_work_current()) return;
        auto walls = blocked(); const auto origin = cambc::self().position();
        auto reachable = walls.reachable_tiles(origin, 1).positions();
        reachable.erase(std::remove(reachable.begin(), reachable.end(), origin), reachable.end());
        // A one-tile reachable mask has at most eight neighbours. Every key
        // below is computed once from this no-action round/epoch/origin.
        struct Step { vec2 position{}; double key = 0; };
        std::array<Step, 8> steps{}; std::size_t count = 0;
        if (reachable.size() > steps.size() || !movement_work_current()) return;
        for (vec2 p : reachable) steps[count++] = {p, 0};
        if (!count) return;
        if (goal.kind == GoalKind::recover && o.in_threat_range) {
            for (const auto& m : camps_) {
                if (!movement_work_current()) return;
                const auto status = m.state();
                if (!status.is_awake()) continue;
                const auto p = m.position(); const int range = m.attack_range(); const double strength = m.strength();
                if (range < 0 || !std::isfinite(strength) || strength < 0) return;
                for (std::size_t i = 0; i < count; ++i)
                    if (cheb(steps[i].position, p) <= range) steps[i].key += strength;
            }
            for (const auto& enemy : enemies_) {
                if (!movement_work_current()) return;
                const auto p = enemy.position(); const int range = enemy.attack_range(); const double strength = enemy.strength();
                if (range < 0 || range == std::numeric_limits<int>::max() || !std::isfinite(strength) || strength < 0) return;
                for (std::size_t i = 0; i < count; ++i)
                    if (cheb(steps[i].position, p) <= range + 1) steps[i].key += strength;
            }
        } else {
            bool complete = true;
            auto target = goals(o, goal, complete);
            if (!complete || !movement_work_current()) return;
            target &= ~walls;
            if (!movement_work_current()) return;
            if (target.is_empty() || target.is_set(origin)) return;
            auto roots = target.positions();
            if (!move_field_) {
                move_field_.emplace(); bitboard visited;
                const auto before = cambc::fuel_remaining();
                const auto expanded = move_field_->compute_to_with_expansion_count(walls, target, at(origin), visited, 256);
                const auto after = cambc::fuel_remaining();
                if (int(cambc::round_number()) == round_ && before >= after) move_compute_fuel_ += unsigned(before - after);
                move_expansions_ += unsigned(expanded);
                move_roots_ = roots;
            } else if (roots != move_roots_) return; // one selected field per owner turn
            if (!movement_work_current()) return;
            const auto distance = move_field_->get(origin);
            std::size_t usable = 0;
            for (std::size_t i = 0; i < count; ++i) {
                if (!movement_work_current()) return;
                const auto d = move_field_->get(steps[i].position);
                if (d < distance) steps[usable++] = {steps[i].position, double(d)};
            }
            count = usable;
        }
        if (!movement_work_current() || cambc::self().position() != origin || o.observation_epoch != epoch_) return;
        // Stable insertion sort of at most eight numeric keys: no SDK getter,
        // distance lookup, allocation or repeated threat query in comparisons.
        for (std::size_t i = 1; i < count; ++i) {
            const auto step = steps[i]; std::size_t j = i;
            while (j && step.key < steps[j - 1].key) { steps[j] = steps[j - 1]; --j; }
            steps[j] = step;
        }
        if (!movement_work_current()) return;
        for (std::size_t i = 0; i < count; ++i) {
            if (o.move_count == o.moves.size()) break;
            const auto p = steps[i].position;
            o.moves[o.move_count++] = {p.y * cache_.width + p.x, true, true, true, true, false};
        }
        move_binding_ = {true, goal, state.phase, state.target, round_, epoch_, {}, o.move_count};
        move_binding_.origin = tile_id(origin);
        move_binding_.geometry = geometry_generation_;
        for (std::size_t i = 0; i < o.move_count; ++i) move_binding_.destinations[i] = o.moves[i].destination;
    }
    void movement(e::Observation& o, const e::State& state, const Goal& goal) {
        const auto start = phase_begin(FuelPhase::apply);
        movement_impl(o, state, goal);
        const unsigned fuel = unsigned(cambc::fuel_remaining());
        const int now = int(cambc::round_number());
        o.fuel = int(fuel);
        if (start.round == round_ && now == round_ && start.fuel >= fuel) move_plan_fuel_ += start.fuel - fuel;
        else {
            if (!phase_aborted_) ++crossing_count_;
            crossed_phases_ |= 1u << 11; invalidate_latches(); o.move_count = 0;
        }
        if (fuel < 16000 || phase_aborted_) { o.move_count = 0; move_binding_.valid = false; }
    }
    void movement(e::Observation& o) { movement(o, state_, reference_goal()); }
    // Call-local result only: it cannot authorize another owner, actor frame,
    // target, health/threat input, or hold horizon.
    struct ExecutionTargetQuote {
        const LiveGroup* target = nullptr;
        int round = -1, hold = -1;
        std::uint64_t epoch = 0;
        double health = 0, threat = 0;
        p5::EstimateState state = p5::EstimateState::unknown;
    };
    e::Observation observe_state(const e::State& state, const Goal& goal, ExecutionTargetQuote* execution_quote = nullptr) {
        if (execution_quote) *execution_quote = {};
        ++observe_count_;
        bind_position_distances();
        auto& me = cambc::self(); e::Observation o;
        o.owner_turn = round_; o.remaining_rounds = std::max(0, settings_.round_limit - round_);
        o.forecast_turns = goal.forecast_turns; o.capture_duration = settings_.capture_rounds;
        o.observation_epoch = epoch_;
        o.fuel = cambc::fuel_remaining(); o.alive = me.is_alive(); o.own_bag = int(me.gem_count());
        if (!o.alive) { o.fuel = int(cambc::fuel_remaining()); return o; }
        const auto own = own_flags();
        const bool delegated_target = target_combat_current(goal);
        packet_position_tile_ = tile_id(own.position);
        o.at_spawn = own.at_spawn; o.spawn_recovery_observed = own.spawn_recovered;
        const auto buffs = me.buffs(); const auto restrictions = me.restrictions();
        o.can_act = !me.has_completed_turn() && !buffs.has_stun();
        o.can_move = me.can_move() && !restrictions.contains(cambc::buff_restrictions::move);
        o.can_attack = me.can_attack() && !restrictions.contains(cambc::buff_restrictions::attack);
        o.can_cast = me.can_cast() && !restrictions.contains(cambc::buff_restrictions::cast);
        o.ranged = me.attack_range() > 1; o.moved_this_turn = moved_; o.successful_cast_this_turn = cast_;
        o.attack_reset_after_move = reset_after_move_; o.observation_epoch = epoch_;
        o.feedback_sequence = feedback_.feedback_sequence; o.feedback_success = feedback_.feedback_success;
        o.feedback_kind = feedback_.feedback_kind; o.feedback_target = feedback_.feedback_target;
        o.safe = own.safe; o.in_threat_range = own.in_threat;
        o.channel = own.channel;
        o.channel_valid = o.channel != e::Channel::none;
        o.channel_safe = own.channel_safe;
        o.channel_cancellable = o.channel == e::Channel::tribute || o.channel == e::Channel::recall;
        o.can_recall = !me.state().contains(cambc::champion_state::recall_started);
        // Both uses below share this observation's actual actor and exact
        // hold horizon. A target mismatch needs a separate assigned-row quote.
        const auto quote_actor = actor();
        const int quote_hold = settings_.capture_rounds + 1;
        bool target_quoted = false, target_solo_safe = false;
        auto* target = group(state.target >= 0 ? state.target : goal.group_id);
        if (target) {
            o.group_id = target->id; o.group_live_known = target->known; o.group_cleared = target->reward;
            o.group_respawning = target->respawning; o.on_group_spawn = target->spawns.is_set(me.position());
            o.at_target = cheb(me.position(), position(cache_.nodes[target->index])) == 0 || o.on_group_spawn;
            const bool producing = goal.production && (goal.kind == GoalKind::camp_entry ||
                goal.kind == GoalKind::camp_attack || goal.kind == GoalKind::camp_spawn);
            // Bank/shop prefixes can intentionally fund the stats needed for
            // this future group. Admit its fight only when production begins,
            // using the actual post-purchase actor observed at that point.
            if (producing && target->known && !target->respawning) {
                target_quoted = true;
                target_solo_safe = target->reward || (delegated_target && target->id == goal.group_id);
                if (!target_solo_safe) {
                    auto c = target->combat; c.actors[0] = quote_actor; c.hold_ticks = quote_hold;
                    if (c.monster_count) {
                        const auto result = p5::forecast_combat(c);
                        target_solo_safe = result.state == p5::EstimateState::feasible;
                        if (execution_quote && frame_actor_valid_ && frame_actor_round_ == round_ && frame_actor_epoch_ == epoch_ &&
                            int(cambc::round_number()) == round_)
                            *execution_quote = {target, round_, quote_hold, epoch_, quote_actor.health, quote_actor.visible_enemy_next_phase, result.state};
                    }
                    if (c.monster_count && !target_solo_safe) o.safe = false;
                }
            }
            for (const auto& enemy : enemies_) if (target->spawns.is_set(enemy.position())) o.enemy_on_region = true;
            for (const auto& m : camps_) if (target->spawns.is_set(m.spawn_position()) && m.is_alive() &&
                (o.attack_target < 0 || m.health() < [&] { for (const auto& old : camps_) if (cid(old) == o.attack_target) return old.health(); return 1e30f; }())) {
                o.attack_target = cid(m); o.target_visible = o.target_category_legal = true;
                o.target_in_range = me.attack_tiles().is_set(m.position());
            }
        }
        for (auto& g : groups_) if (o.group_count < o.groups.size()) {
            const int d = current_distance_[g.index];
            const bool assigned = goal.production ? g.id == goal.group_id :
                (g.index % own_count_ == own_ordinal_ || e::known_unavailable(state, cache_.ids[own_ordinal_ % std::max(1, int(cache_.groups))]));
            bool safe = false;
            if (production_row_needs_quote(goal.production, assigned, g.known, g.respawning)) {
                if (target_quote_reusable(target ? target->id : -1, g.id, target_quoted)) safe = target_solo_safe;
                else if (delegated_target && g.id == goal.group_id) safe = true;
                else { auto c = g.combat; c.actors[0] = quote_actor; c.hold_ticks = quote_hold;
                    safe = g.reward || (c.monster_count && p5::forecast_combat(c).state == p5::EstimateState::feasible); }
            }
            o.groups[o.group_count++] = {g.id, std::max(0, d), assigned, d >= 0, safe, !g.known, false, true};
        }
        if (node({NodeKind::altar_entry, goal.altar_tile_id}) >= 0) {
            const vec2 altar{goal.altar_tile_id % cache_.width, goal.altar_tile_id / cache_.width};
            o.altar_id = altar.y * cache_.width + altar.x;
            o.adjacent_altar = cheb(me.position(), altar) <= cambc::constants::submit_gems_range;
            for (const auto& lock : cambc::vision::altar_locks(team::enemy)) if (lock.position() == altar) {
                o.hostile_lock = o.lock_visible = true; o.lock_attack_legal = me.attack_tiles().is_set(altar);
            }
        }
        o.bank_requested = goal.bank_requested;
        o.tribute_ticks_quote = tribute_exact(o.own_bag) ? settings_.channel_ticks(o.own_bag) : 0;
        update_recovery(own, o.safe);
        o.needs_recovery = recovering_;
        if (const auto item = purchase_item(goal.purchase_family)) {
            o.purchase_id = goal.purchase_family;
            o.purchase_is_equipment = true;
            o.field_equipment_allowed = own_type_ == cambc::champion_type::tavi;
            o.purchase_purchasable = equipment_shop_location_allowed(o.at_spawn, o.field_equipment_allowed,
                o.purchase_is_equipment) && me.can_buy(*item, 1);
            o.purchase_affordable = me.gold_count() >= item->cost();
        }
        if (own_receipts_.capture_round == round_) {
            o.own_payout_observed = true; o.own_payout_group = own_receipts_.capture_group;
            o.own_gems_received = own_receipts_.capture_gems; o.payout_receipt = own_receipts_.capture;
            if (own_receipts_.capture_gold_known) o.own_gold_received = own_receipts_.capture_gold;
        }
        // Default actions use only self reset/shield. A card mode exposes only
        // its exact selected event, with readiness and full target legality;
        // no second cast is exposed after that event actually succeeded.
        for (const auto& ability : me.abilities()) {
            if (goal.target_combat_mode != TargetCombatMode::basic_only) {
                if (!delegated_target || goal.target_combat_mode == TargetCombatMode::ruin_passive_only ||
                    target_event_matches(goal) || int(ability.slot()) != goal.primary_cast_slot ||
                    !target_cast_legal(goal, goal.primary_cast_slot, goal.primary_cast_target)) continue;
                auto& s = o.spells[ability.slot()];
                s.target = goal.primary_cast_target; s.ready = s.visible = s.in_range_mask = s.category_legal = s.useful = true;
                s.movement_exclusive = std::visit([](const auto& p) { return p.exclusive_with_movement; }, *ability.info());
                s.shield_needed = goal.target_combat_mode == TargetCombatMode::ruin_one_self_shield;
                continue;
            }
            if (!ability.info() || !ability.is_ready() || !o.can_cast || cast_) continue;
            auto& s = o.spells[ability.slot()];
            s.movement_exclusive = std::visit([](const auto& p) { return p.exclusive_with_movement; }, *ability.info());
            if (s.movement_exclusive && moved_) continue;
            if ((ability.is_attack_reset() || ability.is_shield()) && ability.can_target_self()) {
                s.target = own_id(); s.ready = s.visible = s.in_range_mask = s.category_legal = true;
                s.useful = ability.is_shield() ? next_incoming_ > 0 : o.target_in_range && !o.can_attack;
                s.shield_needed = ability.is_shield() && s.useful;
            }
        }
        o.fuel = int(cambc::fuel_remaining());
        return o;
    }
    e::Observation observation() {
        const auto sample = phase_begin(FuelPhase::reference_observe);
        auto out = observe_state(state_, reference_goal());
        if (!phase_end(FuelPhase::reference_observe, sample)) { out.can_act = false; out.safe = false; }
        out.fuel = int(cambc::fuel_remaining()); return out;
    }
    void receipts(e::Observation* signal = nullptr) {
        const int current_round = int(cambc::round_number());
        if (current_round != round_) {
            if (!phase_aborted_) {
                ++crossing_count_;
                crossed_phases_ |= 1u << 10;
            }
            invalidate_latches(); return;
        }
        auto& me = cambc::self(); const int bag = int(me.gem_count());
        const bool consecutive = previous_alive_ && previous_round_ >= 0 && round_ == previous_round_ + 1;
        const bool clean_gold_interval = previous_gold_finalized_ &&
            previous_purchase_sequence_ == purchase_attempt_sequence_;
        // Entry observations are not post-action stock samples. An exception
        // before finish_turn must leave next round's gold attribution unknown.
        previous_gold_finalized_ = false;
        if (previous_round_ >= 0 && round_ > previous_round_ + 1) {
            // A suspended computation or an unobserved life/channel interval
            // cannot certify either receipt. Replan from actual current stock.
            bank_intent_ = false; stage_ = Stage::none; state_ = {};
            reset_target_combat();
            ++gap_count_;
            if (gap_count_ <= 2) std::printf("P5 receipt_ambiguity observation_gap=%d count=%llu\n", round_ - previous_round_, (unsigned long long)gap_count_);
        }
        if (consecutive && previous_group_reward_ && last_attack_round_ != previous_round_ &&
            bag > previous_bag_ && previous_group_ >= 0) {
            auto* g = group(previous_group_);
            if (g && g->known && g->respawning && g->spawns.is_set(me.position()) && int(cambc::round_number()) == round_) {
                ++payout_receipt_;
                own_receipts_.capture = payout_receipt_; own_receipts_.capture_round = round_;
                own_receipts_.capture_group = previous_group_; own_receipts_.capture_gems = bag - previous_bag_;
                const auto disposition = captured_bank_.disposition == BankDisposition::card ?
                    BankDisposition::card : BankDisposition::reference;
                const int liquidation_altar = stage_ == Stage::liquidation_bank ? captured_bank_.altar : -1;
                captured_bank_ = {disposition, cached_actor_, previous_group_, round_, round_, bag,
                    payout_receipt_, bank_receipt_, liquidation_altar};
                reset_target_combat();
                own_receipts_.capture_gold_known = false; own_receipts_.capture_gold = 0;
                if ((own_type_ == cambc::champion_type::tavi || own_type_ == cambc::champion_type::heart_of_ruin ||
                     own_type_ == cambc::champion_type::prism) && clean_gold_interval &&
                    capture_gold_inventory_supported()) {
                    // Between consecutive finalized owner samples there is
                    // one round-upkeep award (SDK cached_game_settings). These
                    // three bodies have no gold passive. Supported items have
                    // no gold modifier/autonomous champion damage. Runtime
                    // never sells or attacks champions; the only damaging
                    // cast is Prism's checked direct camp-only slot0. Every
                    // outgoing purchase attempt invalidates the action stamp.
                    const auto gold = capture_gold_delta(previous_gold_, me.gold_count(),
                        cambc::constants::passive_gold, true);
                    if (int(cambc::round_number()) == round_) {
                        own_receipts_.capture_gold_known = gold.known;
                        own_receipts_.capture_gold = gold.amount;
                    }
                }
                std::printf("P5 own_payout group=%d gems=%d receipt=%llu\n", previous_group_, own_receipts_.capture_gems, (unsigned long long)payout_receipt_);
            }
        }
        if (consecutive && previous_submitting_ && !me.is_submitting_gems() && me.is_alive() && bag == 0 &&
            bank_intent_ && bank_start_ticks_ > 0 && round_ - bank_start_round_ >= bank_start_ticks_ && int(cambc::round_number()) == round_) {
            ++bank_receipt_; bank_intent_ = false;
            own_receipts_.bank = bank_receipt_; own_receipts_.bank_round = round_;
            std::printf("P5 own_bank receipt=%llu started_bag=%d round=%d\n", (unsigned long long)bank_receipt_, bank_start_bag_, round_);
        }
        if (captured_bank_.pending()) {
            if (captured_bank_.actor == cached_actor_ && captured_bank_completed(round_, me.is_alive(), bag,
                    own_receipts_.bank, captured_bank_.bank_baseline, own_receipts_.bank_round,
                    bank_start_bag_, captured_bank_.bag)) captured_bank_.disposition = BankDisposition::converted;
            else if (captured_bank_.actor != cached_actor_ ||
                !captured_stock_continues(round_, captured_bank_.observed_round, me.is_alive(), bag, captured_bank_.bag))
                captured_bank_.disposition = BankDisposition::unknown;
            else { captured_bank_.bag = bag; captured_bank_.observed_round = round_; }
        }
        previous_bag_ = bag; previous_gold_ = int(me.gold_count()); previous_submitting_ = me.is_submitting_gems();
        previous_alive_ = me.is_alive(); previous_group_ = -1; previous_group_reward_ = false;
        if (me.is_alive()) for (const auto& g : groups_) if (g.spawns.is_set(me.position())) {
            previous_group_ = g.id; previous_group_reward_ = g.known && g.reward;
        }
        previous_round_ = round_;
        if (signal && own_receipts_.capture_round == round_) {
            signal->own_payout_observed = true; signal->own_payout_group = own_receipts_.capture_group;
            signal->own_gems_received = own_receipts_.capture_gems; signal->payout_receipt = own_receipts_.capture;
            if (own_receipts_.capture_gold_known) signal->own_gold_received = own_receipts_.capture_gold;
        }
    }
    bool apply_native(const e::Decision& decision, const Goal& goal) {
        cache_goal_context(goal);
        cached_error_action_ = int(decision.action.kind); cached_error_target_ = decision.action.target;
        const bool delegated_target = target_combat_current(goal);
        snapshot_reusable_ = false; frame_actor_valid_ = false;
        target_binding_.goal.target_combat_stamp = 0; ++native_action_sequence_;
        last_action_ = int(decision.action.kind); last_reason_ = int(decision.reason);
        if (int(cambc::round_number()) != round_ || phase_aborted_) return false;
        const auto& action = decision.action; auto& me = cambc::self(); bool success = false;
        auto action_current = [&] { return int(cambc::round_number()) == round_; };
        switch (action.kind) {
        case e::ActionKind::move: case e::ActionKind::vacate: case e::ActionKind::withdraw:
            if (!action_current()) return false;
            success = me.move_absolute({action.target % cache_.width, action.target / cache_.width});
            moved_ |= success; break;
        case e::ActionKind::attack:
            if (!me.can_attack()) return false;
            if (goal.kind == GoalKind::altar && goal.phase == e::Phase::bank && action.target == goal.altar_tile_id) {
                for (const auto& lock : cambc::vision::altar_locks(team::enemy)) if (tile_id(lock.position()) == goal.altar_tile_id && me.attack_tiles().is_set(lock.position())) { if (!action_current()) return false; me.attack(lock); success = true; break; }
            } else if (goal.phase == e::Phase::clear) {
                const auto* g = group(goal.group_id);
                if (g) for (const auto& m : cambc::vision::camps())
                    if (cid(m) == action.target && m.is_alive() && g->spawns.is_set(m.spawn_position()) && me.attack_tiles().is_set(m.position())) { if (!action_current()) return false; me.attack(m); success = true; break; }
            }
            if (success) last_attack_round_ = round_;
            break;
        case e::ActionKind::cast: {
            if (goal.phase != e::Phase::clear || action.slot < 0 || action.slot > 1 || cast_ || !me.can_cast() || me.buffs().has_stun() ||
                me.restrictions().contains(cambc::buff_restrictions::cast)) return false;
            const auto ability = me.abilities()[action.slot];
            if (goal.target_combat_mode != TargetCombatMode::basic_only) {
                if (!delegated_target || goal.target_combat_mode == TargetCombatMode::ruin_passive_only ||
                    target_event_matches(goal) || action.slot != goal.primary_cast_slot || action.target != goal.primary_cast_target ||
                    !target_cast_legal(goal, action.slot, action.target)) return false;
                if (goal.target_combat_mode == TargetCombatMode::prism_one_primary) {
                    const auto* g = group(goal.group_id);
                    for (const auto& m : cambc::vision::camps()) if (g && cid(m) == action.target && m.is_visible() && m.is_alive() &&
                        g->spawns.is_set(m.spawn_position()) && ability.valid_tiles().is_set(m.position())) {
                        if (!action_current()) return false;
                        success = ability.use(m); break;
                    }
                } else {
                    if (!action_current()) return false;
                    success = ability.use();
                }
                if (success && action_current()) target_event_ = {goal.target_combat_mode, own_id(), goal.group_id,
                    action.slot, action.target, round_, round_, own_receipts_.capture};
            } else if (action.target == own_id() && ability.info() && ability.is_ready() && ability.can_target_self() &&
                (ability.is_attack_reset() || ability.is_shield()) &&
                !(moved_ && std::visit([](const auto& p) { return p.exclusive_with_movement; }, *ability.info()))) {
                if (!action_current()) return false; success = ability.use();
            }
            cast_ |= success; if (success && ability.is_attack_reset() && moved_) reset_after_move_ = true;
            break;
        }
        case e::ActionKind::start_tribute:
            if (goal.kind != GoalKind::altar || action.target != goal.altar_tile_id ||
                node({NodeKind::altar_entry, goal.altar_tile_id}) < 0 || !tribute_exact(int(me.gem_count())) ||
                cheb(me.position(), {action.target % cache_.width, action.target / cache_.width}) > cambc::constants::submit_gems_range) return false;
            bank_start_bag_ = int(me.gem_count()); bank_start_round_ = round_;
            bank_start_ticks_ = settings_.channel_ticks(bank_start_bag_);
            if (!action_current()) return false;
            me.submit_gems({action.target % cache_.width, action.target / cache_.width});
            bank_intent_ = success = true; break;
        case e::ActionKind::buy: {
            const auto item = purchase_item(action.target);
            if (goal.phase != e::Phase::shop || goal.purchase_family != action.target || !item ||
                !equipment_shop_location_allowed(cambc::map::spawn_region(team::ally).is_set(me.position()),
                    own_type_ == cambc::champion_type::tavi, true) ||
                !me.can_buy(*item, 1)) return false;
            const int cost = int(item->cost()), before = int(me.gold_count());
            // The checked SDK throws on failure. No receipt is manufactured
            // from a same-family inventory item or a passive income delta.
            if (!action_current()) return false;
            ++purchase_attempt_sequence_; // includes a throwing/uncertain SDK attempt
            me.buy(*item, 1);
            const int after = int(me.gold_count());
            if (!action_current()) return false;
            if (after != before - cost) throw std::runtime_error("P5 purchase stock mismatch");
            ++own_receipts_.purchase; own_receipts_.purchase_round = round_;
            own_receipts_.purchase_family = action.target; own_receipts_.purchase_item = int(static_cast<std::uint32_t>(*item));
            own_receipts_.purchase_cost = cost; own_receipts_.purchase_count = 1;
            success = true; break;
        }
        case e::ActionKind::start_recall: if (!action_current()) return false; me.recall(); success = true; break;
        case e::ActionKind::cancel_channel:
            if (!action_current()) return false;
            if (me.is_recalling()) me.cancel_recall(); else if (me.is_submitting_gems()) me.cancel_submit_gems(); else return false;
            bank_intent_ = false;
            success = true; break;
        case e::ActionKind::refresh: case e::ActionKind::inspect: return true;
        default: return false;
        }
        feedback_.feedback_sequence = action.sequence; feedback_.feedback_kind = action.kind;
        feedback_.feedback_target = action.target; feedback_.feedback_success = success;
        return true;
    }
    bool apply(const e::Decision& decision) {
        const auto goal = reference_goal();
        cache_goal_context(goal);
        cached_error_action_ = int(decision.action.kind); cached_error_target_ = decision.action.target;
        if (phase_aborted_ || int(cambc::round_number()) != round_ || cambc::fuel_remaining() < 16000) return false;
        const auto kind = decision.action.kind;
        if (kind == e::ActionKind::move || kind == e::ActionKind::vacate || kind == e::ActionKind::withdraw) {
            bool destination_bound = false;
            for (std::size_t i = 0; i < move_binding_.count; ++i)
                destination_bound |= move_binding_.destinations[i] == decision.action.target;
            if (!move_binding_.valid || move_binding_.goal != goal || move_binding_.phase != state_.phase ||
                move_binding_.target != state_.target || move_binding_.round != round_ || move_binding_.epoch != epoch_ ||
                !destination_bound || move_binding_.origin != tile_id(cambc::self().position())) return false;
        }
        const auto sample = phase_begin(FuelPhase::apply);
        if (sample.round != round_ || sample.fuel < 16000) return false;
        const bool result = apply_native(decision, goal);
        return phase_end(FuelPhase::apply, sample) && result;
    }
public:
    static void draft(const p5::DraftRequest& request) { draft_impl(request); }
    // This handler performs no SDK query, allocation or formatting. The cached
    // round names the origin chunk; suspension makes actual throw-round unknown.
    void unexpected_error(UnexpectedErrorKind kind, int code, const char* message) noexcept {
        if (flushing_diagnostics_) { diagnostics_blocked_ = true; flushing_diagnostics_ = false; }
        if (unexpected_count_ < unexpected_.size()) {
            auto& error = unexpected_[unexpected_count_++]; error.pending = true; error.kind = kind;
            error.actor = cached_actor_; error.cached_round = cached_error_round_;
            error.reference_stage = int(stage_); error.reference_phase = int(state_.phase);
            error.controller_stage = cached_controller_stage_; error.controller_phase = cached_controller_phase_;
            error.action = cached_error_action_; error.target = cached_error_target_; error.code = code;
            error.text = bounded_error_text(message);
        } else unexpected_dropped_ = diagnostic_increment(unexpected_dropped_);
        invalidate_latches();
    }
    unsigned pending_unexpected_errors() const noexcept {
        unsigned count = 0; for (const auto& error : unexpected_) count += error.pending; return count;
    }
    unsigned dropped_unexpected_errors() const noexcept { return unexpected_dropped_; }
    bool diagnostic_output_blocked() const noexcept { return diagnostics_blocked_; }
    template<class Function> bool guard_turn(Function&& function) noexcept {
#if defined(__cpp_exceptions)
        try {
#endif
            cached_error_round_ = int(cambc::round_number());
            cached_controller_stage_ = cached_controller_phase_ = cached_error_action_ = cached_error_target_ = -1;
            flush_unexpected_error(true);
            if (int(cambc::round_number()) != cached_error_round_) { invalidate_latches(); return false; }
            bool completion_pending = false;
            for (const auto& item : card_completions_) completion_pending |= item.pending;
            if (completion_pending) {
                flush_card_completion(true);
                if (int(cambc::round_number()) != cached_error_round_) { invalidate_latches(); return false; }
            }
            if (card_life_.pending()) {
                flush_card_life(true);
                if (int(cambc::round_number()) != cached_error_round_) { invalidate_latches(); return false; }
            }
            promote_card_report();
            std::forward<Function>(function)(); return true;
#if defined(__cpp_exceptions)
        } catch (const cambc::error& error) {
            unexpected_error(UnexpectedErrorKind::sdk, int(error.code()), error.what());
        } catch (const std::exception& error) {
            unexpected_error(UnexpectedErrorKind::standard, -1, error.what());
        } catch (...) {
            unexpected_error(UnexpectedErrorKind::unknown, -1, "unknown exception");
        }
        return false;
#endif
    }
    // Reset project-local authorization/history only. A current no-action
    // Snapshot remains usable for the caller's next project/phase decision.
    void reset_target_combat() noexcept { target_binding_ = {}; target_event_ = {}; }
    bool bind_target_combat(Goal& goal, const Snapshot& snapshot, TargetCombatMode mode,
                           int primary_slot = -1, int primary_target = -1) {
        Goal requested = goal;
        requested.target_combat_mode = mode; requested.target_combat_stamp = 0;
        requested.primary_cast_slot = primary_slot; requested.primary_cast_target = primary_target;
        auto deny = [&] {
            goal.target_combat_mode = TargetCombatMode::basic_only; goal.target_combat_stamp = 0;
            goal.primary_cast_slot = goal.primary_cast_target = -1;
            target_binding_.goal.target_combat_stamp = 0; return false;
        };
        if (mode == TargetCombatMode::basic_only) { deny(); return true; }
        if (!target_mode_matches_type(mode, own_type_)) return deny();
        if (!snapshot.complete || !snapshot.alive || !snapshot.settings_valid || !snapshot_reusable_ ||
            snapshot.round != round_ || reusable_round_ != round_ || snapshot.epoch != epoch_ ||
            reusable_epoch_ != epoch_ || snapshot.position_tile_id != tile_id(reusable_origin_) ||
            !production_goal(requested) || phase_aborted_ || int(cambc::round_number()) != round_) return deny();
        const auto* g = group(requested.group_id); auto& me = cambc::self();
        if (!g || !g->known || g->respawning || !me.is_alive() || me.position() != reusable_origin_ ||
            snapshot.self.id != own_id() || !snapshot.self.self || !snapshot.self.gold.current(round_) ||
            !snapshot.self.bag.current(round_) || snapshot.self.gold.value != int(me.gold_count()) ||
            snapshot.self.bag.value != int(me.gem_count()) || snapshot.stats.health != me.health() ||
            snapshot.stats.max_health != me.max_health() || snapshot.stats.strength != me.strength()) return deny();
        if (target_event_.group >= 0 && target_event_.group != requested.group_id) reset_target_combat();
        const bool spent = target_event_matches(requested);
        if (mode == TargetCombatMode::prism_one_primary) {
            if (own_type_ != cambc::champion_type::prism) return deny();
            requested.primary_cast_slot = primary_slot < 0 ? 0 : primary_slot;
            if (spent && primary_target < 0) requested.primary_cast_target = target_event_.target;
        } else if (mode == TargetCombatMode::ruin_one_self_shield) {
            if (own_type_ != cambc::champion_type::heart_of_ruin) return deny();
            requested.primary_cast_slot = primary_slot < 0 ? 0 : primary_slot;
            requested.primary_cast_target = primary_target < 0 ? own_id() : primary_target;
        } else if (mode == TargetCombatMode::ruin_passive_only) {
            if (own_type_ != cambc::champion_type::heart_of_ruin || primary_slot >= 0 || primary_target >= 0) return deny();
        } else return deny();
        if (mode != TargetCombatMode::ruin_passive_only) {
            if (spent) {
                if (requested.primary_cast_slot != target_event_.slot || requested.primary_cast_target != target_event_.target) return deny();
            } else if (!target_cast_legal(requested, requested.primary_cast_slot, requested.primary_cast_target)) return deny();
        }
        // The caller's exact card-specific full-tail proof uses this packet's
        // current HP, Focus, held stock and kit. No prospective effect other
        // than the single freshly legal named event may be credited. The
        // passive-only mode credits no future event and emits no cast.
        const auto focus = me.focus();
        if (int(cambc::round_number()) != round_) return deny();
        Goal old = target_binding_.goal; old.target_combat_stamp = 0;
        if (old == requested && target_binding_.round == round_ && target_binding_.epoch == epoch_ &&
            target_binding_.action == native_action_sequence_ && target_binding_.focus == focus &&
            target_binding_.goal.target_combat_stamp) {
            goal = target_binding_.goal; return true;
        }
        requested.target_combat_stamp = ++target_stamp_;
        target_binding_.goal = requested; target_binding_.actor = own_id(); target_binding_.round = round_;
        target_binding_.position = snapshot.position_tile_id; target_binding_.epoch = epoch_;
        target_binding_.action = native_action_sequence_; target_binding_.bag = snapshot.self.bag.value;
        target_binding_.gold = snapshot.self.gold.value; target_binding_.health = snapshot.stats.health;
        target_binding_.maximum = snapshot.stats.max_health; target_binding_.strength = snapshot.stats.strength;
        target_binding_.focus = focus; goal = requested;
        return int(cambc::round_number()) == round_ || deny();
    }
    bool begin_turn() {
        const int entry_round = int(cambc::round_number());
        cached_error_round_ = entry_round; cached_actor_ = own_id();
        if (!initialized_) {
            const int start_round = entry_round;
            const unsigned before = unsigned(cambc::fuel_remaining());
            initialize();
            const unsigned after = unsigned(cambc::fuel_remaining());
            cold_completed_round_ = int(cambc::round_number());
            // SDK dynamic initialization precedes this entry measurement.
            // Keep the cold callback distinct from steady packet costs.
            report_ = {}; report_.pending = report_.cold = true;
            report_.round = start_round; report_.end_round = cold_completed_round_;
            report_.entry = before; report_.end = after;
            return false;
        }
        const int current = entry_round;
        // Repeated callers cannot spend the remainder of the cold callback.
        if (current == cold_completed_round_) return false;
        if (current != round_) {
            // A deferred report describes already completed planning. Its own
            // measured print cost is separate from every phase below.
            flush_report(true);
            if (int(cambc::round_number()) != current) return false;
            snapshot_reusable_ = false; frame_actor_valid_ = false;
            ++callback_count_; phase_aborted_ = false;
            snapshot_count_ = refresh_count_ = observe_count_ = actor_count_ = forecast_count_ = 0;
            execution_quote_reuses_ = execution_work_calls_ = 0;
            phase_cost_.fill(0); crossed_phases_ = feasible_count_ = 0;
            field_compute_fuel_ = field_expansions_ = field_reuses_ = 0;
            move_plan_fuel_ = move_compute_fuel_ = move_expansions_ = move_destroy_fuel_ = 0;
            precheck_skips_ = precheck_mask_ = selection_budget_stops_ = 0;
            selection_finalize_fuel_ = selection_deferrals_ = 0;
            packet_position_tile_ = -1;
            card_cost_.fill(0); card_opportunities_ = 0; card_certified_ = false;
            last_action_ = last_reason_ = -1; entry_fuel_ = unsigned(cambc::fuel_remaining());
            round_ = current; moved_ = cast_ = reset_after_move_ = false;
            const auto destroy_start = phase_begin(FuelPhase::apply);
            move_field_.reset(); move_roots_.clear();
            const unsigned after_destroy = unsigned(cambc::fuel_remaining());
            if (int(cambc::round_number()) == round_ && destroy_start.fuel >= after_destroy)
                move_destroy_fuel_ = destroy_start.fuel - after_destroy;
            else { ++crossing_count_; crossed_phases_ |= 1u << 12; invalidate_latches(); return false; }
            bind_position_distances();
            move_binding_.valid = false;
            history_.observe(round_, cambc::global_submitted_gems(team::enemy));
        }
        if (!cambc::self().is_alive()) {
            if (captured_bank_.pending()) captured_bank_.disposition = BankDisposition::unknown;
            stage_ = Stage::none; recovering_ = true; bank_intent_ = false;
            previous_submitting_ = previous_alive_ = false; reset_target_combat();
        }
        return !phase_aborted_ && int(cambc::round_number()) == round_;
    }
    bool reference_busy() const noexcept {
        return stage_ != Stage::none || bank_intent_ || recovering_ || state_.phase == e::Phase::recover ||
            captured_bank_.disposition == BankDisposition::reference;
    }
#if PLAN5_WORKSHOP_BOOTSTRAP
    int visible_paid_source_upper_bound() const {
        // Necessary visibility only: no unseen lifecycle, stock or safety is
        // inferred. A surviving project still needs the ordinary full packet.
        const auto visible = cambc::vision::visible_tiles();
        int count = 0;
        for (const auto& g : groups_)
            if (g.native.gem_value() > 0 && !(g.spawns & visible).is_empty()) ++count;
        return int(cambc::round_number()) == round_ ? count : -1;
    }
#endif
#if PLAN5_WORKSHOP_TRACE
    // Bounded, cached post-decision facts only; no observation or action is added.
    void trace_workshop(int gate, int useful = -1) const {
        if (round_ != 37 && round_ != 985 && round_ != 1933) return;
        if (int(cambc::round_number()) != round_ || cambc::fuel_remaining() < 4000) return;
        int known = 0, reward = 0, respawn = 0;
        for (const auto& g : groups_) { known += g.known; reward += g.reward; respawn += g.respawning; }
        std::printf("P5 workshop-veto a=%d r=%d gate=%d useful=%d groups=%d/%d/%d/%d ref=%d/%d target=%d plan=%d/%d/%d action=%d/%d recovery=%d residual=%d stress=%.1f incoming=%.1f select=%u/%u/%u/%u fuel=%u\n",
            cached_actor_, round_, gate, useful, int(groups_.size()), known, reward, respawn, int(stage_), int(state_.phase), state_.target,
            int(plan_.feasible), plan_.group, plan_.renewal, last_action_, last_reason_, int(recovering_), int(residual_invalid_),
            route_stress_, next_incoming_, forecast_count_, feasible_count_, precheck_mask_, selection_budget_stops_, unsigned(cambc::fuel_remaining()));
    }
#endif
    bool reference_capture_offer_hint() const {
        if (!initialized_ || phase_aborted_ || stage_ != Stage::production || !plan_.feasible ||
            !previous_alive_ || !previous_gold_finalized_ || previous_round_ != round_ - 1 ||
            !previous_group_reward_ || previous_group_ != plan_.group || last_attack_round_ == previous_round_) return false;
        auto& me = cambc::self();
        return int(cambc::round_number()) == round_ && me.is_alive() && int(me.gem_count()) > previous_bag_ &&
            !me.is_submitting_gems() && !me.is_recalling() && !me.is_teleporting() && int(cambc::round_number()) == round_;
    }
    bool accept_reference_capture_offer(const Snapshot& snapshot) {
        const bool accepted = stage_ == Stage::output_bank;
        if ((stage_ != Stage::production && !accepted) || !plan_.feasible || phase_aborted_ ||
            !snapshot.complete || !snapshot.alive || !snapshot.settings_valid || !snapshot_reusable_ ||
            snapshot.round != round_ || reusable_round_ != round_ || snapshot.epoch != epoch_ || reusable_epoch_ != epoch_ ||
            snapshot.position_tile_id != tile_id(reusable_origin_) || snapshot.channel != e::Channel::none ||
            snapshot.self.id != cached_actor_ || !snapshot.self.self || !snapshot.self.visible || !snapshot.self.own_team ||
            !snapshot.self.bag.current(round_) ||
            snapshot.self.bag.value <= 0 || snapshot.receipts.capture_gems <= 0 ||
            !capture_offer_serial_ok(round_, snapshot.receipts.capture_round, snapshot.receipts.capture_group, plan_.group,
                snapshot.receipts.capture, own_receipts_.capture, state_.consumed_payout, accepted) ||
            snapshot.receipts.capture_round != own_receipts_.capture_round ||
            snapshot.receipts.capture_group != own_receipts_.capture_group ||
            snapshot.receipts.capture_gems != own_receipts_.capture_gems) return false;
        const auto* g = group(plan_.group); auto& me = cambc::self();
        if (!g || !g->known || !g->respawning || !me.is_alive() || me.position() != reusable_origin_ ||
            !g->spawns.is_set(me.position()) || int(me.gem_count()) != snapshot.self.bag.value ||
            me.is_submitting_gems() || me.is_recalling() || me.is_teleporting() || int(cambc::round_number()) != round_) return false;
        if (!accepted) {
            e::Observation point; point.owner_turn = round_; point.remaining_rounds = settings_.round_limit - round_;
            point.forecast_turns = plan_.duration;
            stage_ = Stage::output_bank; e::enter(state_, e::Phase::allocate, point);
            state_.consumed_payout = snapshot.receipts.capture;
            bank_baseline_ = bank_receipt_; bank_intent_ = false; move_binding_.valid = false;
        }
        // Only the receipt stage changes. The reference bank/renewal tail stays
        // committed until a real successful SDK card action takes ownership.
        return true;
    }
    void reference_turn(const Snapshot* initial = nullptr) {
        cached_controller_stage_ = cached_controller_phase_ = -1;
        if (!begin_turn()) return;
        auto& me = cambc::self();
        if (!me.is_alive()) return;
        if (!settings_ok_) return;
        if (reference_packet_round_ == round_) { finish_turn(); return; }
        reference_packet_round_ = round_;
        const bool reuse_initial = initial && initial->complete && snapshot_reusable_ &&
            initial->round == round_ && reusable_round_ == round_ &&
            initial->epoch == epoch_ && reusable_epoch_ == epoch_ &&
            initial->position_tile_id == tile_id(reusable_origin_) && me.position() == reusable_origin_ &&
            int(cambc::round_number()) == round_;
        // Eligibility is private and consumed even if no action fits. Only
        // native group/threat facts are reused; executor facts are rebuilt.
        snapshot_reusable_ = false;
        // A same-turn budget deferral is not evidence that captured stock was
        // lost. finish_turn still rejects an actual owner-round crossing.
        if (cambc::fuel_remaining() < 16000) { finish_turn(); return; }
        // The basic-only forecast represents one native action attempt per
        // owner turn. Executor transitions remain bounded inside decision().
        for (int decision_count = 0; decision_count < 1; ++decision_count) {
            if (decision_count || !reuse_initial) {
                const auto refresh_sample = phase_begin(FuelPhase::reference_refresh);
                refresh();
                if (!phase_end(FuelPhase::reference_refresh, refresh_sample)) { finish_turn(); return; }
            }
            auto o = observation();
            if (phase_aborted_) { finish_turn(); return; }
            receipts(&o);
            if (phase_aborted_) { finish_turn(); return; }
            // A caller invoking Reference has ended its provisional card work.
            // Reclaim only the currently observed surviving own stock, never
            // the old card movement, HP, forecast or receipt timing.
            if (captured_bank_.disposition == BankDisposition::card)
                captured_bank_.disposition = BankDisposition::reference;
            if (stage_ == Stage::liquidation_bank && !captured_bank_.pending()) {
                stage_ = Stage::none; state_ = {}; break;
            }
            // A secured terminal bag needs no productive successor. Retire an
            // older nonterminal tail before its renewal proof can veto the
            // existing bank-only winning candidate. Actual recovery/safety
            // below remains mandatory; valid channels keep their priority.
            const bool terminal_inventory = terminal_bank_replan(o.alive, o.channel == e::Channel::none, false,
                cambc::global_submitted_gems(team::ally), o.own_bag, settings_.winning_gems);
            const LiveGroup* hold = nullptr;
            if (!terminal_inventory && o.channel == e::Channel::none) {
                hold = fresh_unpaid_hold();
                if (int(cambc::round_number()) != round_) { invalidate_latches(); finish_turn(); return; }
            }
            if (hold) {
                // The current region can differ from an aborted card's old
                // target. Preserve its actual unpaid capture independently of
                // that target; this authorizes holding only, never a fight or
                // a fabricated complete production plan.
                if (stage_ != Stage::production || plan_.group != hold->id) {
                    stage_ = Stage::none; plan_.feasible = plan_.winning = false; residual_invalid_ = false;
                }
                if (state_.target != hold->id || state_.phase != e::Phase::hold) {
                    state_.target = hold->id; e::enter(state_, e::Phase::hold, o);
                }
                last_action_ = int(e::ActionKind::hold); last_reason_ = int(e::Reason::ordinary);
                finish_turn(); return;
            }
            if (terminal_inventory && !plan_.winning) {
                stage_ = Stage::none; state_ = {}; bank_intent_ = false; residual_invalid_ = false;
            }
            bool stage_changed = false;
            if (o.own_payout_observed && stage_ == Stage::production && o.own_payout_group == plan_.group) {
                stage_ = Stage::output_bank; e::enter(state_, e::Phase::allocate, o); state_.consumed_payout = o.payout_receipt;
                stage_changed = true;
            }
            if ((stage_ == Stage::initial_bank || stage_ == Stage::output_bank) && bank_receipt_ > bank_baseline_) {
                bank_baseline_ = bank_receipt_;
                if (stage_ == Stage::initial_bank && !plan_.bank_only && !plan_.winning) {
                    stage_ = Stage::production; e::enter(state_, e::Phase::bootstrap, o); state_.target = plan_.group;
                } else if (plan_.winning) { stage_ = Stage::none; state_ = {}; break; }
                else { stage_ = Stage::renewal; e::enter(state_, e::Phase::renew, o); }
                stage_changed = true;
            }
            if (stage_changed) o = observation();
            if (phase_aborted_) { finish_turn(); return; }
            const bool finished_tail = stage_ == Stage::output_bank || stage_ == Stage::renewal ||
                (stage_ == Stage::initial_bank && plan_.bank_only && !plan_.winning);
            if (finished_tail && o.channel == e::Channel::none && (stage_changed || !residual_safe())) {
                const bool refreshed = refresh_finished_tail();
                if (phase_aborted_ || int(cambc::round_number()) != round_) { finish_turn(); return; }
                if (!refreshed) residual_invalid_ = true;
                else {
                    residual_invalid_ = false;
                    if (stage_ == Stage::renewal && plan_.via_spawn) enter_spawn_recovery(o);
                }
                o = observation();
                if (phase_aborted_) { finish_turn(); return; }
            }
            // Receipts have already advanced their own stages. A newly larger
            // outside threat invalidates stale economics before late retreat,
            // while a valid channel or safe cleared hold keeps its priority.
            if (stage_ != Stage::none && stage_ != Stage::liquidation_bank && !residual_invalid_ && !residual_safe()) {
                residual_invalid_ = true;
            }
            const bool safe_hold = o.group_cleared && o.on_group_spawn && o.safe;
            bool liquidation_entered = false;
            if (residual_invalid_ && stage_ != Stage::none && o.channel == e::Channel::none && !safe_hold) {
                // Failure of the productive tail does not make secured gems
                // disappear. Prefer a fresh safe bank-only disposition; if
                // physical recovery is needed, retain the obligation through
                // the actual recall/spawn-healing observations below.
                const bool liquidated = stage_ == Stage::output_bank && captured_bank_.pending() &&
                    !recovering_ && o.safe && enter_liquidation(o);
                liquidation_entered = liquidated;
                if (!liquidated) {
                    stage_ = Stage::none; bank_intent_ = false; recovering_ = true; state_ = {};
                    e::enter(state_, e::Phase::recover, o);
                }
                o = observation();
            }
            if (phase_aborted_ || int(cambc::round_number()) != round_) { finish_turn(); return; }
            if (!terminal_inventory && captured_bank_.disposition == BankDisposition::reference &&
                stage_ == Stage::none && o.channel == e::Channel::none && !recovering_ && !safe_hold) {
                liquidation_entered = enter_liquidation(o);
                if (!liquidation_entered) {
                    if (phase_aborted_) { finish_turn(); return; }
                    // No safe current route is a wait/recovery, never license
                    // to resume farming with the unconverted captured bag.
                    if (!o.at_spawn && cambc::self().health() < cambc::self().max_health()) {
                        recovering_ = true; state_ = {}; e::enter(state_, e::Phase::recover, o);
                    } else break;
                }
                o = observation();
            }
            if (stage_ == Stage::liquidation_bank && o.channel == e::Channel::none) {
                const int left = state_.phase_budget - (round_ - state_.phase_start);
                if (recovering_ || !o.safe || left <= 0 ||
                    (!liquidation_entered && liquidation_quote(captured_bank_.altar, left).altar < 0)) {
                    if (phase_aborted_) { finish_turn(); return; }
                    stage_ = Stage::none; bank_intent_ = false; state_ = {};
                    recovering_ = recovering_ || !o.safe ||
                        (!o.at_spawn && cambc::self().health() < cambc::self().max_health());
                    if (!recovering_) break;
                    e::enter(state_, e::Phase::recover, o); o = observation();
                }
            }
            if (stage_ == Stage::production && state_.phase == e::Phase::bootstrap && o.channel == e::Channel::none && position_field_round_ != round_) {
                position_field(); o = observation();
            }
            if (stage_ == Stage::none && o.channel == e::Channel::none && !recovering_ &&
                (terminal_inventory || captured_bank_.disposition != BankDisposition::reference)) {
                select();
                if (phase_aborted_ || int(cambc::round_number()) != round_) { finish_turn(); return; }
                // A newly selected direct-production plan is quoted with one
                // startup tick. Never pay its first production observation
                // from the old nonproduction packet's remaining fuel.
                if (stage_ == Stage::production) { ++selection_deferrals_; break; }
                // The admitted plan survives to the next fresh callback. Do
                // not spend its finalization reserve on a second observation
                // and then restart selection after a suspended turn.
                if (cambc::fuel_remaining() < observe_chunk_peak_ + 16000u) {
                    ++selection_deferrals_; break;
                }
                o = observation();
            }
            if (phase_aborted_ || int(cambc::round_number()) != round_) { finish_turn(); return; }
            if (stage_ == Stage::recovery && o.channel == e::Channel::none) {
                e::consume_feedback(o, state_);
                if (!o.safe || route_stress_ > 0 || o.own_bag != 0 ||
                    round_ - state_.phase_start >= state_.phase_budget) {
                    stage_ = Stage::none; bank_intent_ = false;
                    recovering_ = !o.safe || route_stress_ > 0; state_ = {};
                    if (recovering_) e::enter(state_, e::Phase::recover, o);
                    break;
                }
                if (o.at_spawn) {
                    if (state_.recovery_arrival < 0) state_.recovery_arrival = round_;
                    // Arrival itself never supplies a healing receipt. Require
                    // uninterrupted live spawn presence and actual full HP at
                    // a later owner start; forecast ticks cannot advance this.
                    if (!spawn_recovery_confirmed(round_, state_.recovery_arrival,
                        previous_spawn_round_, o.alive, o.at_spawn, me.health(), me.max_health())) break;
                    stage_ = Stage::renewal; e::enter(state_, e::Phase::renew, o);
                    state_.recovery_arrival = -1; move_binding_.valid = false;
                    o = observation();
                    if (phase_aborted_ || int(cambc::round_number()) != round_) { finish_turn(); return; }
                } else {
                    state_.recovery_arrival = -1;
                    movement(o); o.fuel = cambc::fuel_remaining();
                    if (phase_aborted_ || int(cambc::round_number()) != round_) { finish_turn(); return; }
                    (void)apply(e::move_intent(o, state_));
                    break;
                }
            }
            if (stage_ == Stage::renewal && o.channel == e::Channel::none) {
                e::consume_feedback(o, state_);
                auto* next = plan_.renewal >= 0 && plan_.renewal < cache_.groups ? group(cache_.ids[plan_.renewal]) : nullptr;
                const bool reached = next && me.position() == position(cache_.nodes[plan_.renewal]);
                if (reached && o.safe && work(*next, me.health(), false, true).state == p5::EstimateState::feasible) {
                    std::printf("P5 renewal group=%d round=%d health=%.1f fuel=%u\n", next->id, round_, double(me.health()), unsigned(cambc::fuel_remaining()));
                    stage_ = Stage::none; state_ = {}; break;
                }
                if (!o.safe || round_ - state_.phase_start >= state_.phase_budget) { stage_ = Stage::none; recovering_ = !o.safe; break; }
                if (reached) break;
                o.move_count = 0; movement(o);
                const auto action = e::move_intent(o, state_); if (!apply(action)) break; continue;
            }
            auto decision = e::decision(o, state_);
            // Let the executor first select its actual phase/target. Only an
            // action that needs movement pays for the selected native field.
            if (decision.action.kind == e::ActionKind::yield && decision.reason == e::Reason::illegal &&
                o.channel == e::Channel::none && o.can_move && cambc::fuel_remaining() >= 30000) {
                const auto selected_goal = reference_goal();
                const auto selected_phase = state_.phase; const int selected_target = state_.target;
                movement(o); o.fuel = cambc::fuel_remaining();
                if (phase_aborted_ || int(cambc::round_number()) != round_) { finish_turn(); return; }
                const bool selected_bootstrap = selected_phase == e::Phase::bootstrap && o.safe && o.can_act &&
                    o.channel == e::Channel::none && state_.phase == selected_phase && state_.target == selected_target &&
                    reference_goal() == selected_goal && o.owner_turn == round_ && o.observation_epoch == epoch_ &&
                    move_binding_.valid && move_binding_.goal == selected_goal && move_binding_.phase == selected_phase &&
                    move_binding_.target == selected_target && move_binding_.round == round_ && move_binding_.epoch == epoch_;
                // Selection already happened on this exact packet. Reusing
                // its bound move does not pay the bootstrap selection floor a
                // second time after the selected movement field was built.
                decision = selected_bootstrap ? e::move_intent(o, state_) : e::decision(o, state_);
            }
            if (decision.reason == e::Reason::timeout || decision.reason == e::Reason::capture_unresolved || decision.reason == e::Reason::no_opportunity) {
                stage_ = Stage::none;
            }
            if (!apply(decision)) break;
        }
        finish_turn();
    }
    void finish_turn() {
        snapshot_reusable_ = false;
        auto round_matches = [&] {
            const int now = int(cambc::round_number());
            if (now == round_ && !phase_aborted_) return true;
            if (!phase_aborted_) {
                ++crossing_count_;
                crossed_phases_ |= 1u << 8;
            }
            invalidate_latches(); return false;
        };
        if (!round_matches()) { queue_report(); flush_report(); round_matches(); return; }
        auto& me = cambc::self();
        const bool alive = me.is_alive();
        if (!alive) { previous_alive_ = previous_gold_finalized_ = false; previous_spawn_round_ = -1; reset_target_combat(); queue_report(); flush_report(); round_matches(); return; }
        const auto position = me.position(); const auto state = me.state();
        previous_spawn_round_ = cambc::map::spawn_region(team::ally).is_set(position) ? round_ : -1;
        // Refresh the final stock/channel snapshot; capture attribution on the
        // next turn uses this actual own state, never a lifecycle transition.
        previous_bag_ = int(me.gem_count()); previous_gold_ = int(me.gold_count());
        previous_purchase_sequence_ = purchase_attempt_sequence_; previous_gold_finalized_ = true;
        previous_submitting_ = state.contains(cambc::champion_state::submitting_gems); previous_alive_ = alive;
        previous_round_ = round_; previous_group_ = -1; previous_group_reward_ = false;
        for (const auto& g : groups_) if (g.spawns.is_set(position)) {
            previous_group_ = g.id; previous_group_reward_ = g.known && g.reward;
        }
        round_matches(); queue_report(); flush_report(); round_matches();
    }
    bool snapshot_into(Snapshot& out) {
        out.complete = false; out.group_count = out.altar_count = 0;
        out.round = -1; out.body_ordinal = 0; out.team_score = out.fuel = 0; out.position_tile_id = -1; out.epoch = 0;
        out.alive = out.settings_valid = out.channel_safe = out.safe_hold = out.needs_recovery = out.at_spawn = false;
        out.channel = e::Channel::none; out.type = own_type_; out.self = {}; out.stats = {};
        out.settings = {}; out.windows = {}; out.receipts = {}; out.route_stress = out.next_incoming = 0;
        out.purchases = purchase_quotes_;
        auto fail = [&] {
            out.complete = false; out.settings_valid = false; out.group_count = out.altar_count = 0;
            invalidate_latches(); return false;
        };
        if (!begin_turn()) return fail();
        snapshot_reusable_ = false; ++snapshot_count_;
        // Fill the caller-owned packet and only its counted combat prefixes.
        // No unused group/actor/monster payload is constructed or copied here.
        const auto export_sample = phase_begin(FuelPhase::snapshot_export);
        auto& me = cambc::self();
        out.round = round_; out.body_ordinal = own_ordinal_; out.type = own_type_;
        out.alive = me.is_alive(); out.settings_valid = settings_ok_; out.settings = settings_;
        out.team_score = int(cambc::global_submitted_gems(team::ally)); out.fuel = int(cambc::fuel_remaining());
        out.windows = history_.windows(round_, settings_.round_limit, settings_.winning_gems, cambc::global_submitted_gems(team::enemy));
        out.self = {own_id(), true, true, true,
            {int(me.gem_count()), p5::Knowledge::current, round_}, {int(me.gold_count()), p5::Knowledge::current, round_}};
        if (!out.alive || out.fuel < 16000) {
            out.epoch = epoch_; out.receipts = own_receipts_;
            phase_end(FuelPhase::snapshot_export, export_sample);
            out.fuel = int(cambc::fuel_remaining()); return fail();
        }
        const auto refresh_sample = phase_begin(FuelPhase::snapshot_refresh);
        refresh();
        if (!phase_end(FuelPhase::snapshot_refresh, refresh_sample)) {
            phase_end(FuelPhase::snapshot_export, export_sample);
            out.epoch = epoch_; out.fuel = int(cambc::fuel_remaining()); return fail();
        }
        receipts();
        if (phase_aborted_ || int(cambc::round_number()) != round_) {
            phase_end(FuelPhase::snapshot_export, export_sample); return fail();
        }
        out.stats = actor(); out.position_tile_id = tile_id(me.position()); out.epoch = epoch_;
        packet_position_tile_ = out.position_tile_id;
        out.route_stress = route_stress_; out.next_incoming = next_incoming_; out.receipts = own_receipts_;
        const auto own = own_flags(); update_recovery(own, own.safe);
        out.channel = own.channel; out.channel_safe = own.channel != e::Channel::none && own.channel_safe;
        out.needs_recovery = recovering_; out.at_spawn = own.at_spawn;
        bitboard allied_positions, enemy_positions;
        const int self_id = own_id();
        for (const auto& ally : cambc::vision::champions(team::ally))
            if (int(static_cast<cambc::handle_champion_t>(ally)) != self_id) allied_positions.set(ally.position());
        for (const auto& enemy : enemies_) enemy_positions.set(enemy.position());
        for (const auto& g : groups_) {
            auto& fact = out.groups[out.group_count++];
            fact.id = g.id; fact.entry_tile_id = tile_id(position(cache_.nodes[g.index]));
            fact.gems = int(g.native.gem_value()); fact.gold = int(g.native.gold_value());
            fact.known = g.known; fact.reward = g.reward; fact.respawning = g.respawning;
            copy_combat_prefix(fact.combat, g.combat); fact.external_stress = external_stress(&g);
            fact.other_holder = !(g.spawns & allied_positions).is_empty();
            if (own.safe && g.known && g.reward && g.spawns.is_set(own.position) &&
                (g.spawns & enemy_positions).is_empty()) out.safe_hold = true;
        }
        const auto locks = cambc::vision::altar_locks(team::enemy);
        for (int i = 0; i < cache_.altars; ++i) {
            auto& fact = out.altars[out.altar_count++]; const auto p = position(cache_.altar_tiles[i]);
            fact.tile_id = tile_id(p); fact.visible = visible_.is_set(p); fact.hostile_lock = false;
            fact.adjacent = cheb(own.position, p) <= cambc::constants::submit_gems_range;
            for (const auto& lock : locks) fact.hostile_lock |= lock.position() == p;
        }
        const bool current = phase_end(FuelPhase::snapshot_export, export_sample);
        out.fuel = int(cambc::fuel_remaining());
        out.complete = current && !phase_aborted_ && int(cambc::round_number()) == round_;
        if (!out.complete) return fail();
        if (out.complete && out.settings_valid) {
            snapshot_reusable_ = true; reusable_round_ = round_; reusable_epoch_ = epoch_;
            reusable_origin_ = {out.position_tile_id % cache_.width, out.position_tile_id / cache_.width};
        }
        return out.complete;
    }
    bool prepare_selection() {
        if (!begin_turn()) return false;
        auto& me = cambc::self();
        if (!settings_ok_ || !me.is_alive() || me.is_submitting_gems() || me.is_recalling() || me.is_teleporting() || cambc::fuel_remaining() < 45000) return false;
        if (position_field_round_ != round_) position_field();
        return position_field_round_ == round_ && position_field_origin_ == me.position() && int(cambc::round_number()) == round_;
    }
    // Original immutable spawn geometry already loaded by initialize().
    // This carries no lifecycle, visibility, ownership or combat certificate.
    std::uint64_t geometry_generation() const noexcept { return geometry_generation_; }
    bool group_spawn_mask(int id, bitboard& out) const {
        const auto* physical=group(id);if(!physical)return false;
        out=physical->spawns;return true;
    }
    p5::RouteLeg route(Node from, Node to) const {
        p5::RouteLeg out; out.destination = to.kind == NodeKind::spawn ? 0 : to.id;
        const int a = node(from), b = node(to);
        if (from.kind == NodeKind::current || to.kind == NodeKind::current) {
            const int other = from.kind == NodeKind::current ? b : a;
            if (other >= 0 && position_distances_valid_ && position_field_geometry_ == geometry_generation_ &&
                position_field_round_ == round_ && position_field_origin_ == cambc::self().position()) out.distance = current_distance_[other];
        } else if (a >= 0 && b >= 0) out.distance = static_distance(a, b);
        out.terrain_and_diagonals_checked = out.distance >= 0;
        return out; // Joint cycle safety belongs to the candidate certificate.
    }
    // The caller supplies only investment projects whose own purchase, capture,
    // whole-bank and fresh productive-return gates actually completed. This
    // records cached scalars only; it does not infer success or query the SDK.
    void record_card_completion(int job, int group, int family, int renewal) noexcept {
        if (card_completion_count_ >= card_completions_.size()) {
            card_completion_dropped_ = diagnostic_increment(card_completion_dropped_); return;
        }
        if (!initialized_ || phase_aborted_ || round_ < 0 || cached_actor_ < 0 ||
            job < 0 || group < 0 || family < 0 || renewal < 0) return;
        card_completions_[card_completion_count_++] = {true, cached_actor_, round_, job, group, family, renewal};
    }
    unsigned dropped_card_completions() const noexcept { return card_completion_dropped_; }
    bool record_card_life(const p5::diagnostics::CardLifeRecord& record) noexcept {
        return card_life_.armed() ? card_life_.push(record) : card_life_.arm(record);
    }
    void record_card_plan(unsigned start_fuel, unsigned built_fuel, unsigned chosen_fuel,
        unsigned end_fuel, std::size_t opportunities, bool certified, int end_round, bool menu_complete = true,
        const CardBuildTrace* trace = nullptr) {
        card_cost_.fill(0);
        card_opportunities_ = unsigned(opportunities); card_certified_ = certified;
        const bool measured = end_round == round_ && start_fuel >= built_fuel && built_fuel >= chosen_fuel && chosen_fuel >= end_fuel;
        if (measured)
            card_cost_ = {start_fuel - built_fuel, built_fuel - chosen_fuel, chosen_fuel - end_fuel};
        else crossed_phases_ |= 1u << 9;
        // First three actual attempts survive a full perf slot. These deferred
        // reports contain card costs only (attempt=1), never later phase data.
        if (card_report_count_ < card_reports_.size()) {
            auto& request = card_reports_[card_report_count_++]; request.pending = true;
            request.round = round_; request.end_round = end_round; request.callback = callback_count_;
            request.entry = start_fuel; request.end = end_fuel; request.costs = card_cost_;
            request.opportunities = unsigned(opportunities); request.certified = certified;
            request.menu_complete = menu_complete;
            request.cross_mask = measured ? 0 : 1u << 9;
            request.has_build_trace = trace != nullptr;
            if (trace) request.build_trace = *trace;
        }
    }
    e::Observation execution_observation(const e::State& state, const Goal& goal) {
        cache_goal_context(goal); cached_error_action_ = cached_error_target_ = -1;
        const auto sample = phase_begin(FuelPhase::execution_observe);
        ExecutionTargetQuote execution_quote;
        auto out = observe_state(state, goal, &execution_quote);
        out.owner_turn = round_; out.observation_epoch = epoch_; out.move_count = 0;
        const bool priority = (out.channel_valid && out.channel_safe) ||
            (goal.production && out.group_cleared && out.on_group_spawn && out.safe);
        if (out.alive && out.channel == e::Channel::none && !priority && goal.kind != GoalKind::recover) {
            if (goal.forecast_turns < 0) {
                // Missing route exposure is not a zero-cost forecast or a
                // completed unsafe proof. Keep point facts; authorize no action.
                out.can_act = out.can_move = false;
            } else {
                const auto* target = goal.production ? group(goal.group_id) : nullptr;
                const double threat = external_stress(target);
                const double health = cambc::self().health() - threat * goal.forecast_turns;
                const bool reserve = health > 0 && health >= std::max(.2 * cambc::self().max_health(), 2 * threat);
                bool fight = !goal.production;
                if (target && goal.production) {
                    fight = target_combat_current(goal);
                    if (!fight) {
                        // work() would copy this same group and cached actor,
                        // set the same health, zero threat and hold horizon.
                        // Any changed input takes the original full path.
                        const bool same = execution_quote.target == target && execution_quote.round == round_ &&
                            execution_quote.epoch == epoch_ && execution_quote.hold == settings_.capture_rounds + 1 &&
                            execution_quote.threat == 0 && execution_quote.health == health &&
                            frame_actor_valid_ && frame_actor_round_ == round_ && frame_actor_epoch_ == epoch_ &&
                            int(cambc::round_number()) == round_;
                        if (same) { ++execution_quote_reuses_; fight = execution_quote.state == p5::EstimateState::feasible; }
                        else { ++execution_work_calls_; fight = work(*target, health, false, false).state == p5::EstimateState::feasible; }
                    }
                }
                if (!reserve || !fight) { out.safe = false; out.needs_recovery = true; recovering_ = true; }
            }
        }
        if (!phase_end(FuelPhase::execution_observe, sample)) { out.can_act = false; out.safe = false; }
        out.fuel = int(cambc::fuel_remaining()); return out;
    }
    void add_moves(e::Observation& observed, const e::State& state, const Goal& goal) {
        cache_goal_context(goal);
        observed.move_count = 0; move_binding_.valid = false;
        if (goal.phase != state.phase || observed.observation_epoch != epoch_ || observed.owner_turn != round_ ||
            int(cambc::round_number()) != round_ || observed.channel != e::Channel::none || observed.forecast_turns < 0) return;
        if (goal.target_combat_mode != TargetCombatMode::basic_only && !target_combat_current(goal)) return;
        movement(observed, state, goal); observed.fuel = int(cambc::fuel_remaining());
    }
    ApplyResult apply(const e::Decision& decision, const e::Observation& observed, const e::State& state, const Goal& goal,
                      ApplyTrace* trace = nullptr, ApplyBudgetDeferral* budget_deferral = nullptr) {
        if (budget_deferral) *budget_deferral = ApplyBudgetDeferral::none;
        cache_goal_context(goal);
        cached_error_action_ = int(decision.action.kind); cached_error_target_ = decision.action.target;
        if (trace) {
            *trace = {}; trace->entry_round = int(cambc::round_number()); trace->entry_fuel = int(cambc::fuel_remaining());
            trace->sequence = decision.action.sequence; trace->kind = decision.action.kind; trace->target = decision.action.target;
        }
        auto finish_apply = [&](ApplyResult result, ApplyExit exit) {
            if (trace) {
                trace->exit = exit; trace->result = result; trace->exit_fuel = int(cambc::fuel_remaining());
                trace->exit_round = int(cambc::round_number());
                trace->feedback_sequence = feedback_.feedback_sequence; trace->feedback_kind = feedback_.feedback_kind;
                trace->feedback_target = feedback_.feedback_target; trace->feedback_success = feedback_.feedback_success;
                trace->feedback_matches = decision.action.sequence > 0 && feedback_.feedback_sequence == decision.action.sequence &&
                    feedback_.feedback_kind == decision.action.kind && feedback_.feedback_target == decision.action.target;
            }
            return result;
        };
        if (observed.observation_epoch != epoch_ || observed.owner_turn != round_ || int(cambc::round_number()) != round_ || goal.phase != state.phase) return finish_apply(ApplyResult::stale, ApplyExit::authority);
        if (goal.target_combat_mode != TargetCombatMode::basic_only && !target_combat_current(goal)) return finish_apply(ApplyResult::stale, ApplyExit::target_combat);
        const auto kind = decision.action.kind;
        if (cambc::fuel_remaining() < 16000 && kind != e::ActionKind::preserve_channel && kind != e::ActionKind::hold) {
            if (budget_deferral) *budget_deferral = ApplyBudgetDeferral::entry;
            return finish_apply(ApplyResult::stopped, ApplyExit::entry_fuel);
        }
        if (kind == e::ActionKind::move || kind == e::ActionKind::withdraw || kind == e::ActionKind::vacate) {
            bool destination_bound = false;
            for (std::size_t i = 0; i < move_binding_.count; ++i) destination_bound |= move_binding_.destinations[i] == decision.action.target;
            if (!move_binding_.valid || move_binding_.goal != goal || move_binding_.phase != state.phase ||
                move_binding_.target != state.target || move_binding_.round != round_ || move_binding_.epoch != epoch_ ||
                move_binding_.geometry != geometry_generation_ || !destination_bound) return finish_apply(ApplyResult::stale, ApplyExit::move_binding);
            auto& me = cambc::self(); const vec2 p{decision.action.target % cache_.width, decision.action.target / cache_.width};
            // Membership binds the SDK-selected one-step geometry to this
            // owner/epoch/origin. Requery every mutable blocker at the landing
            // tile instead of rebuilding a second full terrain/reachable board.
            if (move_binding_.origin != tile_id(me.position()) || !me.can_move() ||
                cambc::vision::has_champion(p) || cambc::vision::has_camp(p) ||
                cambc::vision::teleport_tiles().is_set(p)) return finish_apply(ApplyResult::stale, ApplyExit::move_legality);
            if (trace) trace->move_fuel = int(cambc::fuel_remaining());
        }
        if (int(cambc::round_number()) != round_) return finish_apply(ApplyResult::stale, ApplyExit::round);
        if (cambc::fuel_remaining() < 16000 && kind != e::ActionKind::preserve_channel && kind != e::ActionKind::hold) {
            if (budget_deferral) *budget_deferral = ApplyBudgetDeferral::after_move;
            return finish_apply(ApplyResult::stopped, ApplyExit::post_move_fuel);
        }
        const auto sample = phase_begin(FuelPhase::apply);
        if (trace) trace->native_called = true;
        const bool acted = apply_native(decision, goal);
        if (!phase_end(FuelPhase::apply, sample)) return finish_apply(ApplyResult::stale, ApplyExit::native_phase);
        if (!acted) return finish_apply(ApplyResult::stopped, ApplyExit::native_rejected);
        // A provisional card owns the body only after a successful SDK state
        // change. Inspection, refresh, failed moves and other no-ops retain the
        // durably accepted reference bank/renewal fallback.
        const bool mutated = reference_action_mutates(kind, feedback_.feedback_success) &&
            feedback_.feedback_sequence == decision.action.sequence && feedback_.feedback_kind == kind &&
            feedback_.feedback_target == decision.action.target;
        if (mutated) {
            if (captured_bank_.pending()) captured_bank_.disposition = BankDisposition::card;
            stage_ = Stage::none; state_ = {}; plan_ = {}; residual_invalid_ = false;
        }
        ++epoch_; move_binding_.valid = false;
        return finish_apply(ApplyResult::acted, ApplyExit::acted);
    }
    bool productive_ready(int public_group_id) const {
        const auto* g = group(public_group_id);
        if (!g || !cambc::self().is_alive() || cambc::self().position() != position(cache_.nodes[g->index])) return false;
        for (const auto& ally : cambc::vision::champions(team::ally))
            if (int(static_cast<cambc::handle_champion_t>(ally)) != own_id() && g->spawns.is_set(ally.position())) return false;
        return work(*g, cambc::self().health(), false, true).state == p5::EstimateState::feasible;
    }
};
} // namespace p5::native
