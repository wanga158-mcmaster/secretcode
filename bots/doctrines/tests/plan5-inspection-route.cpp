// Actual Dividend goal -> Runtime route/observation -> inspection controller.
// Deterministic read-only SDK host; no native game or fuel qualification.
#define PLAN5_WORKSHOP_BOOTSTRAP 1
#if defined(__wasm__)
#undef __wasm__
#include <cambc.h>
#define __wasm__ 1
#else
#include <cambc.h>
#endif
#include "../dividend/src/policy.h"
#include "../dividend/src/basic-quotes.h"
#include "../common/src/card-life.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <utility>
#include <vector>
// Compile this fixture only with Clang -fno-access-control. Replacing `class`
// would also rewrite the actual headers' template<class Function> declarations.
#include "../dividend/src/native.h"

extern "C" void* __cxa_allocate_exception(std::size_t size) { return std::malloc(size); }
extern "C" void __cxa_throw(void* error, void*, void(*)(void*)) {
    std::fprintf(stderr, "Unexpected SDK fixture exception: %s\n",
                 static_cast<std::exception*>(error)->what());
    std::abort();
}

namespace {
namespace d = p5::dividend;
namespace e = p5::execution;
namespace n = p5::native;
using Bot = d::native_binding::Bot;
constexpr unsigned side = 64, words = 64;
unsigned checks = 0, current_round = 565, bag = 0, gold = 98;
std::int64_t host_fuel = 1000000;
cambc::detail::champion_info actor{};
cambc::detail::camp_state wolf{};
std::array<std::uint64_t, words> personal_vision{};
void fixture_check(bool value, int line) {
    ++checks;
    if (!value) { std::fprintf(stderr, "inspection-route check %u failed at line %d\n", checks, line); std::abort(); }
}
#define CHECK(...) ::fixture_check(bool((__VA_ARGS__)), __LINE__)
unsigned word(int x, int y) { return unsigned(y / 8) * 8 + unsigned(x / 8); }
std::uint64_t bit(int x, int y) { return std::uint64_t{1} << ((y % 8) * 8 + x % 8); }
bool inside(int x, int y) { return x >= 0 && y >= 0 && x < int(side) && y < int(side); }
[[noreturn]] void unsupported_query(const char* name) {
    std::fprintf(stderr, "Unexpected inspection fixture query: %s\n", name);
    CHECK(false); std::abort();
}

void setup(Bot& bot, int distance = 7) {
    current_round = 565; bag = 0; gold = 98; host_fuel = 1000000;
    actor = {0, int(cambc::champion_type::marcella), 27, 1, 1200, 1200, 50, 60, 1, 1,
             1 | (1 << 3) | (1 << 4)};
    wolf = {35, 6, 1218, 1350, 20, 0, {0, 0}};
    auto& r = bot.runtime_;
    r.cache_ = {}; r.cache_.width = side; r.cache_.height = side;
    r.cache_.groups = 1; r.cache_.ids[0] = 32; r.cache_.nodes[0] = {34, 3};
    r.groups_.clear(); r.camps_.clear(); r.enemies_.clear();
    const auto native = cambc::detail::handle_access::make_camp_group(
        static_cast<cambc::handle_camp_group_t>(32), 40, 11, 50, {});
    n::LiveGroup group{native, cambc::bitboard{}, 32, 0};
    group.spawns.set({35, 4}); r.groups_.push_back(std::move(group));
    r.camps_.push_back(cambc::detail::handle_access::make_camp(
        static_cast<cambc::handle_camp_t>(78), static_cast<cambc::handle_camp_group_t>(30),
        cambc::camp_type::wolf, {35, 6}));
    r.settings_ = {5, 10, 1, 2000, 1000, 2};
    r.round_ = int(current_round); r.epoch_ = 1; r.phase_aborted_ = false;
    r.own_type_ = cambc::champion_type::marcella;
    r.own_base_.camp_damage_multiplier = 1;
    r.own_count_ = 1; r.own_ordinal_ = 0;
    r.frame_actor_valid_ = false; r.recovering_ = false;
    r.enemy_stress_ = r.route_stress_ = r.next_incoming_ = 0;
    r.danger_.clear(); r.own_receipts_ = {};
    r.geometry_generation_ = r.position_field_geometry_ = 1;
    r.position_distances_valid_ = true; r.position_field_round_ = r.round_;
    r.position_field_origin_ = {27, 1}; r.current_distance_.fill(-1); r.current_distance_[0] = distance;
    bot.project_ = {}; bot.altar_ = -1; bot.renewal_ = 34;
    auto& p = bot.project_;
    p.stage = d::ProjectStage::purchase_inspection;
    p.selected.group = 32; p.selected.rank.occupied_turns = 137;
    p.selected.purchase_family = 0;
    p.execution_state.target = 32; p.execution_state.phase = e::Phase::bootstrap;
    p.execution_state.phase_start = 541; p.execution_state.phase_budget = 300;
}
d::ProjectFacts facts() {
    d::ProjectFacts f;
    f.observation_round = int(current_round); f.own_gold = int(gold); f.own_bag = int(bag);
    return f;
}
e::Observation observe(Bot& bot, const n::Goal& goal) {
    return bot.runtime_.execution_observation(bot.project_.execution_state, goal);
}
void unknown_check(Bot& bot) {
    const int start = bot.project_.execution_state.phase_start;
    const auto goal = bot.goal(); CHECK(goal.forecast_turns == -1);
    auto o = observe(bot, goal);
    CHECK(o.forecast_turns == -1 && o.safe && !o.needs_recovery);
    CHECK(!o.can_act && !o.can_move && o.move_count == 0);
    const auto action = d::execute_job(o, facts(), bot.project_);
    CHECK(action.action.kind == e::ActionKind::yield && action.reason == e::Reason::unknown_target);
    CHECK(bot.project_.stage == d::ProjectStage::purchase_inspection);
    CHECK(bot.project_.execution_state.phase_start == start);
}
}

namespace cambc::detail { extern "C" {
std::int32_t sdk_get_settings(game_settings* out) {
    *out = {}; out->capture_time = 5; out->respawn_time = 50; out->recall_time = 5;
    out->submit_gem_duration_multiplier = .1; out->submit_gem_minimum_duration = 1;
    out->max_rounds = 2000; out->winning_gems = 1000; return 0;
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
std::int32_t sdk_champion_cooldown(std::uint32_t, std::uint32_t, std::int32_t* out) { *out = 8; return 0; }
std::int32_t sdk_champion(std::uint32_t id, champion_info* out) { CHECK(id == 0); *out = actor; return 0; }
std::int32_t sdk_champion_identity(std::uint32_t id, champion_identity* out) { CHECK(id == 0); *out = {actor.team, actor.type}; return 0; }
std::int32_t sdk_champion_is_alive(std::uint32_t id, std::int32_t* out) { CHECK(id == 0); *out = actor.state & 1; return 0; }
std::int32_t sdk_champion_gems(std::uint32_t id, std::uint32_t* out) { CHECK(id == 0); *out = bag; return 0; }
std::int32_t sdk_champion_gold(std::uint32_t id, std::uint32_t* out) { CHECK(id == 0); *out = gold; return 0; }
std::int32_t sdk_champion_buff_data(std::uint32_t, buff_data*, std::uint32_t, std::uint32_t* count) { *count = 0; return 0; }
std::int32_t sdk_camp_buffs(std::uint32_t, camp_buff_data*, std::uint32_t, std::uint32_t* count) { *count = 0; return 0; }
std::int32_t sdk_champion_restrictions(std::uint32_t, std::uint8_t* out) { *out = 0; return 0; }
std::int32_t sdk_champion_can_shop(std::uint32_t, std::int32_t, std::int32_t* out) { *out = 0; return 0; }
std::int32_t sdk_camp(std::uint32_t id, camp_state* out) { CHECK(id == 78); *out = wolf; return 0; }
std::int32_t sdk_camp_state(std::uint32_t id, std::uint32_t* out) { CHECK(id == 78); *out = 1; return 0; }
std::int32_t sdk_camp_design(std::int32_t, camp_design* out) { *out = {}; out->base_stats.base_attack_range = 3; return 0; }
std::int32_t sdk_altar_locks(altar_lock_info*, std::uint32_t, std::uint32_t* count) { *count = 0; return 0; }
std::int32_t sdk_bc_round_number(std::uint32_t* out) { *out = current_round; return 0; }
std::int64_t sdk_fuel_remaining() { return host_fuel; }
std::int32_t sdk_vision(std::uint64_t* out) { std::copy(personal_vision.begin(), personal_vision.end(), out); return 0; }
// Link-only generic preparation branches must never fabricate data for these
// availability cases. If an intended early exit fails, the fixture stops here.
std::int32_t sdk_bc_submitted_gems(std::uint32_t, std::uint32_t*) { unsupported_query("submitted_gems"); }
std::int32_t sdk_map_camp_layout(std::uint32_t*, std::uint32_t, std::uint32_t*) { unsupported_query("map_camp_layout"); }
std::int32_t sdk_ally_occupancy(std::uint64_t*) { unsupported_query("ally_occupancy"); }
std::int32_t sdk_enemy_occupancy(std::uint64_t*) { unsupported_query("enemy_occupancy"); }
std::int32_t sdk_has_champion(std::int32_t, std::int32_t, std::int32_t*) { unsupported_query("has_champion"); }
std::int32_t sdk_has_camp(std::int32_t, std::int32_t, std::int32_t*) { unsupported_query("has_camp"); }
std::int32_t sdk_champion_at(std::int32_t, std::int32_t, std::uint32_t*) { unsupported_query("champion_at"); }
std::int32_t sdk_DistanceField_ComputeWithExpansionCount(const std::uint64_t*, const std::uint64_t*, std::uint64_t*,
                                                       std::uint16_t*, std::uint16_t, std::uint16_t*) {
    unsupported_query("DistanceField_ComputeWithExpansionCount");
}
std::int32_t sdk_DistanceField_Get(const std::uint16_t*, std::int32_t, std::int32_t, std::uint16_t*) {
    unsupported_query("DistanceField_Get");
}
std::int32_t sdk_bc_map_symmetry(std::int32_t* out) { *out = 0; return 0; }
std::int32_t sdk_bc_map_mask(std::int32_t, std::uint64_t* out) { std::fill_n(out, words, 0); return 0; }
std::int32_t sdk_spawn_region(std::int32_t, aabb* out) { *out = {0, 0, 0, 0}; return 0; }
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
        if (in[word(x, y)] & bit(x, y)) {
            if (*count < capacity) out[*count] = {x, y};
            ++*count;
        }
    return 0;
}
std::int32_t sdk_Bitboard_DrawChebyshev(std::uint64_t* out, std::int32_t x, std::int32_t y, float radius) {
    for (int row = 0; row < int(side); ++row)
        for (int col = 0; col < int(side); ++col)
            if (float(std::max(std::abs(col - x), std::abs(row - y))) <= radius)
                out[word(col, row)] |= bit(col, row);
    return 0;
}
}}

namespace {
void paid_source_visibility_is_only_an_upper_bound() {
    Bot bot(d::Arm::active); setup(bot); auto& r = bot.runtime_;
    r.groups_.clear(); personal_vision.fill(0);
    for (int i = 0; i < 4; ++i) {
        const auto native = cambc::detail::handle_access::make_camp_group(
            static_cast<cambc::handle_camp_group_t>(40 + i), 120, i == 3 ? 0 : 8, 60, {});
        n::LiveGroup group{native, cambc::bitboard{}, 40 + i, i};
        group.spawns.set({10 + i, 10}); group.known = true; r.groups_.push_back(std::move(group));
    }
    // Cached knowledge cannot substitute for fresh personal visibility.
    CHECK(r.visible_paid_source_upper_bound() == 0);
    for (int i = 0; i < 3; ++i) {
        personal_vision[word(10 + i, 10)] |= bit(10 + i, 10);
        r.groups_[i].known = false; r.groups_[i].respawning = true;
        CHECK(r.visible_paid_source_upper_bound() == i + 1);
    }
    // This says only "potentially three". No lifecycle query/certificate is
    // fabricated; the existing full packet must decide actual usability.
    personal_vision[word(13, 10)] |= bit(13, 10);
    CHECK(r.visible_paid_source_upper_bound() == 3); // gold-only is insufficient
    personal_vision.fill(0); CHECK(r.visible_paid_source_upper_bound() == 0);
    ++current_round; CHECK(r.visible_paid_source_upper_bound() == -1);
}
void fresh_remaining_leg() {
    Bot bot(d::Arm::active); setup(bot);
    const auto leg = bot.runtime_.route({}, {n::NodeKind::group_entry, 32});
    CHECK(leg.distance == 7 && leg.turns() == 9);
    const auto goal = bot.goal();
    CHECK(goal.kind == n::GoalKind::camp_entry && !goal.production && goal.forecast_turns == 9);
    const auto o = observe(bot, goal);
    CHECK(o.safe && !o.needs_recovery && o.forecast_turns == 9 && o.alive);
    CHECK(1200 - 20 * goal.forecast_turns == 1020 && 1020 >= .2 * 1200);
    const auto result = d::execute_job(o, facts(), bot.project_);
    CHECK(bot.project_.stage == d::ProjectStage::purchase_inspection);
    CHECK(result.action.kind != e::ActionKind::attack && result.action.kind != e::ActionKind::buy);
    setup(bot); auto frozen = bot.goal(); frozen.forecast_turns = 137;
    const auto old = observe(bot, frozen);
    CHECK(!old.safe && old.needs_recovery && 1200 - 20 * frozen.forecast_turns == -1540);
    setup(bot); bot.project_.stage = d::ProjectStage::output_bank;
    CHECK(bot.goal().forecast_turns == 137);
    bot.project_.stage = d::ProjectStage::production;
    CHECK(bot.goal().forecast_turns == 137 && bot.goal().production);
}
void unknown_routes() {
    Bot bot(d::Arm::active);
    setup(bot); --bot.runtime_.position_field_round_; unknown_check(bot);
    setup(bot); bot.runtime_.position_field_origin_ = {26, 1}; unknown_check(bot);
    setup(bot); ++bot.runtime_.geometry_generation_; unknown_check(bot);
    setup(bot); bot.runtime_.current_distance_[0] = -1; unknown_check(bot);
    setup(bot); bot.runtime_.position_distances_valid_ = false; unknown_check(bot);
    setup(bot); bot.project_.selected.group = 99; bot.project_.execution_state.target = 99; unknown_check(bot);
    setup(bot); bot.runtime_.position_distances_valid_ = false;
    current_round = 841; bot.runtime_.round_ = int(current_round);
    const auto o = observe(bot, bot.goal());
    const auto result = d::execute_job(o, facts(), bot.project_);
    CHECK(result.action.kind == e::ActionKind::yield && bot.project_.stage == d::ProjectStage::aborted);
}
void safety_and_channel() {
    Bot bot(d::Arm::active); setup(bot); actor.health = 200;
    auto o = observe(bot, bot.goal()); CHECK(!o.safe && o.needs_recovery);
    auto result = d::execute_job(o, facts(), bot.project_);
    CHECK(result.action.kind == e::ActionKind::yield && bot.project_.stage == d::ProjectStage::aborted);
    setup(bot); wolf.strength = 200;
    o = observe(bot, bot.goal()); CHECK(!o.safe && o.needs_recovery);
    result = d::execute_job(o, facts(), bot.project_);
    CHECK(bot.project_.stage == d::ProjectStage::aborted);
    setup(bot); actor.state = 0; actor.health = 0;
    o = observe(bot, bot.goal()); CHECK(!o.alive);
    result = d::execute_job(o, facts(), bot.project_);
    CHECK(bot.project_.stage == d::ProjectStage::aborted);
    setup(bot); actor.state |= 1 << 2; bot.runtime_.position_distances_valid_ = false;
    bot.project_.execution_state.phase_start = 0; bot.project_.execution_state.phase_budget = 8;
    o = observe(bot, bot.goal()); CHECK(o.channel_valid && o.channel_safe && o.forecast_turns == -1);
    result = d::execute_job(o, facts(), bot.project_);
    CHECK(result.action.kind == e::ActionKind::preserve_channel);
    CHECK(bot.project_.stage == d::ProjectStage::purchase_inspection);
}
void arrival_requires_tail() {
    Bot bot(d::Arm::active); setup(bot, 0);
    actor.x = 34; actor.y = 3; bot.runtime_.position_field_origin_ = {34, 3};
    auto o = observe(bot, bot.goal()); CHECK(o.at_target && o.group_id == 32 && o.safe);
    auto f = facts(); f.purchase_source_reached = f.purchase_tail_checked = true;
    const auto unsafe = d::execute_job(o, f, bot.project_);
    CHECK(unsafe.reason == e::Reason::unsafe && bot.project_.stage == d::ProjectStage::aborted);
    setup(bot, 0); actor.x = 34; actor.y = 3; bot.runtime_.position_field_origin_ = {34, 3};
    o = observe(bot, bot.goal()); f = facts(); f.purchase_source_reached = true;
    const int start = bot.project_.execution_state.phase_start;
    const auto deferred = d::execute_job(o, f, bot.project_);
    CHECK(deferred.reason == e::Reason::unknown_target && bot.project_.stage == d::ProjectStage::purchase_inspection);
    CHECK(bot.project_.execution_state.phase_start == start);
    // Only a separately completed fresh tail certificate can admit production.
    f.purchase_tail_checked = f.purchase_tail_safe = true;
    const auto admitted = d::execute_job(o, f, bot.project_);
    CHECK(admitted.action.kind == e::ActionKind::refresh && bot.project_.stage == d::ProjectStage::production);
}

// These are current deterministic input records, not a claim that a remote
// query exists. Exercise the actual native facts classifier before its numeric
// tail/route work. Every hard case is a known supply change; unknown records and
// malformed combat inputs remain unchecked rather than becoming unsafe proofs.
n::Snapshot availability_snapshot(Bot& bot) {
    n::Snapshot s;
    s.round = int(current_round); s.epoch = bot.runtime_.epoch_;
    s.complete = s.alive = s.settings_valid = true;
    s.self.id = 0; s.self.self = s.self.visible = s.self.own_team = true;
    s.self.gold = {int(gold), p5::Knowledge::current, int(current_round)};
    s.self.bag = {int(bag), p5::Knowledge::current, int(current_round)};
    s.stats = bot.runtime_.actor(); s.settings = bot.runtime_.settings_;
    s.receipts.purchase = 13; s.receipts.purchase_round = 541;
    s.receipts.purchase_family = 0; s.receipts.purchase_cost = 30; s.receipts.purchase_count = 1;
    s.receipts.bank = 4; s.receipts.capture = 8;
    s.group_count = 2;
    for (int i = 0; i < 2; ++i) {
        auto& g = s.groups[i];
        g.id = i == 0 ? 32 : 34; g.known = true; g.gems = 11; g.gold = 40;
        g.combat.actor_count = g.combat.monster_count = 1;
        g.combat.actors[0] = s.stats;
        g.combat.monsters[0] = {80 + i, 120, 120, 0, true};
    }
    bot.project_.selected.job = d::Job::cash_produce;
    bot.project_.purchase_baseline = 13;
    bot.project_.bank_baseline = 4; bot.project_.payout_baseline = 8;
    return s;
}
void sync_source(Bot& bot, const n::Snapshot& s) {
    auto& live = bot.runtime_.groups_[0];
    live.known = s.groups[0].known; live.respawning = s.groups[0].respawning;
    live.reward = s.groups[0].reward;
}
void unchanged_purchase(const Bot& bot, const d::ProjectFacts& f, int start) {
    CHECK(gold == 98 && bag == 0 && actor.strength == 50);
    CHECK(f.own_gold == 98 && f.own_bag == 0 && f.own_purchase_receipt == 13);
    CHECK(f.purchased_cost == 30 && f.purchased_count == 1 && f.purchased_family == 0);
    CHECK(bot.project_.purchase_baseline == 13 && bot.project_.bank_baseline == 4 && bot.project_.payout_baseline == 8);
    CHECK(!bot.project_.purchase_intent_outstanding && !bot.project_.bank_intent_outstanding);
    CHECK(bot.project_.execution_state.phase_start == start);
}
void known_supply_changes() {
    for (int arrived = 0; arrived < 2; ++arrived)
    for (int role = 0; role < 2; ++role) for (int hard = 0; hard < 4; ++hard) {
        Bot bot(d::Arm::active); setup(bot, arrived ? 0 : 7);
        if (arrived) { actor.x = 34; actor.y = 3; bot.runtime_.position_field_origin_ = {34, 3}; }
        auto s = availability_snapshot(bot);
        auto& changed = s.groups[role];
        switch (hard) {
        case 0: changed.respawning = true; break;
        case 1: changed.reward = true; break; // Collectible cleared supply: named fight-tail is stale, not zero payout.
        case 2: changed.other_holder = true; break;
        default: changed.gems = 0; break;
        }
        sync_source(bot, s);
        const int start = bot.project_.execution_state.phase_start;
        auto o = observe(bot, bot.goal());
        CHECK(o.at_target == bool(arrived)); // Known changed supply also aborts during travel.
        const auto f = bot.facts(s, &o);
        CHECK(f.purchase_targets_unavailable && !f.purchase_tail_checked && !f.purchase_tail_safe);
        CHECK(f.purchase_source_reached == bool(arrived));
        const auto result = d::execute_job(o, f, bot.project_);
        CHECK(result.action.kind == e::ActionKind::yield && result.reason == e::Reason::no_opportunity);
        CHECK(bot.project_.stage == d::ProjectStage::aborted);
        unchanged_purchase(bot, f, start);
    }
    // Known unavailability is not disguised as a low-fuel or missing-route
    // wait. The original clock and exact accepted purchase remain untouched.
    for (int stop = 0; stop < 2; ++stop) {
        Bot bot(d::Arm::active); setup(bot);
        auto s = availability_snapshot(bot); s.groups[1].respawning = true;
        if (stop == 0) host_fuel = 0;
        else bot.runtime_.position_distances_valid_ = false;
        const int start = bot.project_.execution_state.phase_start;
        auto o = observe(bot, bot.goal()); const auto f = bot.facts(s, &o);
        CHECK(f.purchase_targets_unavailable);
        const auto result = d::execute_job(o, f, bot.project_);
        CHECK(result.reason == e::Reason::no_opportunity && bot.project_.stage == d::ProjectStage::aborted);
        unchanged_purchase(bot, f, start);
    }
}
void availability_requires_current_packet() {
    for (int invalid = 0; invalid < 7; ++invalid) {
        Bot bot(d::Arm::active); setup(bot);
        auto s = availability_snapshot(bot); s.groups[0].respawning = true;
        sync_source(bot, s); auto o = observe(bot, bot.goal());
        switch (invalid) {
        case 0: s.complete = false; break;
        case 1: s.settings_valid = false; break;
        case 2: --s.round; break;
        case 3: --o.owner_turn; break;
        case 4: o.group_id = 99; break;
        case 5: bot.project_.stage = d::ProjectStage::output_bank; break;
        default: break;
        }
        const auto f = bot.facts(s, invalid == 6 ? nullptr : &o);
        CHECK(!f.purchase_targets_unavailable && !f.purchase_source_reached && !f.purchase_tail_checked);
        CHECK(bot.project_.purchase_baseline == 13 && gold == 98 && actor.strength == 50);
    }
    CHECK(!d::ProjectFacts{}.purchase_targets_unavailable);
}
void unavailable_is_not_unknown() {
    // At the actual entry, unknown/missing supply and malformed combat cannot
    // authorize work or masquerade as a completed negative availability proof.
    for (int missing = 0; missing < 14; ++missing) {
        Bot bot(d::Arm::active); setup(bot, 0);
        actor.x = 34; actor.y = 3; bot.runtime_.position_field_origin_ = {34, 3};
        auto s = availability_snapshot(bot);
        switch (missing) {
        case 0: s.groups[0].known = false; break;
        case 1: s.groups[1].known = false; break;
        case 2: s.group_count = 1; break;
        case 3: s.groups[0] = s.groups[1]; s.group_count = 1; break;
        case 4: s.groups[0].combat.monster_count = 0; break;
        case 5: s.groups[1].combat.monster_count = 0; break;
        default: {
            // Remembered flags do not become current evidence merely because
            // they would be decisive on an actually known source/successor.
            auto& stale = s.groups[(missing - 6) / 4];
            stale.known = false;
            switch ((missing - 6) % 4) {
            case 0: stale.respawning = true; break;
            case 1: stale.reward = true; break;
            case 2: stale.other_holder = true; break;
            default: stale.gems = 0; break;
            }
            break;
        }
        }
        // Keep the actual selected source observation at group32 for the
        // missing-row case; unavailable metadata is precisely what is tested.
        if (missing != 3) sync_source(bot, s);
        host_fuel = 50000;
        const int start = bot.project_.execution_state.phase_start;
        auto o = observe(bot, bot.goal()); const auto f = bot.facts(s, &o);
        CHECK(o.at_target && f.purchase_source_reached);
        CHECK(!f.purchase_targets_unavailable && !f.purchase_tail_checked && !f.purchase_tail_safe);
        const auto result = d::execute_job(o, f, bot.project_);
        CHECK(result.reason == e::Reason::unknown_target && result.action.kind == e::ActionKind::yield);
        CHECK(bot.project_.stage == d::ProjectStage::purchase_inspection);
        unchanged_purchase(bot, f, start);
    }
    Bot bot(d::Arm::active); setup(bot);
    auto s = availability_snapshot(bot); s.groups[1].known = false;
    auto o = observe(bot, bot.goal()); auto f = bot.facts(s, &o);
    const int start = bot.project_.execution_state.phase_start;
    (void)d::execute_job(o, f, bot.project_);
    CHECK(bot.project_.stage == d::ProjectStage::purchase_inspection && !f.purchase_targets_unavailable);
    // A later current known supply change terminates the same private project;
    // merely retrying does not reset its clock or invent another purchase.
    ++current_round; ++bot.runtime_.round_; s.round = int(current_round);
    s.self.gold.round = s.self.bag.round = s.round;
    s.groups[1].known = true; s.groups[1].other_holder = true;
    o = observe(bot, bot.goal()); f = bot.facts(s, &o);
    CHECK(f.purchase_targets_unavailable);
    CHECK(d::execute_job(o, f, bot.project_).reason == e::Reason::no_opportunity);
    CHECK(bot.project_.stage == d::ProjectStage::aborted);
    unchanged_purchase(bot, f, start);
}
void unavailable_preserves_channel_and_deadline() {
    Bot bot(d::Arm::active); setup(bot);
    auto s = availability_snapshot(bot); s.groups[0].reward = true;
    sync_source(bot, s); actor.state |= 1 << 2;
    const int start = bot.project_.execution_state.phase_start;
    auto o = observe(bot, bot.goal()); auto f = bot.facts(s, &o);
    CHECK(f.purchase_targets_unavailable && o.channel_valid && o.channel_safe);
    CHECK(d::execute_job(o, f, bot.project_).action.kind == e::ActionKind::preserve_channel);
    CHECK(bot.project_.stage == d::ProjectStage::purchase_inspection);
    unchanged_purchase(bot, f, start);
    actor.state &= ~(1 << 2); ++current_round; ++bot.runtime_.round_; s.round = int(current_round);
    s.self.gold.round = s.self.bag.round = s.round;
    o = observe(bot, bot.goal()); f = bot.facts(s, &o);
    const auto result = d::execute_job(o, f, bot.project_);
    CHECK(result.reason == e::Reason::no_opportunity && bot.project_.stage == d::ProjectStage::aborted);
    CHECK(!o.own_payout_observed && f.own_capture_receipt == 8 && f.own_bank_receipt == 4);
    unchanged_purchase(bot, f, start);
    setup(bot); s = availability_snapshot(bot); s.groups[1].respawning = true;
    bot.project_.execution_state.phase_start = 0; bot.project_.execution_state.phase_budget = 8;
    o = observe(bot, bot.goal()); f = bot.facts(s, &o);
    CHECK(f.purchase_targets_unavailable);
    const auto expired = d::execute_job(o, f, bot.project_);
    CHECK(expired.reason == e::Reason::unsafe && bot.project_.stage == d::ProjectStage::aborted);
}
}
int main() {
    paid_source_visibility_is_only_an_upper_bound();
    fresh_remaining_leg(); unknown_routes(); safety_and_channel(); arrival_requires_tail();
    known_supply_changes(); availability_requires_current_packet(); unavailable_is_not_unknown();
    unavailable_preserves_channel_and_deadline();
    std::printf("Inspection route caller checks passed: %u (deterministic host, no native gameplay)\n", checks);
}
