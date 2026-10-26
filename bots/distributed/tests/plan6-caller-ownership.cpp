// Real organization choices and fresh evaluated alternatives cross the same
// admission/replacement boundary as the native adapter. No native games.
#include "../reference/src/policy.h"
#include "../mesh/src/policy.h"
#include "../owner-builder/src/organization.h"
#include "../replicas/src/organization.h"
#include "../salvage/src/policy.h"
#include "../witness/src/policy.h"
#include "../common/src/runtime_catalogue.h"
#include "../common/src/runtime_observation.h"
#include "../common/src/runtime_opportunities.h"
#include "../common/src/runtime_threat.h"
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {
using namespace p6;
namespace fc = p6::forecast;
int checks = 0;
void check(bool value, const char* name) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL caller ownership: %s\n", name); std::exit(1); }
}
template<class T> Fact<T> setting(T value) { return {value, Source::queried_setting}; }
template<class T> Observation<T> seen(T value, ViewClock clock, int id) {
    return {value, clock, id, Knowledge::exact};
}
struct Fixture {
    organization::Input in{};
    std::array<fc::Request, 2> requests{};
    std::array<fc::EvaluatedQuote, 2> quotes{};
    ActorState execution{};
    runtime_opportunities::History opportunities{};
    mesh::Policy policy{};
};
std::unique_ptr<Fixture> fixture() {
    auto f = std::make_unique<Fixture>();
    auto& in = f->in;
    in.frame.clock = {10, 1, 0};
    auto& self = in.frame.own.actor;
    self.id = 1; self.pick_ordinal = 0; self.type = mesh::profile_value.roster[0];
    self.team = Team::ally; self.alive = self.can_move = self.can_attack = true;
    self.health = self.max_health = 100; self.strength = 10; self.attack_range = 1;
    self.ready_attack_group = 0;
    in.ordinal = 0; in.picks = 3; in.settings = {200, 1000, 2};
    in.site_count = 2; in.safe_now = true;
    ScoreHistory history;
    check(history.sample({10,1}, 0), "personal score observation accepted");
    in.windows = history.windows(in.settings, {10,1}, 0);
    for (int i = 0; i < 2; ++i) {
        auto& site = in.sites[i];
        const Point position{i * 2, 0};
        site.group = {i, position, position, position, Lifecycle::living, true, true, true};
        site.known = true;
        auto& r = f->requests[i];
        r.shape = fc::Shape::produce_bank; r.project_group = i;
        r.timing.settings = setting(in.settings);
        r.timing.tribute_multiplier = setting(.1); r.timing.tribute_base = setting(0);
        r.timing.tribute_minimum = setting(1);
        r.kit = seen(Productivity{10,0,false,false,false,false}, in.frame.clock, 1);
        r.ambient_incoming = seen(0., in.frame.clock, 1);
        r.bank_waypoint = 90; r.bank_position = {1,0};
        r.renewal_waypoint = i; r.renewal_position = position;
        r.bank_available = seen(true, in.frame.clock, 90);
        r.group_count = 1;
        auto& g = r.groups[0];
        g.observed = seen(site.group, in.frame.clock, i);
        g.membership_complete = {true, Source::public_map}; g.member_count = 1;
        g.members[0].public_full = {MonsterWork{100+i,10,0}, Source::public_map};
        g.members[0].current = seen(MonsterWork{100+i,10,0}, in.frame.clock, 100+i);
        g.gems = {i ? 10 : 20, Source::public_map}; g.gold = {7, Source::public_map};
        auto leg = [&](Point from, Point to) {
            r.legs[r.leg_count++] = {from, to,
                {std::max(std::abs(from.x-to.x), std::abs(from.y-to.y)), Source::public_map}};
        };
        leg(self.position, position); leg(position, position);
        leg(position, r.bank_position); leg(r.bank_position, position);
        f->quotes[i].evaluate(in.frame, r);
        check(f->quotes[i].result().status == Status::feasible, "both real production alternatives are feasible");
        site.actor_count = 1; site.actors[0] = self.id;
        site.cycles[0] = f->quotes[i].result().certificate;
        site.independent[0] = evaluate_schedule(f->quotes[i].result().schedule, in.windows,
            in.settings, {10,1}, 0);
        check(site.independent[0].eligible(), "both alternatives have evaluated ranks");
    }
    return f;
}
int independent(Fixture& f) {
    std::array<runtime_opportunities::Candidate,2> candidates{};
    for (int i = 0; i < f.in.site_count; ++i) {
        const auto& site = f.in.sites[i];
        auto& candidate=candidates[i];candidate.group=site.group.id;
        candidate.eligible=reference::admission_eligible(f.execution,site.group.id,f.in.frame.clock.round);
        if(f.quotes[i].ready() && f.quotes[i].matches_frame(f.in.frame) &&
           f.quotes[i].result().status==Status::feasible)candidate.kind=runtime_opportunities::Kind::quote;
    }
    const int best=runtime_opportunities::select(candidates,f.opportunities,f.in.frame.clock.round,
        [&](int a,int b){return compare(f.in.sites[a].independent[0],f.in.sites[b].independent[0])>0;});
    return best < 0 ? -1 : f.in.sites[best].group.id;
}
organization::Decision choose(Fixture& f) {
    f.in.independent_group = independent(f);
    f.in.current_group = f.execution.active ? f.execution.project.group : -1;
    f.in.phase = f.execution.phase;
    return f.policy.choose(f.in, false);
}
ReferenceInput observed(const Fixture& f, int group) {
    ReferenceInput out;
    out.frame = f.in.frame;
    out.frame.group = seen(f.in.sites[group].group, out.frame.clock, group);
    out.assigned_group = group; out.safe_now = f.in.safe_now;
    out.at_approach = out.frame.own.actor.position == f.in.sites[group].group.approach;
    out.fresh_target_attack_legal = group == out.frame.own.actor.ready_attack_group;
    return out;
}
bool same_execution(const ActorState& a, const ActorState& b) {
    if (a.project != b.project || a.active != b.active || a.phase != b.phase ||
        a.stage != b.stage || a.failure != b.failure || a.owner_turn != b.owner_turn ||
        a.last_round != b.last_round || a.retired_group != b.retired_group ||
        a.retired_epoch != b.retired_epoch || a.repairs != b.repairs || a.waits != b.waits ||
        a.previous_observed != b.previous_observed || a.payout_seen != b.payout_seen ||
        a.bank_seen != b.bank_seen || a.empty_liquidation != b.empty_liquidation ||
        a.tribute_started != b.tribute_started) return false;
    for (std::size_t i = 0; i < a.epoch_budgets.size(); ++i) {
        const auto& x = a.epoch_budgets[i]; const auto& y = b.epoch_budgets[i];
        if (x.group != y.group || x.epoch != y.epoch || x.repairs != y.repairs || x.retired != y.retired)
            return false;
    }
    return true;
}
void epoch_admission_without_clone() {
    // Retain the former caller boundary as a semantic comparison. The native
    // adapter no longer needs this copy to inspect a potential replacement.
    const auto prior = [](const ActorState& state, int group, int round) {
        if (group < 0 || round < 0) return false;
        if (state.active && state.project.group == group) return true;
        if (reference::replacement_locked(state)) return false;
        auto candidate = state;
        candidate.active = false;
        return candidate.may_admit(group, round);
    };
    const auto verify = [&](const ActorState& state, int group, int round, bool expected,
                            const char* name) {
        const bool current = reference::admission_eligible(state, group, round);
        check(current == expected && current == prior(state, group, round), name);
    };
    ActorState state;
    verify(state, 7, 63, true, "empty epoch permits a new caller candidate with prior semantics");
    verify(state, -1, 63, false, "invalid group remains rejected before ledger inspection");
    verify(state, 7, -1, false, "invalid round remains rejected before ledger inspection");
    // Fill the actual ledger through admission/retirement, exercising its last
    // slot rather than assuming that only the first project has history.
    for (int group = 0; group < 7; ++group) {
        check(state.admit(group, 63, 4, 100, 0, 0), "earlier epoch project is actually admitted");
        state.retire(Failure::expired);
    }
    check(state.admit(7, 63, 4, 100, 0, 0), "last ledger slot holds a real active incumbent");
    check(!state.may_admit(7, 63) && !state.may_admit(8, 63) && state.epoch_eligible(7, 63),
          "active admission stays closed while the shared epoch check remains ownership independent");
    verify(state, 7, 63, true, "active incumbent remains eligible with prior semantics");
    verify(state, 0, 63, false, "another retired project remains ineligible while an incumbent is active");
    verify(state, 8, 63, true, "unlocked production retains a possible new replacement");
    const auto active = state;
    for (int variant = 0; variant < 6; ++variant) {
        state = active;
        state.phase = variant == 0 ? Phase::hold : variant == 1 ? Phase::bank : Phase::travel;
        state.tribute_started = variant == 2; state.payout_seen = variant == 3;
        state.bank_seen = variant == 4; state.empty_liquidation = variant == 5;
        verify(state, 8, 64, false, "conversion lock rejects even a fresh-epoch replacement with prior semantics");
        verify(state, 7, 64, true, "conversion lock retains its actual incumbent across the epoch boundary");
    }
    state = active; state.retire(Failure::expired);
    check(!state.epoch_eligible(7, 63) && !state.may_admit(7, 63),
          "last-slot retirement closes both shared epoch and inactive admission checks");
    verify(state, 7, 63, false, "retired incumbent cannot reenter its old epoch");
    verify(state, 7, 64, true, "retired incumbent is eligible again exactly at epoch rollover");
    check(state.may_admit(7, 64) && state.admit(7, 64, 4, 100, 0, 0),
          "next epoch eligibility still reaches actual admission");
}
void retired_policy_proposal() {
    auto f = fixture();
    check(f->execution.admit(0,10,4,190,0,0), "former region admitted");
    f->execution.retire(Failure::expired);
    check(compare(f->in.sites[0].independent[0], f->in.sites[1].independent[0]) > 0,
          "retired region would outrank the live alternative");
    const auto decision = choose(*f);
    check(decision.group == 0 && f->in.independent_group == 1,
          "real Mesh region proposal competes with the ranked admissible alternative");
    const int selected = reference::resolve_work_group(f->execution, decision.group,
        f->in.independent_group, 10, [&](int group) { return group >= 0 && group < 2; });
    check(selected == 1, "retired regional proposal resolves to the real independent quote");
    auto actual = observed(*f, selected);
    const auto action = reference::choose_forecast_replacing_into(f->execution, actual, &f->quotes[selected]);
    check(action && f->execution.active && f->execution.project.group == 1,
          "caller admits and reaches the alternative executor");
    check(!f->execution.may_admit(0,10), "alternative does not reset original retirement");
    auto released = f->execution; released.active = false;
    check(!reference::admission_eligible(released,0,63) && reference::admission_eligible(released,0,64),
          "original region becomes eligible only in its next epoch");
}
void atomic_replacement() {
    auto f = fixture();
    check(f->execution.admit(1,10,4,190,0,0), "different incumbent admitted");
    f->execution.phase = Phase::clear;
    const auto decision = choose(*f);
    check(decision.intent == organization::Intent::work && decision.group == 0,
          "real Mesh policy selects its freshly certified regional replacement");
    auto actual = observed(*f, decision.group);
    const auto original = f->execution;
    check(!reference::choose_forecast_replacing_into(f->execution, actual, nullptr) &&
          same_execution(f->execution, original), "missing quote preserves incumbent and history");
    auto stale = actual; ++stale.frame.clock.revision;
    check(!reference::choose_forecast_replacing_into(f->execution, stale, &f->quotes[0]) &&
          same_execution(f->execution, original), "stale quote preserves incumbent and history");
    auto unsafe = std::make_unique<fc::EvaluatedQuote>();
    auto request = std::make_unique<fc::Request>(f->requests[0]);
    request->ambient_incoming.value = 1000;
    unsafe->evaluate(actual.frame, *request);
    check(unsafe->result().status != Status::feasible, "unsafe replacement is actually forecast-rejected");
    check(!reference::choose_forecast_replacing_into(f->execution, actual, unsafe.get()) &&
          same_execution(f->execution, original), "unsafe quote preserves incumbent and history");
    const auto action = reference::choose_forecast_replacing_into(f->execution, actual, &f->quotes[0]);
    check(action && action->action == Action::attack && f->execution.active &&
          f->execution.project.group == 0, "fresh safe replacement commits only after reaching its executor");
    const auto* retired = f->execution.budget(1,0);
    check(retired && retired->retired, "successful replacement retires its old project once");
}
void protected_stages() {
    for (const auto phase : {Phase::hold, Phase::bank}) {
        auto f = fixture();
        check(f->execution.admit(1,10,4,190,0,0), "protected incumbent admitted");
        f->execution.phase = phase;
        const auto decision = choose(*f);
        auto actual = observed(*f, decision.group);
        const auto original = f->execution;
        check(!reference::choose_forecast_replacing_into(f->execution, actual, &f->quotes[0]) &&
              same_execution(f->execution, original), "organization work cannot replace capture or bank");
    }
    auto f = fixture();
    check(f->execution.admit(1,10,4,190,0,0), "channel incumbent admitted");
    f->execution.phase = Phase::bank;
    const auto decision = choose(*f);
    check(decision.intent == organization::Intent::work && decision.group == 0,
          "real Mesh region remains a competing organization proposal");
    f->in.frame.own.actor.channel = Channel::tribute;
    f->in.frame.own.actor.channel_valid = true;
    // Native turn() preserves this live channel before invoking a policy.
    // The executor boundary must agree even if it is given the pending real
    // organization proposal and no quote for it.
    auto actual = observed(*f, decision.group);
    const auto action = reference::choose_forecast_replacing_into(f->execution, actual, nullptr);
    check(action && action->action == Action::preserve_channel && f->execution.active &&
          f->execution.project.group == 1, "actual valid channel precedes replacement and quote availability");
}
void completion_before_routing() {
    auto f = fixture();
    check(f->execution.admit(0,10,4,190,0,0), "receipt project admitted");
    f->execution.phase = Phase::hold;
    f->execution.previous_observed = true;
    auto actual = observed(*f, 0);
    actual.frame.own.actor.spawn_group = 0;
    actual.frame.group.value.lifecycle = Lifecycle::respawning;
    actual.frame.own.actor.bag = 4; actual.frame.own.gold = 60;
    actual.completion = {actual.frame.clock,0,true,false,true};
    actual.safe_now = false;
    const auto original = f->execution;
    auto rejected = actual;
    ++rejected.completion.clock.round;
    record_completion(f->execution, rejected);
    check(same_execution(f->execution, original), "stale payout cannot update ownership");
    rejected = actual; rejected.completion.group = 1;
    record_completion(f->execution, rejected);
    check(same_execution(f->execution, original), "another project's payout cannot update ownership");
    rejected = actual; rejected.completion.attribution_unambiguous = false;
    record_completion(f->execution, rejected);
    check(same_execution(f->execution, original), "ambiguous payout cannot update ownership");

    // The native caller ingests the complete observation before its unsafe or
    // low-fuel routing return. Ingestion consumes no gameplay turn or action.
    record_completion(f->execution, actual);
    check(f->execution.active && f->execution.project.group == 0 &&
          f->execution.payout_seen && f->execution.phase == Phase::allocate &&
          f->execution.owner_turn == original.owner_turn &&
          f->execution.last_round == original.last_round,
          "unsafe payout observation is retained without authorizing an action");
    auto fuel_limited = original;
    actual.safe_now = true;
    record_completion(fuel_limited, actual);
    check(fuel_limited.payout_seen && fuel_limited.phase == Phase::allocate &&
          fuel_limited.owner_turn == original.owner_turn &&
          fuel_limited.last_round == original.last_round,
          "payout also survives an action turn skipped for fuel");

    // The receipt is now one-shot-consumed and the resource baseline refreshed.
    // A later fresh bank quote must bind from the acknowledged allocation stage.
    f->execution.previous_bag = 4; f->execution.previous_gold = 60;
    actual.completion = {};
    ++actual.frame.clock.round;
    actual.frame.own.actor.position = {1,0}; actual.frame.own.actor.spawn_group = -1;
    actual.frame.group.clock = actual.frame.clock;
    actual.at_bank = actual.altar_legal = true;
    auto request = std::make_unique<fc::Request>(f->requests[0]);
    request->shape = fc::Shape::bank_only; request->group_count = 0;
    request->kit = seen(Productivity{10,0,false,false,false,false}, actual.frame.clock, 1);
    request->ambient_incoming = seen(0., actual.frame.clock, 1);
    request->bank_available = seen(true, actual.frame.clock, 90);
    request->leg_count = 2;
    request->legs[0] = {{1,0},{1,0},{0,Source::public_map}};
    request->legs[1] = {{1,0},{0,0},{1,Source::public_map}};
    auto bank = std::make_unique<fc::EvaluatedQuote>();
    bank->evaluate(actual.frame, *request);
    check(bank->result().status == Status::feasible, "later bank alternative is freshly evaluated");
    const auto proposal = reference::choose_forecast_replacing_into(f->execution, actual, bank.get());
    check(proposal && proposal->phase == Phase::bank && f->execution.phase == Phase::bank,
          "acknowledged payout advances through a real bank quote after diversion");
    check(confirm_tribute_started(f->execution, actual.frame,
          seen(Action::tribute, actual.frame.clock, 1)), "own successful tribute start is confirmed");

    ++actual.frame.clock.round;
    actual.frame.group.clock = actual.frame.clock;
    actual.frame.own.actor.bag = 0; actual.frame.own_team_score = 4;
    actual.safe_now = false;
    actual.completion = {actual.frame.clock,0,false,true,true};
    const auto submitting = f->execution;
    rejected = actual; --rejected.completion.clock.round;
    record_completion(f->execution, rejected);
    check(same_execution(f->execution, submitting), "stale bank receipt cannot update ownership");
    rejected = actual; rejected.completion.group = 1;
    record_completion(f->execution, rejected);
    check(same_execution(f->execution, submitting), "bank receipt cannot change another project");
    rejected = actual; rejected.frame.own_team_score = 3;
    record_completion(f->execution, rejected);
    check(same_execution(f->execution, submitting), "insufficient actual score cannot forge a bank receipt");
    record_completion(f->execution, actual);
    check(f->execution.active && f->execution.bank_seen &&
          f->execution.phase == Phase::travel && !f->execution.tribute_started &&
          f->execution.owner_turn == submitting.owner_turn &&
          f->execution.last_round == submitting.last_round,
          "actual bank is retained before unsafe recovery without issuing an action");

    // An intervening return may clear the caller notice and make every quote
    // stale. Renewal still uses the authenticated milestone and physical point.
    actual.completion = {}; actual.safe_now = true;
    ++actual.frame.clock.round; actual.frame.group.clock = actual.frame.clock;
    actual.solo = {}; actual.at_renewal = false;
    check(!bank->matches_frame(actual.frame), "old bank quote is stale on the return turn");
    const auto returning = reference::choose(f->execution, actual);
    check(returning.action == Action::move && returning.phase == Phase::travel &&
          f->execution.active && f->execution.bank_seen,
          "cheap renewal reaches movement after the one-shot bank notice is gone");
    check(!reference::renewed_without_action(true,f->execution,actual,returning),
          "pending physical renewal keeps its owned movement instead of releasing a policy action");
    ++actual.frame.clock.round; actual.frame.group.clock = actual.frame.clock;
    actual.frame.own.actor.position = {0,0}; actual.at_renewal = true;
    actual.recall_safe=true; // Complete zero-stress fixture, as in the native caller.
    const auto closing=reference::choose(f->execution, actual);
    check(!f->execution.active && !f->execution.bank_seen && !f->execution.payout_seen,
          "only reaching the named physical renewal point closes the converted project");
    check(reference::renewed_without_action(true,f->execution,actual,closing),
          "completed actual bank renewal releases only its unperformed idle recall");
    const auto closed=f->execution;
    const auto repeated=reference::choose(f->execution,actual);
    check(repeated.action==Action::yield && same_execution(f->execution,closed) &&
          !reference::renewed_without_action(true,f->execution,actual,repeated),
          "renewal handoff does not admit or release a second reference action in the same owner turn");
}
void empty_renewal_handoff() {
    auto f=fixture();auto& state=f->execution;
    check(state.admit(1,9,4,191,0,0) && state.repair(),"prior epoch repair is charged");
    state.retire(Failure::observed_failed_action);
    check(state.admit(0,10,4,190,0,0) && state.repair(),"payout owner retains its own repair charge");
    state.phase=Phase::hold;state.previous_observed=true;
    auto actual=observed(*f,0);actual.frame.own.actor.spawn_group=0;
    actual.frame.group.value.lifecycle=Lifecycle::respawning;
    actual.frame.own.gold=7;actual.completion={actual.frame.clock,0,true,false,true};
    actual.at_renewal=true;actual.recall_safe=true;
    record_completion(state,actual);
    check(state.empty_liquidation && state.active && !state.bank_seen,
          "actual zero-gem gold receipt creates renewal ownership without a bank receipt");
    auto stocked=actual.frame;stocked.own.gold=0; // Legal field buy observed next turn.
    const auto idle_allowed=[&](const Frame& frame,bool independent=true,bool safe=true,
                               bool recovering=false,bool shop=false) {
        return reference::idle_renewal_may_yield(state,frame,independent,safe,recovering,shop);
    };
    check(idle_allowed(stocked),"fully supplied independent owner may request idle renewal handoff");
    check(!idle_allowed(stocked,false) && !idle_allowed(stocked,true,false) &&
          !idle_allowed(stocked,true,true,true) && !idle_allowed(stocked,true,true,false,true),
          "organization orders and actual danger recovery or shop need retain their paths");
    for(int kind=0;kind<6;++kind) {
        auto needed=stocked;
        if(kind==0)--needed.own.actor.health;
        if(kind==1)needed.own.gold=1;
        if(kind==2)needed.own.actor.bag=1;
        if(kind==3)needed.own.actor.channel=Channel::recall;
        if(kind==4)needed.own.actor.alive=false;
        if(kind==5)needed.clock.self=99;
        check(!idle_allowed(needed),"incomplete supply or invalid current actor cannot suppress recall");
    }
    auto unowned=state;unowned.active=false;
    check(!reference::idle_renewal_may_yield(unowned,stocked,true,true,false,false),
          "an unavailable unowned source cannot turn ordinary fallback into a renewal handoff");
    auto no_receipt=state;no_receipt.bank_seen=no_receipt.empty_liquidation=false;
    check(!reference::idle_renewal_may_yield(no_receipt,stocked,true,true,false,false),
          "an unfinished production job cannot claim completed renewal");
    const auto pending=state;
    auto channel=actual;channel.frame.own.actor.channel=Channel::recall;
    channel.frame.own.actor.channel_valid=true;
    auto channel_state=pending;
    const auto preserving=reference::choose(channel_state,channel);
    check(preserving.action==Action::preserve_channel && channel_state.active &&
          !reference::renewed_without_action(true,channel_state,channel,preserving),
          "a valid recall channel survives coincident renewal geometry");
    auto unsafe=actual;unsafe.safe_now=false;auto unsafe_state=pending;
    const auto retreat=reference::choose(unsafe_state,unsafe);
    check(!reference::renewed_without_action(true,unsafe_state,unsafe,retreat),
          "unsafe retirement is not a successful conversion handoff");
    const auto closing=reference::choose(state,actual);
    check(reference::renewed_without_action(true,state,actual,closing) && !state.active &&
          !state.bank_seen && !state.empty_liquidation && state.repairs==1 &&
          !state.epoch_eligible(1,10) && state.budget(0,0)->repairs==1,
          "gold-only renewal closes while preserving both projects' repair and retirement history");
}
using Opportunity = runtime_opportunities::Candidate;
using OpportunityKind = runtime_opportunities::Kind;
std::array<Opportunity,2> opportunities(const Fixture& f, OpportunityKind first, OpportunityKind second) {
    std::array<Opportunity,2> out{};
    for(int i=0;i<2;++i)out[i]={f.in.sites[i].group.id,i?second:first,
        reference::admission_eligible(f.execution,f.in.sites[i].group.id,f.in.frame.clock.round),false};
    return out;
}
int select_opportunity(Fixture& f,const std::array<Opportunity,2>& candidates) {
    const int index=runtime_opportunities::select(candidates,f.opportunities,f.in.frame.clock.round,
        [&](int a,int b){return compare(f.in.sites[a].independent[0],f.in.sites[b].independent[0])>0;});
    f.in.independent_group=index<0?-1:candidates[index].group;
    return index;
}
int resolve_opportunity(const Fixture& f,const organization::Decision& decision,
                        const std::array<Opportunity,2>& candidates) {
    return reference::resolve_work_group(f.execution,decision.group,f.in.independent_group,
        f.in.frame.clock.round,[&](int group) {
            for(int i=0;i<2;++i)if(candidates[i].group==group)
                return runtime_opportunities::usable(candidates[i],f.opportunities,i,f.in.frame.clock.round);
            return false;
        });
}
void refresh_quotes(Fixture& f) {
    for(int i=0;i<2;++i) {
        auto& site=f.in.sites[i];auto& request=f.requests[i];const auto clock=f.in.frame.clock;
        request.kit.clock=request.ambient_incoming.clock=request.bank_available.clock=clock;
        request.groups[0].observed=seen(site.group,clock,site.group.id);
        request.groups[0].members[0].current.clock=clock;
        f.quotes[i].evaluate(f.in.frame,request);
        check(f.quotes[i].result().status==Status::feasible,"fresh owner frame has a real feasible quote");
        site.actor_count=1;site.actors[0]=f.in.frame.clock.self;
        site.cycles[0]=f.quotes[i].result().certificate;
        site.independent[0]=evaluate_schedule(f.quotes[i].result().schedule,f.in.windows,f.in.settings,
            {clock.round,clock.self},f.in.frame.own_team_score);
    }
}
void rejected_opportunity_rotates() {
    auto f=fixture();const auto owner=f->execution;
    auto candidates=opportunities(*f,OpportunityKind::quote,OpportunityKind::quote);
    check(select_opportunity(*f,candidates)==0,"current quote ranking initially prefers source zero");
    f->requests[0].ambient_incoming=seen(100.,f->in.frame.clock,1);
    f->quotes[0].evaluate(f->in.frame,f->requests[0]);
    check(f->quotes[0].ready() && f->quotes[0].matches_frame(f->in.frame) &&
          f->quotes[0].result().status!=Status::feasible,"actual self evaluator supplies a current completed rejection");
    check(f->opportunities.reject(0,0,10),"completed self rejection starts bounded selection cooldown");
    check(same_execution(f->execution,owner),"selection rejection cannot mutate ownership or epoch budgets");
    f->in.sites[0].actor_count=0;f->in.sites[0].independent[0]={};
    candidates=opportunities(*f,OpportunityKind::inspection,OpportunityKind::quote);
    check(select_opportunity(*f,candidates)==1,"usable quote replaces the cooled source in independent ranking");
    const auto decision=f->policy.choose(f->in,false);
    const int group=resolve_opportunity(*f,decision,candidates);
    check(group==1,"real organization proposal resolves through the same opportunity predicate");
    auto actual=observed(*f,group);
    const auto action=reference::choose_forecast_replacing_into(f->execution,actual,&f->quotes[1]);
    check(action && f->execution.active && f->execution.project.group==1,
          "real reference caller admits the alternative instead of healthy recovery");
    const auto cooled=f->opportunities;
    check(!f->opportunities.reject(0,0,11) && f->opportunities==cooled,
          "repeat rejection cannot extend an active cooldown");
    check(!f->opportunities.available(0,0,17) && f->opportunities.available(0,0,18),
          "selection cooldown expires after eight rounds");
}
void deferred_opportunity_yields() {
    auto f=fixture();++f->in.frame.clock.round;
    check(!f->quotes[0].matches_frame(f->in.frame),"setup or fuel deferral has no current quote");
    auto candidates=opportunities(*f,OpportunityKind::deferred,OpportunityKind::none);
    for(int gate=0;gate<3;++gate) { // Fuel admission, site paths, or cold storage.
        const auto owner=f->execution;const auto history=f->opportunities;
        check(!f->opportunities.inspect(0,0,11,true,false,true) && f->opportunities==history,
              "fuel path and cold deferrals do not consume inspection budget");
        check(select_opportunity(*f,candidates)==0,"deferred source retains its independent assignment");
        const auto decision=f->policy.choose(f->in,false);
        const int group=resolve_opportunity(*f,decision,candidates);
        check(group==0 && runtime_opportunities::deferred(candidates[group]),
              "real organization and resolve retain the deferred source for yield");
        // The SDK adapter recognizes this same disposition before calling a
        // reference fallback: absence of a quote authorizes no gameplay action.
        const StepProposal action=runtime_opportunities::deferred(candidates[group])?
            StepProposal{}:reference::choose(f->execution,observed(*f,group));
        check(action.action==Action::yield && same_execution(f->execution,owner) &&
              f->opportunities==history,"deferred routing yields without recall cooldown or retirement");
    }
    ++f->in.frame.clock.round;refresh_quotes(*f);
    candidates=opportunities(*f,OpportunityKind::quote,OpportunityKind::quote);
    check(select_opportunity(*f,candidates)==0,"later sufficient-budget evidence reopens the same assignment");
    const auto decision=f->policy.choose(f->in,false);
    const int group=resolve_opportunity(*f,decision,candidates);auto actual=observed(*f,group);
    const auto action=reference::choose_forecast_replacing_into(f->execution,actual,&f->quotes[group]);
    check(action && action->action==Action::attack && f->execution.active,
          "fresh real quote admits productive work after a harmless deferral");
}
void exact_pad_inspection_budget() {
    runtime_opportunities::History history;const auto empty=history;
    check(!history.inspect(0,20,10,false,false) && history==empty,
          "adjacent approach position cannot spend exact-pad inspection budget");
    check(!history.inspect(0,20,10,true,false),"first fresh on-pad incomplete observation remains inspectable");
    const auto once=history;
    check(!history.inspect(0,20,10,true,false) && history==once,
          "repeated calls in one owner round cannot double count a probe");
    check(!history.inspect(0,20,11,true,false,true) && history==once,
          "a deferred later turn does not consume or reset an existing probe");
    check(history.inspect(0,20,12,true,false) && !history.available(0,20,19),
          "second fresh exact-pad incomplete observation cools only that site");
    check(history.available(1,21,12),"incomplete inspection does not suppress another site");
    check(history.available(0,20,20) && !history.inspect(0,20,20,true,false),
          "expired inspection cooldown starts a fresh two-observation budget");
    check(history.inspect(0,20,21,true,false),"second retry observation can start another bounded cooldown");
    runtime_opportunities::History completed;
    completed.inspect(0,20,10,true,false);completed.inspect(0,20,11,true,true);
    check(!completed.inspect(0,20,12,true,false) && completed.available(0,20,12),
          "complete current membership resets an incomplete inspection count");
    const auto unchanged=completed;
    check(!completed.reject(8,20,12) && !completed.inspect(-1,20,12,true,false) &&
          !completed.available(0,-1,12) && completed==unchanged,"invalid opportunity identity cannot mutate history");
}
void protected_opportunity_and_policy_restrictions() {
    auto f=fixture();check(f->execution.admit(0,10,4,190,0,0),"protected opportunity owner admitted");
    f->opportunities.reject(0,0,10);
    const auto admitted=f->execution;
    for(int variant=0;variant<5;++variant) {
        f->execution=admitted;
        f->execution.phase=variant==0?Phase::hold:variant==1?Phase::bank:variant==2?Phase::allocate:Phase::travel;
        f->execution.payout_seen=variant==2;f->execution.bank_seen=variant==3;f->execution.empty_liquidation=variant==4;
        const auto owner=f->execution;const auto history=f->opportunities;
        auto candidates=opportunities(*f,OpportunityKind::none,OpportunityKind::quote);
        candidates[0].protected_incumbent=true;
        check(select_opportunity(*f,candidates)==0,"capture payout bank and renewal preserve the protected incumbent");
        const auto decision=f->policy.choose(f->in,false);
        check(resolve_opportunity(*f,decision,candidates)==0 && same_execution(f->execution,owner) &&
              f->opportunities==history,"protected resolution changes no receipt or ownership state");
    }
    f=fixture();auto& self=f->in.frame.own.actor;self.pick_ordinal=1;self.type=owner_builder::roster[1];
    f->in.ordinal=1;refresh_quotes(*f);
    auto candidates=opportunities(*f,OpportunityKind::quote,OpportunityKind::quote);
    for(int i=0;i<2;++i)candidates[i].eligible &= owner_builder::fixed_owner(self,candidates[i].group,f->in.picks);
    check(select_opportunity(*f,candidates)==1,"caller supplied fixed-owner eligibility outranks another source's better score");
    owner_builder::ExecutablePolicy owner_policy;const auto owner_choice=owner_policy.choose(f->in,false);
    check(owner_choice.group==1 && resolve_opportunity(*f,owner_choice,candidates)==1,
          "real active OwnerBuilder keeps its fixed source through shared selection and resolution");
    f->opportunities.reject(1,1,10);
    check(select_opportunity(*f,candidates)==-1,"cooldown cannot reroute fixed owner onto another owner's source");
    self.type=replicas::roster[1];refresh_quotes(*f);f->opportunities={};f->in.proposed_group=0;
    candidates=opportunities(*f,OpportunityKind::quote,OpportunityKind::quote);
    for(auto& candidate:candidates)candidate.eligible &= candidate.group!=f->in.proposed_group;
    check(select_opportunity(*f,candidates)==1,"replica independent eligibility excludes the startup owner's source");
    replicas::ExecutablePolicy replica_policy;const auto replica_choice=replica_policy.choose(f->in,true);
    check(replica_choice.group==1 && resolve_opportunity(*f,replica_choice,candidates)==1,
          "real replica control keeps its named independent source instead of merging into startup");
    check(f->in.sites[0].known && !f->in.sites[0].blocked && f->in.sites[1].known && !f->in.sites[1].blocked,
          "selection history never rewrites physical visibility or blockage");
}
void durable_self_failure_delivery() {
    auto f=fixture();salvage::Policy policy;
    auto actual=observed(*f,0);
    const auto first=reference::choose_forecast_replacing_into(f->execution,actual,&f->quotes[0]);
    check(first && f->execution.active && f->execution.project.group==0,
          "real forecast executor owns the source before the failure");
    // The native dead-actor boundary uses this exact transition. The next
    // complete observation can happen while recovery still diverts policy.
    f->execution.retire(Failure::observed_unavailable);
    const auto failed=f->execution.project;
    check(runtime_observation::deliver_failure(policy,failed,f->execution.failure),
          "complete observation durably delivers the exact native retirement token");
    f->in.safe_now=false;f->in.observed_failure=true;
    check(policy.choose(f->in,false).intent==organization::Intent::independent &&
          policy.state.pending_self.project==failed,
          "unsafe policy boundary retains notice without granting a repair");
    f->in.safe_now=true;f->in.observed_failure=false;
    ++f->in.frame.clock.round;refresh_quotes(*f);
    f->in.current_group=-1;f->in.proposed_group=1;f->in.independent_group=independent(*f);
    const auto decision=policy.choose(f->in,false);
    check(decision.intent==organization::Intent::independent && decision.group==1 &&
          policy.state.pending_self.reason==Failure::none,
          "later safe policy consumes self failure and selects real different-source work");
    const auto* old=policy.state.record(0,failed.epoch);
    check(old && old->retired && !old->repaired && policy.state.group==-1,
          "failure retires its exact policy epoch without blaming the proposed source");
    const int selected=reference::resolve_work_group(f->execution,decision.group,
        f->in.independent_group,f->in.frame.clock.round,
        [&](int group){return group>=0 && group<2 && f->quotes[group].matches_frame(f->in.frame);});
    actual=observed(*f,selected);
    const auto next=reference::choose_forecast_replacing_into(f->execution,actual,&f->quotes[selected]);
    check(selected==1 && next && f->execution.active && f->execution.project.group==1,
          "actual resolver and forecast executor admit the unretired alternative");
    check(runtime_observation::deliver_failure(policy,failed,Failure::observed_unavailable) &&
          policy.state.pending_self.reason==Failure::none && old->retired && !old->repaired,
          "duplicate delivery does not consume again or replenish repair rights");
    check(!reference::admission_eligible(f->execution,0,63) &&
          reference::admission_eligible(f->execution,0,64),
          "shared executor retains failed source retirement until a new epoch");
    f->execution.retire(Failure::expired);f->in.frame.clock.round=64;refresh_quotes(*f);
    actual=observed(*f,0);
    const auto renewed=reference::choose_forecast_replacing_into(f->execution,actual,&f->quotes[0]);
    check(renewed && f->execution.active && f->execution.project==ProjectID{0,1},
          "fresh actual quote admits a new cycle without reopening the old epoch");
}
void witnessed_basic_appointment_caller() {
    auto f=fixture();
    struct LocalCamp {
        int id;Point spawn;double hp,strength;
        Point spawn_position() const {return spawn;}
        struct State {bool is_alive()const{return true;}bool is_awake()const{return false;}};
        State state()const{return {};}
    };
    const std::array<LocalCamp,3> members{{{100,{3,0},100,20},{101,{3,1},100,0},{102,{3,2},100,0}}};
    const std::array<Point,3> spawns{{{3,0},{3,1},{3,2}}};
    const std::array<int,3> ranges{1,3,1};
    const auto pads=runtime_catalogue::verification_pads<Point>(spawns,ranges,{0,1},[&](Point p) {
        if(p.x<0 || p.y<0 || p.x>8 || p.y>8)return false;
        for(const auto s:spawns)if(p==s)return false;
        return true;
    });
    check(pads.available && pads.pads[0]!=pads.pads[1] && pads.pads[0]!=pads.pads[2] &&
          pads.pads[1]!=pads.pads[2],"public gem geometry supplies three distinct legal whole-member pads");
    auto& in=f->in;auto& own=in.frame.own.actor;
    in.ordinal=own.pick_ordinal=2;own.type=witness::profile_value.roster[2];own.position=pads.pads[2];
    in.frame.actor_count=2;
    for(int i=0;i<2;++i) {
        auto ally=own;ally.id=10+i;ally.pick_ordinal=i;ally.type=witness::profile_value.roster[i];
        ally.position=pads.pads[i];ally.attack_range=ranges[i];
        in.frame.actors[i]=seen(ally,in.frame.clock,ally.id);
    }
    auto& site=in.sites[0];site.group={0,own.position,spawns[0],own.position,Lifecycle::living,true,true,true};
    auto& request=f->requests[0];request.groups[0].observed=seen(site.group,in.frame.clock,0);
    request.groups[0].member_count=3;
    for(int i=0;i<3;++i) {
        const MonsterWork value{members[i].id,members[i].hp,members[i].strength};
        request.groups[0].members[i].public_full={value,Source::public_map};
        request.groups[0].members[i].current=seen(value,in.frame.clock,members[i].id);
    }
    request.renewal_position=own.position;request.leg_count=0;
    auto leg=[&](Point a,Point b){request.legs[request.leg_count++]={a,b,
        {std::max(std::abs(a.x-b.x),std::abs(a.y-b.y)),Source::public_map}};};
    leg(own.position,own.position);leg(own.position,own.position);
    leg(own.position,request.bank_position);leg(request.bank_position,own.position);
    f->requests[1].legs[0].from=own.position;
    for(int i=0;i<2;++i) {
        f->quotes[i].evaluate(in.frame,f->requests[i]);auto& s=in.sites[i];s.actor_count=0;
        if(f->quotes[i].result().status==Status::feasible) {
            s.actor_count=1;s.actors[0]=own.id;s.cycles[0]=f->quotes[i].result().certificate;
            s.independent[0]=evaluate_schedule(f->quotes[i].result().schedule,in.windows,in.settings,
                {in.frame.clock.round,in.frame.clock.self},in.frame.own_team_score);
        }
    }
    check(f->quotes[0].result().issue==fc::Issue::unsafe && f->quotes[1].result().status==Status::feasible,
          "actual self evaluator rejects solo gem work while a real independent alternative remains");
    runtime_observation::CampSnapshot snapshot;
    check(snapshot.collect(members,in.frame.clock,[](const auto& m){return m.id;},
        [](const auto&){return 3;},[]{return true;},[](const auto& source,auto& reading) {
            reading.position=source.spawn;reading.health=reading.max_health=source.hp;reading.strength=source.strength;
        }),"same runtime snapshot collector records complete current selected members");
    std::uint8_t mask=0;
    for(int ordinal=0;ordinal<3;++ordinal) {
        const auto* actor=in.ordinal_actor(ordinal);
        if(actor && actor->can_attack && runtime_observation::whole_basic_members(members,
            [&](int id){return snapshot.find(in.frame.clock,id);},[&](Point p) {
                return std::max(std::abs(p.x-actor->position.x),std::abs(p.y-actor->position.y))<=actor->attack_range;
            }))mask|=static_cast<std::uint8_t>(1u<<ordinal);
    }
    check(mask==7,"real current snapshot and each actor's actual basic geometry verify all three core bits");
    in.proposed_group=0;in.independent_group=independent(*f);in.rally=own.position;in.at_rally=true;
    in.verification_travel=0;in.witness_core_basic=seen(mask,in.frame.clock,0);in.verification_model_complete=true;
    check(in.independent_group==1,"real independent selector retains the safe competing source");
    const auto owner=f->execution;witness::Policy policy;const auto decision=policy.choose(in,false);
    check(decision.intent==organization::Intent::approach && decision.explicit_waypoint &&
          decision.waypoint==own.position && policy.state.successful_core_verifications==1,
          "real Witness caller verifies a crew and stages without converting it to work authority");
    auto actual=observed(*f,0);
    check(!reference::choose_forecast_replacing_into(f->execution,actual,&f->quotes[0]) &&
          same_execution(f->execution,owner),"positive basic rendezvous cannot bind an unsafe solo quote or acquire ownership");

    // A separate current travelling frame uses the same public appointment and
    // actual alternatives. No successful movement or future crew work is assumed.
    auto travelling=fixture();travelling->in=in;travelling->requests=f->requests;
    auto& route=travelling->in;auto& walker=route.frame.own.actor;
    walker.position={route.rally.x+1,route.rally.y};walker.ready_attack_group=-1;
    route.at_rally=false;route.verification_travel=1;route.witness_core_basic={};
    const auto origin=walker.position;
    std::array<runtime_threat::Enemy,2> enemies{};
    auto& tavi=enemies[0];tavi.id=40;tavi.position={origin.x+7,origin.y};
    tavi.stress=200;tavi.move_speed=1;tavi.attack_range=3;tavi.ranged=true;tavi.complete=true;
    tavi.slots[0].reset=true;
    tavi.slots[1].harmful=true;tavi.slots[1].exclusive_movement=true;tavi.slots[1].range=4;
    auto& vendetta=enemies[1];vendetta.id=41;vendetta.position={origin.x+8,origin.y};
    vendetta.stress=200;vendetta.move_speed=1;vendetta.attack_range=1;vendetta.complete=true;
    vendetta.slots[0].reset=true;vendetta.slots[1].dash=true;vendetta.slots[1].range=2;
    route.local_enemy_count=route.enemy_stress_count=2;
    const std::array<Champion,2> types{Champion::tavi,Champion::vendetta};
    const std::array<double,2> strengths{50,70};
    for(int i=0;i<2;++i) {
        const auto& threat=enemies[i];
        route.local_enemies[i]=seen(threat,route.frame.clock,threat.id);
        route.enemy_stress[i]=seen(threat.stress,route.frame.clock,threat.id);
        auto enemy=walker;enemy.id=threat.id;enemy.type=types[i];enemy.team=Team::enemy;
        enemy.pick_ordinal=-1;enemy.position=threat.position;enemy.strength=strengths[i];
        enemy.attack_range=threat.attack_range;enemy.ranged=threat.ranged;
        route.frame.actors[2+i]=seen(enemy,route.frame.clock,enemy.id);
    }
    route.frame.actor_count=4;
    check(runtime_threat::max_reach(tavi,2)==5 && runtime_threat::max_reach(vendetta,2)==7,
          "actual Tavi and Vendetta kits bound two enemy turns without assuming cooldown or kill savings");
    const auto prepared=organization::prepare_immediate(route);
    check(prepared.complete && prepared.clock==route.frame.clock && prepared.count==2,
          "actual caller prepares the complete current raw-stress join once");
    auto cached_exposure=[&](Point position) {
        auto current=walker;current.position=position;
        return runtime_threat::exposure(current,route.frame.clock,prepared);
    };
    const auto same_exposure=[](const Exposure& a,const Exposure& b) {
        return a.health==b.health && a.max_health==b.max_health &&
            a.incoming_total==b.incoming_total && a.next_phase_incoming==b.next_phase_incoming &&
            a.own_timed_shield==b.own_timed_shield && a.assigned_timed_heal==b.assigned_timed_heal &&
            a.observations_complete==b.observations_complete;
    };
    const auto immediate=cached_exposure(origin);
    check(same_exposure(immediate,organization::immediate_exposure(route,origin)),
          "prepared current-point caller exposure matches the fresh wrapper path");
    route.safe_now=survives(immediate);
    check(!survives(organization::local_exposure(walker,400)) && route.safe_now &&
          immediate.observations_complete && immediate.incoming_total==0,
          "far visible enemies no longer force the real prepolicy safety diversion");

    // Full-cycle models still receive both complete enemy actors and their full
    // cast stress. Local reach is never relabelled as a production certificate.
    for(int i=0;i<2;++i) {
        auto& candidate=travelling->requests[i];candidate.legs[0].from=origin;
        const auto to=candidate.legs[0].to;
        candidate.legs[0].distance.value=std::max(std::abs(origin.x-to.x),std::abs(origin.y-to.y));
        candidate.enemy_cast_count=2;
        for(int enemy=0;enemy<2;++enemy)
            candidate.enemy_cast_stress[enemy]=seen(enemies[enemy].stress-strengths[enemy],
                route.frame.clock,enemies[enemy].id);
        travelling->quotes[i].evaluate(route.frame,candidate);
        route.sites[i].actor_count=0;
        if(travelling->quotes[i].result().issue!=fc::Issue::unsafe)
            std::fprintf(stderr,"threat caller alternative %d: status=%d issue=%d abort=%d\n",i,
                static_cast<int>(travelling->quotes[i].result().status),
                static_cast<int>(travelling->quotes[i].result().issue),
                static_cast<int>(travelling->quotes[i].result().abort));
        check(travelling->quotes[i].matches_frame(route.frame) &&
              travelling->quotes[i].result().issue==fc::Issue::unsafe,
              "each real alternative retains full enemy stress and is actually forecast-rejected");
    }
    route.independent_group=independent(*travelling);
    check(route.independent_group<0,"actual alternative selection invents no locally discounted full-cycle quote");
    witness::Policy travelling_policy;
    const auto approach=travelling_policy.choose(route,false);
    check(approach.intent==organization::Intent::approach && approach.group==0 &&
          approach.explicit_waypoint && approach.waypoint==route.rally &&
          travelling_policy.state.successful_core_verifications==0,
          "real Witness choice reaches its appointment without invented current crew contact");
    bool free_pad=approach.waypoint!=origin;
    for(const auto spawn:spawns)free_pad &= approach.waypoint!=spawn;
    for(int i=0;i<2;++i)free_pad &= approach.waypoint!=route.frame.actors[i].value.position;
    const auto destination=cached_exposure(approach.waypoint);
    check(same_exposure(destination,organization::immediate_exposure(route,approach.waypoint)),
          "same prepared caller snapshot matches wrapper exposure at the selected movement point");
    check(free_pad && std::max(std::abs(origin.x-approach.waypoint.x),
          std::abs(origin.y-approach.waypoint.y))==1 &&
          survives(destination),
          "current free adjacent appointment step passes the same production movement-safety predicate");
    auto production=observed(*travelling,approach.group);
    const auto unowned=travelling->execution;
    check(production.safe_now==route.safe_now &&
          !reference::choose_forecast_replacing_into(travelling->execution,production,
              &travelling->quotes[approach.group]) && same_execution(travelling->execution,unowned),
          "computed local safety reaches the real executor but cannot bind the unsafe source quote");
}
} // namespace
int main() {
    retired_policy_proposal(); atomic_replacement(); protected_stages(); completion_before_routing();
    epoch_admission_without_clone();empty_renewal_handoff();
    rejected_opportunity_rotates();deferred_opportunity_yields();
    exact_pad_inspection_budget();protected_opportunity_and_policy_restrictions();
    durable_self_failure_delivery();
    witnessed_basic_appointment_caller();
    std::printf("caller ownership: %d checks passed\n", checks);
}
