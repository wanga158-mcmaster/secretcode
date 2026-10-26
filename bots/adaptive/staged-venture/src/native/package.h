#pragma once
#include "../policy.h"
#include "../../../common/src/coalition.h"
#include "../../../common/src/sdk_shop.h"

namespace p8::staged_venture::native {
namespace paths=p8::native;
using Inventory=reversible_workshop::Inventory;
inline constexpr int survival_item=reversible_workshop::survival_item;
#ifndef P8_CONTROL
#define P8_CONTROL 0
#endif
inline constexpr Arm arm=P8_CONTROL?Arm::control:Arm::active;
inline Inventory survival_inventory(){Inventory i;i.count[survival_item]=1;return i;}
// Geometry alone is reordered. Inputs retain the real draft ordinals, and no
// foreign purse, package, or evidence ledger is read.
inline DraftRoster geometry_roster(DraftRoster draft) {
    if(draft.picks>=3) {
        std::swap(draft.actors[0],draft.actors[1]);std::swap(draft.champions[0],draft.champions[1]);
        std::swap(draft.actors[1],draft.actors[2]);std::swap(draft.champions[1],draft.champions[2]);
    }
    return draft;
}
struct Projection {NumericState ordinary{},invested{};bool valid=false;};
// One plain shard, with queried design arithmetic performed as engine floats.
// Unsupported passives or current dynamic stat regimes remain unavailable.
inline Projection project(const cambc::champion& actor,const NumericState& actual,bool already_invested) {
    Projection out;const auto base=actor.base_stats();
    const auto items=actor.item_count();
    for(int item=0;item<reversible_workshop::item_types;++item)
        if(items[static_cast<cambc::item_type>(item)]!=std::uint32_t(already_invested&&item==survival_item))return out;
    const auto info=static_cast<cambc::item_type>(survival_item).info();
    if(!info.purchasable||info.passive||(info.combine_count&&info.combine_count<=1)||
        info.cost<=0||info.health_points<=0||info.strength_points||info.focus_points)return out;
    const float maximum=base.base_health+float(info.health_points)*base.health_growth;
    if(actual.equipment_hash!=(already_invested?survival_inventory().hash():Inventory{}.hash())||
        actual.max_health!=double(already_invested?maximum:base.base_health)||
        actual.strength!=double(base.base_strength)||actual.focus!=double(base.base_focus)||
        actual.growth!=double(base.strength_growth))return out;
    out.ordinary=actual;out.ordinary.max_health=base.base_health;out.ordinary.health=base.base_health;
    out.ordinary.equipment_hash=Inventory{}.hash();out.ordinary.bag=0;
    out.invested=out.ordinary;out.invested.max_health=maximum;out.invested.health=maximum;
    out.invested.equipment_hash=survival_inventory().hash();
    out.valid=valid_numeric_state(out.ordinary)&&valid_numeric_state(out.invested)&&maximum>base.base_health;return out;
}
inline p6::Frame with_state(p6::Frame frame,const NumericState& state) {
    frame.own.actor.health=state.health;frame.own.actor.max_health=state.max_health;
    frame.own.actor.strength=state.strength;return frame;
}
inline ContextDeclaration declaration(const Plan& plan,const NumericState& state,const Inputs& in,bool invested,p6::Gems gems) {
    ContextDeclaration d;auto& k=d.key;k.settings_id=in.settings_identity.value;
    k.route_hash=plan_route_hash(plan);k.return_route_hash=plan_return_hash(plan);
    k.actor_id=plan.collector;k.champion=int(in.draft.champions[1]);k.role=invested?1:0;
    k.source_group=plan.group;k.route_template=0;k.endpoint=plan.altar;k.threat_regime=in.threat_regime.value;
    k.participants=plan.crew;k.participant_count=plan.crew_count;
    std::sort(k.participants.begin(),k.participants.begin()+k.participant_count);
    k.build={state.equipment_hash,state.max_health,state.strength,state.focus,state.growth};
    k.hp_band=health_band(state.health,state.max_health);
    const double bag_bound=double(std::max<p6::Gems>(cambc::constants::winning_gems,gems));
    const double maximum_focus=state.focus+(in.draft.champions[1]==p6::Champion::prism?
        double(cambc::constants::passive_abilities::prism_focus_per_gem)*bag_bound:0.);
    // No casts are forecast. Prism's actual bag-dependent Focus is a declared
    // transition inside the same build, never a free combat or income credit.
    d.envelope={{std::nextafter(0.,1.),state.max_health},{state.max_health,state.max_health},
        {state.strength,state.strength},{state.focus,maximum_focus},{state.growth,state.growth},
        {0,bag_bound},0};
    d.kind=invested?EpisodeKind::full_organization:EpisodeKind::return_component;
    d.require_spawn_shop=true;d.require_supported_work=invested;return d;
}
inline bool independent_offer(Offer& offer,const paths::Solo& own,std::span<const paths::Solo> baselines,const p6::Frame& frame) {
    offer={};offer.plan=own.plan;offer.schedule.target_id=own.plan.group;int prefix=0;bool self=false;
    for(const auto& baseline:baselines) {
        if(!baseline.feasible||offer.safety_count==p6::max_actors)return false;
        auto current=baseline;paths::normalize(current,frame);
        self|=current.model.schedule.actors[0].actor_id==frame.clock.self;
        if(!paths::append(offer.schedule,current.model.schedule,0,prefix))return false;
        offer.safety[offer.safety_count++]=current.model.certificate;prefix+=64;
    }
    return self&&paths::queue_alternatives(offer,baselines,frame);
}
// Each fixed spawn-entry H recurrence pays recall, full healing, arrival,
// within-spawn alignment (at most eight steps), and one purchase/setup turn.
// These are preparation costs, never additional renewal/evidence receipts.
inline int preparation() {
    const double healing=cambc::constants::spawn_healing;
    if(!std::isfinite(healing)||healing<=0||cambc::constants::recall_time<=0)return -1;
    const int recovery=int(std::ceil(1./std::min(1.,healing)));
    return recovery<=8?int(cambc::constants::recall_time)+recovery+2+8:-1;
}
inline bool preparation_safe(Offer& offer,const p6::Frame& frame,int turns) {
    if(turns<0)return false;double ambient=0;
    for(const auto& camp:cambc::vision::camps())if(camp.is_alive()&&camp.is_awake())ambient+=camp.strength();
    for(const auto& enemy:cambc::vision::champions(cambc::team::enemy)) {
        double cast=0;for(const auto& ability:enemy.abilities())
            if(ability.info()&&(ability.is_damage()||ability.is_aoe_damage()||ability.is_cc()))cast=std::max(cast,double(ability.damage_amount()));
        ambient+=enemy.strength()+cast;
    }
    const double extra=ambient*turns;if(!std::isfinite(extra))return false;
    for(int i=0;i<offer.safety_count;++i) {
        auto& c=offer.safety[i];for(auto* e:{&c.clear,&c.hold,&c.bank,&c.recovery})e->incoming_total+=extra;
        if(!c.valid(frame,c.actor,c.group))return false;
    }
    return offer.safety_count>0;
}
inline bool delayed(p6::Schedule& schedule,int turns) {
    if(turns<0)return false;
    for(int p=0;p<schedule.production_count;++p)schedule.productions[p].tick+=turns;
    for(int a=0;a<schedule.actor_count;++a) {
        schedule.actors[a].occupied_turns.value+=turns;
        for(int e=0;e<schedule.actors[a].event_count;++e)schedule.actors[a].events[e].tick+=turns;
    }
    return true;
}
inline bool comparison(PurchaseComparison& out,const Inputs& in,const Offer& future,const Offer& ordinary,
                       p6::Gems cost,const p6::Observation<bool>& legal,bool buying) {
    if(!p6::same_actors(future.schedule,ordinary.schedule)||cost<0)return false;
    out={};out.funded=future.schedule;out.ordinary=ordinary.schedule;out.item_legal=legal;
    if(!delayed(out.funded,preparation()))return false;
    const auto* own=common::certificate(future,in.frame.clock.self);
    const auto* helper=common::certificate(future,in.draft.actors[0]);
    if(!own||!helper)return false;
    out.new_own_survival={own->clear,own->hold,own->bank,own->recovery};out.helper_safety=*helper;
    if(buying) {
        const int a=p6::actor_index(out.funded,in.frame.clock.self);if(a<0)return false;
        auto& actor=out.funded.actors[a];if(actor.event_count==p6::max_actor_events)return false;
        for(int e=actor.event_count;e>0;--e)actor.events[e]=actor.events[e-1];
        actor.events[0]={p6::EventKind::purchase,32000,0,-1,{cost,p6::Source::queried_setting},-1,p6::Source::scheduled};++actor.event_count;
    }
    return common::evaluate(in,out.funded).eligible()&&common::evaluate(in,out.ordinary).eligible()&&
        p6::compare(common::evaluate(in,out.funded),common::evaluate(in,out.ordinary))>0;
}
} // namespace p8::staged_venture::native
