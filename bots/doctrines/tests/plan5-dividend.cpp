#include "../dividend/src/policy.h"
#include "../common/src/execute.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { std::fprintf(stderr, "Dividend check %d failed at line %d: %s\n", checks, __LINE__, #condition); std::abort(); } } while (false)
using namespace p5;
using namespace p5::dividend;
static RouteLeg route(int destination, int distance = 0) { return {destination, distance, true, true}; }
static Observation base() {
    Observation input;
    input.settings = {2, 10, 1, 200, 1000, 2}; // Explicit synthetic settings, not engine constants.
    ScoreHistory history; CHECK(history.observe(0, 0));
    input.windows = history.windows(0, 200, 1000, 0);
    input.self.id = 7; input.self.self = input.self.visible = input.self.own_team = true;
    input.self.bag = {20, Knowledge::current, 0}; input.self.gold = {0, Knowledge::current, 0};
    input.stats.id = 7; input.stats.self = input.stats.visible = true;
    input.stats.health = input.stats.max_health = 100; input.stats.strength = 10;
    input.current_to_altar = route(50); input.altar_to_spawn = route(60); input.bank_return = route(900);
    input.bank_legal = input.bank_route_safe = true;
    input.count = 2;
    for (std::size_t i = 0; i < input.count; ++i) {
        auto& group = input.opportunities[i];
        group.group = int(i) + 1; group.live_observed = group.reward_or_alive = true;
        group.gems = i ? 40 : 10; group.gold = 0;
        group.from_current = group.from_spawn = group.from_altar = route(group.group);
        if (i == 0) group.from_altar.distance = 2;
        group.to_altar = route(50); group.next_productive = route(900);
        group.combat.actor_count = group.combat.monster_count = 1;
        group.combat.actors[0] = input.stats;
        group.combat.monsters[0] = {int(i), i ? 180.0 : 40.0, i ? 180.0 : 40.0, i ? 5.0 : 0.0, true};
    }
    input.purchase_count = 1;
    input.purchases[0] = {0, 1, 40, 20, 0, 0, 0, true};
    return input;
}
static Decision choose(const Observation& input, Arm arm = Arm::active) {
    return choose_job(input, {}, Profile{arm});
}
static execution::Observation receipt_observation() {
    execution::Observation o;o.remaining_rounds=200;o.forecast_turns=30;o.fuel=100000;
    o.safe=o.can_act=o.can_move=o.can_attack=true;o.altar_id=50;o.tribute_ticks_quote=2;
    o.group_count=1;o.groups[0]={1,0,true,true,true,false,false,true};
    o.at_target=o.group_live_known=o.target_visible=o.target_category_legal=o.target_in_range=true;
    o.group_id=1;o.attack_target=90;return o;
}
static execution::Observation receipt_only(execution::Observation o) {
    // Match the caller's transition packet: factual point/channel flags remain
    // intact, while no planner fuel or Runtime movement candidates are present.
    o.fuel=0;o.move_count=0;return o;
}
static void purchase_proof(ProjectFacts& f, const Decision& selected) {
    f.purchased_count=1;
    for(int i=0;i<selected.schedule.event_count;++i)
        if(selected.schedule.events[i].kind==AccountingEventKind::purchase)
            f.purchased_cost=int(selected.schedule.events[i].gold);
    f.own_stats_known=true;f.own_stats.id=selected.schedule.actors[0].id;
    f.own_stats.self=f.own_stats.visible=true;
    f.own_stats.health=selected.purchase_min_health;f.own_stats.max_health=selected.purchase_max_health;
    f.own_stats.strength=selected.purchase_strength;f.own_stats.basic_modifier=selected.purchase_basic_modifier;
}
static void receipt_prepass_regressions() {
    namespace e=execution;
    const auto direct=choose(base(),Arm::route_first_control),financed=choose(base());
    CHECK(direct.job==Job::produce_bank && financed.job==Job::finance_produce);
    for(bool held:{false,true}) {
        ProjectFacts f;f.observation_round=0;f.own_bag=20;f.own_gold=0;
        f.own_capture_receipt=10;f.own_bank_receipt=20;f.own_purchase_receipt=30;
        auto p=start_project(direct,f);auto o=receipt_observation();
        CHECK(execute_job(o,f,p).action.kind==e::ActionKind::attack && p.execution_state.phase==e::Phase::clear);
        if(held){o.owner_turn=f.observation_round=1;o.group_cleared=o.on_group_spawn=true;
            CHECK(execute_job(o,f,p).action.kind==e::ActionKind::hold && p.execution_state.phase==e::Phase::hold);}
        o.owner_turn=f.observation_round=4;o.group_cleared=false;o.group_respawning=o.on_group_spawn=true;
        o.own_payout_observed=true;o.own_payout_group=1;o.own_gems_received=10;o.payout_receipt=11;
        f.own_capture_receipt=11;f.own_bag=30;
        auto disabled=receipt_only(o);
        CHECK(execute_job(disabled,f,p).action.kind==e::ActionKind::yield);
        CHECK(p.stage==ProjectStage::output_bank && p.payout_baseline==11 && p.bank_baseline==20);
        CHECK(!p.bank_intent_outstanding && !p.purchase_intent_outstanding);
        // The capture signal may expire. Durable output-bank stage survives
        // until a new correctly targeted observation supplies actual movement.
        o.own_payout_observed=false;o.move_count=1;o.observation_epoch=1;
        o.moves[0]={51,true,true,true,true,false};
        const auto vacate=execute_job(o,f,p);
        CHECK(vacate.action.kind==e::ActionKind::vacate && vacate.action.target==51 && !p.bank_intent_outstanding);
        o.owner_turn=f.observation_round=5;o.on_group_spawn=false;o.adjacent_altar=true;o.tribute_ticks_quote=3;
        o.feedback_sequence=vacate.action.sequence;o.feedback_kind=e::ActionKind::vacate;o.feedback_target=51;o.feedback_success=true;
        CHECK(execute_job(o,f,p).action.kind==e::ActionKind::start_tribute && p.bank_intent_outstanding);
        o.owner_turn=f.observation_round=8;f.own_bag=0;f.own_gold=60;f.own_bank_receipt=21;
        disabled=receipt_only(o);
        const auto completed_bank=execute_job(disabled,f,p);
        std::printf("DIVIDEND receipt-prepass held=%d completed-bank action=%d reason=%d stage=%d\n",held,int(completed_bank.action.kind),int(completed_bank.reason),int(p.stage));
        CHECK(completed_bank.action.kind==e::ActionKind::yield && completed_bank.reason==e::Reason::low_fuel);
        CHECK(p.stage==ProjectStage::renewal && p.bank_baseline==21 && !p.bank_intent_outstanding && !p.purchase_intent_outstanding);
        o.move_count=1;o.moves[0]={901,true,true,true,true,false};o.observation_epoch=2;o.feedback_sequence=0;
        CHECK(execute_job(o,f,p).action.kind==e::ActionKind::move);
        o.owner_turn=f.observation_round=9;f.at_named_productive_waypoint=true;f.productive_waypoint=900;
        CHECK(execute_job(receipt_only(o),f,p).action.kind==e::ActionKind::yield && p.stage==ProjectStage::complete);
    }
    // Completed initial bank and confirmed buy each advance without emitting
    // the next intent from old bank/shop group facts (all solo_safe=false).
    ProjectFacts f;f.observation_round=0;f.own_bag=20;f.own_gold=0;
    f.own_bank_receipt=20;f.own_purchase_receipt=30;auto p=start_project(financed,f);
    auto o=receipt_observation();o.adjacent_altar=true;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::start_tribute);
    const int bank_started=p.bank_intent_round;
    o.owner_turn=f.observation_round=1;o.channel=e::Channel::tribute;o.channel_valid=o.channel_safe=true;
    CHECK(execute_job(receipt_only(o),f,p).action.kind==e::ActionKind::preserve_channel);
    CHECK(p.stage==ProjectStage::initial_bank && p.bank_intent_round==bank_started && p.bank_intent_outstanding && !p.purchase_intent_outstanding);
    o.owner_turn=f.observation_round=2;o.channel=e::Channel::none;o.at_spawn=true;
    o.purchase_purchasable=o.purchase_affordable=true;f.own_bag=0;f.own_gold=40;f.own_bank_receipt=21;
    CHECK(execute_job(receipt_only(o),f,p).action.kind==e::ActionKind::yield);
    CHECK(p.stage==ProjectStage::purchase && p.bank_baseline==21 && p.purchase_baseline==30 && !p.bank_intent_outstanding && !p.purchase_intent_outstanding);
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::buy && p.purchase_intent_outstanding);
    f.own_purchase_confirmed=true;f.own_purchase_receipt=31;f.purchased_family=0;f.own_gold=0;
    purchase_proof(f,financed);
    o.groups[0].id=o.group_id=2;o.groups[0].solo_safe=false;o.attack_target=99;
    CHECK(execute_job(receipt_only(o),f,p).action.kind==e::ActionKind::refresh);
    CHECK(p.stage==ProjectStage::purchase_inspection && p.execution_state.phase==e::Phase::bootstrap && p.purchase_baseline==31 && !p.purchase_intent_outstanding && !p.bank_intent_outstanding);
    o.groups[0].solo_safe=true;o.at_spawn=false;
    f.purchase_source_reached=f.purchase_tail_checked=f.purchase_tail_safe=true;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::refresh && p.stage==ProjectStage::production);
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::attack && p.execution_state.phase==e::Phase::clear);

    // The pure API can represent an actual still-running tribute when capture
    // adds to its bag. Preserve that channel, then consume its later own bank
    // after the one-round capture signal expires; no scheduled timer is proof.
    f={};f.observation_round=0;f.own_bag=20;f.own_gold=0;f.own_capture_receipt=10;f.own_bank_receipt=20;
    p=start_project(direct,f);o=receipt_observation();(void)execute_job(o,f,p);
    o.owner_turn=f.observation_round=4;o.on_group_spawn=true;o.own_payout_observed=true;o.own_payout_group=1;
    o.own_gems_received=10;o.payout_receipt=f.own_capture_receipt=11;f.own_bag=30;
    o.channel=e::Channel::tribute;o.channel_valid=o.channel_safe=true;
    CHECK(execute_job(receipt_only(o),f,p).action.kind==e::ActionKind::preserve_channel);
    CHECK(p.stage==ProjectStage::output_bank && p.bank_intent_outstanding && p.bank_baseline==20);
    o.owner_turn=f.observation_round=5;o.own_payout_observed=false;o.channel=e::Channel::none;
    f.own_bag=0;f.own_gold=60;f.own_bank_receipt=21;
    CHECK(execute_job(receipt_only(o),f,p).action.kind==e::ActionKind::yield);
    CHECK(p.stage==ProjectStage::renewal && !p.bank_intent_outstanding && p.bank_baseline==21);
}
static void cash_cycle_regressions() {
    namespace e=execution;
    auto input=base();input.self.gold.value=32;input.self.bag.value=0;
    input.current_to_spawn=route(0,0);input.purchases[0].cost=30;input.purchases[0].strength_gain=10;
    input.opportunities[0].combat.monsters[0]={101,120,120,10,true};
    const auto active=choose(input),control=choose(input,Arm::route_first_control);
    CHECK(active.feasible && control.feasible && active.job==Job::cash_produce && control.job==Job::cash_produce);
    CHECK(active.group==2 && control.group==2 && active.purchase_family==0 && active.rank.Y[0]==40);
    CHECK(active.schedule.events[0].kind==AccountingEventKind::purchase && active.schedule.events[0].gold==30);
    int banks=0;for(int i=0;i<active.schedule.event_count;++i)banks+=active.schedule.events[i].kind==AccountingEventKind::tribute_start;
    CHECK(banks==1 && active.schedule.actors[0].bag==0);
    ProjectFacts f;f.observation_round=0;f.own_gold=32;f.own_bag=0;
    f.own_bank_receipt=5;f.own_purchase_receipt=10;f.own_capture_receipt=6;
    auto p=start_project(active,f);auto o=receipt_observation();o.at_spawn=true;
    o.purchase_purchasable=o.purchase_affordable=true;o.group_id=o.groups[0].id=2;
    CHECK(p.stage==ProjectStage::purchase_first && p.execution_state.phase==e::Phase::shop);
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::buy && !p.bank_intent_outstanding);
    CHECK(p.purchase_intent_gold==32 && p.purchase_intent_bag==0);
    auto confirmed=f;confirmed.own_gold=2;confirmed.own_purchase_receipt=11;
    confirmed.purchased_family=0;confirmed.own_purchase_confirmed=true;purchase_proof(confirmed,active);
    for(int bad=0;bad<11;++bad){auto q=p;auto wrong=confirmed;
        if(bad==0)wrong.purchased_family=1;if(bad==1)wrong.purchased_cost=29;
        if(bad==2)wrong.purchased_count=2;if(bad==3)wrong.own_gold=3;
        if(bad==4)wrong.own_bag=1;if(bad==5)wrong.own_stats_known=false;
        if(bad==6)wrong.own_stats.strength-=1;if(bad==7)wrong.own_stats.health-=1;
        if(bad==8)wrong.own_stats.max_health-=1;if(bad==9)wrong.own_stats.basic_modifier-=.01;
        if(bad==10)wrong.own_purchase_confirmed=false;
        CHECK(execute_job(receipt_only(o),wrong,q).action.kind==e::ActionKind::yield && q.stage==ProjectStage::aborted);
    }
    CHECK(execute_job(receipt_only(o),confirmed,p).action.kind==e::ActionKind::refresh && p.stage==ProjectStage::purchase_inspection);
    CHECK(!p.purchase_intent_outstanding && !p.bank_intent_outstanding && p.bank_baseline==5);
    f=confirmed;o.at_spawn=false;o.attack_target=91;
    f.purchase_source_reached=f.purchase_tail_checked=f.purchase_tail_safe=true;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::refresh && p.stage==ProjectStage::production);
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::attack);
    o.owner_turn=f.observation_round=4;o.own_payout_observed=true;o.own_payout_group=2;
    o.own_gems_received=40;o.payout_receipt=7;f.own_capture_receipt=7;f.own_bag=40;
    o.adjacent_altar=true;o.tribute_ticks_quote=4;
    CHECK(execute_job(o,f,p).action.kind==e::ActionKind::start_tribute && p.stage==ProjectStage::output_bank);
    o.owner_turn=f.observation_round=8;o.own_payout_observed=false;f.own_bag=0;f.own_gold=82;f.own_bank_receipt=6;
    CHECK(execute_job(receipt_only(o),f,p).action.kind==e::ActionKind::yield && p.stage==ProjectStage::renewal);
    o.owner_turn=f.observation_round=9;f.at_named_productive_waypoint=true;f.productive_waypoint=900;
    CHECK(execute_job(receipt_only(o),f,p).action.kind==e::ActionKind::yield && p.stage==ProjectStage::complete);
    std::printf("DIVIDEND cash=32 buy=30 no-initial-tribute actual-buy,capture,wholebank,renewal guarded\n");
}
int main() {
    receipt_prepass_regressions();
    cash_cycle_regressions();
    auto input = base();
    const auto active = choose(input), control = choose(input, Arm::route_first_control);
    CHECK(active.feasible && active.job == Job::finance_produce && active.group == 2);
    CHECK(control.feasible && control.job == Job::produce_bank && control.group == 1);
    CHECK(active.rank.Y[0] == 60 && control.rank.Y[0] == 30);
    CHECK(active.rank.final_gold[0] == 80 && control.rank.final_gold[0] == 60);
    CHECK(active.schedule.events[0].kind == AccountingEventKind::tribute_start);
    CHECK(active.schedule.events[2].kind == AccountingEventKind::purchase);
    CHECK(active.schedule.events[2].gold == 40);
    CHECK(active.schedule.events[3].group == 2);
    CHECK(active.rank.productive_arrival == 24 && control.rank.productive_arrival == 16);
    CHECK(std::strcmp(active.first_action, control.first_action) != 0);
    const auto no_capital = project(input, input.opportunities[1], true);
    CHECK(!no_capital.feasible && no_capital.forecast.state == EstimateState::unsafe);
    std::printf("DIVIDEND positive active_group=2 control_group=1 secured=60:30 return=24:16\n");

    input.altar_to_spawn.distance = 100;
    const auto detour = choose(input);
    CHECK(detour.feasible && detour.group == 1 && detour.purchase_family < 0);
    input = base(); input.windows.deadline = input.windows.window = {10, 10, 10}; input.windows.official_remaining = 10;
    const auto late = choose(input);
    CHECK(late.feasible && late.job == Job::bank_now && late.rank.Y[0] == 20);
    std::printf("DIVIDEND negative long_detour_group=1 short_window=bank20\n");

    input = base(); input.purchases[0].cost = 41;
    CHECK(choose(input).group == 1); // No future high-route payout funds its earlier purchase.
    input = base(); input.self.gold.value = 40;
    CHECK(choose(input).group == 1 && choose(input).job != Job::finance_produce);
    input = base(); input.self.bag.value = 0;
    CHECK(choose(input).group == 1 && choose(input).job == Job::produce_bank);
    input = base(); input.self.bag = {}; input.self.gold = {};
    CHECK(!choose(input).feasible);
    input = base(); input.pick_ordinal = 1; // Varg cannot spend Marcella's tribute income.
    CHECK(choose(input).group == 1 && choose(input).purchase_family < 0);
    input = base(); input.count = 9;
    CHECK(!choose(input).feasible);
    input = base(); input.opportunities[1].group = input.opportunities[0].group;
    CHECK(choose(input).group == 1);
    input = base(); input.opportunities[0].live_observed = false; input.opportunities[1].live_observed = false;
    input.self.bag.value = 0;
    CHECK(choose(input).job == Job::inspect && !choose(input).feasible);
    input = base(); input.safe_channel = true;
    CHECK(choose(input).job == Job::preserve);
    input = base(); input.fuel_remaining = 15999;
    CHECK(choose(input).job == Job::preserve && !choose(input).feasible);
    input = base(); input.fuel_remaining = 44999;
    CHECK(choose(input).job == Job::bank_now);
    input = base(); input.team_score = 980;
    CHECK(choose(input).job == Job::bank_now && choose(input).rank.immediate_win);
    input.bank_return = {};
    CHECK(choose(input).rank.immediate_win); // No invented postgame return is needed.
    input = base(); input.bank_legal = input.bank_route_safe = false;
    CHECK(!choose(input).feasible); // A production route cannot certify an illegal conversion.

    // Privacy: hidden/stale bags have no positive lower bound; others' gold erased.
    ActorObservation other{9, false, true, true, {40, Knowledge::current, 8}, {999, Knowledge::current, 8}};
    CHECK(private_view(other, 8).bag.value == 40);
    CHECK(private_view(other, 8).gold.knowledge == Knowledge::unknown);
    other.visible = false;
    CHECK(private_view(other, 8).bag.knowledge == Knowledge::unknown);
    other.visible = true;
    CHECK(private_view(other, 9).bag.knowledge == Knowledge::unknown);

    // Independent hand damage oracle: rate10; 20HP then30HP takes2+2+3 ticks.
    CombatInput combat;
    combat.actor_count = combat.monster_count = 2;
    combat.actors[0] = base().stats; combat.actors[0].strength = 5;
    combat.actors[1] = combat.actors[0]; combat.actors[1].id = 8; combat.actors[1].self = false;
    combat.monsters[0] = {1, 20, 20, 3, true}; combat.monsters[1] = {2, 30, 30, 4, true};
    auto forecast = forecast_combat(combat);
    CHECK(forecast.state == EstimateState::feasible && forecast.clear_ticks == 7);
    CHECK(forecast.worst_camp_damage == 34 && forecast.remaining_health[0] == 66 && forecast.remaining_health[1] == 66);
    combat.actors[1].health = 30;
    CHECK(forecast_combat(combat).state == EstimateState::unsafe); // No tank-absorbs-everything assumption.
    combat.actors[1].visible = false;
    CHECK(forecast_combat(combat).state == EstimateState::unknown);
    combat = {}; combat.actor_count = combat.monster_count = 1; combat.actors[0] = base().stats;
    combat.monsters[0] = {3, 40, 40, 0, true};
    combat.actors[0].direct_or_reset_damage = 80;
    CHECK(forecast_combat(combat).clear_ticks == 4); // An unavailable spell supplies no damage.
    combat.actors[0].spell_known_ready = combat.actors[0].legal_first_cast = true;
    CHECK(forecast_combat(combat).clear_ticks == 2);
    combat.actors[0].reserved_cast = true;
    CHECK(forecast_combat(combat).clear_ticks == 4);
    combat.actors[0].enemy_cast_bound_known = false;
    CHECK(forecast_combat(combat).state == EstimateState::unknown);
    CHECK(route(4, -1).turns() == -1 && route(4, 257).turns() == -1);
    CycleCandidate shield_trip;
    shield_trip.combat = combat;
    shield_trip.combat.actors[0].enemy_cast_bound_known = true;
    shield_trip.combat.actors[0].health = 30;
    shield_trip.combat.actors[0].shield = 100;
    shield_trip.combat.actors[0].shield_expiry_tick = 10;
    shield_trip.combat.monsters[0].effective_strength = 10;
    shield_trip.to_group = route(1, 20); shield_trip.to_altar = route(50); shield_trip.return_to_work = route(900);
    shield_trip.reward_gems = 10;
    CHECK(forecast_cycle(shield_trip, base().settings).state == EstimateState::unsafe);
    shield_trip.to_group.safety_checked = false;
    CHECK(forecast_cycle(shield_trip, base().settings).state == EstimateState::unknown);
    CHECK(Profile{}.valid_team_size(3) && Profile{}.valid_team_size(5) && !Profile{}.valid_team_size(2));
    CHECK(Profile::draft_request().required_anchor == "marcella");
    CHECK(Profile{Arm::active}.draft_request().roles[1].productivity ==
          Profile{Arm::route_first_control}.draft_request().roles[1].productivity);
    DraftContext draft;
    draft.team_size = 3; draft.pick_index = 0; draft.independent_cells = 3; draft.pool_count = 3;
    draft.pool[0] = {"marcella", true, 1, 10, 100, 0, 0, 0};
    draft.pool[1] = {"varg", true, 1, 10, 100, 0, 0, 0};
    draft.pool[2] = {"heart_of_ruin", true, 1, 10, 1000, 0, 0, 0};
    CHECK(choose_draft(draft, Profile::draft_request()).alias == "marcella");
    draft.own_count = 1; draft.pick_index = 1; draft.own_picks[0] = "marcella";
    CHECK(choose_draft(draft, Profile::draft_request()).alias == "heart_of_ruin");
    CHECK(choose_draft(draft, Profile{Arm::active}.draft_request()).alias ==
          choose_draft(draft, Profile{Arm::route_first_control}.draft_request()).alias);

    // Drive the chosen physical organization using fresh self receipts, not its
    // scheduled tick values. Control starts production; active starts tribute.
    namespace e = p5::execution;
    ProjectFacts facts; facts.observation_round = 0; facts.own_gold = 0; facts.own_bag = 20;
    auto project_state = start_project(active, facts);
    e::Observation observed;
    observed.remaining_rounds = 200; observed.forecast_turns = 30; observed.fuel = 100000;
    observed.safe = observed.can_act = observed.can_move = observed.can_attack = true;
    observed.adjacent_altar = true; observed.altar_id = 50; observed.tribute_ticks_quote = 2;
    CHECK(execute_job(observed, facts, project_state).action.kind == e::ActionKind::start_tribute);
    observed.owner_turn = facts.observation_round = 1;
    observed.channel = e::Channel::tribute; observed.channel_valid = observed.channel_safe = true;
    CHECK(execute_job(observed, facts, project_state).action.kind == e::ActionKind::preserve_channel);
    observed.owner_turn = facts.observation_round = 2;
    observed.channel = e::Channel::none; observed.at_spawn = true;
    facts.own_bank_receipt = 1; facts.own_gold = 40; facts.own_bag = 0;
    observed.purchase_purchasable = observed.purchase_affordable = true;
    CHECK(execute_job(observed, facts, project_state).action.kind == e::ActionKind::buy);
    observed.owner_turn = facts.observation_round = 3;
    facts.own_purchase_confirmed = true; facts.own_purchase_receipt = 1; facts.purchased_family = 0; facts.own_gold = 0;
    purchase_proof(facts,active);
    observed.at_spawn = false; observed.group_count = 1;
    observed.groups[0] = {2, 0, true, true, true, false, false, true};
    observed.at_target = observed.group_live_known = true; observed.group_id = 2;
    observed.target_visible = observed.target_category_legal = observed.target_in_range = true;
    observed.attack_target = 99;
    CHECK(execute_job(observed, facts, project_state).action.kind == e::ActionKind::refresh &&
          project_state.stage == ProjectStage::purchase_inspection);
    facts.purchase_source_reached = facts.purchase_tail_checked = facts.purchase_tail_safe = true;
    CHECK(execute_job(observed, facts, project_state).action.kind == e::ActionKind::refresh &&
          project_state.stage == ProjectStage::production);
    CHECK(execute_job(observed, facts, project_state).action.kind == e::ActionKind::attack);
    observed.owner_turn = facts.observation_round = 4;
    observed.group_cleared = observed.on_group_spawn = true; observed.capture_duration = 2;
    CHECK(execute_job(observed, facts, project_state).action.kind == e::ActionKind::hold);
    observed.owner_turn = facts.observation_round = 5; observed.group_respawning = true;
    CHECK(execute_job(observed, facts, project_state).action.kind == e::ActionKind::hold);
    observed.owner_turn = facts.observation_round = 6;
    observed.own_payout_observed = true; observed.own_payout_group = 2; observed.own_gems_received = 40; observed.payout_receipt = 1;
    facts.own_bag = 40; observed.move_count = 1; observed.moves[0] = {51, true, true, true, true, false};
    CHECK(execute_job(observed, facts, project_state).action.kind == e::ActionKind::vacate);
    observed.owner_turn = facts.observation_round = 7; observed.on_group_spawn = false;
    observed.tribute_ticks_quote = 4;
    CHECK(execute_job(observed, facts, project_state).action.kind == e::ActionKind::start_tribute);
    observed.owner_turn = facts.observation_round = 8;
    observed.channel = e::Channel::tribute;
    CHECK(execute_job(observed, facts, project_state).action.kind == e::ActionKind::preserve_channel);
    observed.owner_turn = facts.observation_round = 11;
    observed.channel = e::Channel::none; facts.own_bank_receipt = 2; facts.own_bag = 0;
    facts.at_named_productive_waypoint = true; facts.productive_waypoint = 900;
    CHECK(execute_job(observed, facts, project_state).action.kind == e::ActionKind::yield);
    CHECK(project_state.stage == ProjectStage::complete);
    std::printf("DIVIDEND stages=bank,purchase,production,hold,payout,bank,renewal evidence=own-observations\n");
    facts = {}; facts.observation_round = 0; facts.own_gold = 0; facts.own_bag = 20;
    project_state = start_project(active, facts);
    observed = {}; observed.remaining_rounds = 200; observed.fuel = 100000; observed.safe = true;
    observed.can_act = observed.adjacent_altar = true; observed.altar_id = 50; observed.tribute_ticks_quote = 2;
    CHECK(execute_job(observed, facts, project_state).action.kind == e::ActionKind::start_tribute);
    observed.owner_turn = facts.observation_round = 2;
    facts.own_bank_receipt = 1; facts.own_gold = 39; facts.own_bag = 0;
    CHECK(execute_job(observed, facts, project_state).action.kind == e::ActionKind::yield);
    CHECK(project_state.stage == ProjectStage::aborted);
    facts.own_bank_receipt = 0; facts.own_gold = 0; facts.own_bag = 20;
    observed.owner_turn = facts.observation_round = 0;
    project_state = start_project(active, facts); facts.own_bag = 0;
    CHECK(execute_job(observed, facts, project_state).action.kind == e::ActionKind::yield);
    CHECK(project_state.stage == ProjectStage::aborted); // Bag loss never manufactures tribute income.
    project_state = start_project(active, facts);
    project_state.stage = ProjectStage::production;
    project_state.execution_state.phase = e::Phase::hold;
    project_state.execution_state.target = 2;
    project_state.bank_baseline = 1;
    facts.own_bank_receipt = 2; facts.own_bag = 40;
    observed.safe = observed.can_act = observed.adjacent_altar = true;
    observed.altar_id = 50; observed.tribute_ticks_quote = 4;
    observed.own_payout_observed = true; observed.own_payout_group = 2;
    observed.own_gems_received = 40; observed.payout_receipt = 1;
    CHECK(execute_job(observed, facts, project_state).action.kind == e::ActionKind::start_tribute);
    CHECK(project_state.stage == ProjectStage::output_bank && project_state.bank_baseline == 2);
    // A bank from before capture cannot finish the new output's conversion.
    observed.owner_turn = facts.observation_round = 1; facts.own_bag = 0;
    CHECK(execute_job(observed, facts, project_state).action.kind == e::ActionKind::yield);
    CHECK(project_state.stage == ProjectStage::aborted);
    project_state = start_project(active, facts); project_state.stage = ProjectStage::renewal;
    facts.observation_round = observed.owner_turn = 0;
    observed.can_move = true; observed.move_count = 1; observed.observation_epoch = 1;
    observed.moves[0] = {51, true, true, true, true, false};
    const auto renewal_move = execute_job(observed, facts, project_state);
    CHECK(renewal_move.action.kind == e::ActionKind::move);
    observed.feedback_sequence = renewal_move.action.sequence;
    observed.feedback_kind = e::ActionKind::move; observed.feedback_target = 51; observed.feedback_success = false;
    CHECK(execute_job(observed, facts, project_state).action.kind == e::ActionKind::refresh);
    // A same-family buy from before the purchase stage cannot satisfy this plan.
    facts = {}; facts.observation_round = 0; facts.own_gold = 0; facts.own_bag = 20;
    project_state = start_project(active, facts);
    observed = {}; observed.remaining_rounds = 200; observed.fuel = 100000; observed.safe = true;
    observed.can_act = observed.adjacent_altar = true; observed.altar_id = 50; observed.tribute_ticks_quote = 2;
    CHECK(execute_job(observed, facts, project_state).action.kind == e::ActionKind::start_tribute);
    observed.owner_turn = facts.observation_round = 2; observed.at_spawn = true;
    observed.purchase_purchasable = observed.purchase_affordable = true;
    facts.own_bank_receipt = 1; facts.own_bag = 0; facts.own_gold = 40;
    facts.own_purchase_receipt = 1; facts.own_purchase_confirmed = true; facts.purchased_family = 0;
    purchase_proof(facts,active);
    CHECK(execute_job(observed, facts, project_state).action.kind == e::ActionKind::buy);
    CHECK(project_state.stage == ProjectStage::purchase);
    facts.own_purchase_receipt = 2; facts.own_gold = 0;
    execute_job(observed, facts, project_state); // Instant purchase can be reobserved in this owner turn.
    CHECK(project_state.stage == ProjectStage::purchase_inspection && !project_state.purchase_intent_outstanding);
    project_state = start_project(active, facts); project_state.stage = ProjectStage::renewal;
    facts.at_named_productive_waypoint = true; facts.productive_waypoint = 900;
    observed.safe = false;
    execute_job(observed, facts, project_state);
    CHECK(project_state.stage == ProjectStage::aborted);
    // Minimal independent reference: zero initial bag/gold, work, own payout,
    // whole-bag bank, return, and a fresh productive attack on a different group.
    input = base(); input.self.bag.value = 0;
    const auto reference = choose(input, Arm::route_first_control);
    CHECK(reference.feasible && reference.job == Job::produce_bank && reference.group == 1);
    facts = {}; facts.observation_round = 0; facts.own_gold = facts.own_bag = 0;
    project_state = start_project(reference, facts);
    observed = {}; observed.remaining_rounds = 200; observed.fuel = 100000; observed.safe = true;
    observed.can_act = observed.can_attack = observed.at_target = observed.group_live_known = true;
    observed.group_id = 1; observed.group_count = 1;
    observed.groups[0] = {1, 0, true, true, true, false, false, true};
    observed.target_visible = observed.target_category_legal = observed.target_in_range = true; observed.attack_target = 80;
    CHECK(execute_job(observed, facts, project_state).action.kind == e::ActionKind::attack);
    observed.owner_turn = facts.observation_round = 1;
    observed.group_cleared = observed.on_group_spawn = true; observed.capture_duration = 2;
    CHECK(execute_job(observed, facts, project_state).action.kind == e::ActionKind::hold);
    observed.owner_turn = facts.observation_round = 4;
    observed.own_payout_observed = true; observed.own_payout_group = 1; observed.own_gems_received = 10; observed.payout_receipt = 1;
    facts.own_bag = 10; observed.on_group_spawn = false; observed.adjacent_altar = true;
    observed.altar_id = 50; observed.tribute_ticks_quote = 1;
    CHECK(execute_job(observed, facts, project_state).action.kind == e::ActionKind::start_tribute);
    observed.owner_turn = facts.observation_round = 5;
    facts.own_bag = 0; facts.own_bank_receipt = 1; facts.own_gold = 20;
    facts.at_named_productive_waypoint = true; facts.productive_waypoint = 900;
    CHECK(execute_job(observed, facts, project_state).action.kind == e::ActionKind::yield);
    CHECK(project_state.stage == ProjectStage::complete);
    input = base(); input.self.bag.value = 0; input.self.gold.value = 20;
    input.opportunities[0].live_observed = false;
    input.opportunities[1].combat.monsters[0] = {2, 20, 20, 0, true};
    const auto renewed = choose(input, Arm::route_first_control);
    CHECK(renewed.feasible && renewed.group == 2);
    facts.own_capture_receipt = 1; facts.at_named_productive_waypoint = false;
    project_state = start_project(renewed, facts);
    observed.group_id = 2; observed.groups[0].id = 2;
    observed.group_cleared = observed.own_payout_observed = false;
    CHECK(execute_job(observed, facts, project_state).action.kind == e::ActionKind::attack);
    std::printf("DIVIDEND reference=start0,production,own-payout,bank,return,next-production\n");
    std::printf("DIVIDEND_RESULT checks=%d native=false\n", checks);
}
