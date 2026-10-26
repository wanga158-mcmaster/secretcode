#include "../workshop/src/policy.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { std::fprintf(stderr, "Workshop check %d failed at line %d: %s\n", checks, __LINE__, #condition); std::abort(); } } while (false)
using namespace p5;
using namespace p5::workshop;
namespace e = p5::execution;

static RouteLeg leg(int destination, int distance = 0) { return {destination, distance, true, true}; }
static Group group(int id, int gems, int gold, double hp, double incoming, const CombatActor& actor) {
    Group g;
    g.id = id; g.gems = gems; g.gold = gold; g.live_observed = g.reward_or_alive = true;
    g.combat.actor_count = g.combat.monster_count = 1;
    g.combat.actors[0] = actor;
    g.combat.monsters[0] = {id + 100, hp, hp, incoming, true};
    return g;
}
static Observation fixture() {
    Observation o;
    // Synthetic oracle settings and timings, never claims about native balance.
    o.settings = {2, 10, 1, 200, 1000, 0};
    ScoreHistory history; CHECK(history.observe(0, 0));
    o.windows = history.windows(0, 200, 1000, 0);
    o.self.id = 7; o.self.self = o.self.visible = o.self.own_team = true;
    o.self.bag = {0, Knowledge::current, 0}; o.self.gold = {0, Knowledge::current, 0};
    o.stats.id = 7; o.stats.self = o.stats.visible = o.stats.ranged = true;
    o.stats.health = o.stats.max_health = 100; o.stats.strength = 20;
    o.bank_legal = true; o.current_to_altar = leg(50); o.bank_return = leg(900);
    o.count = 2;
    o.routes[0] = {group(1, 20, 10, 20, 0, o.stats), group(11, 20, 0, 20, 0, o.stats),
                   leg(1), leg(11), leg(50), leg(900)};
    o.routes[1] = {group(2, 10, 30, 20, 0, o.stats), group(12, 50, 0, 240, 8, o.stats),
                   leg(2), leg(12), leg(50), leg(900)};
    o.purchase_count = 1;
    o.purchases[0] = {0, 200, 1, 30, 40, 0, 0, true, true};
    return o;
}
static Decision choose(const Observation& o, Arm arm = Arm::active) {
    return choose_job(o, {}, Profile{arm});
}
static ProjectFacts facts(const Decision& d) {
    ProjectFacts f;
    f.observation_round = 0; f.own_actor = 7; f.own_bag = d.initial_bag; f.own_gold = d.initial_gold;
    f.own_capture_receipt = 10; f.own_purchase_receipt = 20; f.own_bank_receipt = 30;
    return f;
}
static e::Observation execution_observation() {
    e::Observation o;
    o.remaining_rounds = 120; o.forecast_turns = 30; o.fuel = 100000;
    o.capture_duration = 2; o.safe = o.can_act = o.can_move = o.can_attack = true;
    o.ranged = true; o.field_equipment_allowed = o.purchase_purchasable = true;
    o.group_count = 2;
    o.groups[0] = {2, 0, true, true, true, false, false, true};
    o.groups[1] = {12, 0, true, true, true, false, false, true};
    o.group_id = 2; o.at_target = o.group_live_known = true;
    o.target_visible = o.target_category_legal = o.target_in_range = true; o.attack_target = 102;
    o.move_count = 1; o.moves[0] = {40, true, true, true, true, false};
    o.observation_epoch = 1;
    return o;
}
static void own_payout(e::Observation& o, ProjectFacts& f, int turn, int group_id,
                       int gems, int gold, std::uint64_t receipt) {
    o.owner_turn = turn; f.observation_round = turn;
    o.group_id = group_id; o.own_payout_observed = true; o.own_payout_group = group_id;
    o.own_gems_received = gems; o.own_gold_received = gold; o.payout_receipt = receipt;
    f.own_capture_receipt = receipt; f.own_bag += gems; f.own_gold += gold;
}
static void confirm_purchase(e::Observation& o, ProjectFacts& f, int turn) {
    o.owner_turn = turn; f.observation_round = turn; o.own_payout_observed = false;
    f.own_purchase_confirmed = true; ++f.own_purchase_receipt;
    f.purchased_family = 0; f.purchased_item = 200; f.purchased_cost = 30; f.purchased_shards = 1;
    f.own_gold -= 30;
}
static void planning() {
    auto o = fixture();
    const auto a = choose(o), c = choose(o, Arm::route_first_control);
    CHECK(a.feasible && c.feasible);
    CHECK(a.first_group == 2 && a.successor_group == 12 && a.timing == Timing::after_first_capture);
    CHECK(c.first_group == 1 && c.successor_group == 11 && c.timing == Timing::none);
    CHECK(a.rank.Y == (std::array<AccountingStock, 3>{60, 60, 60}));
    CHECK(c.rank.Y == (std::array<AccountingStock, 3>{40, 40, 40}));
    CHECK(a.rank.actor_ids == c.rank.actor_ids && a.rank.windows == c.rank.windows);
    CHECK(a.rank.final_bag[0] == 0 && a.rank.final_gold[0] == 0);
    CHECK(c.rank.final_gold[0] == 10 && a.rank.D == (std::array<AccountingStock, 3>{0, 0, 0}));
    CHECK(a.forecast.first_payout_tick == 7 && a.forecast.purchase_tick == 7);
    CHECK(a.forecast.second_payout_tick == 20 && a.forecast.bank_tick == 28 && a.forecast.productive_tick == 30);
    CHECK(a.forecast.second_injury == 64 && a.forecast.final_health == 36);
    CHECK(!project(o, o.routes[1]).feasible);
    CHECK(!project(o, o.routes[1], Timing::now, &o.purchases[0]).feasible);
    CHECK(a.schedule.event_count == 6 && c.schedule.event_count == 5);
    CHECK(a.schedule.events[0].kind == AccountingEventKind::capture && a.schedule.events[0].group == 2);
    CHECK(a.schedule.events[1].kind == AccountingEventKind::purchase && a.schedule.events[1].tick == 7);

    // No starting resources are needed for the reference's first real source.
    o.purchase_count = 0;
    const auto zero = choose(o);
    CHECK(zero.feasible && zero.first_group == 1 && zero.timing == Timing::none && zero.rank.Y[0] == 40);
    CHECK(zero.schedule.actors[0].bag == 0 && zero.schedule.actors[0].gold == 0);

    o = fixture(); o.windows.window = {24, 24, 24}; o.windows.deadline = {24, 24, 24};
    const auto short_a = choose(o), short_c = choose(o, Arm::route_first_control);
    CHECK(short_a.first_group == 1 && short_c.first_group == 1);
    CHECK(short_a.rank.Y[0] == 40 && project(o, o.routes[1], Timing::after_first_capture, &o.purchases[0]).rank.Y[0] == 0);
    o = fixture(); o.routes[1].between.distance = 100;
    CHECK(choose(o).first_group == 1); // Financing no longer repays inside the represented windows.
    o = fixture(); o.needs_recovery = true;
    CHECK(choose(o).job == Job::recover && choose(o, Arm::route_first_control).job == Job::recover);
    o = fixture(); o.purchases[0].strength_gain = 0;
    CHECK(choose(o).first_group == 1 && choose(o).timing == Timing::none);
    o = fixture(); o.self.gold.value = 30;
    const auto now = choose(o);
    CHECK(now.feasible && now.first_group == 2 && now.timing == Timing::now);
    CHECK(now.forecast.purchase_tick == 0 && now.schedule.events[0].kind == AccountingEventKind::purchase);
    o = fixture(); o.routes[1].first.gold = 29;
    CHECK(choose(o).first_group == 1);

    o = fixture(); o.count = 9; CHECK(!choose(o).feasible);
    o = fixture(); o.purchase_count = 4; CHECK(!choose(o).feasible);
    o = fixture(); o.routes[1].successor.id = o.routes[1].first.id;
    CHECK(!project(o, o.routes[1], Timing::after_first_capture, &o.purchases[0]).feasible);
    o = fixture(); o.self.gold.knowledge = Knowledge::stale; CHECK(!choose(o).feasible);
    o = fixture(); o.self.self = false; CHECK(!choose(o).feasible);
    o = fixture(); o.routes[0].to_first.safety_checked = false; o.count = 1; CHECK(!choose(o).feasible);
    o = fixture(); o.count = 1; o.routes[0].first.live_observed = false;
    CHECK(choose(o).job == Job::inspect);
    o = fixture(); o.fuel_remaining = 44999; CHECK(!choose(o).feasible);
    o.safe_channel = true; CHECK(choose(o).job == Job::preserve);

    o = fixture(); o.self.bag.value = 10; o.team_score = 990;
    const auto win = choose(o);
    CHECK(win.feasible && win.job == Job::bank_now && win.rank.immediate_win);
    CHECK(win.schedule.terminal_win && win.rank.Y[0] == 10);
    o.fuel_remaining = 44999;
    CHECK(choose(o).job == Job::bank_now && choose(o).rank.immediate_win);
    o.fuel_remaining = 15999; CHECK(!choose(o).feasible);
    o = fixture(); o.purchase_count = 0;
    o.routes[1].first = group(2, 20, 10, 20, 0, o.stats);
    o.routes[1].successor = group(12, 20, 0, 20, 0, o.stats);
    o.routes[0].to_first.distance = 1;
    CHECK(choose(o).first_group == 2 && choose(o, Arm::route_first_control).first_group == 2);
    const PrivateState committed{true, 1, 11, Timing::none, -1};
    CHECK(choose_job(o, committed, Profile{Arm::active}).first_group == 1);
    CHECK(choose_job(o, committed, Profile{Arm::route_first_control}).first_group == 1);
    CHECK(Profile::draft_request().required_anchor == "tavi");
    CHECK(Profile{}.valid_team_size(3) && Profile{}.valid_team_size(5) && !Profile{}.valid_team_size(6));
}
static void chronological_safety() {
    auto o = fixture(); o.count = 1; o.purchase_count = 0;
    auto& r = o.routes[0];
    r.first = group(1, 20, 10, 40, 10, o.stats); r.successor = group(11, 20, 0, 60, 10, o.stats);
    CHECK(forecast_combat(r.first.combat).state == EstimateState::feasible);
    CHECK(forecast_combat(r.successor.combat).state == EstimateState::feasible);
    CHECK(!project(o, r).feasible); // 40 + 60 damage is fatal to this same actor.

    o = fixture(); o.count = 1; o.purchase_count = 0;
    o.stats.shield = 60; o.stats.shield_expiry_tick = 100;
    o.routes[0].first = group(1, 20, 10, 80, 10, o.stats);
    o.routes[0].successor = group(11, 20, 0, 70, 10, o.stats);
    CHECK(!project(o, o.routes[0]).feasible); // Shield spent in the first fight is not replenished.
    o.routes[0].successor = group(11, 20, 0, 50, 10, o.stats);
    const auto shield = project(o, o.routes[0]);
    CHECK(shield.feasible && shield.forecast.final_health == 30 && shield.forecast.final_shield == 0);
    o.stats.shield_expiry_tick = 3;
    CHECK(!project(o, o.routes[0]).feasible); // Travel ages shielding before first combat.

    o = fixture(); o.count = 1; o.purchase_count = 0;
    o.stats.strength = 2; o.stats.direct_or_reset_damage = 152;
    o.stats.spell_known_ready = o.stats.legal_first_cast = true;
    o.routes[0].first = group(1, 20, 10, 20, 0, o.stats);
    o.routes[0].successor = group(11, 20, 0, 40, 3, o.stats);
    CHECK(forecast_combat(o.routes[0].successor.combat).state == EstimateState::feasible);
    CHECK(!project(o, o.routes[0]).feasible); // The unavailable successor cast receives no credit.
    o.routes[0].successor.combat.monsters[0].effective_strength = 0;
    const auto spell = project(o, o.routes[0]);
    CHECK(spell.feasible && spell.forecast.first_clear == 1 && spell.forecast.second_clear == 40);
    CHECK(!spell.forecast.successor_spell_credited);

    o = fixture(); o.count = 1; o.self.gold.value = 30;
    o.stats.health = 30;
    o.purchases[0] = {1, 201, 1, 30, 0, 100, 0, true, true};
    CHECK(project(o, o.routes[0]).feasible);
    CHECK(!project(o, o.routes[0], Timing::now, &o.purchases[0]).feasible); // Max=200, actual health stays30.
    o.stats.health = 50;
    const auto health = project(o, o.routes[0], Timing::now, &o.purchases[0]);
    CHECK(health.feasible && health.forecast.final_health == 50);
    o = fixture(); o.count = 1; o.purchase_count = 0; o.stats.visible_enemy_next_phase = 4;
    CHECK(!project(o, o.routes[0]).feasible); // Travel, holding, channel and renewal all cost exposure.
}
static void receipt_transition_only() {
    const auto d=choose(fixture());auto f=facts(d);auto o=execution_observation();auto p=start_project(d,f);
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::attack);
    own_payout(o,f,7,2,10,30,11);
    auto disabled=o;disabled.fuel=0;disabled.at_target=false;disabled.can_act=disabled.can_cast=disabled.can_move=disabled.can_attack=false;
    CHECK(execute_job(disabled,f,p).action.kind==e::ActionKind::yield);
    CHECK(p.stage==ProjectStage::deferred_purchase && !p.purchase_intent && !p.bank_intent && p.capture_baseline==11);
    o.own_payout_observed=false;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::buy && p.purchase_intent);
    confirm_purchase(o,f,7);
    disabled=o;disabled.fuel=0;disabled.at_target=false;disabled.can_act=disabled.can_cast=disabled.can_move=disabled.can_attack=false;
    for(auto& g:disabled.groups)g.solo_safe=false; // Actual shop observation has no production certificate.
    CHECK(execute_job(disabled,f,p).action.kind==e::ActionKind::yield);
    CHECK(p.stage==ProjectStage::second_production && !p.purchase_intent && !p.bank_intent);
    o.group_id=12;o.attack_target=112;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::attack); // Fresh production observation after actual buy.
    auto bad=start_project(d,facts(d));auto bad_o=execution_observation();auto bad_f=facts(d);
    (void)execute_job(bad_o,bad_f,bad);own_payout(bad_o,bad_f,7,99,10,30,11);
    bad_o.fuel=0;bad_o.at_target=false;bad_o.can_act=bad_o.can_cast=bad_o.can_move=bad_o.can_attack=false;
    CHECK(execute_job(bad_o,bad_f,bad).action.kind==e::ActionKind::yield && bad.stage==ProjectStage::aborted);
}
static void receipt_execution() {
    const auto selected = choose(fixture());
    auto f = facts(selected); auto o = execution_observation(); auto p = start_project(selected, f);
    CHECK(p.stage == ProjectStage::first_production);
    CHECK(execute_job(o, f, p).action.kind == e::ActionKind::attack);
    CHECK(p.execution_state.target == 2 && p.stage == ProjectStage::first_production);
    own_payout(o, f, 7, 2, 10, 30, 11); o.on_group_spawn = true;
    const auto buy = execute_job(o, f, p);
    CHECK(buy.action.kind == e::ActionKind::buy && buy.action.target == 200);
    CHECK(p.stage == ProjectStage::deferred_purchase && p.purchase_intent);
    confirm_purchase(o, f, 7); o.at_target = false;
    CHECK(execute_job(o, f, p).action.kind == e::ActionKind::move);
    CHECK(p.stage == ProjectStage::second_production); // A real buy may confirm on the same owner turn.
    o.owner_turn = f.observation_round = 8; o.group_id = 12; o.at_target = true; o.on_group_spawn = false;
    CHECK(execute_job(o, f, p).action.kind == e::ActionKind::attack);
    own_payout(o, f, 20, 12, 50, 0, 12); o.on_group_spawn = true;
    CHECK(execute_job(o, f, p).action.kind == e::ActionKind::vacate);
    CHECK(p.stage == ProjectStage::output_bank);
    o.owner_turn = f.observation_round = 21; o.own_payout_observed = false; o.on_group_spawn = false;
    o.adjacent_altar = true; o.altar_id = 50; o.tribute_ticks_quote = 6;
    const auto bank = execute_job(o, f, p);
    CHECK(bank.action.kind == e::ActionKind::start_tribute && bank.action.whole_bag && bank.action.starting_bag == 60);
    o.channel = e::Channel::tribute; o.channel_valid = o.channel_safe = true;
    o.owner_turn = f.observation_round = 22;
    CHECK(execute_job(o, f, p).action.kind == e::ActionKind::preserve_channel);
    o.channel = e::Channel::none; o.owner_turn = f.observation_round = 27; f.own_bag = 0; ++f.own_bank_receipt;
    CHECK(execute_job(o, f, p).action.kind == e::ActionKind::move);
    CHECK(p.stage == ProjectStage::renewal);
    o.owner_turn = f.observation_round = 28; f.at_named_productive_waypoint = true; f.productive_waypoint = 900;
    CHECK(execute_job(o, f, p).action.kind == e::ActionKind::yield && p.stage == ProjectStage::complete);

    // Missing, wrong, old or resource-inconsistent first payout cannot finance a buy.
    for (int corrupt = 0; corrupt < 5; ++corrupt) {
        f = facts(selected); o = execution_observation(); p = start_project(selected, f);
        (void)execute_job(o, f, p); own_payout(o, f, 7, 2, 10, 30, 11);
        if (corrupt == 0) f.own_capture_receipt = 10;
        if (corrupt == 1) o.own_payout_group = 99;
        if (corrupt == 2) o.payout_receipt = f.own_capture_receipt = 10;
        if (corrupt == 3) f.own_gold = 29;
        if (corrupt == 4) o.own_gold_received = 29;
        CHECK(execute_job(o, f, p).action.kind == e::ActionKind::yield && p.stage == ProjectStage::aborted);
    }
    f = facts(selected); o = execution_observation(); p = start_project(selected, f);
    (void)execute_job(o, f, p); o.group_cleared = true;
    CHECK(execute_job(o, f, p).phase == e::Phase::hold);
    o.owner_turn = f.observation_round = 8; o.on_group_spawn = true; o.group_respawning = true;
    CHECK(execute_job(o, f, p).action.kind != e::ActionKind::buy && p.stage == ProjectStage::first_production);

    // A receipt already present before the deferred buy is baselined. Neither
    // it nor the wrong item/cost/count may acknowledge a new purchase intent.
    for (int corrupt = 0; corrupt < 5; ++corrupt) {
        f = facts(selected); o = execution_observation(); p = start_project(selected, f);
        (void)execute_job(o, f, p); own_payout(o, f, 7, 2, 10, 30, 11);
        f.own_purchase_receipt = 90;
        CHECK(execute_job(o, f, p).action.kind == e::ActionKind::buy);
        confirm_purchase(o, f, 7);
        if (corrupt == 0) f.own_purchase_receipt = 90;
        if (corrupt == 1) f.purchased_family = 1;
        if (corrupt == 2) f.purchased_item = 201;
        if (corrupt == 3) f.purchased_cost = 29;
        if (corrupt == 4) f.purchased_shards = 2;
        CHECK(execute_job(o, f, p).action.kind == e::ActionKind::yield && p.stage == ProjectStage::aborted);
    }
    f = facts(selected); f.own_actor = 8;
    CHECK(start_project(selected, f).stage == ProjectStage::aborted);
    f = facts(selected); o = execution_observation(); p = start_project(selected, f);
    CHECK(execute_job(o, f, p).action.kind == e::ActionKind::attack);
    own_payout(o, f, 7, 2, 10, 30, 11); (void)execute_job(o, f, p); confirm_purchase(o, f, 7);
    (void)execute_job(o, f, p);
    o.owner_turn = f.observation_round = 8; o.group_id = 12; o.at_target = true;
    (void)execute_job(o, f, p);
    own_payout(o, f, 20, 2, 50, 0, 11);
    CHECK(execute_job(o, f, p).action.kind == e::ActionKind::yield && p.stage == ProjectStage::aborted);
}
int main() {
    planning(); chronological_safety(); receipt_transition_only(); receipt_execution();
    std::printf("Workshop offline semantic checks passed: %d (synthetic, no native gameplay)\n", checks);
}
