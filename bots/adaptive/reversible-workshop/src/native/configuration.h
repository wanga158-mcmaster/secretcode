#pragma once
#include "../policy.h"
#include "menu_storage.h"
#include "../../../common/src/coalition.h"
#include "../../../common/src/sdk_shop.h"

namespace p8::reversible_workshop::native {
namespace paths=p8::native;
#ifndef P8_CONTROL
#define P8_CONTROL 0
#endif
inline constexpr Arm arm=P8_CONTROL?Arm::control:Arm::active;
inline Inventory damage_inventory(){Inventory i;i.count[damage_item]=1;return i;}
inline Inventory survival_inventory(){Inventory i;i.count[survival_item]=1;return i;}
struct Projection { NumericState state{};p6::Frame frame{};bool valid=false; };
// The bounded pair contains one plain shard per configuration, no emblem or
// passive. Float arithmetic mirrors the queried champion design before its
// result is lifted to double. Actual post-transaction stats must match again.
inline Projection project(const sdk::Snapshot& snapshot,const Inventory& current,const Inventory& desired) {
    Projection out;
    if(!snapshot.valid||snapshot.frame.own.actor.type!=p6::Champion::tavi||
        !((current==damage_inventory()||current==survival_inventory())&&
          (desired==damage_inventory()||desired==survival_inventory()))||
        snapshot.own.equipment_hash!=current.hash())return out;
    const auto base=cambc::self().base_stats();
    auto stats=[&](const Inventory& inventory) {
        std::array<float,3> result{base.base_health,base.base_strength,base.base_focus};
        for(int item:{damage_item,survival_item})if(inventory.count[item]) {
            const auto info=static_cast<cambc::item_type>(item).info();
            if(!info.purchasable||info.passive||(info.combine_count&&info.combine_count<=1))return std::array<float,3>{};
            result[0]+=float(info.health_points)*base.health_growth;
            result[1]+=float(info.strength_points)*base.strength_growth;
            result[2]+=float(info.focus_points)*base.focus_growth;
        }
        return result;
    };
    const auto before=stats(current),after=stats(desired);
    if(snapshot.own.max_health!=double(before[0])||snapshot.own.strength!=double(before[1])||
        snapshot.own.focus!=double(before[2])||snapshot.own.growth!=double(base.strength_growth)||after[0]<=0)return out;
    out.state=snapshot.own;out.state.max_health=after[0];out.state.strength=after[1];out.state.focus=after[2];
    out.state.equipment_hash=desired.hash();
    // Preparation requires actual full recovery at spawn before transacting.
    // The Health shard then heals its added capacity under that current rule.
    out.state.health=std::min(out.state.max_health,snapshot.own.health+std::max(0.,out.state.max_health-snapshot.own.max_health));
    out.frame=snapshot.frame;auto& actor=out.frame.own.actor;
    actor.health=out.state.health;actor.max_health=out.state.max_health;actor.strength=out.state.strength;
    out.valid=valid_numeric_state(out.state);return out;
}
inline double public_work(const paths::Site& site) {
    double work=0;for(const auto& camp:site.group)work+=double(camp.base_stats().base_health);return work;
}
inline ContextDeclaration declare(const Plan& plan,const NumericState& state,const Inputs& in,bool supported,p6::Gems source_gems) {
    ContextDeclaration d;auto& key=d.key;key.settings_id=in.settings_identity.value;
    key.route_hash=plan_route_hash(plan);key.return_route_hash=plan_return_hash(plan);
    key.actor_id=in.frame.clock.self;key.champion=int(in.frame.own.actor.type);key.role=supported?1:0;
    key.source_group=plan.group;key.route_template=supported?1:0;key.endpoint=plan.altar;
    key.threat_regime=in.threat_regime.value;key.participants=plan.crew;key.participant_count=plan.crew_count;
    std::sort(key.participants.begin(),key.participants.begin()+key.participant_count);
    key.build={state.equipment_hash,state.max_health,state.strength,state.focus,state.growth};
    key.hp_band=health_band(state.health,state.max_health);
    // Current plain Tavi shards have no bag-dependent Focus or passive.
    d.envelope={{std::nextafter(0.,1.),state.max_health},{state.max_health,state.max_health},
        {state.strength,state.strength},{state.focus,state.focus},{state.growth,state.growth},
        {0,double(std::max<p6::Gems>(cambc::constants::winning_gems,source_gems))},0};
    d.kind=supported?EpisodeKind::full_organization:EpisodeKind::return_component;
    d.require_spawn_shop=true;d.require_supported_work=supported;return d;
}
// Synchronous numeric observations only; a disabled probe preserves all other
// callers. Points are assembly entry, assembled self before queue, queue return.
struct IndependentOfferProbe {
    void* context=nullptr;
    void (*observe)(void*,unsigned,int) noexcept=nullptr;
    std::array<std::uint64_t,3>* queue_budget=nullptr;
    void mark(unsigned at,int value=-1) const noexcept {if(observe)observe(context,at,value);}
};
inline bool independent_offer(common::Offer& offer,const paths::Solo& own,std::span<const paths::Solo> baselines,
                              const p6::Frame& frame,IndependentOfferProbe probe={},
                              const p6::Frame* observed_allies=nullptr) {
    probe.mark(0);
    if(!assemble_independent_offer(offer,own,baselines,frame,paths::append))return false;
    probe.mark(1);
    const bool queued=paths::queue_alternatives(offer,baselines,frame,probe.queue_budget,observed_allies);
    probe.mark(2,int(queued));
    return queued;
}
// Every projected B recurrence includes its actual spawn re-entry overhead.
// This cost is preparation, not an episode receipt or a productive renewal.
inline int preparation(const sdk::Snapshot& snapshot) {
    const double healing=cambc::constants::spawn_healing;
    if(!std::isfinite(healing)||healing<=0||cambc::constants::recall_time<=0)return -1;
    const int recovery=int(std::ceil(1./std::min(1.,healing)));
    if(recovery>8)return -1;
    // Include arrival/start boundary as well as the two actual transaction
    // turns. The full-heal bound deliberately exceeds current missing health.
    // Up to eight real retained steps may align a recalled champion to the
    // one declared spawn tile; another ally may occupy its original tile.
    (void)snapshot;return int(cambc::constants::recall_time)+recovery+3+8;
}
inline Funding preparation_funding(const Inputs& in) {
    Funding result;
    if(in.frame.own.actor.type!=p6::Champion::tavi||in.frame.own.gold<0||
        !in.market.inventory.current(in.frame.clock,in.frame.clock.self)||in.market.inventory.value!=damage_inventory())return result;
    const auto& sale=in.market.items[damage_item];const auto& purchase=in.market.items[survival_item];
    if(sale.price.source!=p6::Source::queried_setting||sale.refund.source!=p6::Source::queried_setting||
        sale.price.value<=0||sale.refund.value!=sale.price.value||purchase.price.source!=p6::Source::queried_setting||
        purchase.price.value<=0||purchase.purchasable.source!=p6::Source::queried_setting||!purchase.purchasable.value)return result;
    p6::Gems available=0;
    if(!p6::add(in.frame.own.gold,sale.refund.value,available)||purchase.price.value>available)return result;
    // Conditional PREPARATION only: held D is liquidated only after reaching
    // actual spawn and rechecking shared funded()/safe_shop(). No income.
    result.refunds=sale.refund.value;result.purchases=purchase.price.value;
    result.final_gold=available-purchase.price.value;result.valid=true;return result;
}
inline bool preparation_safe(Offer& offer,const p6::Frame& frame,int owner_turns) {
    if(owner_turns<0)return false;
    double ambient=0;
    for(const auto& camp:cambc::vision::camps())if(camp.is_alive()&&camp.is_awake())ambient+=camp.strength();
    for(const auto& enemy:cambc::vision::champions(cambc::team::enemy)) {
        double cast=0;for(const auto& ability:enemy.abilities())
            if(ability.info()&&(ability.is_damage()||ability.is_aoe_damage()||ability.is_cc()))cast=std::max(cast,double(ability.damage_amount()));
        ambient+=enemy.strength()+cast;
    }
    const double extra=ambient*owner_turns;if(!std::isfinite(extra))return false;
    for(int i=0;i<offer.safety_count;++i) {
        auto& certificate=offer.safety[i];
        for(auto* exposure:{&certificate.clear,&certificate.hold,&certificate.bank,&certificate.recovery})exposure->incoming_total+=extra;
        if(!certificate.valid(frame,certificate.actor,certificate.group))return false;
    }
    return offer.safety_count>0;
}
inline bool comparison(RebuildComparison& out,const Inputs& in,const Offer& changed,const Offer& unchanged,
                       const Funding& funds,int setup) {
    if(!funds.valid||setup<0||!p6::same_actors(changed.schedule,unchanged.schedule))return false;
    out.changed=changed.schedule;out.unchanged=unchanged.schedule;
    for(int i=0;i<out.changed.production_count;++i)out.changed.productions[i].tick+=setup;
    for(int i=0;i<out.changed.actor_count;++i) {
        auto& actor=out.changed.actors[i];actor.occupied_turns.value+=setup;
        for(int e=0;e<actor.event_count;++e)actor.events[e].tick+=setup;
        if(actor.actor_id==in.frame.clock.self) {
            if(actor.event_count==p6::max_actor_events)return false;
            // Insert before production: only positive NET outlay is a cost.
            // Actual refunds are separately funded; they never become reward.
            for(int e=actor.event_count;e>0;--e)actor.events[e]=actor.events[e-1];
            actor.events[0]={p6::EventKind::purchase,32000,0,-1,
                {std::max<p6::Gems>(0,funds.purchases-funds.refunds),p6::Source::queried_setting},-1,p6::Source::scheduled};
            ++actor.event_count;
        }
    }
    return common::evaluate(in,out.changed).eligible()&&common::evaluate(in,out.unchanged).eligible();
}
} // namespace p8::reversible_workshop::native
