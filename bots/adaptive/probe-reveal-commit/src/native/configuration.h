#pragma once
// Define the arm before the Finite Counterprotocol include, which has its own
// default: this must be the first P8_CONTROL definition reached, so the arena
// knob P8_CONTROL=1 builds the control arm (as #define P8_CONTROL 1 before
// every include does).
#ifndef P8_CONTROL
#define P8_CONTROL 0
#endif
#include "../policy.h"
#include "../../../finite-counterprotocol/src/native/configuration.h"
namespace p8::probe_reveal_commit::native {
using namespace p8::native;
using finite_counterprotocol::native::RivalDistance;
using finite_counterprotocol::native::focus_upper;
using finite_counterprotocol::native::rival_regime;
using finite_counterprotocol::native::physical_regime;
using finite_counterprotocol::native::independent_offer;
inline constexpr Arm arm=P8_CONTROL?Arm::control:Arm::active;
struct Slots {int low=-1,value=-1,general=-1,income=-1,helper=-1;bool ready=false;};
inline Slots slots(const Routes& routes) {
    Slots out;if(routes.sites.size()<5)return out;std::vector<int> order;
    auto work=[&](int s){double hp=0;for(const auto& camp:routes.sites[s].group)hp+=camp.base_stats().base_health;return hp;};
    for(int s=0;s<int(routes.sites.size());++s)order.push_back(s);
    std::sort(order.begin(),order.end(),[&](int a,int b){const double x=work(a),y=work(b);return x!=y?x<y:group_id(routes.sites[a].group)<group_id(routes.sites[b].group);});
    out.low=order.front();
    for(const int s:order)if(s!=out.low&&work(s)>work(out.low)){if(out.value<0)out.value=s;else if(out.general<0){out.general=s;break;}}
    if(out.value<0||out.general<0)return out;
    for(const int s:order)if(s!=out.low&&s!=out.value&&s!=out.general){if(out.income<0)out.income=s;else if(out.helper<0){out.helper=s;break;}}
    out.ready=out.helper>=0;return out;
}
inline bool full_work(const p8::native::Site& site) {
    for(const auto& camp:site.group)if(!camp.is_visible()||!camp.is_alive()||double(camp.health())!=double(camp.base_stats().base_health))return false;
    return true;
}
inline bool retarget(const Routes& routes,Solo& out,const Field& destination,int productive_group,const p6::Frame& frame,bool cooldown=false) {
    if(!out.feasible)return false;
    const auto route=routes.route(routes.banks[out.bank].approach.destination,destination);if(!route_valid(route))return false;
    int extra=std::max(0,walking(route)-walking(out.plan.renewal));auto& actor=out.model.schedule.actors[0];
    if(cooldown)extra=std::max(extra,out.model.schedule.productions[0].tick+routes.sites[out.site].group.max_respawn_timer()+1-int(actor.occupied_turns.value));
    actor.occupied_turns.value+=extra;
    for(int e=0;e<actor.event_count;++e)if(actor.events[e].kind==p6::EventKind::renewal){actor.events[e].tick+=extra;actor.events[e].waypoint_id=productive_group;}
    out.model.certificate.recovery.incoming_total+=out.ambient_incoming*extra;out.plan.renewal=route;
    out.plan.forecast_rounds=int(actor.occupied_turns.value);
    out.feasible=out.plan.forecast_rounds<=std::min(common::forecast_window,int(cambc::constants::max_rounds)-frame.clock.round)&&
        plan_valid(out.plan,frame.clock.self)&&out.model.certificate.valid(frame,frame.clock.self,out.plan.group);return out.feasible;
}
inline std::unique_ptr<Solo> model(const Routes& routes,const Slots& roles,int source,int bank,const p6::Frame& frame,bool public_full=false) {
    // Construct the returned prvalue directly in its final heap allocation.
    // In-place retargeting avoids another complete Solo on the WASI stack.
    auto out=std::unique_ptr<Solo>(new Solo(solo(routes,source,bank,frame,false,public_full)));if(!out->feasible)return {};
    const auto& destination=frame.own.actor.pick_ordinal==0?routes.sites[roles.general].approach:
        frame.own.actor.pick_ordinal==2?routes.sites[roles.value].rally[1]:routes.sites[source].approach;
    const int productive=frame.own.actor.pick_ordinal==0?roles.general:frame.own.actor.pick_ordinal==2?roles.value:source;
    if(!retarget(routes,*out,destination,group_id(routes.sites[productive].group),frame,frame.own.actor.pick_ordinal==1||source==roles.general))return {};
    return out;
}
inline double ambient() {
    double value=0;for(const auto& camp:cambc::vision::camps())if(camp.is_alive()&&camp.is_awake())value+=camp.strength();
    for(const auto& enemy:cambc::vision::champions(cambc::team::enemy)) {
        double cast=0;for(const auto& ability:enemy.abilities())if(ability.info()&&(ability.is_damage()||ability.is_aoe_damage()||ability.is_cc()))cast=std::max(cast,double(ability.damage_amount()));
        value+=enemy.strength()+cast;
    }
    return value;
}
inline bool named_site(probe_reveal_commit::Site& out,const Routes& routes,int source,int bank,RivalDistance& distances) {
    out={};out.opportunity=group_id(routes.sites[source].group);out.altar=sdk::altar_id(routes.banks[bank].altar);
    out.width=cambc::map::width();out.height=cambc::map::height();out.altar_tile=routes.banks[bank].altar;
    out.own_approaches[0]=routes.banks[bank].approach.destination;out.approach_count=2;
    const auto enemy_terrain=cambc::map::walls()|cambc::map::holes()|cambc::map::altars()|cambc::map::spawn_region(cambc::team::ally);
    bool found=false;
    for(int y=-1;y<=1&&!found;++y)for(int x=-1;x<=1&&!found;++x) {
        const p6::Point at{out.altar_tile.x+x,out.altar_tile.y+y};
        if((!x&&!y)||at.x<0||at.y<0||at.x>=out.width||at.y>=out.height||at==out.own_approaches[0]||
            enemy_terrain.is_set(tile(at))||cambc::map::bushes().is_set(tile(at)))continue;
        bool spawn=false;for(const auto& group:cambc::map::camp_groups())spawn|=group.spawn_tiles().is_set(tile(at));
        if(!spawn){out.target=out.own_approaches[1]=at;found=true;}
    }
    out.map_identity={distances.map_identity(),p6::Source::public_map};return found&&finite_counterprotocol::site_valid(out)&&distances.prepare(out);
}
inline ContextDeclaration declaration(const Layout& layout,const Inputs& in,p6::Gems payload) {
    ContextDeclaration d;d.kind=EpisodeKind::full_organization;d.require_supported_work=true;auto& k=d.key;const auto& state=in.own_state.value;
    const auto& p=layout.supported;k.settings_id=in.settings_identity.value;k.actor_id=in.frame.clock.self;k.champion=int(in.frame.own.actor.type);k.role=1;
    k.source_group=p.group;k.route_template=16;k.endpoint=p.altar;k.threat_regime=in.threat_regime.value;k.route_hash=plan_route_hash(p);k.return_route_hash=plan_return_hash(p);
    k.participants=p.crew;k.participant_count=2;std::sort(k.participants.begin(),k.participants.begin()+2);
    k.build={state.equipment_hash,state.max_health,state.strength,state.focus,state.growth};k.hp_band=health_band(state.health,state.max_health);
    const double focus=in.frame.own.actor.type==p6::Champion::prism?focus_upper(state.focus,payload):state.focus;
    d.envelope={{std::nextafter(0.,1.),state.max_health},{state.max_health,state.max_health},{state.strength,state.strength},
        {state.focus,focus},{state.growth,state.growth},{0,double(std::max(payload,state.bag))},0};return d;
}
// Own low-load cycle followed by one real current continuation. Actor1's
// protected complete schedule is copied once, with identical events/payloads.
// Helper2's prefix payout is copied once; future support AND foregone own U
// occupy it in every branch, without crediting another speculative reward.
inline bool branch(p6::Schedule& out,std::array<p6::CycleCertificate,p6::max_actors>& safety,int& safety_count,
                   const Offer& prefix,const Offer& next,const Inputs& in,int bound,int not_before,
                   const Route& helper_return,int helper_source,double target_exposure,int& start,int& end,int& next_event) {
    const int owner=in.draft.actors[0],protected_actor=in.draft.actors[1],helper=in.draft.actors[2];
    const int own=p6::actor_index(prefix.schedule,owner),peer=p6::actor_index(prefix.schedule,helper);
    if(own<0||peer<0||!route_valid(helper_return)||helper_source<0||!std::isfinite(target_exposure)||target_exposure<0)return false;
    end=int(prefix.schedule.actors[own].occupied_turns.value);start=std::max({end,int(prefix.schedule.actors[peer].occupied_turns.value),not_before});
    out={};out.target_id=next.plan.group;safety_count=0;
    for(int a=0;a<prefix.schedule.actor_count;++a) {
        const auto& first=prefix.schedule.actors[a];const int n=p6::actor_index(next.schedule,first.actor_id);if(n<0)return false;
        auto actor=first;actor.event_count=0;const bool fixed=first.actor_id==protected_actor;
        const auto& tail=next.schedule.actors[n];
        if(first.actor_id==helper&&int(tail.occupied_turns.value)<walking(helper_return))return false;
        const int finish=fixed?int(first.occupied_turns.value):start+int(tail.occupied_turns.value);
        if(finish>bound)return false;actor.occupied_turns={finish,p6::Source::conservative};
        for(int cycle=0;cycle<(fixed?1:2);++cycle)for(int e=0;e<(cycle?tail:first).event_count;++e) {
            auto event=(cycle?tail:first).events[e];
            if(!fixed&&event.kind==p6::EventKind::recovery)continue;
            if(!fixed&&!cycle&&event.kind==p6::EventKind::renewal)continue;
            if(cycle&&first.actor_id==helper) {
                // This branch retains the full foregone-U time charge but
                // claims only the actual retained productive return, without
                // a second helper capture/tribute or its speculative reward.
                if(event.kind!=p6::EventKind::renewal)continue;
                event.waypoint_id=helper_source;
            }
            if(actor.event_count==p6::max_actor_events)return false;
            event.tick+=cycle?start:0;event.event_id+=cycle?8192:0;
            if(event.kind==p6::EventKind::production) {
                const auto& source=cycle?next.schedule:prefix.schedule;if(out.production_count==p6::max_productions)return false;
                auto production=source.productions[event.production_index];production.tick=event.tick;production.event_id=event.event_id;
                event.production_index=out.production_count;out.productions[out.production_count++]=production;
                if(cycle&&first.actor_id==owner)next_event=event.event_id;
            }
            actor.events[actor.event_count++]=event;
        }
        out.actors[out.actor_count++]=actor;const auto* initial=common::certificate(prefix,first.actor_id);const auto* future=common::certificate(next,first.actor_id);
        if(!initial||!future)return false;auto certificate=*initial;
        if(!fixed) {
            const auto* seen=in.frame.visible(first.actor_id);if(!seen)return false;
            const double incoming=initial->recovery.incoming_total+future->recovery.incoming_total+
                ambient()*std::max(0,start-int(first.occupied_turns.value))+
                (first.actor_id==helper?target_exposure*double(tail.occupied_turns.value):0.);
            const double next_incoming=std::max(initial->recovery.next_phase_incoming,future->recovery.next_phase_incoming);
            for(auto* exposure:{&certificate.clear,&certificate.hold,&certificate.bank,&certificate.recovery})
                *exposure={seen->health,seen->max_health,incoming,next_incoming,0,0,true};
        }
        if(!certificate.valid(in.frame,first.actor_id,certificate.group)||!independent_safety(certificate))return false;
        safety[safety_count++]=certificate;
    }
    return next_event>=0;
}
inline bool retarget_supported(Offer& offer,const Routes& routes,const Slots& roles,int bank,const p6::Frame& frame) {
    const int owner=frame.own.actor.pick_ordinal==0?frame.clock.self:
        offer.plan.collector;
    const int a=p6::actor_index(offer.schedule,owner);if(a<0)return false;
    const auto route=routes.route(routes.banks[bank].approach.destination,routes.sites[roles.general].approach);
    const auto old=routes.route(routes.banks[bank].approach.destination,routes.sites[roles.value].approach);
    if(!route_valid(route)||!route_valid(old))return false;
    const int extra=std::max(0,walking(route)-walking(old));auto& actor=offer.schedule.actors[a];actor.occupied_turns.value+=extra;
    for(int e=0;e<actor.event_count;++e)if(actor.events[e].kind==p6::EventKind::renewal) {
        actor.events[e].tick+=extra;actor.events[e].waypoint_id=group_id(routes.sites[roles.general].group);
    }
    for(int c=0;c<offer.safety_count;++c)if(offer.safety[c].actor==owner) {
        for(auto* exposure:{&offer.safety[c].clear,&offer.safety[c].hold,&offer.safety[c].bank,&offer.safety[c].recovery})
            exposure->incoming_total+=ambient()*extra;
    }
    if(frame.clock.self==owner)offer.plan.renewal=route;
    const int self=p6::actor_index(offer.schedule,frame.clock.self);if(self<0)return false;
    offer.plan.forecast_rounds=int(offer.schedule.actors[self].occupied_turns.value);
    if(!plan_valid(offer.plan,frame.clock.self))return false;
    for(int c=0;c<offer.safety_count;++c)if(!offer.safety[c].valid(frame,offer.safety[c].actor,offer.safety[c].group))return false;
    return true;
}
// Freeze the route and conservative duration at declaration. A later cheaper
// current clear may use that charge; a longer or changed route is unavailable.
inline bool frozen_offer(Offer& offer,const Plan& frozen,const p6::Frame& frame) {
    auto comparable=offer.plan;comparable.forecast_rounds=frozen.forecast_rounds;
    if(!same_plan(comparable,frozen)||offer.plan.forecast_rounds>frozen.forecast_rounds)return false;
    const int own=p6::actor_index(offer.schedule,frame.clock.self);if(own<0)return false;
    const int extra=frozen.forecast_rounds-int(offer.schedule.actors[own].occupied_turns.value);if(extra<0)return false;
    auto& actor=offer.schedule.actors[own];actor.occupied_turns.value+=extra;
    for(int e=0;e<actor.event_count;++e)if(actor.events[e].kind==p6::EventKind::renewal)actor.events[e].tick+=extra;
    for(int c=0;c<offer.safety_count;++c)if(offer.safety[c].actor==frame.clock.self) {
        for(auto* exposure:{&offer.safety[c].clear,&offer.safety[c].hold,&offer.safety[c].bank,&offer.safety[c].recovery})
            exposure->incoming_total+=ambient()*extra;
        if(!offer.safety[c].valid(frame,frame.clock.self,offer.safety[c].group))return false;
    }
    offer.plan=frozen;offer.plan.remaining_rounds=std::min(common::forecast_window,int(cambc::constants::max_rounds)-frame.clock.round);return true;
}
} // namespace p8::probe_reveal_commit::native
