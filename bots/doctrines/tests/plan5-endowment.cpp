#include "../endowment/src/policy.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace p5;
using namespace p5::endowment;
namespace e = p5::execution;
static int checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { std::fprintf(stderr, "Endowment check %d failed line %d: %s\n", checks, __LINE__, #c); std::abort(); } } while (false)
static RouteLeg leg(int id, int distance = 0) { return {id, distance, true, true}; }
static Group group(int id, int gems, double hp, double incoming) {
    Group g; g.id = id; g.gems = gems; g.live_observed = g.reward_or_alive = true;
    g.monster_count = 1; g.monsters[0] = {id + 100, hp, hp, incoming, true};
    g.from_current = g.from_altar = g.from_spawn = leg(id); g.to_altar = leg(50); g.return_to_work = leg(900);
    g.prism_casts[0] = {id + 100, true, true, true, true, true, true, false, false, false, false, false};
    return g;
}
static Observation fixture() {
    Observation o; o.settings = {0, 10, 1, 200, 1000, 2};
    ScoreHistory history; CHECK(history.observe(0, 0)); o.windows = history.windows(0, 200, 1000, 0);
    o.windows.window = {20, 20, 20};
    auto& a = o.actors[0]; a.resources = {7, true, true, true, {20, Knowledge::current, 0}, {0, Knowledge::current, 0}};
    a.stats.id = 7; a.stats.self = a.stats.visible = a.stats.ranged = true;
    a.stats.health = a.stats.max_health = 100; a.stats.strength = 20;
    o.focus.permanent = 20; o.focus.current = 40; o.focus.per_gem = 1;
    o.focus.modifiers = {1.5, 1}; o.focus.observation_round = 0;
    o.focus.valid_until_round = 200; o.focus.base_is_permanent = true;
    o.group_count = 3; o.groups[0] = group(1, 30, 100, 12);
    o.groups[1] = group(2, 10, 20, 0); o.groups[2] = group(3, 20, 20, 0);
    // Third source is the designated zero-resource bootstrap, reached only when
    // that separate state is supplied. Its current leg is not a growth option.
    o.groups[2].from_current.distance = -1; o.groups[2].from_altar.distance = -1;
    o.bootstrap_group = 3; o.rebuild_group = 2;
    o.current_to_altar = leg(50); o.altar_to_spawn = leg(60); o.final_return = leg(900);
    o.bank_legal = o.safe_bank_reachable = o.final_return_productive = true;
    o.execution.owner_turn = 0; o.execution.remaining_rounds = 200; o.execution.forecast_turns = 26;
    o.execution.fuel = 100000; o.execution.safe = o.execution.can_act = o.execution.can_attack = true;
    o.execution.can_move = o.execution.can_cast = true;
    o.execution.ranged = true;
    o.execution.spells[0] = o.groups[0].prism_casts[0];
    return o;
}
static Decision choose(const Observation& o, Arm arm = Arm::active) { return choose_job(o, Profile{arm}); }
static void economics() {
    auto o = fixture(); const auto a = choose(o), c = choose(o, Arm::bank_first_control);
    CHECK(a.feasible && c.feasible && a.job == Job::retain_rebuild && c.job == Job::bank_rebuild);
    CHECK(a.retention_crosses_threshold && a.rank.Y == (std::array<AccountingStock, 3>{50, 50, 50}));
    CHECK(c.rank.Y == (std::array<AccountingStock, 3>{30, 30, 30}));
    CHECK(a.rank.final_bag[0] == 0 && c.rank.final_bag[0] == 0);
    CHECK(a.actors[0].final_health == 40 && a.actors[0].count == 5);
    CHECK(a.actors[0].steps[0].expected_focus == 40 && a.actors[0].steps[0].cast_slot == 0 &&
          a.actors[0].steps[0].cast_target == 101);
    CHECK(a.actors[0].steps[2].expected_focus == 20 && a.actors[0].steps[2].cast_slot == -1);
    CHECK(c.actors[0].steps[1].expected_focus == 20 && c.actors[0].steps[1].cast_slot == 0);
    CHECK(a.rank.occupied_turns == 25 && c.rank.occupied_turns == 13);
    o.windows.window = {10, 10, 10};
    CHECK(choose(o).job == Job::bank_rebuild && choose(o).rank.Y[0] == 20);
    o = fixture(); o.groups[0].monsters[0].effective_strength = 0;
    CHECK(!project(o, o.groups[0], true, helper_alternatives(o)).feasible);
    CHECK(choose(o).job == Job::bank_rebuild); // No actual admission threshold, no retention.
    o = fixture(); o.execution.spells[0].ready = false;
    CHECK(choose(o).job == Job::bank_rebuild);
    o = fixture(); o.execution.successful_cast_this_turn = true; CHECK(choose(o).job == Job::bank_rebuild);
    o = fixture(); o.focus.current = 41; CHECK(!choose(o).feasible);
    o = fixture(); o.focus.base_is_permanent = false; CHECK(!choose(o).feasible);
    o = fixture(); o.focus.valid_until_round = 12; CHECK(!choose(o).feasible);
    o = fixture(); o.actors[0].stats.health = 75; CHECK(choose(o).job == Job::bank_rebuild);
    o = fixture(); o.actors[0].stats.shield = 20; o.actors[0].stats.shield_expiry_tick = 3;
    const auto expired_shield = choose(o);
    std::fprintf(stderr, "Endowment shield chronology: health=%.1f shield=%.1f (hand52/0: first12 shield, then48HP)\n",
        expired_shield.actors[0].final_health, expired_shield.actors[0].final_shield);
    CHECK(expired_shield.actors[0].final_health == 52 && expired_shield.actors[0].final_shield == 0);
    o = fixture(); o.safe_bank_reachable = false;
    CHECK(choose(o, Arm::bank_first_control).job == Job::retain_rebuild);
    o = fixture(); o.group_count = 9; CHECK(!choose(o).feasible);
    o = fixture(); o.actor_count = 4; CHECK(!choose(o).feasible);
    o = fixture(); o.actors[0].resources.gold.knowledge = Knowledge::stale; CHECK(!choose(o).feasible);
    o = fixture(); o.execution.channel = e::Channel::tribute; o.execution.channel_valid = o.execution.channel_safe = true;
    o.execution.fuel = 1; CHECK(choose(o).job == Job::preserve);
    o = fixture(); o.team_score = 980; o.execution.adjacent_altar = o.execution.legal_winning_bank = true;
    o.execution.altar_id = 50; o.execution.fuel = 44999;
    CHECK(choose(o).job == Job::winning_bank && choose(o).rank.Y[0] == 20);
    o.focus = {}; o.group_count = 0; CHECK(choose(o).job == Job::winning_bank);
    CHECK(Profile::draft_request().required_anchor == "prism" && draft_actor_count("cassi_and_issac") == 2);
}
static ProjectFacts facts(const Decision& d) {
    ProjectFacts f; const auto& p = d.actors[d.self_index];
    f.observation_round = d.observation_round; f.own_actor = p.actor; f.own_bag = p.initial_bag; f.own_gold = p.initial_gold;
    f.own_permanent_focus = 20; f.own_focus = 20 + f.own_bag;
    f.own_stats = fixture().actors[0].stats; f.own_stats.id = f.own_actor;
    f.capture_receipt = 10; f.purchase_receipt = 20; f.bank_receipt = 30; return f;
}
static e::Observation executing() {
    auto o = fixture().execution; o.group_count = 2;
    o.groups[0] = {1, 0, true, true, true, false, false, true};
    o.groups[1] = {2, 0, true, true, true, false, false, true};
    o.at_target = o.group_live_known = true; o.group_id = 1;
    o.target_visible = o.target_category_legal = o.target_in_range = true; o.attack_target = 101;
    o.move_count = 1; o.moves[0] = {40, true, true, true, true, false}; o.observation_epoch = 1;
    return o;
}
static void runtime() {
    const auto d = choose(fixture()); auto f = facts(d); auto o = executing(); auto p = start_project(d, f);
    const auto cast = execute_job(o, f, p); CHECK(cast.action.kind == e::ActionKind::cast && cast.action.slot == 0);
    o.feedback_sequence = cast.action.sequence; o.feedback_kind = e::ActionKind::cast; o.feedback_target = 101; o.feedback_success = true;
    CHECK(execute_job(o, f, p).action.kind == e::ActionKind::attack && p.cast_done);
    o.owner_turn = f.observation_round = 9; o.own_payout_observed = true; o.own_payout_group = 1;
    o.own_gems_received = 30; o.payout_receipt = f.capture_receipt = 11; f.own_bag = 50; f.own_focus = 70;
    o.adjacent_altar = true; o.altar_id = 50;
    const auto bank1 = execute_job(o, f, p); CHECK(bank1.action.kind == e::ActionKind::start_tribute && bank1.action.starting_bag == 50);
    CHECK(bank1.action.whole_bag && bank1.action.channel_ticks == 5);
    o.own_payout_observed = false; o.owner_turn = f.observation_round = 14;
    f.own_bag = 0; f.own_focus = 20; ++f.bank_receipt; o.at_target = false;
    CHECK(execute_job(o, f, p).action.kind == e::ActionKind::move && p.step_index == 2);
    o.owner_turn = f.observation_round = 16; o.group_id = 2; o.at_target = true; o.attack_target = 102;
    CHECK(execute_job(o, f, p).action.kind == e::ActionKind::attack); // No recycled growth spell.
    o.owner_turn = f.observation_round = 21; o.own_payout_observed = true; o.own_payout_group = 2;
    o.own_gems_received = 10; o.payout_receipt = f.capture_receipt = 12; f.own_bag = 10; f.own_focus = 30;
    const auto bank2 = execute_job(o, f, p); CHECK(bank2.action.kind == e::ActionKind::start_tribute && bank2.action.starting_bag == 10);
    o.own_payout_observed = false; o.owner_turn = f.observation_round = 22; f.own_bag = 0; f.own_focus = 20; ++f.bank_receipt;
    const auto renewal = execute_job(o, f, p); CHECK(renewal.action.kind == e::ActionKind::move);
    o.feedback_sequence = renewal.action.sequence; o.feedback_kind = e::ActionKind::move; o.feedback_target = 40; o.feedback_success = false;
    CHECK(execute_job(o, f, p).action.kind == e::ActionKind::refresh);
    o.owner_turn = f.observation_round = 23; f.at_named_productive_waypoint = true; f.productive_waypoint = 900;
    CHECK(execute_job(o, f, p).action.kind == e::ActionKind::yield && p.stage == Stage::complete);
    for (int bad = 0; bad < 5; ++bad) {
        f = facts(d); o = executing(); p = start_project(d, f);
        if (bad == 0) o.spells[0].ready = false;
        if (bad == 1) f.own_bag = 0;
        if (bad == 2) f.own_focus = 20;
        if (bad == 3) f.own_permanent_focus = 40;
        if (bad == 4) f.own_focus = std::numeric_limits<double>::quiet_NaN();
        CHECK(execute_job(o, f, p).action.kind == e::ActionKind::refresh && p.stage != Stage::running);
    }
    f = facts(d); o = executing(); p = start_project(d, f);
    o.spells[0].target = 999; // Otherwise legal, but outside the forecast growth group.
    CHECK(execute_job(o, f, p).action.kind == e::ActionKind::refresh && p.stage == Stage::reobserve);
    f = facts(d); o = executing(); p = start_project(d, f);
    CHECK(execute_job(o, f, p).action.kind == e::ActionKind::cast);
    CHECK(execute_job(o, f, p).action.kind == e::ActionKind::refresh && p.stage == Stage::running);
    o.owner_turn = f.observation_round = p.executor.phase_budget + 1;
    CHECK(execute_job(o, f, p).action.kind == e::ActionKind::refresh && p.stage == Stage::reobserve);
    for (int bad = 0; bad < 3; ++bad) {
        f = facts(d); o = executing(); p = start_project(d, f); (void)execute_job(o, f, p);
        o.owner_turn = f.observation_round = 9; o.own_payout_observed = true; o.own_payout_group = 1;
        o.own_gems_received = 30; o.payout_receipt = f.capture_receipt = 11; f.own_bag = 50; f.own_focus = 70;
        if (bad == 0) f.capture_receipt = 10;
        if (bad == 1) o.own_payout_group = 2;
        if (bad == 2) o.payout_receipt = f.capture_receipt = 10;
        CHECK(execute_job(o, f, p).action.kind == e::ActionKind::refresh && p.stage == Stage::aborted);
    }
}
static void bootstrap_and_helpers() {
    auto o = fixture(); o.actors[0].resources.bag.value = 0; o.focus.current = 20;
    const auto zero = choose(o); CHECK(zero.feasible && zero.job == Job::bootstrap);
    CHECK(zero.actors[0].initial_bag == 0 && zero.rank.final_bag[0] == 0); // Safe bank+renewal fallback exists.
    auto f = facts(zero); auto runtime_o = executing(); runtime_o.group_id = zero.group;
    runtime_o.spells[0].target = zero.group + 100; auto p = start_project(zero, f);
    CHECK(execute_job(runtime_o, f, p).action.kind == e::ActionKind::cast);
    runtime_o.owner_turn = f.observation_round = 5; runtime_o.own_payout_observed = true;
    runtime_o.own_payout_group = zero.group; runtime_o.own_gems_received = 10;
    runtime_o.payout_receipt = f.capture_receipt = 11; f.own_bag = 10; f.own_focus = 30;
    CHECK(execute_job(runtime_o, f, p).action.kind == e::ActionKind::refresh && p.stage == Stage::reobserve);
    CHECK(p.step_index == 0); // The forecasted bank has not become observed score.

    o = fixture(); o.actor_count = 2; o.actors[1] = o.actors[0];
    auto& helper = o.actors[1]; helper.resources.id = helper.stats.id = 8;
    helper.resources.self = helper.stats.self = false; helper.resources.bag.value = 0;
    helper.resources.gold.value = 999; helper.stats.ranged = false;
    helper.independent = group(100, 10, 20, 0);
    const auto pair = choose(o); CHECK(pair.feasible && pair.schedule.actor_count == 2);
    CHECK(pair.schedule.actors[1].gold == 0 && pair.rank.Y[0] == 60);
    helper.independent.id = 1; helper.independent.from_current.destination = 1;
    CHECK(!project(o, o.groups[0], true, helper_alternatives(o)).feasible);
    CHECK(choose(o).feasible && choose(o).job == Job::bank_rebuild); // Distinct safe fallback remains legal.
}
static Observation common_bootstrap(bool helper_self) {
    auto o = fixture(); o.actor_count = 2; o.group_count = 1; o.bootstrap_group = 1;
    o.actors[0].resources.bag.value = 0; o.focus.current = 20;
    o.actors[1] = o.actors[0]; auto& helper = o.actors[1];
    helper.resources.id = helper.stats.id = 8; helper.stats.ranged = false;
    helper.resources.self = helper.stats.self = helper_self;
    o.actors[0].resources.self = o.actors[0].stats.self = !helper_self;
    helper.independent = group(100, 10, 20, 0);
    helper.to_bootstrap = leg(1); helper.after_bootstrap = leg(100);
    o.bootstrap_partner_visible_usable = true;
    return o;
}
static void cooperative_handoff() {
    auto input = common_bootstrap(false);
    const auto pair = bootstrap(input, helper_alternatives(input));
    CHECK(pair.feasible && pair.actors[0].steps[0].coalition && pair.actors[1].steps[0].withdraw_when_safe);
    CHECK(pair.rank.final_bag[0] == 0 && pair.rank.final_bag[1] == 0 && pair.rank.Y[0] == 40);
    input.bootstrap_partner_visible_usable = false;
    CHECK(!bootstrap(input, helper_alternatives(input)).feasible); // Solo Prism dies; no fictional helper.
    input = common_bootstrap(true);
    const auto d = bootstrap(input, helper_alternatives(input));
    CHECK(d.feasible && d.self_index == 1 && d.prism_index == 0 && d.actors[1].count == 4);
    auto f = facts(d); auto o = executing(); o.ranged = false;
    o.groups[1].id = 100; o.group_cleared = o.on_group_spawn = true;
    o.coalition_visible = o.coalition_safety_fresh = o.coalition_safe = true;
    o.holder_visible_on_spawn = o.remaining_coalition_safety_fresh = o.remaining_coalition_safe = true;
    f.holder_actor = 7; f.holder_group = 1; f.holder_observation_round = 0; f.holder_on_spawn = true;
    for (int bad = 0; bad < 5; ++bad) {
        auto bad_f = f; auto bad_o = o; auto p = start_project(d, bad_f);
        if (bad == 0) bad_f.holder_actor = 9;
        if (bad == 1) bad_f.holder_group = 100;
        if (bad == 2) bad_f.holder_observation_round = -1;
        if (bad == 3) bad_f.holder_on_spawn = false;
        if (bad == 4) bad_o.remaining_coalition_safe = false;
        CHECK(execute_job(bad_o, bad_f, p).action.kind == e::ActionKind::hold && p.step_index == 0);
    }
    auto p = start_project(d, f); const auto leave = execute_job(o, f, p);
    CHECK(leave.action.kind == e::ActionKind::withdraw && p.step_index == 0);
    o.owner_turn = f.observation_round = 1; o.feedback_sequence = leave.action.sequence;
    o.feedback_kind = e::ActionKind::withdraw; o.feedback_success = true; o.feedback_target = leave.action.target;
    o.group_cleared = o.on_group_spawn = o.at_target = false;
    CHECK(execute_job(o, f, p).action.kind == e::ActionKind::move && p.step_index == 1);
    o.owner_turn = f.observation_round = 2; o.at_target = true; o.group_id = 100; o.attack_target = 200;
    CHECK(execute_job(o, f, p).action.kind == e::ActionKind::attack);
    o.owner_turn = f.observation_round = 4; o.own_payout_observed = true; o.own_payout_group = 100;
    o.own_gems_received = 10; o.payout_receipt = f.capture_receipt = 11; f.own_bag = 10;
    o.adjacent_altar = true; o.altar_id = 50;
    CHECK(execute_job(o, f, p).action.kind == e::ActionKind::start_tribute);
    o.owner_turn = f.observation_round = 5; o.own_payout_observed = false; f.own_bag = 0; ++f.bank_receipt;
    CHECK(execute_job(o, f, p).action.kind == e::ActionKind::move && p.step_index == 3);
    o.owner_turn = f.observation_round = 6; f.at_named_productive_waypoint = true; f.productive_waypoint = 900;
    CHECK(execute_job(o, f, p).action.kind == e::ActionKind::yield && p.stage == Stage::complete);
}
static void funded_purchase() {
    auto input = fixture(); input.groups[0].gold = 10; input.queried_spawn_heal_per_tick = 72;
    const Purchase purchase{2, 77, 1, 10, 0, 0, 10, 0, true};
    const auto d = project(input, input.groups[0], true, helper_alternatives(input), &purchase);
    CHECK(d.feasible && d.actors[0].count == 6 && d.actors[0].steps[2].kind == StepKind::purchase);
    CHECK(d.actors[0].steps[3].expected_focus == 30 && d.actors[0].steps[3].cast_slot == -1);
    auto f = facts(d); auto o = executing(); auto p = start_project(d, f);
    const auto cast = execute_job(o, f, p); CHECK(cast.action.kind == e::ActionKind::cast);
    o.feedback_sequence = cast.action.sequence; o.feedback_kind = e::ActionKind::cast;
    o.feedback_target = cast.action.target; o.feedback_success = true;
    CHECK(execute_job(o, f, p).action.kind == e::ActionKind::attack);
    o.owner_turn = f.observation_round = 9; o.own_payout_observed = true; o.own_payout_group = 1;
    o.own_gems_received = 30; o.own_gold_received = 10; o.payout_receipt = f.capture_receipt = 11;
    f.own_bag = 50; f.own_gold = 10; f.own_focus = 70; o.adjacent_altar = true; o.altar_id = 50;
    CHECK(execute_job(o, f, p).action.kind == e::ActionKind::start_tribute);
    o.owner_turn = f.observation_round = 14; o.own_payout_observed = false; o.at_target = false;
    f.own_bag = 0; f.own_focus = 20; ++f.bank_receipt;
    CHECK(execute_job(o, f, p).action.kind == e::ActionKind::move && p.step_index == 2);
    o.owner_turn = f.observation_round = 16; o.at_spawn = o.spawn_recovery_observed = true;
    o.purchase_purchasable = true;
    CHECK(execute_job(o, f, p).action.kind == e::ActionKind::yield); // Arrival is not a rest turn.
    o.owner_turn = f.observation_round = 17;
    CHECK(execute_job(o, f, p).action.kind == e::ActionKind::buy);
    const auto pending = p; const auto before = f;
    f.purchase_confirmed = true; f.purchase_receipt = 21; f.purchased_item = 77;
    f.purchased_family = 2; f.purchased_cost = 10; f.purchased_shards = 1;
    f.own_gold = 0; f.own_focus = f.own_permanent_focus = 30;
    for (int bad = 0; bad < 12; ++bad) {
        auto bad_f = f; auto trial = pending;
        if (bad == 0) bad_f.purchase_receipt = before.purchase_receipt;
        if (bad == 1) bad_f.purchased_item = 78;
        if (bad == 2) bad_f.purchased_family = 0;
        if (bad == 3) bad_f.purchased_cost = 9;
        if (bad == 4) bad_f.purchased_shards = 2;
        if (bad == 5) bad_f.own_gold = 1;
        if (bad == 6) bad_f.own_permanent_focus = 20;
        if (bad == 7) bad_f.own_actor = 8;
        if (bad == 8) bad_f.own_stats.health = 50; // promised actual rest healing is missing.
        if (bad == 9) bad_f.own_stats.strength = 19;
        if (bad == 10) bad_f.own_stats.health = bad_f.own_stats.max_health = 99;
        if (bad == 11) bad_f.own_stats.basic_modifier = .9;
        CHECK(execute_job(o, bad_f, trial).action.kind == e::ActionKind::refresh && trial.stage != Stage::running);
    }
    CHECK(execute_job(o, f, p).action.kind == e::ActionKind::move && p.step_index == 3);
    o.owner_turn = f.observation_round = 19; o.at_spawn = false; o.at_target = true; o.group_id = 2; o.attack_target = 102;
    CHECK(execute_job(o, f, p).action.kind == e::ActionKind::attack);
    o.owner_turn = f.observation_round = 22; o.own_payout_observed = true; o.own_payout_group = 2;
    o.own_gems_received = 10; o.own_gold_received = 0; o.payout_receipt = f.capture_receipt = 12;
    f.own_bag = 10; f.own_focus = 40;
    CHECK(execute_job(o, f, p).action.kind == e::ActionKind::start_tribute);
    o.owner_turn = f.observation_round = 23; o.own_payout_observed = false;
    f.own_bag = 0; f.own_focus = 30; ++f.bank_receipt;
    CHECK(execute_job(o, f, p).action.kind == e::ActionKind::move && p.step_index == 5);
    f.at_named_productive_waypoint = true; f.productive_waypoint = 900;
    CHECK(execute_job(o, f, p).action.kind == e::ActionKind::yield && p.stage == Stage::complete);
}
static void receipt_transition_only() {
    auto input=fixture();input.actors[0].resources.gold.value=10;input.queried_spawn_heal_per_tick=72;
    const Purchase item{2,77,1,10,0,0,10,0,true};
    const auto d=project(input,input.groups[1],false,helper_alternatives(input),&item);
    CHECK(d.feasible && d.actors[0].steps[2].cast_slot==0 && d.actors[0].steps[2].cast_target==102);
    auto f=facts(d);auto o=executing();auto p=start_project(d,f);o.adjacent_altar=true;o.altar_id=50;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::start_tribute);
    o.owner_turn=f.observation_round=2;f.own_bag=0;f.own_focus=20;++f.bank_receipt;o.at_spawn=o.spawn_recovery_observed=true;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::yield && p.step_index==1);
    o.owner_turn=f.observation_round=3;o.purchase_purchasable=true;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::buy);
    f.purchase_confirmed=true;++f.purchase_receipt;f.purchased_item=77;f.purchased_family=2;f.purchased_cost=10;f.purchased_shards=1;
    f.own_gold=0;f.own_focus=f.own_permanent_focus=30;
    auto disabled=o;disabled.fuel=0;disabled.at_target=false;disabled.can_act=disabled.can_cast=disabled.can_move=disabled.can_attack=false;
    CHECK(execute_job(disabled,f,p).action.kind==e::ActionKind::yield);
    CHECK(p.stage==Stage::running && p.step_index==2 && !p.pending_cast && !p.intent);
    o.group_id=2;o.attack_target=102;o.spells[0].target=102;
    const auto cast=execute_job(o,f,p);
    CHECK(cast.action.kind==e::ActionKind::cast && cast.action.target==102 && p.pending_cast==cast.action.sequence && p.executor.phase==e::Phase::clear);
}
static void one_cast_bound() {
    auto o = fixture(); auto g = group(1, 30, 300, 4); auto a = o.actors[0].stats; int clear = 0;
    CHECK(!fight(o, g, a, 20, 20, 0, clear)); // one60 damage cannot replace repeated casts in a24-basic fight.
    g = group(1, 30, 10, 1); g.monster_count = 2; g.monsters[1] = {102, 10, 10, 1, true};
    a = o.actors[0].stats;
    CHECK(fight(o, g, a, 20, 20, 0, clear) && clear == 4 && a.health == 95); // cast1+transition2+basic1
    std::array<CombatActor, actor_cap> party{}; party[0] = o.actors[0].stats; party[1] = party[0]; party[1].id = 8; party[1].self = false;
    g = group(1, 30, 300, 4); // both actors can get one primary event, not one per body.
    CHECK(combat_once(o, g, party, 2, 0, 20, 20, 0, clear) && clear == 13 && party[0].health == 48 && party[1].health == 48);
    for (int bad = 0; bad < 6; ++bad) {
        g = group(1, 30, 10, 1); a = o.actors[0].stats;
        if (bad == 0) g.monsters[0].id = -1;
        if (bad == 1) g.monsters[0].public_health = 0;
        if (bad == 2) g.monsters[0].observed_health = -1;
        if (bad == 3) g.monsters[0].observed_health = 11;
        if (bad == 4) g.monsters[0].effective_strength = -1;
        if (bad == 5) { g.monster_count = 2; g.monsters[1] = g.monsters[0]; }
        CHECK(!fight(o, g, a, 20, 20, 0, clear));
    }
    g = group(1, 30, 10, 1); g.monster_count = 2; g.monsters[1] = {102, 10, 0, 1, true}; a = o.actors[0].stats;
    CHECK(fight(o, g, a, 20, 20, 0, clear) && clear == 1 && a.health == 99);
    g.monsters[0].observed_health = 0; a = o.actors[0].stats;
    CHECK(fight(o, g, a, 20, 20, 0, clear) && clear == 0 && a.health == 100);
}
int main() {
    economics(); runtime(); bootstrap_and_helpers(); cooperative_handoff(); funded_purchase(); receipt_transition_only(); one_cast_bound();
    std::printf("Endowment offline checks passed: %d (synthetic, no native gameplay)\n", checks);
}
