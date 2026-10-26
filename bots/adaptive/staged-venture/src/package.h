#pragma once
#include "profile.h"
#include "../../common/src/policy.h"
namespace p8::staged_venture {
using common::Arm;using common::Offer;using common::Decision;using common::Admission;using common::Rejection;
enum class VenturePhase { preliminary,purchase_pending,commissioning,retired };
struct Inputs:common::Inputs {
    p6::Observation<bool> spawn_shop_legal{};
    // Personally visible current gear/stats; contains no foreign gold or proof.
    p6::Observation<NumericState> collector_state{};
};
struct Package {
    Plan preliminary{},supported{};
    ContextDeclaration ordinary{},invested{};
    int bundle=1; // Common S-first menu, one legal shard in this bounded package.
    p6::Fact<p6::Gems> cost{};
    p6::Fact<double> health_gain{};
};
inline bool tests_decisive_return(const Package& p) {
    const auto& a=p.ordinary.key;const auto& b=p.invested.key;
    return plan_valid(p.preliminary,a.actor_id)&&plan_valid(p.supported,b.actor_id)&&
        p.preliminary.organization==Organization::U&&p.supported.organization==Organization::H&&
        p.preliminary.collector==a.actor_id&&p.supported.collector==a.actor_id&&
        a.actor_id==b.actor_id&&a.champion==b.champion&&a.settings_id==b.settings_id&&
        a.source_group==b.source_group&&a.endpoint==b.endpoint&&a.route_template==b.route_template&&
        a.threat_regime==b.threat_regime&&a.return_route_hash&&a.return_route_hash==b.return_route_hash&&
        a.return_route_hash==plan_return_hash(p.preliminary)&&b.return_route_hash==plan_return_hash(p.supported)&&
        a.route_hash==plan_route_hash(p.preliminary)&&b.route_hash==plan_route_hash(p.supported)&&
        a.source_group==p.preliminary.group&&b.source_group==p.supported.group&&
        a.endpoint==p.preliminary.altar&&b.endpoint==p.supported.altar&&
        p.ordinary.kind==EpisodeKind::return_component&&p.invested.kind==EpisodeKind::full_organization&&
        p.ordinary.require_spawn_shop&&p.invested.require_spawn_shop&&p.invested.require_supported_work;
}
inline bool package_valid(const Package& p,const DraftRoster& draft) {
    if (!draft.valid()||p.bundle!=1||p.cost.source!=p6::Source::queried_setting||p.cost.value<=0||
        p.health_gain.source!=p6::Source::queried_setting||!std::isfinite(p.health_gain.value)||p.health_gain.value<=0||
        !tests_decisive_return(p))return false;
    const auto& a=p.ordinary.key;const auto& b=p.invested.key;
    if(a.actor_id!=draft.actors[1]||a.champion!=int(draft.champions[1])||
        a.participant_count!=1||a.participants[0]!=a.actor_id||p.preliminary.crew_count!=1||
        p.supported.crew_count!=2||crew_index(p.supported,draft.actors[0])<0||
        crew_index(p.supported,draft.actors[1])<0||b.participant_count!=2)return false;
    for(int i=0;i<b.participant_count;++i)if(crew_index(p.supported,b.participants[i])<0)return false;
    return a.build.equipment_hash&&b.build.equipment_hash&&a.build.equipment_hash!=b.build.equipment_hash&&
        b.build.max_health==a.build.max_health+p.health_gain.value&&b.build.strength==a.build.strength&&
        b.build.focus==a.build.focus&&b.build.growth==a.build.growth;
}
struct PurchaseComparison {
    p6::Schedule funded{},ordinary{};
    p6::Observation<bool> item_legal{};
    std::array<p6::Exposure,4> new_own_survival{};
    p6::CycleCertificate helper_safety{};
};
struct PurchaseCommand {
    bool valid=false;
    p6::ViewClock clock{};
    std::uint32_t token=0;
    int bundle=-1;
    p6::Gems cost=0,gold_before=0;
    NumericState before{};
};
struct Venture {
    Package package{};
    VenturePhase phase=VenturePhase::preliminary;
    int component=-1,organization=-1,proof_round=-1;
    std::uint32_t reservation=0,next=1;
    PurchaseCommand issued{};
    std::array<p6::Exposure,4> purchase_safety{};
    bool declared=false,paid=false;
    bool declare(const Package& value,const DraftRoster& draft,EvidenceLedger& ledger) {
        if(declared||!package_valid(value,draft))return false;
        const int a=ledger.predeclare(value.ordinary),b=ledger.predeclare(value.invested);
        if(a<0||b<0)return false;
        package=value;component=a;organization=b;declared=true;return true;
    }
    bool component_current(const Inputs& in,const EvidenceLedger& ledger) const {
        return declared&&tests_decisive_return(package)&&common::slot_current(in,ledger,component,package.ordinary.key)&&
            ledger.component_qualified(component,package.ordinary.key,in.frame.clock.round);
    }
    bool ready(const Inputs& in,const EvidenceLedger& ledger,Arm arm,bool ongoing=false) const {
        if(!declared||!paid||phase!=VenturePhase::commissioning)return false;
        const auto* episode=ledger.active();const auto* slot=ledger.state(organization);
        const auto& key=package.invested.key;
        const bool represented=ongoing&&episode&&episode->context==organization&&slot&&
            key.actor_id==in.frame.clock.self&&key.champion==int(in.frame.own.actor.type)&&
            key.settings_id==in.settings_identity.value&&key.threat_regime==in.threat_regime.value&&
            slot->declaration.envelope.contains(in.own_state.value,key.build);
        if(!represented&&!common::slot_current(in,ledger,organization,key))return false;
        if(arm==Arm::control)return true;
        const bool qualified=ledger.organization_qualified(organization,package.invested.key,in.frame.clock.round);
        return !ledger.suspended(organization,in.frame.clock.round/common::project_rounds)&&
            (qualified||(!ledger.campaign_exhausted(organization)&&proof_round>=0&&
                in.frame.clock.round-proof_round<common::qualification_rounds));
    }
    void retire(EvidenceLedger& ledger) {
        if(reservation)ledger.abandon_declared_rebuild(reservation);
        phase=VenturePhase::retired;
    }
};
inline void bind(Inputs& in,const Venture& venture) {
    if(!venture.declared||!venture_actor(in.frame.own.actor,in.draft))return;
    in.component_slot=venture.component;in.supported_slot=venture.organization;
    in.component_entry=venture.package.ordinary.key;in.supported_entry=venture.package.invested.key;
}
inline bool helper_present(const Inputs& in,const Package& package,const p6::CycleCertificate& safe) {
    const int id=in.draft.actors[0];const auto* helper=in.frame.visible(id);
    return helper&&helper_actor(*helper,in.draft)&&helper->alive&&helper->channel==p6::Channel::none&&
        safe.actor==id&&safe.group==package.supported.group&&safe.valid(in.frame,id,safe.group);
}
inline PurchaseCommand propose_purchase(const Inputs& in,const LocalFrame& local,const Work& work,Venture& venture,
                                      EvidenceLedger& ledger,Arm arm,const PurchaseComparison& comparison) {
    if(!common::valid_inputs(in)||!venture_actor(in.frame.own.actor,in.draft)||!venture.declared||
        venture.phase!=VenturePhase::preliminary||!venture.next||ledger.active()||work.active||
        !local.fresh()||local.frame.clock!=in.frame.clock||!local.legal.value.safe_now||
        !in.frame.own.at_spawn||in.frame.own.actor.bag!=0||in.frame.own.actor.channel!=p6::Channel::none||
        !in.spawn_shop_legal.current(in.frame.clock,in.frame.clock.self)||!in.spawn_shop_legal.value||
        !common::guarded_key_current(in,venture.package.ordinary.key)||
        (arm==Arm::active&&!venture.component_current(in,ledger))||
        venture.package.cost.value>in.frame.own.gold||
        !comparison.item_legal.current(in.frame.clock,in.frame.clock.self)||!comparison.item_legal.value||
        !helper_present(in,venture.package,comparison.helper_safety)||
        !p6::same_actors(comparison.funded,comparison.ordinary))return {};
    const auto funded=common::evaluate(in,comparison.funded),ordinary=common::evaluate(in,comparison.ordinary);
    if(!funded.eligible()||!ordinary.eligible()||p6::compare(funded,ordinary)<=0)return {};
    const auto& before=in.own_state.value;const auto& after=venture.package.invested.key.build;
    const double health=std::min(after.max_health,before.health+venture.package.health_gain.value);
    double previous=0;
    for(const auto& exposure:comparison.new_own_survival) {
        if(exposure.health!=health||exposure.max_health!=after.max_health||exposure.assigned_timed_heal!=0||
            exposure.incoming_total<previous||!p6::survives(exposure))return {};
        previous=exposure.incoming_total;
    }
    int purchases=0;
    for(int a=0;a<comparison.funded.actor_count;++a)for(int e=0;e<comparison.funded.actors[a].event_count;++e) {
        const auto& event=comparison.funded.actors[a].events[e];if(event.kind!=p6::EventKind::purchase)continue;
        if(comparison.funded.actors[a].actor_id!=in.frame.clock.self||event.tick!=0||
            !event.gold_cost.known()||event.gold_cost.value!=venture.package.cost.value)return {};
        ++purchases;
    }
    for(int a=0;a<comparison.ordinary.actor_count;++a)for(int e=0;e<comparison.ordinary.actors[a].event_count;++e)
        if(comparison.ordinary.actors[a].events[e].kind==p6::EventKind::purchase)return {};
    if(purchases!=1)return {};
    if(arm==Arm::active) {
        venture.reservation=ledger.reserve_declared_rebuild(venture.component,venture.organization,
            in.frame.clock.round,venture.package.invested.key);
        if(!venture.reservation)return {};
        venture.proof_round=ledger.state(venture.component)->latest_positive;
    }
    venture.issued={true,in.frame.clock,venture.next++,venture.package.bundle,venture.package.cost.value,
        in.frame.own.gold,before};
    venture.purchase_safety=comparison.new_own_survival;venture.phase=VenturePhase::purchase_pending;
    return venture.issued;
}
inline bool confirm_purchase(Inputs& after,const PurchaseCommand& command,Venture& venture,
                             EvidenceLedger& ledger,Arm arm,const p6::CycleCertificate& helper_safety) {
    const auto& issued=venture.issued;const auto& expected=venture.package.invested.key.build;
    const auto& own=after.own_state.value;
    if(venture.phase!=VenturePhase::purchase_pending||!command.valid||command.token!=issued.token||
        command.clock!=issued.clock||command.bundle!=issued.bundle||command.cost!=issued.cost||
        !common::valid_inputs(after)||after.frame.clock.self!=issued.clock.self||
        after.frame.clock.round!=issued.clock.round||after.frame.clock.revision<=issued.clock.revision||
        !after.frame.own.at_spawn||!after.spawn_shop_legal.current(after.frame.clock,after.frame.clock.self)||
        !after.spawn_shop_legal.value||after.frame.own.gold!=issued.gold_before-issued.cost||
        own.bag!=0||own.equipment_hash!=expected.equipment_hash||own.max_health!=expected.max_health||
        own.health!=std::min(expected.max_health,issued.before.health+venture.package.health_gain.value)||
        own.strength!=expected.strength||own.focus!=expected.focus||own.growth!=expected.growth||
        !common::guarded_key_current(after,venture.package.invested.key)||
        !helper_present(after,venture.package,helper_safety)) {
        venture.retire(ledger);return false;
    }
    bind(after,venture);common::refresh_evidence(after,ledger);
    if(arm==Arm::active&&!ledger.begin_after_declared_rebuild(venture.reservation,after.frame.clock.round,
        {{venture.package.invested.key,own,all_premises},true,true,true,true,true})) {
        venture.retire(ledger);return false;
    }
    venture.paid=true;venture.phase=VenturePhase::commissioning;return true;
}
inline void failed_purchase(Venture& venture,EvidenceLedger& ledger) {venture.retire(ledger);}
} // namespace p8::staged_venture
