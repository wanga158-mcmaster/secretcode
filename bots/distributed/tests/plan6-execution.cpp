#include "../reference/src/policy.h"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
using namespace p6;
namespace {
int checks = 0;
void check(bool ok, const char* message) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
Frame frame(int round = 10) {
    Frame f; f.clock = {round, 1, 0};
    f.own.actor.id = 1; f.own.actor.alive = true;
    f.own.actor.team = Team::ally;
    f.own.actor.health = f.own.actor.max_health = 100;
    f.own.actor.strength = 10; f.own.gold = 15;
    Group g; g.id = 7; g.original_spawn_visible = true; g.lifecycle = Lifecycle::living;
    f.group = {g, f.clock, 7, Knowledge::exact}; return f;
}
CycleCertificate certificate(const Frame& f, int actor = 1) {
    CycleCertificate c; c.clock = f.clock; c.actor = actor; c.group = 7;
    c.clear = c.hold = c.bank = c.recovery = {100,100,20,0,0,0,true};
    c.route_known = c.bank_executable = c.renewal_reachable = c.solo = true; return c;
}
void next(ReferenceInput& in) {
    ++in.frame.clock.round; in.frame.group.clock = in.frame.clock;
    in.solo.clock = in.frame.clock;
    in.completion = {};
}
}
int main() {
    auto f = frame();
    check(survives({100,100,20,0,0,0,true}), "owner survives twenty damage without helper");
    check(!survives({35,100,20,0,0,0,true}), "remaining fifteen violates twenty health reserve");
    check(!survives({100,100,20,0,0,0,false}), "incomplete safety facts cannot certify solo work");
    auto helper_dependent = certificate(f);
    helper_dependent.clear = helper_dependent.hold = helper_dependent.bank =
        helper_dependent.recovery = {100,100,95,0,0,20,true};
    check(!helper_dependent.valid(f,1,7),
          "solo completion cannot spend a departing helper's future heal");
    helper_dependent.clear = helper_dependent.hold = helper_dependent.bank =
        helper_dependent.recovery = {100,100,95,0,20,0,true};
    check(helper_dependent.valid(f,1,7), "solo owner may retain its own timed shield");
    check(!personal_gold(f, 2) && personal_gold(f,1) == 15,
          "another actor has no queryable gold, even in personal vision");
    Actor helper; helper.id = 2; helper.alive = true; helper.bag = 40;
    helper.team = Team::ally;
    f.actor_count = 1; f.actors[0] = {helper, f.clock, 2, Knowledge::exact};
    check(current_bag(f,2) == 40, "current visible subject's bag is exact");
    f.actors[0].value.team = Team::enemy;
    check(participant_failure(f,2) == Failure::conflicting_evidence &&
          !certificate(f,2).valid(f,2,7), "enemy champion cannot impersonate a helper");
    f.actors[0].value.team = Team::ally;
    f.actors[1] = f.actors[0]; f.actors[1].value.health = 35; f.actor_count = 2;
    check(!f.valid() && !f.visible(2), "conflicting duplicate current actor records fail closed");
    f.actor_count = 1;
    ++f.clock.revision;
    check(!current_bag(f,2), "action invalidates a cached same-round bag");
    check(participant_failure(f,2) == Failure::not_observed,
          "lost contact is not evidence of death");
    helper.channel = Channel::recall;
    f.actors[0] = {helper, f.clock, 2, Knowledge::exact};
    check(participant_failure(f,2) == Failure::observed_unavailable,
          "observed recall supersedes earlier readiness");
    f = frame(); f.group.value.original_spawn_visible = false;
    check(!f.live_group(7), "no lifecycle query from an unseen original spawn");
    Actor cassi; cassi.pick_ordinal = 0; cassi.type = Champion::cassi;
    Actor issac = cassi; issac.type = Champion::issac;
    check(linked_role(cassi,0,Champion::cassi) && linked_role(issac,0,Champion::issac) &&
          !linked_role(issac,0,Champion::cassi), "linked pick resolved by actual subtype");
    f = frame(); auto c = certificate(f,2);
    check(!witnessed_release(f,2,7,c), "predicted unseen holder cannot license departure");
    helper.channel = Channel::none; helper.spawn_group = 7;
    helper.health = helper.max_health = 100;
    f.actors[0] = {helper, f.clock, 2, Knowledge::exact}; f.actor_count = 1;
    f.group.value.lifecycle = Lifecycle::reward_available;
    f.group.value.uncontested_now = true;
    check(witnessed_release(f,2,7,c), "fresh occupied holder and solo bound permit withdrawal");
    f.actors[0].value.health = 35;
    check(!witnessed_release(f,2,7,c), "restamping stale healthy certificate cannot hide damaged holder");
    f.actors[0].value.health = 100;
    c.hold.health = 35;
    check(!witnessed_release(f,2,7,c), "unsafe remaining owner requires support or split");
    const std::array monsters{MonsterWork{1,100,5}};
    std::array kit{Productivity{10,720,false,false,true,false}};
    auto work = combat_work(monsters,kit);
    check(work.resolved && work.rounds == 10 && work.incoming == 50,
          "unavailable spell gives no credit: ten turns and fifty incoming");
    kit[0].cast_ready = true;
    work = combat_work(monsters,kit);
    check(work.rounds == 1 && work.incoming == 5, "one scheduled ready cast changes bound");
    kit[0].cast_reserved_for_protection = true;
    check(combat_work(monsters,kit).rounds == 10, "protection cast not double credited");

    ActorState state;
    check(state.admit(7,63,1,100,0,0), "project admitted before epoch boundary");
    check(state.repair() && !state.repair(), "one repair per project");
    check(state.project == ProjectID{7,0} && !state.admit(7,64,1,99,0,0),
          "round boundary neither renames active project nor replenishes repair");
    for (int n = 0; n < 8; ++n) check(state.wait_once(), "bounded wait permitted");
    check(!state.wait_once(), "ninth rendezvous wait prohibited");
    state.retire(Failure::expired);
    check(!state.may_admit(7,63) && state.may_admit(7,64), "retirement expires at next epoch only");
    check(state.admit(8,63,1,100,0,0), "another group can be admitted in same epoch");
    state.retire(Failure::expired);
    check(!state.may_admit(7,63), "intervening project cannot erase group retirement");
    ActorState renewed;
    check(renewed.admit(7,10,1,100,0,0) && renewed.repair(), "first epoch repair consumed");
    renewed.active = false; // Successful terminal renewal of the same project.
    check(renewed.admit(7,20,1,90,0,0) && !renewed.repair(),
          "successful renewal cannot replenish same project epoch repair");

    ReferenceInput in; in.frame = frame(); in.solo = certificate(in.frame);
    in.assigned_group = 7; in.forecast_rounds = 3; in.remaining_rounds = 200;
    in.capture_window = 6; in.safe_now = in.at_approach = true;
    in.fresh_target_attack_legal = true; state = {};
    check(reference::choose(state,in).action == Action::attack,
          "ordinary zero-resource start can produce without future income");
    next(in); in.frame.group.value.lifecycle = Lifecycle::reward_available;
    in.frame.own.actor.spawn_group = 7;
    check(reference::choose(state,in).action == Action::hold, "clearance enters actual occupied hold");
    for (int n = 0; n < 6; ++n) {
        next(in);
        check(reference::choose(state,in).action == Action::hold,
              "elapsed occupation including hidden reset is not payout");
    }
    check(!state.payout_seen, "no capture completion inferred from private count");
    next(in); in.completion = {in.frame.clock,7,true,false,true};
    reference::choose(state,in);
    check(!state.payout_seen, "contradictory payout flag without resource change is rejected");
    next(in); in.frame.own.actor.bag = 7;
    in.completion = {in.frame.clock,7,true,false,true};
    check(reference::choose(state,in).phase == Phase::bank && state.payout_seen,
          "unambiguous own payout releases spawn and starts conversion");
    next(in); in.at_bank = in.altar_legal = true;
    check(reference::choose(state,in).action == Action::tribute, "bank actual owned bag");
    check(confirm_tribute_started(state,in.frame,
              {Action::tribute,in.frame.clock,1,Knowledge::exact}),
          "successful own tribute action recorded after proposal");
    next(in); in.frame.own.actor.channel = Channel::none; in.frame.own.actor.bag = 0;
    in.frame.own_team_score = 7;
    in.completion = {in.frame.clock,7,false,true,true}; in.at_renewal = false;
    check(reference::choose(state,in).action == Action::move && state.bank_seen,
          "completed own bank returns to named productive waypoint");
    next(in); in.at_renewal = true; in.frame.group.value.lifecycle = Lifecycle::living;
    in.frame.own.actor.spawn_group = -1;
    check(reference::choose(state,in).action == Action::attack && state.active,
          "one-tick seven-gem bank completes between observations then renews production");

    ActorState channel_state;
    check(channel_state.admit(7,63,1,100,20,0), "multi-tick channel project");
    channel_state.phase = Phase::bank; channel_state.phase_limit = 0;
    auto channel_in = in; channel_in.frame = frame(64); channel_in.frame.own.actor.bag = 20;
    channel_in.frame.own.actor.channel = Channel::tribute;
    channel_in.frame.own.actor.channel_valid = true;
    check(reference::choose(channel_state,channel_in).action == Action::preserve_channel &&
          channel_state.project == ProjectID{7,0},
          "expired appointment cannot revoke valid safe multi-tick tribute");

    state = {}; in = {}; in.frame = frame(63); in.safe_now = true;
    in.frame.own.actor.channel = Channel::recall; in.frame.own.actor.channel_valid = true;
    check(reference::choose(state,in).action == Action::preserve_channel,
          "safe recall survives appointment expiry too");
    next(in); in.frame.own.actor.channel = Channel::none;
    in.frame.own.at_spawn = true;
    check(reference::choose(state,in).action == Action::recover_on_spawn,
          "recall arrival stays through spawn recovery");
    state = {}; in = {}; in.frame = frame(); in.safe_now = true;
    in.assigned_group = 7; in.frame.group = {}; in.inspection_failed = true;
    check(reference::choose(state,in).action == Action::inspect, "first unknown source inspection");
    next(in);
    check(reference::choose(state,in).phase == Phase::recover && !state.may_admit(7,in.frame.clock.round),
          "two failed unknown observations retire source instead of endless inspection");

    TurnBudget budget{{10,1,0},100000};
    ActionGuard guard{budget.clock,true,true,true,true,true,true,true,true};
    check(budget.allows(Action::move,guard), "legal movement proposal");
    for (int i = 0; i < 3; ++i) {
        budget.result(Action::move,Outcome::hidden_collision);
        check(!budget.moved && !budget.allows(Action::move,guard),
              "failed hidden collision invalidates view and never moves actor");
        guard.clock = budget.clock;
    }
    check(!budget.allows(Action::move,guard), "fourth collision attempt forbidden");
    budget = {{10,1,0},100000}; guard.clock = budget.clock;
    guard.landing_legal = false;
    check(!budget.allows(Action::cast,guard), "range mask alone cannot authorize dash landing");
    guard.landing_legal = true;
    budget.result(Action::cast,Outcome::success); guard.clock = budget.clock;
    check(!budget.allows(Action::cast,guard), "one successful cast across both slots");
    budget = {{10,1,0},100000}; budget.result(Action::move,Outcome::success);
    guard.clock = budget.clock;
    check(!budget.allows(Action::attack,guard,true), "ranged walk consumes basic attack allowance");
    budget.fuel = 15000;
    check(!budget.select_allowed() && !budget.allows(Action::attack,guard),
          "low fuel cannot reenter selection or combat");
    int errors = 0, yields = 0;
    turn_boundary([] { throw std::runtime_error("synthetic engine failure"); },
                  [&](const char*) { ++errors; }, [&] { ++yields; });
    check(errors == 1 && yields == 1, "unexpected engine error logs once then yields");
    std::printf("{\"suite\":\"execution\",\"passed\":true,\"checks\":%d}\n",checks);
}
