#pragma once
#include "profile.h"
#include "../../common/src/policy.h"
#include "../../earned-center/src/trace.h"
namespace p8::disagreement_partnership {
using common::Arm; using common::Offer; using common::Decision; using common::Rejection;
using CycleTrace=earned_center::CycleTrace;
struct Alternatives { p6::Schedule independent{},supported{}; };
struct Inputs:common::Inputs { std::array<Alternatives,2> models{}; };
struct Spec {
    Plan assay{},supported{};
    ContextDeclaration route{},organization{};
    std::array<Hypothesis,2> models{{{Mechanism::common_schedule,{0,0},all_premises},{Mechanism::reposition_two,{2,2},all_premises}}};
    int nominal_stage=0,nominal_model=0;
    // Zero disables paid support. When enabled this marks the entire actual
    // payout-to-bank-arrival leg, inclusive of its first and last owner turns.
    int paid_bank_turn_bound=0;
};
inline bool members_valid(const Plan& p,const DraftRoster& draft) {
    if (!draft.valid()||p.organization!=Organization::H||p.collector!=draft.actors[2]||p.crew_count<2||p.crew_count>3||
        common::participant(p,draft.actors[1])) return false;
    for (int i=0;i<p.crew_count;++i) { bool found=false; for (int j=0;j<draft.picks;++j) found|=p.crew[i]==draft.actors[j]; if (!found) return false; }
    return true;
}
inline bool covers_entry(const ContextDeclaration& c) {
    const auto& b=c.key.build; const auto& e=c.envelope;
    const double lower=c.key.hp_band==HealthBand::three_quarters_to_full?.75*b.max_health:
        c.key.hp_band==HealthBand::half_to_three_quarters?.5*b.max_health:std::nextafter(0.,std::numeric_limits<double>::infinity());
    const double upper=c.key.hp_band==HealthBand::three_quarters_to_full?b.max_health:
        std::nextafter((c.key.hp_band==HealthBand::half_to_three_quarters?.75:.5)*b.max_health,0.);
    return e.valid()&&e.health.lower<=lower&&e.health.upper>=upper&&e.max_health.contains(b.max_health)&&
        e.strength.contains(b.strength)&&e.focus.contains(b.focus)&&e.growth.contains(b.growth);
}
inline bool spec_valid(const Spec& s,const DraftRoster& draft) {
    if (!draft.valid()||s.nominal_stage<1||s.nominal_stage>120||s.nominal_model<0||s.nominal_model>1||
        s.assay.organization!=Organization::U||s.assay.collector!=draft.actors[2]||!plan_valid(s.assay,draft.actors[2])||
        !plan_valid(s.supported,draft.actors[2])||!members_valid(s.supported,draft)||
        s.route.key.actor_id!=draft.actors[2]||s.route.key.champion!=int(draft.champions[2])||!valid(s.route.key)||
        s.route.kind!=EpisodeKind::return_component||!covers_entry(s.route)||s.route.key.source_group!=s.assay.group||s.route.key.route_hash!=plan_route_hash(s.assay)||
        s.route.key.participant_count!=1||s.route.key.participants[0]!=draft.actors[2]||
        s.organization.kind!=EpisodeKind::full_organization||!s.organization.require_supported_work||
        !valid(s.organization.key)||s.organization.key.actor_id!=draft.actors[2]||
        s.organization.key.champion!=int(draft.champions[2])||s.organization.key.route_hash!=plan_route_hash(s.supported)||
        s.organization.key.source_group!=s.supported.group||s.organization.key.participant_count!=s.supported.crew_count||
        !route_component_matches(s.route.key,s.organization.key)) return false;
    for (int i=0;i<s.organization.key.participant_count;++i) if (!common::participant(s.supported,s.organization.key.participants[i])) return false;
    for (const auto& m:s.models) if (!m.offset.valid()||m.offset.upper>common::max_probe_turns||m.required_premises!=all_premises) return false;
    if (s.paid_bank_turn_bound<0||s.paid_bank_turn_bound>common::max_probe_turns||
        (s.paid_bank_turn_bound&&s.paid_bank_turn_bound<s.nominal_stage+s.models[1].offset.upper+1)) return false;
    return s.models[0].mechanism==Mechanism::common_schedule&&s.models[1].mechanism==Mechanism::reposition_two&&
        s.models[1].offset.lower==s.models[0].offset.lower+2&&s.models[1].offset.upper==s.models[0].offset.upper+2;
}
// A single route-stage observation, not a full-organization proof. It becomes
// usable only after the same productive Work converts its own actual payout
// and returns. There is no peer observation or ledger input.
struct Assay {
    bool attempted=false,active=false,attached=false,resolved=false,unavailable=false,model_set_failed=false;
    bool deliberate=false; int owner=-1,issue_round=-1,last_round=-1,payout_round=-1,arrival_round=-1,completed_round=-1,marked_turns=0;
    std::uint64_t generation=0; p6::ViewClock start{}; std::array<bool,2> consistent{true,true};
    Interval observed{}; Spec spec{};
    bool issue(const Spec& fixed,const Inputs& in,bool paid) {
        if (attempted||!common::valid_inputs(in)||!spec_valid(fixed,in.draft)||!investigator(in.frame.own.actor,in.draft)||(paid&&fixed.paid_bank_turn_bound<=0)||
            !common::guarded_key_current(in,fixed.route.key)||in.frame.own.actor.bag!=0||
            in.frame.own.actor.health<common::minimum_trial_health_fraction*in.frame.own.actor.max_health||
            !fixed.route.envelope.contains(in.own_state.value,fixed.route.key.build)) return false;
        attempted=active=true; deliberate=paid; owner=in.frame.clock.self; issue_round=in.frame.clock.round; start=in.frame.clock; spec=fixed; return true;
    }
    bool attach(const Work& work) {
        if (!active||attached||!work.active||!work.generation||work.started_clock!=start||work.last_round>=issue_round||
            work.productive_action_seen||work.payout_seen||work.bank_seen||plan_route_hash(work.plan)!=spec.route.key.route_hash) return false;
        generation=work.generation; attached=true; return true;
    }
    void fail() { if (attempted) { unavailable=true; active=false; } }
    void observe(const Inputs& in,const LocalFrame& local,const Work& work,const Command& command,ProbeBudget& probes) {
        if (!active) return;
        const int round=local.frame.clock.round;
        if (round==last_round) return;
        if (!attached||!common::valid_inputs(in)||!local.fresh()||!(local.frame.clock==in.frame.clock)||local.frame.clock.self!=owner||
            (last_round<0?round!=issue_round:round!=last_round+1)||work.generation!=generation||!(work.started_clock==start)||
            work.last_round!=round||!(command.clock==local.frame.clock)||command.token!=work.issued.token||work.pending||
            local.fuel_resumed||!work.route_witness_active||
            plan_route_hash(work.plan)!=spec.route.key.route_hash||
            !spec.route.envelope.contains(in.own_state.value,spec.route.key.build)||
            int(in.frame.own.actor.type)!=spec.route.key.champion||in.settings_identity.value!=spec.route.key.settings_id||
            in.threat_regime.value!=spec.route.key.threat_regime) { fail(); return; }
        last_round=round;
        if (work.payout_seen&&payout_round<0) payout_round=round;
        const bool bank_arrival=payout_round>=0&&work.tribute_started&&
            local.frame.own.actor.position==spec.assay.bank.tiles[spec.assay.bank.count-1];
        if (deliberate&&payout_round>=0&&arrival_round<0) {
            // Every actual owner turn is marked here: movement, waiting and
            // arrival all count, regardless of the emitted command kind.
            ++marked_turns; probes.observed_diagnostic_turns(marked_turns);
            if (marked_turns>spec.paid_bank_turn_bound||marked_turns>common::max_probe_turns||
                (marked_turns==spec.paid_bank_turn_bound&&!bank_arrival)) { fail(); return; }
        }
        if (payout_round>=0&&arrival_round<0&&bank_arrival) arrival_round=round;
        if (!work.active) {
            active=false;
            if (!work.cycle_completed||!work.productive_action_seen||!work.bank_seen||!work.renewal_seen||
                work.payout_lower<=0||work.bank_lower<=0||payout_round<issue_round||arrival_round<payout_round) { unavailable=true; return; }
            observed={arrival_round-payout_round,arrival_round-payout_round}; completed_round=round;
            for (int i=0;i<2;++i) {
                const Interval prediction{spec.nominal_stage+spec.models[i].offset.lower,spec.nominal_stage+spec.models[i].offset.upper};
                if (observed.disjoint(prediction)) consistent[i]=false;
            }
            model_set_failed=!consistent[0]&&!consistent[1]; resolved=!model_set_failed;
        }
    }
    bool current(const Inputs& in) const {
        return resolved&&!unavailable&&completed_round>=0&&in.frame.clock.round>=completed_round&&
            in.frame.clock.round-completed_round<qualification_rounds&&common::guarded_key_current(in,spec.route.key)&&
            spec.route.envelope.contains(in.own_state.value,spec.route.key.build);
    }
};
class Partnership {
    Spec spec_{}; int supported_=-1; bool declared_=false;
public:
    Assay assay{};
    bool declared() const { return declared_; }
    int slot() const { return supported_; }
    const Spec& spec() const { return spec_; }
    bool declare(const Spec& fixed,const DraftRoster& draft,EvidenceLedger& ledger) {
        if (declared_||ledger.active()||ledger.size()!=0||!spec_valid(fixed,draft)) return false;
        const int slot=ledger.predeclare(fixed.organization); if (slot<0) return false;
        spec_=fixed; supported_=slot; declared_=true; return true;
    }
};
inline bool crew_current(const Inputs& in,const Offer& o) {
    const auto& p=o.plan;
    if (!common::participant(p,in.frame.clock.self)) return false;
    for (int i=0;i<p.crew_count;++i) {
        const auto* a=in.frame.visible(p.crew[i]); const auto* safe=common::certificate(o,p.crew[i]);
        if (!a||!common::roster_actor(*a,in.draft)||!a->alive||a->channel!=p6::Channel::none||!safe||!safe->valid(in.frame,a->id,p.group)) return false;
    }
    if (p.organization==Organization::U||p.organization==Organization::G)
        return p.crew_count==1&&p.collector==in.frame.clock.self&&independent_safety(*common::certificate(o,in.frame.clock.self));
    return members_valid(p,in.draft);
}
inline int preferred(const Inputs& in,int model) {
    if (model<0||model>1) return -1;
    const auto& m=in.models[model];
    if (!p6::same_actors(m.independent,m.supported)||!p6::same_actors(m.independent,in.offers[in.independent].schedule)) return -1;
    const auto u=common::evaluate(in,m.independent),h=common::evaluate(in,m.supported);
    if (!u.eligible()||!h.eligible()) return -1;
    return p6::compare(h,u)>0?int(Organization::H):int(Organization::U);
}
template<class T> inline bool same_fact(const p6::Fact<T>& a,const p6::Fact<T>& b) { return a.value==b.value&&a.source==b.source; }
// Both explanations carry exactly the same actors, payloads and work. The
// delayed route adds its declared wait to each tribute, to the second job's
// production, and twice to final occupied/return time. It cannot manufacture
// a model disagreement by changing a camp reward or dropping displaced work.
inline bool route_models_match(const p6::Schedule& a,const p6::Schedule& b,int owner,int delay) {
    if (!p6::same_actors(a,b)||a.production_count!=b.production_count||a.target_id!=b.target_id||delay<=0||
        a.immediate_win.actor_id>=0||b.immediate_win.actor_id>=0||a.terminal_tiebreak.matched_opponent_continuation||b.terminal_tiebreak.matched_opponent_continuation) return false;
    int delayed_production=-1;
    for (int i=0;i<a.actor_count;++i) {
        const auto& x=a.actors[i]; const auto& y=b.actors[i]; const bool self=x.actor_id==owner;
        if (x.actor_id!=y.actor_id||x.event_count!=y.event_count||!same_fact(x.initial_bag,y.initial_bag)||!same_fact(x.initial_gold,y.initial_gold)||
            x.occupied_turns.source!=y.occupied_turns.source||y.occupied_turns.value!=x.occupied_turns.value+(self?2*delay:0)) return false;
        int productions=0,tributes=0;
        for (int e=0;e<x.event_count;++e) {
            const auto& u=x.events[e]; const auto& v=y.events[e]; int shift=0;
            if (u.kind==p6::EventKind::purchase||v.kind==p6::EventKind::purchase) return false; // Frozen gear, labor commitment only.
            if (self&&u.kind==p6::EventKind::production) { if (++productions==2) {shift=delay;delayed_production=u.production_index;} }
            if (self&&u.kind==p6::EventKind::tribute) shift=++tributes*delay;
            if (self&&u.kind==p6::EventKind::renewal) shift=2*delay;
            if (u.kind!=v.kind||u.event_id!=v.event_id||u.production_index!=v.production_index||u.waypoint_id!=v.waypoint_id||
                !same_fact(u.gold_cost,v.gold_cost)||u.source!=v.source||v.tick!=u.tick+shift) return false;
        }
        if (self&&(productions!=2||tributes!=2)) return false;
    }
    for (int i=0;i<a.production_count;++i) {
        const auto& x=a.productions[i]; const auto& y=b.productions[i];
        if (x.event_id!=y.event_id||y.tick!=x.tick+(i==delayed_production?delay:0)||!same_fact(x.gems,y.gems)||!same_fact(x.gold,y.gold)||
            x.holders!=y.holders||x.holder_count!=y.holder_count||x.source!=y.source) return false;
    }
    return delayed_production>=0;
}
inline bool matching_models(const Inputs& in,const Partnership& p) {
    if (!p.declared()) return false;
    const int delay=p.spec().models[1].offset.lower-p.spec().models[0].offset.lower;
    return in.models[0].independent.target_id==p.spec().assay.group&&in.models[0].supported.target_id==p.spec().supported.group&&
        route_models_match(in.models[0].independent,in.models[1].independent,in.draft.actors[2],delay)&&
        route_models_match(in.models[0].supported,in.models[1].supported,in.draft.actors[2],delay);
}
inline bool disagreement(const Inputs& in) {
    const int a=preferred(in,0),b=preferred(in,1); return a>=0&&b>=0&&a!=b;
}
inline Organization organization(const Inputs& in,const Partnership& p,Arm arm) {
    if (!p.declared()||!matching_models(in,p)) return Organization::U;
    if (arm==Arm::control) { const int result=preferred(in,p.spec().nominal_model); return result==int(Organization::H)?Organization::H:Organization::U; }
    if (!p.assay.current(in)||p.assay.consistent[0]==p.assay.consistent[1]) return Organization::U;
    return preferred(in,p.assay.consistent[0]?0:1)==int(Organization::H)?Organization::H:Organization::U;
}
} // namespace p8::disagreement_partnership
