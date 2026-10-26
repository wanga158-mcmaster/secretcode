#pragma once
#include "profile.h"
#include "../../common/src/policy.h"
namespace p8::circuitbreaker {
using common::Arm;using common::Offer;using common::Decision;using common::Admission;using common::Rejection;
enum class ChangeKind { equipment,route,membership };
struct ContextChange {
    ContextKey previous{},current{};
    ChangeKind kind=ChangeKind::route;
    int member=-1;
    // For route change this is the actual newly inspected retained route,
    // rather than a proposed detour or a round/epoch label.
    std::uint64_t inspected_route=0;
};
struct Inputs:common::Inputs {
    p6::Observation<bool> spawn_shop_legal{};
    std::array<p6::Observation<NumericState>,6> member_states{};
    p6::Observation<ContextChange> actual_change{};
};
struct Member {
    int actor=-1; p6::Champion champion=p6::Champion::other;
    EntryBuild build{};TransitionEnvelope envelope{};
};
struct Subsystem {
    Plan plan{};ContextDeclaration declaration{};
    std::array<Member,3> members{};
};
struct Package {
    Plan bootstrap{};ContextDeclaration ordinary{};
    // Exactly one initial subsystem and one predeclared changed context.
    std::array<Subsystem,2> systems{};
};
inline const p6::Observation<NumericState>* member_state(const Inputs& in,int id) {
    if(id==in.frame.clock.self)return &in.own_state;
    for(const auto& observed:in.member_states)if(observed.current(in.frame.clock,id))return &observed;
    return nullptr;
}
inline const Member* member(const Subsystem& s,int id) {
    for(const auto& m:s.members)if(m.actor==id)return &m;
    return nullptr;
}
inline bool subsystem_valid(const Subsystem& s,const DraftRoster& draft,int owner) {
    const auto& d=s.declaration;const auto& k=d.key;const auto& p=s.plan;
    if(!draft.valid()||!p8::valid(k)||k.actor_id!=owner||p.organization!=Organization::J||
        p.collector!=draft.actors[0]||p.crew_count!=3||!plan_valid(p,owner)||
        d.kind!=EpisodeKind::full_organization||!d.require_spawn_shop||!d.require_supported_work||
        k.participant_count!=3||k.source_group!=p.group||k.endpoint!=p.altar||
        k.route_hash!=plan_route_hash(p)||k.return_route_hash!=plan_return_hash(p))return false;
    for(int i=0;i<3;++i) {
        const auto& m=s.members[i];bool actual=false;
        for(int j=0;j<draft.picks;++j)actual|=m.actor==draft.actors[j]&&m.champion==draft.champions[j];
        if(!actual||crew_index(p,m.actor)<0||k.participants[i]!=m.actor||
            !m.build.equipment_hash||!m.envelope.health.valid()||!m.envelope.max_health.valid()||
            !m.envelope.strength.valid()||!m.envelope.focus.valid()||!m.envelope.growth.valid()||
            !m.envelope.bag.valid()||m.envelope.planned_equipment_hash)return false;
        if(m.actor==owner&&(m.build!=k.build||int(m.champion)!=k.champion))return false;
    }
    return true;
}
inline bool package_valid(const Package& p,const DraftRoster& draft) {
    const auto& k=p.ordinary.key;const auto& a=p.systems[0].declaration.key;
    if(!p8::valid(k)||!plan_valid(p.bootstrap,k.actor_id)||p.bootstrap.organization!=Organization::U||
        p.bootstrap.collector!=k.actor_id||p.bootstrap.crew_count!=1||k.participant_count!=1||
        k.participants[0]!=k.actor_id||k.source_group!=p.bootstrap.group||k.endpoint!=p.bootstrap.altar||
        k.route_hash!=plan_route_hash(p.bootstrap)||k.return_route_hash!=plan_return_hash(p.bootstrap)||
        p.ordinary.kind!=EpisodeKind::return_component||!p.ordinary.require_spawn_shop||
        p.ordinary.require_supported_work||k.champion!=a.champion||k.settings_id!=a.settings_id||
        k.build!=a.build||!subsystem_valid(p.systems[0],draft,k.actor_id)||
        !subsystem_valid(p.systems[1],draft,k.actor_id))return false;
    const auto& b=p.systems[1].declaration.key;
    if(a==b||a.settings_id!=b.settings_id||a.actor_id!=b.actor_id||a.champion!=b.champion||a.role!=b.role)return false;
    bool member_build_changed=false;
    for(const auto& m:p.systems[1].members) {
        const auto* old=member(p.systems[0],m.actor);
        member_build_changed|=old&&old->build.equipment_hash!=m.build.equipment_hash;
    }
    return a.route_hash!=b.route_hash||a.participants!=b.participants||member_build_changed;
}
inline bool members_current(const Inputs& in,const Subsystem& s,bool during_work=false) {
    for(const auto& m:s.members) {
        const auto* actor=in.frame.visible(m.actor);const auto* actual=member_state(in,m.actor);
        if(!actor||!roster_actor(*actor,in.draft)||!actor->alive||actor->type!=m.champion||
            !actual||!actual->current(in.frame.clock,m.actor)||!valid_numeric_state(actual->value)||
            actor->health!=actual->value.health||actor->max_health!=actual->value.max_health||
            actor->strength!=actual->value.strength||actor->bag!=actual->value.bag||
            !m.envelope.contains(actual->value,m.build))return false;
        if(!during_work&&m.build!=EntryBuild{actual->value.equipment_hash,actual->value.max_health,
            actual->value.strength,actual->value.focus,actual->value.growth})return false;
    }
    return true;
}
enum class Retirement { none,contradictions,safety,empty_model };
struct SubsystemState {
    int slot=-1;std::uint32_t seen_serial=0;
    std::array<bool,3> last_three{};int eligible_count=0,next=0;
    std::uint16_t trials=0;
    bool retired=false,material_changed=false;Retirement reason=Retirement::none;int retired_round=-1;
    void retire(Retirement why,int round) {
        if(!retired){retired=true;reason=why;retired_round=round;}
    }
};
struct Circuit {
    Package package{};int ordinary=-1,selected=0,bootstrap_round=-1;
    std::array<SubsystemState,2> systems{};
    bool declared=false,bootstrapped=false,requalification_used=false;
    ProbeBudget probes{};
    std::uint32_t bootstrap_serial=0;
    std::uint64_t recovery_generation=0;
    p6::ViewClock recovery_start{};
    std::uint16_t independent_renewals=0;
    std::uint64_t bootstrap_generation=0;p6::ViewClock bootstrap_start{};
    bool bootstrap_shop=false,bootstrap_trace_failed=false;
    bool declare(const Package& p,const DraftRoster& draft,EvidenceLedger& ledger) {
        if(declared||ledger.size()!=0||!package_valid(p,draft))return false;
        const int u=ledger.predeclare(p.ordinary),j=ledger.predeclare(p.systems[0].declaration),
            changed=ledger.predeclare(p.systems[1].declaration);
        if(u<0||j<0||changed<0)return false;
        package=p;ordinary=u;systems[0].slot=j;systems[1].slot=changed;declared=true;return true;
    }
    void synchronize(const EvidenceLedger& ledger) {
        if(!declared)return;
        const auto* u=ledger.state(ordinary);
        if(u)for(const auto& o:u->outcomes)if(o.episode.serial>bootstrap_serial&&o.full_positive) {
            bootstrap_serial=o.episode.serial;bootstrap_round=o.episode.issue_round;
            // latest_positive is an actual owned conversion plus renewal.
            bootstrap_round=u->latest_positive;bootstrapped=true;
        }
        for(auto& state:systems) {
            const auto* slot=ledger.state(state.slot);if(!slot)continue;
            state.trials=std::max(state.trials,slot->campaign_episodes);
            // At most four retained outcomes; process oldest first. A runtime
            // calls this at each terminal boundary, before another issue.
            for(int n=0;n<max_outcomes;++n) {
                const Outcome* next=nullptr;
                for(const auto& o:slot->outcomes)if(o.episode.serial>state.seen_serial&&
                    (!next||o.episode.serial<next->episode.serial))next=&o;
                if(!next)break;
                state.seen_serial=next->episode.serial;
                if(next->status==Status::safety_failed)state.retire(Retirement::safety,next->terminal_round);
                if(next->eligible&&next->applicable&&next->status==Status::completed) {
                    state.last_three[state.next]=next->contradiction;state.next=(state.next+1)%3;
                    state.eligible_count=std::min(3,state.eligible_count+1);
                    int count=0;for(bool x:state.last_three)count+=x;
                    if(count>=2)state.retire(Retirement::contradictions,next->terminal_round);
                }
            }
            if(slot->safety_suspended)state.retire(Retirement::safety,slot->latest_terminal);
            if(slot->model_set_failed)state.retire(Retirement::empty_model,slot->latest_terminal);
            state.material_changed|=slot->material_premise_changed;
        }
    }
    bool fresh_full(const EvidenceLedger& ledger,int which,int round) const {
        return declared&&which>=0&&which<2&&!systems[which].retired&&!systems[which].material_changed&&
            ledger.organization_qualified(systems[which].slot,package.systems[which].declaration.key,round);
    }
    bool may_trial(const EvidenceLedger& ledger,int which,int round) const {
        if(!declared||which!=selected||which<0||which>=2||systems[which].retired||systems[which].material_changed||
            !bootstrapped||round<bootstrap_round||
            ledger.suspended(systems[which].slot,round/common::project_rounds))return false;
        if(fresh_full(ledger,which,round))return true;
        const auto* slot=ledger.state(systems[which].slot);
        if(!slot||(!slot->campaign_started&&!which&&round-bootstrap_round>=qualification_rounds))return false;
        return systems[which].trials<2&&!ledger.campaign_exhausted(systems[which].slot);
    }
    void track_bootstrap(const Work& work) {
        if(!declared||bootstrapped||work.plan.organization!=Organization::U||
            plan_route_hash(work.plan)!=package.ordinary.key.route_hash)return;
        bootstrap_generation=work.generation;bootstrap_start=work.started_clock;
        bootstrap_shop=bootstrap_trace_failed=false;
    }
    void observe_bootstrap(const LocalFrame& local,const Work& work,const p6::Observation<bool>& shop) {
        if(!declared||bootstrapped||!bootstrap_generation||work.generation!=bootstrap_generation||
            work.started_clock!=bootstrap_start||!local.fresh()||work.last_round!=local.frame.clock.round)return;
        bootstrap_trace_failed|=local.fuel_resumed||!local.frame.own.actor.alive||!work.route_witness_active;
        if(work.bank_seen&&local.frame.own.at_spawn&&shop.current(local.frame.clock,local.frame.clock.self)&&shop.value)
            bootstrap_shop=true;
        if(!bootstrap_trace_failed&&bootstrap_shop&&work.cycle_completed&&work.renewal_seen&&
            work.productive_action_seen&&work.clearance_seen&&work.payout_seen&&work.bank_seen&&
            work.payout_lower>0&&work.bank_lower>0) {
            bootstrapped=true;bootstrap_round=local.frame.clock.round;
        }
    }
    // Recovery is a physical witness only. It confers no organization proof.
    // It observes this actor's authenticated completed U Work after retirement.
    void observe_independent(const LocalFrame& local,const Work& work) {
        bool stopped=false;for(const auto& s:systems)stopped|=s.retired&&work.started_clock.round>=s.retired_round;
        if(!stopped||!local.fresh()||work.plan.organization!=Organization::U||
            work.plan.collector!=local.frame.clock.self||!work.cycle_completed||!work.renewal_seen||
            !work.route_witness_active||!work.payout_seen||!work.bank_seen||work.payout_lower<=0||work.bank_lower<=0||
            work.last_round!=local.frame.clock.round||work.started_clock.self!=local.frame.clock.self||
            !local.frame.own.actor.alive||(recovery_generation==work.generation&&recovery_start==work.started_clock))return;
        recovery_generation=work.generation;recovery_start=work.started_clock;saturating_increment(independent_renewals);
    }
};
inline void bind(Inputs& in,const Circuit& circuit) {
    in.component_slot=circuit.ordinary;in.component_entry=circuit.package.ordinary.key;
    in.supported_slot=circuit.systems[circuit.selected].slot;
    in.supported_entry=circuit.package.systems[circuit.selected].declaration.key;
}
} // namespace p8::circuitbreaker
