#include "../reference/src/policy.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
using namespace p6;
namespace fc = p6::forecast;
namespace {
int checks = 0;
void check(bool ok, const char* name) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL forecast integration: %s\n", name); std::exit(1); }
}
template<class T> Fact<T> known(T value) { return {value, Source::queried_setting}; }
template<class T> Observation<T> seen(T value, ViewClock clock, int subject) {
    return {value, clock, subject, Knowledge::exact};
}
struct Fixture { ReferenceInput actual{}; fc::Request request{}; };
void leg(fc::Request& r, Point from, Point to, int distance) {
    r.legs[r.leg_count++] = {from, to, {distance, Source::public_map}};
}
void timing(Fixture& t) {
    auto& r = t.request; const auto clock = t.actual.frame.clock;
    r.timing.settings = known(Settings{100,1000,2});
    r.timing.tribute_multiplier = known(.1); r.timing.tribute_base = known(0);
    r.timing.tribute_minimum = known(1);
    r.ambient_incoming = seen(0.,clock,clock.self);
    r.bank_waypoint = 90; r.bank_position = {1,0};
    r.renewal_waypoint = 91; r.renewal_position = {0,0};
    r.bank_available = seen(true,clock,r.bank_waypoint);
}
void production(Fixture& t, int id = 17, Point position = {0,0}) {
    t.request = {}; timing(t);
    auto& r = t.request; auto& f = t.actual.frame;
    r.group_count = 1;
    auto& g = r.groups[0];
    g.observed = seen(Group{id,position,position,position,Lifecycle::living,true,true,true},f.clock,id);
    g.membership_complete = {true,Source::public_map}; g.member_count = 2;
    // The approach is an already cleared original member spawn; the group's
    // second member remains alive. This permits an occupied spawn before clear.
    g.members[0].public_full = {MonsterWork{100+id,1,0},Source::public_map};
    g.members[0].current = seen(MonsterWork{100+id,0,0},f.clock,100+id);
    g.members[1].public_full = {MonsterWork{200+id,2,1},Source::public_map};
    g.members[1].current = seen(MonsterWork{200+id,2,1},f.clock,200+id);
    g.gems = {10,Source::public_map}; g.gold = {7,Source::public_map};
    f.group = g.observed; f.own.actor.ready_attack_group = id;
    r.kit = seen(Productivity{1,0,false,false,false,false},f.clock,f.clock.self);
    const int distance = std::abs(position.x-f.own.actor.position.x);
    leg(r,f.own.actor.position,position,distance); leg(r,position,position,0);
    leg(r,position,r.bank_position,std::abs(position.x-r.bank_position.x));
    leg(r,r.bank_position,r.renewal_position,1);
}
void banking(Fixture& t, int project) {
    t.request = {}; timing(t);
    auto& r = t.request;
    r.shape = fc::Shape::bank_only; r.project_group = project;
    leg(r,t.actual.frame.own.actor.position,r.bank_position,
        std::abs(t.actual.frame.own.actor.position.x-r.bank_position.x));
    leg(r,r.bank_position,r.renewal_position,1);
}
void refresh(Fixture& t, int round) {
    auto& f = t.actual.frame; f.clock.round = round;
    f.group.clock = f.clock;
    t.actual.completion = {};
    t.request.kit.clock = f.clock;
    t.request.ambient_incoming.clock = f.clock;
    t.request.bank_available.clock = f.clock;
    for (auto& g : t.request.groups) {
        g.observed.clock = f.clock;
        for (auto& m : g.members) m.current.clock = f.clock;
    }
}
std::unique_ptr<Fixture> fixture() {
    auto t = std::make_unique<Fixture>(); auto& f = t->actual.frame;
    f.clock = {10,1,0};
    auto& a = f.own.actor;
    a.id = 1; a.team = Team::ally; a.type = Champion::varg;
    a.alive = a.can_move = a.can_attack = true;
    a.health = a.max_health = 100; a.strength = 1; a.attack_range = 1;
    a.spawn_group = 17; f.own.gold = 9;
    t->actual.safe_now = t->actual.at_approach = t->actual.fresh_target_attack_legal = true;
    production(*t); return t;
}
void evaluated_paths() {
    auto t = fixture();
    auto quote = std::make_unique<fc::EvaluatedQuote>();
    ActorState empty;
    check(!quote->ready() && !bind_cycle_forecast(empty,t->actual,*quote),
          "unevaluated quote cannot bind an executor input");
    check(!reference::choose_forecast(empty,t->actual,*quote) && !empty.active,
          "unevaluated quote cannot admit a project");
    quote->evaluate(t->actual.frame,t->request);
    const auto ordinary = bind_cycle_forecast(empty,t->actual,t->request);
    const auto cached = bind_cycle_forecast(empty,t->actual,*quote);
    check(quote->ready() && ordinary && cached &&
          cached->assigned_group == ordinary->assigned_group &&
          cached->forecast_rounds == ordinary->forecast_rounds &&
          cached->remaining_rounds == ordinary->remaining_rounds &&
          cached->capture_window == ordinary->capture_window &&
          cached->solo.recovery.incoming_total == ordinary->solo.recovery.incoming_total,
          "owned quote binds the same duration and safety as ordinary evaluation");
    ActorState direct_state, cached_state;
    const auto direct = reference::choose_forecast(direct_state,t->actual,t->request);
    const auto reused = reference::choose_forecast(cached_state,t->actual,*quote);
    check(direct && reused && direct->action == reused->action &&
          direct->stage == reused->stage && direct->phase == reused->phase &&
          direct->reason == reused->reason && direct->group == reused->group &&
          direct_state.project == cached_state.project && direct_state.phase == cached_state.phase &&
          direct_state.phase_limit == cached_state.phase_limit && direct_state.active && cached_state.active,
          "owned quote produces the same admitted action and incumbent bounds");

    auto changed = std::make_unique<Fixture>(*t);
    ++changed->actual.frame.own.gold;
    check(!bind_cycle_forecast(empty,changed->actual,*quote), "cached bind rejects changed gold");
    changed->actual = t->actual; ++changed->actual.frame.own.actor.bag;
    check(!bind_cycle_forecast(empty,changed->actual,*quote), "cached bind rejects changed bag");
    changed->actual = t->actual; ++changed->actual.frame.clock.revision;
    check(!bind_cycle_forecast(empty,changed->actual,*quote), "cached bind rejects changed view revision");
    changed->actual = t->actual; ++changed->actual.frame.clock.round;
    check(!bind_cycle_forecast(empty,changed->actual,*quote), "cached bind rejects a later round");
    changed->actual = t->actual; --changed->actual.frame.own.actor.health;
    check(!bind_cycle_forecast(empty,changed->actual,*quote), "cached bind rejects changed own health");
    changed->actual = t->actual; changed->actual.frame.group.value.retreat = {1,1};
    check(!bind_cycle_forecast(empty,changed->actual,*quote),
          "cached bind retains the separate current group geometry check");
    changed->actual = t->actual; changed->actual.frame.group.value.lifecycle = Lifecycle::respawning;
    check(!bind_cycle_forecast(empty,changed->actual,*quote),
          "cached bind retains the current group lifecycle check");
    t->request.groups[0].gems.value = 100000;
    const auto still_owned = bind_cycle_forecast(empty,t->actual,*quote);
    check(quote->request().groups[0].gems.value == 10 &&
          still_owned && still_owned->forecast_rounds == cached->forecast_rounds,
          "mutating the original request cannot change the owned executor quote");

    auto visible = fixture(); auto& f = visible->actual.frame;
    Actor ally = f.own.actor; ally.id = 2; ally.spawn_group = -1;
    f.actor_count = 1; f.actors[0] = seen(ally,f.clock,ally.id);
    auto visible_quote = std::make_unique<fc::EvaluatedQuote>();
    visible_quote->evaluate(f,visible->request);
    check(bind_cycle_forecast(empty,visible->actual,*visible_quote).has_value(),
          "owned quote accepts its complete current visible actor input");
    --f.actors[0].value.health;
    check(!bind_cycle_forecast(empty,visible->actual,*visible_quote),
          "cached bind rejects changed active actor fields");
    ++f.actors[0].value.health; f.actors[0].knowledge = Knowledge::stale;
    check(!bind_cycle_forecast(empty,visible->actual,*visible_quote),
          "cached bind rejects changed active actor provenance");
    f.actors[0].knowledge = Knowledge::exact; f.actors[5].value.health = 1;
    check(bind_cycle_forecast(empty,visible->actual,*visible_quote).has_value(),
          "inactive actor tail cannot change a count-bounded quote input");

    ActorState incumbent;
    check(incumbent.admit(9,10,5,90,0,9), "cached-path incumbent admitted separately");
    incumbent.phase = Phase::hold;
    const auto prior = incumbent.project;
    check(!reference::choose_forecast(incumbent,t->actual,*quote) &&
          incumbent.project == prior && incumbent.phase == Phase::hold,
          "cached quote cannot rename or abandon another active safe project");
    auto holding = fixture(); holding->actual.frame.own.actor.bag = 5; banking(*holding,17);
    auto bank_quote = std::make_unique<fc::EvaluatedQuote>();
    bank_quote->evaluate(holding->actual.frame,holding->request);
    ActorState capturing;
    check(capturing.admit(17,10,5,90,0,9), "cached-path capture project admitted");
    capturing.phase = Phase::hold;
    check(!bind_cycle_forecast(capturing,holding->actual,*bank_quote),
          "cached bank-only quote cannot replace an uncompleted capture stage");
    auto unavailable = std::make_unique<fc::EvaluatedQuote>();
    changed->actual = t->actual;
    changed->actual.frame.own.actor.channel = Channel::tribute;
    changed->actual.frame.own.actor.channel_valid = true;
    changed->actual.frame.own.actor.bag = 20;
    const auto channel = reference::choose_forecast(incumbent,changed->actual,*unavailable);
    check(channel && channel->action == Action::preserve_channel && incumbent.project == prior,
          "cached path preserves a safe real channel before quote availability");

    auto completed = fixture(); ActorState bank_state;
    check(bank_state.admit(17,10,5,90,10,9), "cached-path bank project admitted");
    bank_state.phase = Phase::bank; bank_state.last_round = 10;
    completed->actual.frame.own.actor.bag = 10;
    check(confirm_tribute_started(bank_state,completed->actual.frame,
          seen(Action::tribute,completed->actual.frame.clock,1)),
          "cached-path completion still requires the separate successful action receipt");
    refresh(*completed,11); completed->actual.frame.own.actor.bag = 0;
    completed->actual.frame.own_team_score = 10;
    completed->actual.completion = {completed->actual.frame.clock,17,false,true,true};
    const auto converted = reference::choose_forecast(bank_state,completed->actual,*unavailable);
    check(converted && converted->action == Action::move && bank_state.bank_seen && bank_state.active,
          "cached path progresses actual bank completion with no new quote");
    ActorState unreceipted;
    check(unreceipted.admit(17,10,5,90,10,9), "unreceipted bank project admitted");
    unreceipted.phase = Phase::bank;
    check(!reference::choose_forecast(unreceipted,completed->actual,*unavailable) && !unreceipted.bank_seen,
          "cached path cannot turn an unreceipted completion claim into conversion");
}
bool same_exposure(const Exposure& a,const Exposure& b) {
    return a.health==b.health && a.max_health==b.max_health && a.incoming_total==b.incoming_total &&
        a.next_phase_incoming==b.next_phase_incoming && a.own_timed_shield==b.own_timed_shield &&
        a.assigned_timed_heal==b.assigned_timed_heal && a.observations_complete==b.observations_complete;
}
bool same_reference(const ReferenceInput& a,const ReferenceInput& b) {
    const auto& f=a.frame;const auto& g=b.frame;
    const auto& x=f.group.value;const auto& y=g.group.value;
    const auto& c=a.solo;const auto& d=b.solo;
    return f.clock==g.clock && f.own==g.own && f.own_team_score==g.own_team_score &&
        f.actors==g.actors && f.actor_count==g.actor_count && f.group.clock==g.group.clock &&
        f.group.subject==g.group.subject && f.group.knowledge==g.group.knowledge &&
        x.id==y.id && x.approach==y.approach && x.spawn==y.spawn && x.retreat==y.retreat &&
        x.lifecycle==y.lifecycle && x.original_spawn_visible==y.original_spawn_visible &&
        x.all_spawns_visible==y.all_spawns_visible && x.uncontested_now==y.uncontested_now &&
        c.clock==d.clock && c.actor==d.actor && c.group==d.group &&
        same_exposure(c.clear,d.clear) && same_exposure(c.hold,d.hold) &&
        same_exposure(c.bank,d.bank) && same_exposure(c.recovery,d.recovery) &&
        c.route_known==d.route_known && c.bank_executable==d.bank_executable &&
        c.renewal_reachable==d.renewal_reachable && c.solo==d.solo &&
        a.completion.clock==b.completion.clock && a.completion.group==b.completion.group &&
        a.completion.own_payout==b.completion.own_payout && a.completion.own_bank==b.completion.own_bank &&
        a.completion.attribution_unambiguous==b.completion.attribution_unambiguous &&
        a.assigned_group==b.assigned_group && a.forecast_rounds==b.forecast_rounds &&
        a.remaining_rounds==b.remaining_rounds && a.capture_window==b.capture_window &&
        a.at_approach==b.at_approach && a.at_bank==b.at_bank && a.at_renewal==b.at_renewal &&
        a.altar_legal==b.altar_legal && a.recall_safe==b.recall_safe && a.safe_now==b.safe_now &&
        a.fresh_target_attack_legal==b.fresh_target_attack_legal && a.inspection_failed==b.inspection_failed;
}
bool same_proposal(const std::optional<StepProposal>& a,const std::optional<StepProposal>& b) {
    return a.has_value()==b.has_value() && (!a || (a->action==b->action && a->stage==b->stage &&
        a->phase==b->phase && a->reason==b->reason && a->group==b->group));
}
void inplace_paths() {
    auto t=fixture();auto quote=std::make_unique<fc::EvaluatedQuote>();
    quote->evaluate(t->actual.frame,t->request);
    ActorState empty;
    const auto expected=bind_cycle_forecast(empty,t->actual,*quote);
    auto actual=std::make_unique<ReferenceInput>(t->actual);
    check(expected && bind_cycle_forecast_into(empty,*actual,*quote) && same_reference(*actual,*expected),
          "in-place bind matches every field of the copying binder");
    auto reject_unchanged=[&](const ActorState& state,const fc::EvaluatedQuote& value,const char* name) {
        const auto before=std::make_unique<ReferenceInput>(*actual);
        check(!bind_cycle_forecast_into(state,*actual,value) && same_reference(*actual,*before),name);
    };
    ++actual->frame.own.gold;
    actual->assigned_group=77;actual->forecast_rounds=555;actual->remaining_rounds=3;actual->capture_window=77;
    actual->completion={actual->frame.clock,17,true,true,true};
    reject_unchanged(empty,*quote,"rejected in-place view leaves frame, admission and completion fields unchanged");
    *actual=t->actual;actual->frame.group.value.retreat={1,1};
    reject_unchanged(empty,*quote,"rejected in-place group check commits none of the bound fields");
    auto unavailable=std::make_unique<fc::EvaluatedQuote>();
    *actual=t->actual;
    reject_unchanged(empty,*unavailable,"not-ready in-place quote leaves all input fields unchanged");

    ActorState direct_state,inplace_state;
    const auto direct=reference::choose_forecast(direct_state,t->actual,*quote);
    const auto into=reference::choose_forecast_into(inplace_state,*actual,*quote);
    check(direct && same_proposal(direct,into) && direct_state.active && inplace_state.active &&
          direct_state.project==inplace_state.project && direct_state.phase==inplace_state.phase &&
          direct_state.phase_limit==inplace_state.phase_limit && same_reference(*actual,*expected),
          "in-place selection preserves the public wrapper's admitted proposal and bounds");

    ActorState incumbent;
    check(incumbent.admit(9,10,5,90,0,9),"in-place incumbent admitted separately");
    incumbent.phase=Phase::hold;*actual=t->actual;
    reject_unchanged(incumbent,*quote,"in-place incumbent rejection leaves all observed fields unchanged");
    const auto prior=std::make_unique<ReferenceInput>(*actual);
    ActorState copy_incumbent=incumbent;
    const auto rejected=reference::choose_forecast(copy_incumbent,*actual,*quote);
    const auto rejected_into=reference::choose_forecast_into(incumbent,*actual,*quote);
    check(!rejected && same_proposal(rejected,rejected_into) && incumbent.project==copy_incumbent.project &&
          incumbent.phase==Phase::hold && copy_incumbent.phase==Phase::hold && same_reference(*actual,*prior),
          "in-place selection cannot replace an incumbent or mutate rejected observations");
    actual->frame.own.actor.channel=Channel::tribute;actual->frame.own.actor.channel_valid=true;
    actual->frame.own.actor.bag=20;*prior=*actual;copy_incumbent=incumbent;
    const auto channel=reference::choose_forecast(copy_incumbent,*actual,*unavailable);
    const auto channel_into=reference::choose_forecast_into(incumbent,*actual,*unavailable);
    check(channel && channel->action==Action::preserve_channel && same_proposal(channel,channel_into) &&
          incumbent.project==copy_incumbent.project && same_reference(*actual,*prior),
          "in-place selection preserves a real safe channel without binding or changing inputs");

    auto completed=fixture();ActorState bank_state;
    check(bank_state.admit(17,10,5,90,10,9),"in-place receipt project admitted");
    bank_state.phase=Phase::bank;bank_state.last_round=10;completed->actual.frame.own.actor.bag=10;
    check(confirm_tribute_started(bank_state,completed->actual.frame,
          seen(Action::tribute,completed->actual.frame.clock,1)),"in-place bank records a separate actual action receipt");
    refresh(*completed,11);completed->actual.frame.own.actor.bag=0;
    completed->actual.frame.own_team_score=10;
    completed->actual.completion={completed->actual.frame.clock,17,false,true,true};
    *actual=completed->actual;ActorState copy_bank=bank_state;
    const auto bank=reference::choose_forecast(copy_bank,completed->actual,*unavailable);
    const auto bank_into=reference::choose_forecast_into(bank_state,*actual,*unavailable);
    *prior=completed->actual;prior->solo={};prior->assigned_group=-1;
    check(bank && bank->action==Action::move && same_proposal(bank,bank_into) &&
          bank_state.bank_seen && copy_bank.bank_seen && bank_state.project==copy_bank.project &&
          same_reference(*actual,*prior),
          "in-place actual bank progression matches the wrapper and preserves frame and completion");
    ActorState no_receipt;
    check(no_receipt.admit(17,10,5,90,10,9),"in-place unreceipted project admitted");
    no_receipt.phase=Phase::bank;*actual=completed->actual;*prior=*actual;
    check(!reference::choose_forecast_into(no_receipt,*actual,*unavailable) &&
          !no_receipt.bank_seen && same_reference(*actual,*prior),
          "in-place unavailable quote cannot manufacture completion or alter its claim");
}
}
int main() {
    auto t = fixture(); ActorState state;
    auto bound = bind_cycle_forecast(state,t->actual,t->request);
    check(bound && bound->solo.valid(t->actual.frame,1,17) &&
          bound->forecast_rounds == 12 && bound->capture_window == 3,
          "reference receives generated cycle duration and cumulative safety");
    check(!bound->completion.own_payout && !bound->completion.own_bank,
          "predicted resource events never become completion observations");
    auto first = reference::choose_forecast(state,t->actual,t->request);
    check(first && first->action == Action::attack && state.active && state.project.group == 17,
          "generated first-income quote starts actual production without supplied certificate");
    refresh(*t,11);
    t->actual.frame.own.actor.health = 98;
    t->actual.frame.group.value.lifecycle = Lifecycle::reward_available;
    t->request.groups[0].observed = t->actual.frame.group;
    for (int i = 0; i < 2; ++i) t->request.groups[0].members[i].current.value.health = 0;
    auto held = reference::choose_forecast(state,t->actual,t->request);
    check(held && held->action == Action::hold && state.phase == Phase::hold,
          "actual clearance and occupied original spawn enter holding");
    refresh(*t,15);
    auto predicted = reference::choose_forecast(state,t->actual,t->request);
    check(predicted && predicted->action == Action::hold && !state.payout_seen,
          "forecast's original payout time cannot override an unobserved capture reset");
    banking(*t,17); t->actual.frame.own.actor.bag = 5;
    check(!bind_cycle_forecast(state,t->actual,t->request),
          "bank-only safety cannot replace capture-window safety before observed payout");
    t->actual.frame.own.actor.bag = 0;
    refresh(*t,16);
    t->actual.frame.group.value.lifecycle = Lifecycle::respawning;
    t->actual.frame.own.actor.bag = 10; t->actual.frame.own.gold = 16;
    t->actual.completion = {t->actual.frame.clock,17,true,false,true};
    banking(*t,17);
    auto paid = reference::choose_forecast(state,t->actual,t->request);
    check(paid && paid->phase == Phase::bank && state.payout_seen && state.project.group == 17,
          "actual payout advances to inventory conversion despite source now respawning");
    refresh(*t,17); t->actual.frame.own.actor.position = {1,0};
    t->actual.at_bank = t->actual.altar_legal = true; banking(*t,17);
    auto bank = reference::choose_forecast(state,t->actual,t->request);
    check(bank && bank->action == Action::tribute,
          "fresh inventory-only quote starts own whole-bag tribute");
    check(confirm_tribute_started(state,t->actual.frame,
          seen(Action::tribute,t->actual.frame.clock,1)), "successful self action journal recorded separately");
    refresh(*t,18); t->actual.frame.own.actor.bag = 0;
    t->actual.frame.own_team_score = 10;
    t->actual.completion = {t->actual.frame.clock,17,false,true,true};
    auto converted = reference::choose_forecast(state,t->actual,t->request);
    check(converted && state.bank_seen && converted->action == Action::move,
          "observed one-tick completion progresses when no new income quote is feasible");
    refresh(*t,19); t->actual.frame.own.actor.position = {0,0}; t->actual.at_renewal = true;
    auto renewed = reference::choose_forecast(state,t->actual,t->request);
    check(renewed && !state.active && !state.bank_seen,
          "actual named renewal closes prior project without fabricated production");
    refresh(*t,20); t->actual.at_renewal = t->actual.at_approach = false;
    t->actual.at_bank = t->actual.altar_legal = false;
    t->actual.frame.own.actor.spawn_group = -1;
    production(*t,18,{2,0});
    auto again = reference::choose_forecast(state,t->actual,t->request);
    check(again && again->action == Action::move && state.active && state.project.group == 18,
          "new current observations resume a different productive source after banking");

    auto other = fixture(); ActorState incumbent;
    check(incumbent.admit(9,10,5,90,0,9), "independent incumbent admitted");
    incumbent.phase = Phase::hold;
    const auto prior = incumbent.project;
    check(!reference::choose_forecast(incumbent,other->actual,other->request) &&
          incumbent.project == prior && incumbent.phase == Phase::hold,
          "another source forecast cannot rename or abandon an active safe stage");
    other->request.kit.knowledge = Knowledge::stale;
    ActorState empty;
    check(!reference::choose_forecast(empty,other->actual,other->request) && !empty.active,
          "stale request cannot borrow another quote's fresh certificate");
    other->actual.frame.clock.round = 64;
    other->actual.frame.own.actor.channel = Channel::tribute;
    other->actual.frame.own.actor.channel_valid = true;
    other->actual.frame.own.actor.bag = 20;
    other->request.timing.settings.source = Source::unknown;
    auto channel = reference::choose_forecast(incumbent,other->actual,other->request);
    check(channel && channel->action == Action::preserve_channel && incumbent.project == prior,
          "valid safe channel survives unknown new forecast and epoch boundary");
    evaluated_paths();
    inplace_paths();
    std::printf("{\"suite\":\"forecast-integration\",\"checks\":%d,\"passed\":true}\n",checks);
}
