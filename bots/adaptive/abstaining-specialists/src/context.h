#pragma once
#include "profile.h"
#include "../../reversible-workshop/src/transactions.h"
namespace p8::abstaining_specialists {
using common::Offer;using common::Arm;using common::Rejection;using common::Admission;
using reversible_workshop::Inventory;using reversible_workshop::Rebuild;
using reversible_workshop::TransactionCommand;using reversible_workshop::TransactionReceipt;
inline constexpr int damage_item=0,focus_item=2;
struct FocusPoint {std::int64_t bag=0;double focus=0;p6::Exposure cumulative{};};
struct LoadedModel {
    ContextKey context{};p6::Fact<double> focus_per_gem{};std::array<FocusPoint,3> points{};
};
struct Configuration {Plan plan{};ContextKey entry{};Inventory inventory{};int context=-1;};
struct PurchaseComparison {p6::Schedule changed{},unchanged{};};
struct Inputs:reversible_workshop::Inputs {
    std::array<p6::Observation<LoadedModel>,2> loaded{};
    std::array<p6::Observation<ContextDeclaration>,2> generalist_bound{};
    std::array<p6::Observation<LoadedModel>,2> generalist_loaded{};
    std::array<PurchaseComparison,3> purchases{}; // Focus, plain damage, damage retaining Focus.
    p6::Observation<Offer> existing_inventory{};
    p6::Observation<Offer> private_income{};
    // A current physical proposal: no history, qualification or gold is sent.
    p6::Observation<int> collector_proposal{};
    p6::Observation<bool> return_attribution_available{};
};
inline bool same_plan(const Plan& a,const Plan& b){return plan_route_hash(a)==plan_route_hash(b)&&a.organization==b.organization&&
    a.collector==b.collector&&a.altar==b.altar&&a.crew==b.crew&&a.crew_count==b.crew_count&&a.pads==b.pads&&
    a.forecast_rounds==b.forecast_rounds&&a.capture_window==b.capture_window;}
inline bool configuration_valid(const Configuration& c,int actor,Organization kind){
    if(!plan_valid(c.plan,actor)||c.plan.organization!=kind||c.plan.collector!=actor||!c.inventory.valid()||
        !valid(c.entry)||c.entry.actor_id!=actor||c.entry.route_hash!=plan_route_hash(c.plan)||
        c.entry.return_route_hash!=plan_return_hash(c.plan)||c.entry.source_group!=c.plan.group||
        c.entry.build.equipment_hash!=c.inventory.hash()||c.entry.participant_count!=c.plan.crew_count)return false;
    for(int i=0;i<c.entry.participant_count;++i)if(!common::participant(c.plan,c.entry.participants[i]))return false;
    return true;
}
inline bool exact(const Offer& o,const Configuration& c){return o.context_slot==c.context&&o.entry==c.entry&&same_plan(o.plan,c.plan);}
inline bool loaded_forecast(const Inputs& in,const p6::Observation<LoadedModel>& observed,const ContextDeclaration& declaration,const Plan& plan){
    if(!carry_actor(in.frame.own.actor,in.draft)||!observed.current(in.frame.clock,in.frame.clock.self)||
        observed.value.context!=declaration.key||declaration.key.route_hash!=plan_route_hash(plan)||
        declaration.key.actor_id!=plan.collector||observed.value.focus_per_gem.source!=p6::Source::queried_setting||
        !std::isfinite(observed.value.focus_per_gem.value)||observed.value.focus_per_gem.value<=0)return false;
    const auto& model=observed.value;const auto& e=declaration.envelope;const auto& b=declaration.key.build;
    if(model.points[0].bag!=0||model.points[1].bag<=0||model.points[2].bag!=0||
        !e.bag.contains_bag(model.points[1].bag)||(model.points[1].bag<std::numeric_limits<std::int64_t>::max()&&e.bag.contains_bag(model.points[1].bag+1)))return false;
    double previous=0;
    for(const auto& p:model.points){const auto& risk=p.cumulative;const double expected=b.focus+double(p.bag)*model.focus_per_gem.value;
        if(!std::isfinite(expected)||p.focus!=expected||!e.bag.contains_bag(p.bag)||!e.focus.contains(p.focus)||
            risk.health!=e.health.lower||risk.max_health!=b.max_health||risk.assigned_timed_heal!=0||risk.incoming_total<previous||!p6::survives(risk))return false;
        previous=risk.incoming_total;}
    return true;
}
inline bool loaded_now(const Inputs& in,const p6::Observation<LoadedModel>& model,const ContextDeclaration& declaration,const Plan& plan){
    const auto& state=in.own_state.value;const auto& build=declaration.key.build;
    return loaded_forecast(in,model,declaration,plan)&&declaration.envelope.contains(state,build)&&
        state.focus==build.focus+double(state.bag)*model.value.focus_per_gem.value;
}
struct PurchasePermit {
    bool frozen=false,used=false,abandoned=false;int proof_round=-1;ContextKey source{},target{};
    Inventory from{},to{};p6::Gems reserved_cash=0,cost=0;
};
struct Specialist {
    int owner=-1;std::array<Configuration,2> supported{}; // Cheap actual carry trial, distinct purchased outer H.
    std::array<Configuration,2> generalist{}; // Damage alone, or damage retaining already-owned Focus.
    Inventory original{};PurchasePermit permit{};Rebuild rebuild{};
    EntryBuild transaction_target{};int pending_purchase=-1;
    bool declared=false,trial_seen=false,abstaining=false,specialist_bought=false;
    explicit Specialist(int actor):owner(actor){}
    bool declare(const std::array<Configuration,2>& h,const std::array<Configuration,2>& g,const EvidenceLedger& ledger){
        if(declared||ledger.active()||h[0].entry.champion!=int(p6::Champion::prism))return false;
        for(const auto& c:h){const auto* slot=ledger.state(c.context);if(!configuration_valid(c,owner,Organization::H)||!slot||slot->attempts||
            slot->declaration.key!=c.entry||slot->declaration.kind!=EpisodeKind::full_organization||!slot->declaration.require_supported_work)return false;}
        for(const auto& c:g)if(!configuration_valid(c,owner,Organization::G))return false;
        auto focus=h[0].inventory;++focus.count[focus_item];auto damage=h[0].inventory;++damage.count[damage_item];
        auto retained=focus;++retained.count[damage_item];
        if(focus!=h[1].inventory||damage!=g[0].inventory||retained!=g[1].inventory||h[0].plan.group==h[1].plan.group||
            g[0].plan.group==h[1].plan.group||g[1].plan.group!=g[0].plan.group||
            h[0].entry.build.max_health!=h[1].entry.build.max_health||h[0].entry.build.strength!=h[1].entry.build.strength||
            h[1].entry.build.focus<=h[0].entry.build.focus||g[0].entry.build.focus!=h[0].entry.build.focus||
            g[1].entry.build.focus!=h[1].entry.build.focus||g[0].entry.build.strength<=h[0].entry.build.strength||
            g[1].entry.build.strength!=g[0].entry.build.strength)return false;
        supported=h;generalist=g;original=h[0].inventory;declared=true;return true;
    }
};
inline bool local_ledger(const Inputs& in,const Specialist& state,const EvidenceLedger& ledger){
    if(!state.declared||state.owner!=in.frame.clock.self)return false;
    for(const auto& c:state.supported){const auto* slot=ledger.state(c.context);if(!slot||slot->declaration.key!=c.entry||c.entry.actor_id!=state.owner)return false;}
    return true;
}
inline bool qualified(const Inputs& in,const EvidenceLedger& ledger,const Configuration& c){return common::guarded_key_current(in,c.entry)&&
    ledger.organization_qualified(c.context,c.entry,in.frame.clock.round);}
inline bool permit_current(const Inputs& in,const Specialist& state){const auto& p=state.permit;
    return p.frozen&&!p.used&&!p.abandoned&&p.source==state.supported[0].entry&&p.target==state.supported[1].entry&&
        p.from==state.original&&p.to==state.supported[1].inventory&&in.frame.clock.round>=p.proof_round&&in.frame.clock.round-p.proof_round<qualification_rounds;
}
inline bool generalist_now(const Inputs& in,const Specialist& state,const Plan& plan){
    if(!carry_actor(in.frame.own.actor,in.draft))return true;
    if(!state.declared||state.owner!=in.frame.clock.self)return false;
    for(int i=0;i<2;++i){if(!in.generalist_bound[i].current(in.frame.clock,state.owner)||!in.generalist_loaded[i].current(in.frame.clock,state.owner))continue;
        const auto& d=in.generalist_bound[i].value;const auto& key=d.key;
        bool named=false;for(const Configuration* c:{&state.supported[0],&state.supported[1],&state.generalist[0],&state.generalist[1]})named|=key.build==c->entry.build;
        const double band_lower=key.hp_band==HealthBand::three_quarters_to_full?.75:key.hp_band==HealthBand::half_to_three_quarters?.5:0;
        if(named&&valid(key)&&key.settings_id==in.settings_identity.value&&key.threat_regime==in.threat_regime.value&&
            key.actor_id==state.owner&&key.champion==int(p6::Champion::prism)&&key.source_group==plan.group&&
            key.route_hash==plan_route_hash(plan)&&key.return_route_hash==plan_return_hash(plan)&&key.endpoint==plan.altar&&
            key.participant_count==1&&key.participants[0]==state.owner&&d.envelope.health.lower<=band_lower*key.build.max_health&&
            loaded_now(in,in.generalist_loaded[i],d,plan))return true;
    }
    return false;
}
}
