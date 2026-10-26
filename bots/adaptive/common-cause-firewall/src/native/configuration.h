#pragma once
#include "../purchases.h"
#include "../../../common/src/coalition.h"
#include "../../../common/src/sdk_shop.h"

namespace p8::common_cause_firewall::native {
namespace paths=p8::native;
#ifndef P8_CONTROL
#define P8_CONTROL 0
#endif
inline constexpr Arm arm=P8_CONTROL?Arm::control:Arm::active;
inline constexpr int damage_item=reversible_workshop::damage_item;
inline Inventory damage_inventory(){Inventory i;i.count[damage_item]=1;return i;}
inline DraftRoster supported_roster(DraftRoster d) {
    if(d.picks>=3){std::swap(d.actors[0],d.actors[1]);std::swap(d.champions[0],d.champions[1]);}return d;
}
inline double public_work(const paths::Site& site) {
    double result=0;for(const auto& camp:site.group)result+=double(camp.base_stats().base_health);return result;
}
inline Plan ordinary(const paths::Routes& routes,int s,int b,int actor,bool spawn_visit) {
    Plan p;const auto& site=routes.sites[s];const auto& bank=routes.banks[b];
    p.group=paths::group_id(site.group);p.collector=actor;p.crew[0]=actor;p.crew_count=1;p.altar=sdk::altar_id(bank.altar);
    p.ingress=paths::stay(site.approach.destination);p.capture=routes.route(site.approach.destination,site.capture);
    p.bank=routes.route(site.capture.destination,bank.approach);
    p.renewal=spawn_visit?paths::Routes::join(routes.route(bank.approach.destination,routes.spawn),
        routes.route(routes.spawn.destination,site.approach)):routes.route(bank.approach.destination,site.approach);
    p.forecast_rounds=120;p.remaining_rounds=std::min(120,int(cambc::constants::max_rounds)-int(cambc::round_number()));
    p.capture_window=cambc::constants::capture_time;p.gem_bearing=p.complete_verified=p.rally_legal=true;
    return p;
}
inline bool field_avoids_spawn(const Plan& p) {
    for(Leg leg:{Leg::ingress,Leg::capture,Leg::bank,Leg::renewal}) {
        const auto& r=route_for(p,leg);for(int i=0;i<r.count;++i)
            if(cambc::map::spawn_region(cambc::team::ally).is_set(paths::tile(r.tiles[i])))return false;
    }
    return true;
}
inline bool disjoint_returns(const Plan& a,const Plan& b) {
    for(Leg x:{Leg::bank,Leg::renewal})for(Leg y:{Leg::bank,Leg::renewal}) {
        const auto& ar=route_for(a,x);const auto& br=route_for(b,y);
        for(int i=0;i<ar.count;++i)for(int j=0;j<br.count;++j)if(ar.tiles[i]==br.tiles[j])return false;
    }
    return true;
}
inline bool independent_offer(Offer& offer,const paths::Solo& own,std::span<const paths::Solo> models,const p6::Frame& frame) {
    offer={};offer.plan=own.plan;offer.schedule.target_id=own.plan.group;int prefix=0;bool self=false;
    for(const auto& baseline:models) {
        if(!baseline.feasible||offer.safety_count==p6::max_actors)return false;
        auto current=baseline;paths::normalize(current,frame);self|=current.model.schedule.actors[0].actor_id==frame.clock.self;
        if(!paths::append(offer.schedule,current.model.schedule,0,prefix))return false;
        offer.safety[offer.safety_count++]=current.model.certificate;prefix+=64;
    }
    return self&&paths::queue_alternatives(offer,models,frame);
}
inline ContextDeclaration declaration(const Plan& plan,const NumericState& state,const Inputs& in,int champion,bool supported,bool spawn_visit,p6::Gems maximum_bag) {
    ContextDeclaration d;auto& k=d.key;k.settings_id=in.settings_identity.value;k.route_hash=plan_route_hash(plan);
    k.return_route_hash=plan_return_hash(plan);k.actor_id=plan.collector;k.champion=champion;k.role=supported?1:0;
    k.source_group=plan.group;k.route_template=0;k.endpoint=plan.altar;k.threat_regime=in.threat_regime.value;
    k.participants=plan.crew;k.participant_count=plan.crew_count;std::sort(k.participants.begin(),k.participants.begin()+k.participant_count);
    k.build={state.equipment_hash,state.max_health,state.strength,state.focus,state.growth};k.hp_band=health_band(state.health,state.max_health);
    const double focus=state.focus+(champion==int(p6::Champion::prism)?double(cambc::constants::passive_abilities::prism_focus_per_gem)*double(maximum_bag):0.);
    d.envelope={{std::nextafter(0.,1.),state.max_health},{state.max_health,state.max_health},{state.strength,state.strength},
        {state.focus,focus},{state.growth,state.growth},{0,double(maximum_bag)},0};
    d.kind=supported?EpisodeKind::full_organization:EpisodeKind::return_component;
    d.require_spawn_shop=spawn_visit;d.require_supported_work=supported;return d;
}
inline bool project_damage(const sdk::Snapshot& snapshot,NumericState& after,p6::Frame& frame) {
    const auto base=cambc::self().base_stats();const auto info=static_cast<cambc::item_type>(damage_item).info();
    if(sdk::own_inventory()!=Inventory{}||!info.purchasable||info.passive||(info.combine_count&&info.combine_count<=1)||
        info.health_points||info.focus_points||info.strength_points<=0||snapshot.own.max_health!=double(base.base_health)||
        snapshot.own.strength!=double(base.base_strength)||snapshot.own.focus!=double(base.base_focus)||
        snapshot.own.growth!=double(base.strength_growth))return false;
    after=snapshot.own;after.strength=double(base.base_strength+float(info.strength_points)*base.strength_growth);
    after.equipment_hash=damage_inventory().hash();frame=snapshot.frame;frame.own.actor.strength=after.strength;return valid_numeric_state(after);
}
inline bool purchase_schedule(PurchasePlan& out,const Inputs& in,const Offer& improved,const Offer& ordinary,p6::Gems cost) {
    if(cost<=0||!p6::same_actors(improved.schedule,ordinary.schedule))return false;
    out={};out.item=damage_item;out.with_purchase=improved.schedule;out.without_purchase=ordinary.schedule;
    // The actual buy consumes this owner turn. Charge every represented actor's
    // displaced work, including helper labor for a spawn-compartment purchase.
    for(int p=0;p<out.with_purchase.production_count;++p)++out.with_purchase.productions[p].tick;
    for(int a=0;a<out.with_purchase.actor_count;++a) {
        auto& actor=out.with_purchase.actors[a];++actor.occupied_turns.value;
        for(int e=0;e<actor.event_count;++e)++actor.events[e].tick;
        if(actor.actor_id==in.frame.clock.self) {
            if(actor.event_count==p6::max_actor_events)return false;
            for(int e=actor.event_count;e>0;--e)actor.events[e]=actor.events[e-1];
            actor.events[0]={p6::EventKind::purchase,32000,0,-1,{cost,p6::Source::queried_setting},-1,p6::Source::scheduled};++actor.event_count;
        }
    }
    return common::evaluate(in,out.with_purchase).eligible()&&common::evaluate(in,out.without_purchase).eligible();
}
// Card-local field executor: the shared SDK shop deliberately remains spawn-only.
inline bool current_field_state(const sdk::Snapshot& snapshot) {
    if(!snapshot.valid||snapshot.frame.own.actor.type!=p6::Champion::tavi||snapshot.frame.own.at_spawn||
        snapshot.own.bag||int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<18000)return false;
    const auto& me=cambc::self();
    return sdk::identity(me)==snapshot.frame.clock.self&&me.is_alive()&&!me.is_recalling()&&!me.is_submitting_gems()&&!me.is_teleporting()&&
        !me.buffs().has_stun()&&sdk::point(me.position())==snapshot.frame.own.actor.position&&
        !cambc::map::spawn_region(cambc::team::ally).is_set(me.position())&&
        double(me.health())==snapshot.own.health&&double(me.max_health())==snapshot.own.max_health&&
        double(me.strength())==snapshot.own.strength&&double(me.focus())==snapshot.own.focus&&
        double(me.base_stats().strength_growth)==snapshot.own.growth&&std::int64_t(me.gem_count())==snapshot.own.bag&&
        std::int64_t(me.gold_count())==snapshot.frame.own.gold&&sdk::equipment_identity(me)==snapshot.own.equipment_hash&&
        int(cambc::round_number())==snapshot.frame.clock.round;
}
struct FieldResult {PurchaseReceipt receipt{};sdk::Snapshot after{};};
inline FieldResult field_buy(sdk::Adapter& adapter,const sdk::Snapshot& snapshot,const PurchaseCommand& command) {
    FieldResult out;out.receipt.command_clock=command.clock;out.receipt.token=command.token;out.receipt.item=command.item;
    if(!command.valid||!command.token||command.at_spawn||command.clock!=snapshot.frame.clock||command.item<0||
        command.item>=reversible_workshop::item_types||!current_field_state(snapshot)||command.before!=sdk::own_inventory()||
        command.gold_before!=snapshot.frame.own.gold||command.champion!=int(snapshot.frame.own.actor.type)||
        command.settings_id!=sdk::settings_identity()||command.state_before.equipment_hash!=snapshot.own.equipment_hash||
        command.state_before.health!=snapshot.own.health||command.state_before.max_health!=snapshot.own.max_health||
        command.state_before.strength!=snapshot.own.strength||command.state_before.focus!=snapshot.own.focus||
        command.state_before.growth!=snapshot.own.growth||command.state_before.bag!=snapshot.own.bag)return out;
    auto expected=command.before;++expected.count[command.item];if(expected!=command.after)return out;
    const auto item=static_cast<cambc::item_type>(command.item);const auto info=item.info();
    if(!info.purchasable||info.passive||command.cost!=info.cost||command.cost>snapshot.frame.own.gold||
        (info.combine_count&&expected.count[command.item]>=int(info.combine_count))||!cambc::self().can_buy(item))return out;
    const auto base=cambc::self().base_stats();
    const double maximum=double(float(snapshot.own.max_health)+float(info.health_points)*base.health_growth);
    const double strength=double(float(snapshot.own.strength)+float(info.strength_points)*base.strength_growth);
    const double focus=double(float(snapshot.own.focus)+float(info.focus_points)*base.focus_growth);
    if(cambc::fuel_remaining()<15000||int(cambc::round_number())!=command.clock.round)return out;
    out.receipt.attempted=true;cambc::self().buy(item,1);out.after=adapter.observe();
    out.receipt.observed_clock=out.after.frame.clock;
    // Field Health changes capacity only. No stat projection or buy return is
    // accepted as healing: actual HP must be unchanged in the fresh snapshot.
    out.receipt.success=out.after.valid&&out.after.frame.clock.round==command.clock.round&&
        out.after.frame.clock.revision>command.clock.revision&&!out.after.frame.own.at_spawn&&
        out.after.frame.own.actor.position==snapshot.frame.own.actor.position&&out.after.own.health==snapshot.own.health&&
        out.after.own.max_health==maximum&&out.after.own.strength==strength&&out.after.own.focus==focus&&
        out.after.own.growth==snapshot.own.growth&&out.after.own.bag==0&&out.after.frame.own.actor.alive&&
        out.after.frame.own.actor.channel==p6::Channel::none&&
        out.after.frame.own.gold==command.gold_before-command.cost&&out.after.own.equipment_hash==command.after.hash()&&
        sdk::own_inventory()==command.after&&int(cambc::round_number())==command.clock.round;
    return out;
}
} // namespace p8::common_cause_firewall::native
