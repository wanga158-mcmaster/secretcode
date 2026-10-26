#pragma once
#include "../policy.h"
#include "../../../common/src/coalition.h"
namespace p8::assay_guild::native {
using namespace p8::native;
#ifndef P8_CONTROL
#define P8_CONTROL 0
#endif
inline constexpr Arm arm=P8_CONTROL?Arm::control:Arm::active;
inline double focus_upper(double base,p6::Gems bag) {
    const double predicted=base+double(cambc::constants::passive_abilities::prism_focus_per_gem)*double(bag);
    // Observe the SDK float exactly; bound its finite arithmetic rounding in
    // the declared transition instead of rounding the observation itself.
    return predicted+8.*std::numeric_limits<float>::epsilon()*std::max(1.,std::abs(predicted));
}
inline DraftRoster geometry_roster(DraftRoster actual,Configuration configuration) {
    if(actual.picks<3)return {};
    if(configuration==Configuration::joint_clear) {
        std::swap(actual.actors[1],actual.actors[2]);std::swap(actual.champions[1],actual.champions[2]);
    } else {
        const auto copy=actual;actual.actors[0]=copy.actors[1];actual.champions[0]=copy.champions[1];
        actual.actors[1]=copy.actors[2];actual.champions[1]=copy.champions[2];actual.actors[2]=copy.actors[0];actual.champions[2]=copy.champions[0];
    }
    return actual;
}
inline bool independent_offer(Offer& out,const Solo& own,std::span<const Solo> models,const p6::Frame& frame) {
    const auto empty=std::make_unique<Offer>();out=*empty;out.plan=own.plan;out.schedule.target_id=own.plan.group;bool self=false;int prefix=0;
    for(const auto& model:models) {
        if(!model.feasible||out.safety_count==p6::max_actors)return false;
        auto copy=std::make_unique<Solo>(model);normalize(*copy,frame);self|=copy->model.schedule.actors[0].actor_id==frame.clock.self;
        if(!append(out.schedule,copy->model.schedule,0,prefix))return false;
        out.safety[out.safety_count++]=copy->model.certificate;prefix+=64;
    }
    return self&&queue_alternatives(out,models,frame);
}
inline ContextDeclaration declaration(const Plan& income,const Inputs& in,int role,p6::Gems payload) {
    ContextDeclaration d;auto& k=d.key;const auto& state=in.own_state.value;
    k.settings_id=in.settings_identity.value;k.actor_id=in.frame.clock.self;k.champion=int(in.frame.own.actor.type);k.role=role;
    k.source_group=income.group;k.route_template=role;k.endpoint=income.altar;k.threat_regime=in.threat_regime.value;
    k.route_hash=plan_route_hash(income);k.return_route_hash=plan_return_hash(income);k.participants=income.crew;k.participant_count=income.crew_count;
    std::sort(k.participants.begin(),k.participants.begin()+k.participant_count);
    k.build={state.equipment_hash,state.max_health,state.strength,state.focus,state.growth};k.hp_band=health_band(state.health,state.max_health);
    const double maximum=double(std::max<p6::Gems>(payload,state.bag));
    const double focus=in.frame.own.actor.type==p6::Champion::prism?focus_upper(state.focus,std::max<p6::Gems>(payload,state.bag)):state.focus;
    d.envelope={{std::nextafter(0.,1.),state.max_health},{state.max_health,state.max_health},{state.strength,state.strength},
        {state.focus,focus},{state.growth,state.growth},{0,maximum},0};return d;
}
inline bool nominal_assay(AssaySpec& out,const Solo& model,const Offer& offered,const Inputs& in) {
    int first=-1,last=-1,payout=-1,renewal=-1;
    for(int i=0;i<model.model.phase_count;++i) {
        const auto& phase=model.model.phases[i];if(phase.kind!=p6::forecast::PhaseKind::clear||phase.group!=model.plan.group||phase.actor!=in.frame.clock.self)continue;
        if(first<0)first=phase.start;last=phase.finish;
    }
    for(int i=0;i<offered.schedule.production_count;++i)for(int h=0;h<offered.schedule.productions[i].holder_count;++h)
        if(offered.schedule.productions[i].holders[h]==in.frame.clock.self)payout=offered.schedule.productions[i].tick;
    const int own=p6::actor_index(offered.schedule,in.frame.clock.self);if(own<0)return false;
    for(int e=0;e<offered.schedule.actors[own].event_count;++e)if(offered.schedule.actors[own].events[e].kind==p6::EventKind::renewal)renewal=offered.schedule.actors[own].events[e].tick;
    if(first<0||last<=first||payout<0||renewal<payout)return false;
    out.ordinary=offered.plan;out.fixed=declaration(offered.plan,in,0,model.model.schedule.productions[0].gems.value);
    // Phase start is the first attack turn; finish is the first following
    // full-clear observation. No ingress or inclusive extra attack is added.
    out.nominal_clear={last-first,p6::Source::scheduled};out.nominal_logistics={renewal-payout,p6::Source::scheduled};return true;
}
inline int public_bank(const Routes& routes,int source) {
    int best=-1,cost=0;
    for(int b=0;b<int(routes.banks.size());++b) {
        const auto bank=routes.route(routes.sites[source].capture.destination,routes.banks[b].approach);
        const auto back=routes.route(routes.banks[b].approach.destination,routes.sites[source].approach);
        if(!route_valid(bank)||!route_valid(back))continue;const int here=walking(bank)+walking(back);
        if(best<0||here<cost||(here==cost&&sdk::altar_id(routes.banks[b].altar)<sdk::altar_id(routes.banks[best].altar))){best=b;cost=here;}
    }
    return best;
}
inline int serial_delay(const p8::native::Site& site,const p6::Frame& frame,const DraftRoster& draft) {
    auto targets=std::make_unique<std::array<p6::MonsterWork,32>>();int count=0;
    for(const auto& camp:site.group) {
        if(count==32)return -1;const auto stats=camp.base_stats();(*targets)[count++]={sdk::identity(camp),
            camp.is_visible()?(camp.is_alive()?double(camp.health()):0.):double(stats.base_health),
            camp.is_visible()?double(camp.strength()):double(stats.base_strength)};
    }
    const auto* clearer=frame.visible(draft.actors[0]);const auto* collector=frame.visible(draft.actors[1]);if(!clearer||!collector)return -1;
    const std::array<p6::Productivity,2> joint{{{clearer->strength,0,clearer->ranged},{collector->strength,0,collector->ranged}}};
    const auto together=p6::combat_work({targets->data(),std::size_t(count)},joint);
    const auto alone=p6::combat_work({targets->data(),std::size_t(count)},{joint.data(),1});
    return together.resolved&&alone.resolved?std::max(0,alone.rounds-together.rounds):-1;
}
struct RoleModel {Offer offer{};RoleSpec role{};bool feasible=false;};
inline void role_model(RoleModel& out,const Routes& routes,int source,int bank,const Inputs& in,std::span<const Solo> baseline,
                       const Solo& helper_income,Configuration configuration) {
    out.feasible=false;if(configuration==Configuration::independent||baseline.size()!=3||in.frame.own.actor.pick_ordinal>=2)return;
    const auto roster=geometry_roster(in.draft,configuration);const int helper=roster.actors[2];
    auto alternatives=std::make_unique<std::array<Solo,3>>();bool found=false;
    for(int i=0;i<3;++i) {(*alternatives)[i]=baseline[i];if(baseline[i].plan.collector==helper){(*alternatives)[i]=helper_income;found=true;}}
    if(!found||helper_income.plan.group==group_id(routes.sites[source].group))return;
    auto joint=std::make_unique<Coalition>(coalition(routes,source,bank,in.frame,roster,*alternatives,Organization::H,false));if(!joint->feasible)return;
    out.offer=joint->offer;auto& role=out.role;role.configuration=configuration;role.primary=out.offer.plan;
    auto future=subject(in.frame,*in.frame.visible(helper));future.own.actor.position=routes.sites[helper_income.site].approach.destination;
    auto next=std::make_unique<Solo>(solo(routes,helper_income.site,helper_income.bank,future,false));if(!next->feasible)return;
    if(in.frame.clock.self==helper){role.next=next->plan;role.next_safety=next->model.certificate;}
    const int delta=configuration==Configuration::serial?serial_delay(routes.sites[source],in.frame,in.draft):0;if(delta<0)return;
    double target_incoming=0;for(const auto& camp:routes.sites[source].group)target_incoming+=camp.is_visible()?double(camp.strength()):double(camp.base_stats().base_strength);
    for(int a=0;a<out.offer.schedule.actor_count;++a) {
        auto& actor=out.offer.schedule.actors[a];if(actor.actor_id!=in.draft.actors[0]&&actor.actor_id!=in.draft.actors[1])continue;
        actor.occupied_turns.value+=delta;for(int e=0;e<actor.event_count;++e)actor.events[e].tick+=delta;
    }
    for(int p=0;p<out.offer.schedule.production_count;++p) {
        auto& production=out.offer.schedule.productions[p];bool participant=false;
        for(int h=0;h<production.holder_count;++h)participant|=production.holders[h]==in.draft.actors[0]||production.holders[h]==in.draft.actors[1];
        if(participant)production.tick+=delta;
    }
    for(int c=0;c<out.offer.safety_count;++c)if(out.offer.safety[c].actor==in.draft.actors[0]||out.offer.safety[c].actor==in.draft.actors[1]) {
        double ambient=0;for(const auto& model:*alternatives)if(model.plan.collector==out.offer.safety[c].actor)ambient=model.ambient_incoming;
        for(auto* exposure:{&out.offer.safety[c].clear,&out.offer.safety[c].hold,&out.offer.safety[c].bank,&out.offer.safety[c].recovery})exposure->incoming_total+=(target_incoming+ambient)*delta;
    }
    std::array<economy::BankVisit,3> visits{};auto bank_plans=std::make_unique<std::array<Plan,3>>();
    for(int i=0;i<3;++i) {
        const auto& model=(*alternatives)[i];const int actor=model.plan.collector;auto& plan=(*bank_plans)[i];plan=model.plan;
        if(actor==roster.actors[0])plan=formation_plan(routes,source,bank,subject(in.frame,*in.frame.visible(actor)),roster,Organization::H,false,&model);
        if(!route_valid(plan.bank)||!route_valid(plan.renewal))return;
        // Shared H already charges full payload channels for the collector,
        // and the distinct next-source channel for the released helper.
        p6::Gems bag=solo_bank_bag(model);if(actor==roster.actors[0])bag=routes.sites[source].group.gem_value();
        visits[i]={actor,plan.bank.tiles[plan.bank.count-1],bag,model.ambient_incoming,plan.renewal.tiles[std::min(1,plan.renewal.count-1)],
            {plan.bank.tiles.data(),std::size_t(plan.bank.count)}};
    }
    if(!apply_bank_queue(out.offer,visits,in.frame))return;
    const int actor=p6::actor_index(out.offer.schedule,in.frame.clock.self);if(actor<0)return;
    role.nominal_duration=int(out.offer.schedule.actors[actor].occupied_turns.value);role.primary.forecast_rounds=role.nominal_duration;out.offer.plan=role.primary;
    const bool collector=collecting(role,in.frame.clock.self);const auto& income=collector?role.primary:role.next;
    role.context=declaration(income,in,int(configuration),collector?routes.sites[source].group.gem_value():routes.sites[helper_income.site].group.gem_value());
    role.context.kind=EpisodeKind::full_organization;role.context.require_supported_work=true;
    role.context.key.participant_count=2;role.context.key.participants={std::min(in.draft.actors[0],in.draft.actors[1]),std::max(in.draft.actors[0],in.draft.actors[1]),-1,-1,-1,-1};
    role.context.key.route_hash=role_hash(role,in.frame.clock.self);out.offer.entry=role.context.key;
    out.feasible=role_valid(role,in);
}
inline bool independent_unchanged(const p6::Schedule& a,const p6::Schedule& b,int actor) {
    const int x=p6::actor_index(a,actor),y=p6::actor_index(b,actor);if(x<0||y<0)return false;
    const auto& left=a.actors[x];const auto& right=b.actors[y];
    if(left.initial_bag.value!=right.initial_bag.value||left.initial_bag.source!=right.initial_bag.source||
        left.initial_gold.value!=right.initial_gold.value||left.initial_gold.source!=right.initial_gold.source||
        left.occupied_turns.value!=right.occupied_turns.value||left.occupied_turns.source!=right.occupied_turns.source||left.event_count!=right.event_count)return false;
    for(int i=0;i<left.event_count;++i) {
        const auto& l=left.events[i];const auto& r=right.events[i];
        if(l.kind!=r.kind||l.event_id!=r.event_id||l.tick!=r.tick||l.gold_cost.value!=r.gold_cost.value||l.gold_cost.source!=r.gold_cost.source||
            l.waypoint_id!=r.waypoint_id||l.source!=r.source)return false;
        if(l.kind==p6::EventKind::production) {
            if(l.production_index<0||r.production_index<0||l.production_index>=a.production_count||r.production_index>=b.production_count)return false;
            const auto& lp=a.productions[l.production_index];const auto& rp=b.productions[r.production_index];
            if(lp.event_id!=rp.event_id||lp.tick!=rp.tick||lp.gems.value!=rp.gems.value||lp.gems.source!=rp.gems.source||
                lp.gold.value!=rp.gold.value||lp.gold.source!=rp.gold.source||lp.holder_count!=rp.holder_count||lp.holders!=rp.holders||lp.source!=rp.source)return false;
        }
    }
    return true;
}
} // namespace p8::assay_guild::native
