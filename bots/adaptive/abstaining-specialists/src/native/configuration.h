#pragma once
#include "../policy.h"
#include "../../../common/src/coalition.h"
#include "../../../common/src/sdk_shop.h"
namespace p8::abstaining_specialists::native {
namespace paths=p8::native;
#ifndef P8_CONTROL
#define P8_CONTROL 0
#endif
inline constexpr Arm arm=P8_CONTROL?Arm::control:Arm::active;
inline Inventory inventory(int damage,int focus=0){Inventory out;out.count[damage_item]=damage;out.count[focus_item]=focus;return out;}
struct Projection {NumericState state{};p6::Frame frame{};bool valid=false;};
// The collector owns this declaration. A fresh safety forecast may use current
// HP, but must never turn a changed affected actor into the old H membership.
struct HelperRegime {
    int actor=-1,range=-1;p6::Champion champion=p6::Champion::other;
    std::uint64_t equipment=0;
    double max_health=0,strength=0,health_growth=0,strength_growth=0,focus_growth=0;
    double base_focus=0,focus_per_gem=0;p6::Gems bag_limit=0;
    bool valid=false;
};
inline bool helper_regime(const sdk::Snapshot& snapshot,int id,p6::Gems bag_limit,HelperRegime& out) {
    out={};const auto* observed=snapshot.frame.visible(id);
    if(!snapshot.valid||id==snapshot.frame.clock.self||!observed||!observed->alive||
        !roster_actor(*observed,snapshot.draft)||!common::supported_champion(observed->type)||
        observed->bag<0||observed->bag>bag_limit||bag_limit<=0||
        int(cambc::round_number())!=snapshot.frame.clock.round)return false;
    for(const auto& member:cambc::vision::champions(cambc::team::ally)) {
      if(sdk::identity(member)==id) {
        if(!member.is_alive()||sdk::type(member.champion_name())!=observed->type||
            paths::point(member.position())!=observed->position||double(member.health())!=observed->health||
            double(member.max_health())!=observed->max_health||double(member.strength())!=observed->strength||
            std::int64_t(member.gem_count())!=observed->bag||member.attack_range()!=observed->attack_range)return false;
        const auto base=member.base_stats();const double focus=member.focus();
        const double slope=observed->type==p6::Champion::prism?
            double(cambc::constants::passive_abilities::prism_focus_per_gem):0.;
        const double empty_focus=focus-double(observed->bag)*slope;
        for(double value:{observed->max_health,observed->strength,double(base.health_growth),
            double(base.strength_growth),double(base.focus_growth),focus,slope,empty_focus})
            if(!std::isfinite(value)||value<0)return false;
        if(observed->max_health<=0||observed->attack_range<0||
            empty_focus+double(observed->bag)*slope!=focus)return false;
        out={id,observed->attack_range,observed->type,sdk::equipment_identity(member),
            observed->max_health,observed->strength,double(base.health_growth),double(base.strength_growth),
            double(base.focus_growth),empty_focus,slope,bag_limit,true};
        return int(cambc::round_number())==snapshot.frame.clock.round;
      }
    }
    return false;
}
inline bool same_helper_regime(const HelperRegime& frozen,const HelperRegime& current) {
    return frozen.valid&&current.valid&&frozen.actor==current.actor&&frozen.champion==current.champion&&
        frozen.equipment==current.equipment&&frozen.max_health==current.max_health&&frozen.strength==current.strength&&
        frozen.health_growth==current.health_growth&&frozen.strength_growth==current.strength_growth&&
        frozen.focus_growth==current.focus_growth&&frozen.range==current.range&&frozen.base_focus==current.base_focus&&
        frozen.focus_per_gem==current.focus_per_gem&&frozen.bag_limit==current.bag_limit;
}
inline bool supported_inventory(const Inventory& held) {
    if(!held.valid()||held.count[damage_item]>2||held.count[focus_item]>1)return false;
    for(int item=0;item<reversible_workshop::item_types;++item)if(item!=damage_item&&item!=focus_item&&held.count[item])return false;
    return true;
}
// Plain additions only. Prism cannot sell, and a later damage addition keeps
// every already-owned Focus shard and its real bag-dependent Focus bonus.
inline Projection project(const sdk::Snapshot& snapshot,const Inventory& from,const Inventory& to) {
    Projection out;if(!snapshot.valid||snapshot.frame.own.actor.type!=p6::Champion::prism||snapshot.own.bag||
        !supported_inventory(from)||!supported_inventory(to)||snapshot.own.equipment_hash!=from.hash())return out;
    for(int item=0;item<reversible_workshop::item_types;++item)if(to.count[item]<from.count[item])return out;
    const auto base=cambc::self().base_stats();
    auto stats=[&](const Inventory& held) {
        std::array<float,3> result{base.base_health,base.base_strength,base.base_focus};
        for(int item:{damage_item,focus_item}) {
            const auto info=static_cast<cambc::item_type>(item).info();
            if(!info.purchasable||info.passive||(info.combine_count&&held.count[item]>=int(info.combine_count)))return std::array<float,3>{};
            result[0]+=float(info.health_points*held.count[item])*base.health_growth;
            result[1]+=float(info.strength_points*held.count[item])*base.strength_growth;
            result[2]+=float(info.focus_points*held.count[item])*base.focus_growth;
        }return result;
    };
    const auto before=stats(from),after=stats(to);
    if(snapshot.own.max_health!=double(before[0])||snapshot.own.strength!=double(before[1])||snapshot.own.focus!=double(before[2])||
        snapshot.own.growth!=double(base.strength_growth)||after[0]<=0||after[0]!=before[0])return out;
    out.state=snapshot.own;out.state.max_health=after[0];out.state.strength=after[1];out.state.focus=after[2];out.state.equipment_hash=to.hash();
    out.frame=snapshot.frame;out.frame.own.actor.strength=after[1];out.valid=valid_numeric_state(out.state);return out;
}
inline bool actual_loaded(const sdk::Snapshot& snapshot,const Inventory& held) {
    if(!snapshot.valid||!supported_inventory(held)||snapshot.own.equipment_hash!=held.hash())return false;
    auto empty=snapshot;empty.own.bag=0;empty.frame.own.actor.bag=0;
    const double bonus=double(snapshot.own.bag)*double(cambc::constants::passive_abilities::prism_focus_per_gem);
    empty.own.focus-=bonus;const auto projected=project(empty,held,held);
    return projected.valid&&snapshot.own.focus==projected.state.focus+bonus;
}
inline ContextDeclaration declaration(const Plan& plan,const NumericState& state,const Inputs& in,p6::Gems gems,bool full) {
    ContextDeclaration d;auto& key=d.key;key.settings_id=in.settings_identity.value;key.route_hash=plan_route_hash(plan);key.return_route_hash=plan_return_hash(plan);
    key.actor_id=in.frame.clock.self;key.champion=int(in.frame.own.actor.type);key.role=full?1:0;key.source_group=plan.group;key.route_template=full?1:0;key.endpoint=plan.altar;
    key.threat_regime=in.threat_regime.value;key.participants=plan.crew;key.participant_count=plan.crew_count;std::sort(key.participants.begin(),key.participants.begin()+key.participant_count);
    key.build={state.equipment_hash,state.max_health,state.strength,state.focus,state.growth};key.hp_band=health_band(state.health,state.max_health);
    const double lower=key.hp_band==HealthBand::three_quarters_to_full?.75*state.max_health:key.hp_band==HealthBand::half_to_three_quarters?.5*state.max_health:std::nextafter(0.,1.);
    const double maximum=state.focus+double(gems)*double(cambc::constants::passive_abilities::prism_focus_per_gem);
    d.envelope={{lower,state.max_health},{state.max_health,state.max_health},{state.strength,state.strength},{state.focus,maximum},{state.growth,state.growth},{0,double(gems)},0};
    d.kind=full?EpisodeKind::full_organization:EpisodeKind::return_component;d.require_supported_work=full;return d;
}
inline p6::Observation<LoadedModel> loaded(const ContextDeclaration& declared,const p6::CycleCertificate& safety,const p6::ViewClock& clock) {
    LoadedModel model;model.context=declared.key;model.focus_per_gem={double(cambc::constants::passive_abilities::prism_focus_per_gem),p6::Source::queried_setting};
    const auto gems=p6::Gems(declared.envelope.bag.upper);double previous=0;const std::array<double,3> incoming{safety.clear.incoming_total,
        std::max(safety.clear.incoming_total,safety.hold.incoming_total),std::max(safety.bank.incoming_total,safety.recovery.incoming_total)};
    for(int i=0;i<3;++i) {
        previous=std::max(previous,incoming[i]);const auto bag=i==1?gems:0;
        model.points[i]={bag,declared.key.build.focus+double(bag)*model.focus_per_gem.value,
            {declared.envelope.health.lower,declared.key.build.max_health,previous,0,0,0,true}};
    }return {model,clock,clock.self,p6::Knowledge::exact};
}
inline bool independent_offer(Offer& out,const paths::Solo& own,std::span<const paths::Solo> models,const p6::Frame& frame) {
    out={};out.plan=own.plan;out.schedule.target_id=own.plan.group;int prefix=0;bool self=false;
    for(const auto& baseline:models) {
        if(!baseline.feasible||out.safety_count==p6::max_actors)return false;auto copy=baseline;paths::normalize(copy,frame);
        self|=copy.model.schedule.actors[0].actor_id==frame.clock.self;if(!paths::append(out.schedule,copy.model.schedule,0,prefix))return false;
        out.safety[out.safety_count++]=copy.model.certificate;prefix+=64;
    }return self&&paths::queue_alternatives(out,models,frame);
}
inline int setup(const sdk::Snapshot& snapshot,bool purchase) {
    if(snapshot.frame.own.at_spawn) {
        const auto spawns=cambc::map::spawn_region(cambc::team::ally).positions();if(spawns.empty())return -1;
        return (snapshot.frame.own.actor.position==paths::point(spawns.front())?0:8)+(purchase?1:0);
    }
    const double healing=cambc::constants::spawn_healing;if(!std::isfinite(healing)||healing<=0)return -1;
    const int recovery=int(std::ceil(1./std::min(1.,healing)));if(recovery>8)return -1;
    return int(cambc::constants::recall_time)+recovery+9+(purchase?1:0);
}
inline bool delayed(Offer& offer,int turns,const p6::Frame& frame) {
    if(turns<0)return false;double ambient=0;
    for(const auto& camp:cambc::vision::camps())if(camp.is_alive()&&camp.is_awake())ambient+=camp.strength();
    for(const auto& enemy:cambc::vision::champions(cambc::team::enemy)) {
        double cast=0;for(const auto& ability:enemy.abilities())if(ability.info()&&(ability.is_damage()||ability.is_aoe_damage()||ability.is_cc()))cast=std::max(cast,double(ability.damage_amount()));
        ambient+=enemy.strength()+cast;
    }
    for(int p=0;p<offer.schedule.production_count;++p) {
        bool affected=false;for(int h=0;h<offer.schedule.productions[p].holder_count;++h)affected|=common::participant(offer.plan,offer.schedule.productions[p].holders[h]);
        if(affected)offer.schedule.productions[p].tick+=turns;
    }
    for(int a=0;a<offer.schedule.actor_count;++a)if(common::participant(offer.plan,offer.schedule.actors[a].actor_id)) {
        auto& actor=offer.schedule.actors[a];actor.occupied_turns.value+=turns;
        for(int e=0;e<actor.event_count;++e)actor.events[e].tick+=turns;
        if(actor.occupied_turns.value>std::min(common::forecast_window,int(cambc::constants::max_rounds)-frame.clock.round))return false;
    }
    for(int c=0;c<offer.safety_count;++c) {
        auto& safe=offer.safety[c];if(common::participant(offer.plan,safe.actor))
            for(auto* exposure:{&safe.clear,&safe.hold,&safe.bank,&safe.recovery})exposure->incoming_total+=ambient*turns;
        if(!safe.valid(frame,safe.actor,safe.group))return false;
    }return true;
}
inline bool comparison(PurchaseComparison& out,const Inputs& in,const Offer& future,const Offer& current,p6::Gems cost,int preparation) {
    if(cost<=0||preparation<1||!p6::same_actors(future.schedule,current.schedule))return false;
    out.changed=future.schedule;out.unchanged=current.schedule;const int own=p6::actor_index(out.changed,in.frame.clock.self);if(own<0)return false;
    for(int p=0;p<out.changed.production_count;++p) {
        bool dependent=false;for(int h=0;h<out.changed.productions[p].holder_count;++h)dependent|=common::participant(future.plan,out.changed.productions[p].holders[h]);
        if(dependent)out.changed.productions[p].tick+=preparation;
    }
    for(int a=0;a<out.changed.actor_count;++a)if(common::participant(future.plan,out.changed.actors[a].actor_id)) {
        auto& actor=out.changed.actors[a];actor.occupied_turns.value+=preparation;for(int e=0;e<actor.event_count;++e)actor.events[e].tick+=preparation;
    }
    auto& actor=out.changed.actors[own];if(actor.event_count==p6::max_actor_events)return false;
    for(int e=actor.event_count;e>0;--e)actor.events[e]=actor.events[e-1];++actor.event_count;
    actor.events[0]={p6::EventKind::purchase,32000,0,-1,{cost,p6::Source::queried_setting},-1,p6::Source::scheduled};
    return common::evaluate(in,out.changed).eligible()&&common::evaluate(in,out.unchanged).eligible();
}
} // namespace p8::abstaining_specialists::native
