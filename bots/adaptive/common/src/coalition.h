#pragma once
#include "candidates.h"

namespace p8::native {
inline Route stay(p6::Point at) { Route r;r.tiles[0]=at;r.count=1;r.verified=true;return r; }
inline Route via_pad(const Routes& routes,p6::Point from,const Site& site,p6::Point pad) {
    if(from==pad)return stay(pad);
    auto ingress=routes.route(from,site.approach);
    if (!route_valid(ingress)||distance(site.approach.destination,pad)!=1)return {};
    Route tail=stay(site.approach.destination);tail.tiles[tail.count++]=pad;
    return Routes::join(ingress,tail);
}
// Geometry-only declaration uses actual public draft IDs. It supplies no
// visibility, safety, income or permission; choose/commit must prove those now.
inline Plan formation_plan(const Routes& routes,int s,int b,const p6::Frame& frame,
                           const common::DraftRoster& draft,Organization organization,
                           bool spawn_visit,const Solo* alternative=nullptr) {
    Plan p;if (!draft.valid()||(organization!=Organization::H&&organization!=Organization::J))return p;
    const auto& site=routes.sites[s];const auto& bank=routes.banks[b];
    const int count=organization==Organization::H?2:3;
    if(site.rally_count<count || (organization==Organization::J&&site.joint_capture_count<count))return p;
    p.organization=organization;p.group=group_id(site.group);p.collector=draft.actors[0];
    p.altar=bank.altar.y*cambc::map::width()+bank.altar.x;p.crew_count=count;
    p.crew[0]=draft.actors[0];p.crew[1]=draft.actors[organization==Organization::H?2:1];
    if(count==3)p.crew[2]=draft.actors[2];
    for(int i=0;i<count;++i)p.pads[i]=site.rally[i].destination;
    const int own=crew_index(p,frame.clock.self);if(own<0)return {};
    p.ingress=via_pad(routes,frame.own.actor.position,site,p.pads[own]);
    if(organization==Organization::H&&own!=0)p.capture=stay(p.pads[own]);
    else p.capture=routes.route(p.pads[own],organization==Organization::J?site.joint_capture[own]:site.capture);
    if(!route_valid(p.capture))return {};
    p.bank=routes.route(p.capture.tiles[p.capture.count-1],bank.approach);
    const auto& renewal=organization==Organization::H&&own!=0&&alternative?
        routes.sites[alternative->site].approach:site.approach;
    p.renewal=spawn_visit?Routes::join(routes.route(bank.approach.destination,routes.spawn),
        routes.route(routes.spawn.destination,renewal)):routes.route(bank.approach.destination,renewal);
    if(organization==Organization::H&&own!=0) {
        if(!alternative||alternative->plan.group==p.group)return {};
        p.release=routes.route(p.pads[own],renewal);
    }
    p.forecast_rounds=120;p.remaining_rounds=std::min(120,int(cambc::constants::max_rounds)-frame.clock.round);
    p.capture_window=cambc::constants::capture_time;p.gem_bearing=p.complete_verified=p.rally_legal=true;
    return plan_valid(p,frame.clock.self)?p:Plan{};
}
struct Coalition { common::Offer offer{}; bool feasible=false; };
inline int walking(const Route& route) {return route.count-1+(route.count>1?2:0);}
inline void normalize(Solo& model,const p6::Frame& frame) {
    auto& a=model.model.schedule.actors[0];
    if(a.actor_id!=frame.clock.self){a.initial_bag.source=p6::Source::visible_subject;a.initial_gold={0,p6::Source::conservative};}
    model.model.certificate.clock=frame.clock;
}
inline Coalition coalition(const Routes& routes,int s,int b,const p6::Frame& frame,
                           const common::DraftRoster& draft,std::span<const Solo> alternatives,
                           Organization organization,bool spawn_visit) {
    Coalition out;const auto& site=routes.sites[s];
    if(!draft.valid()||alternatives.empty()||alternatives.size()>p6::max_actors)return out;
    auto alternative=[&](int actor)->const Solo* {
        for(const auto& a:alternatives)if(a.feasible&&a.model.schedule.actors[0].actor_id==actor)return &a;
        return nullptr;
    };
    auto plan=formation_plan(routes,s,b,frame,draft,organization,spawn_visit,alternative(frame.clock.self));
    if(!plan_valid(plan,frame.clock.self))return out;
    // Conditional cooperation forecast for a currently visible safe crew.
    // Bounded travel and eight assembly turns are charged. Actual adoption is
    // verified by Work; a missed meeting fails the issued campaign, and never
    // supplies a receipt, a partner purse or another actor's private proof.
    for(int i=0;i<plan.crew_count;++i) {
        const auto* a=frame.visible(plan.crew[i]);
        if(!a||!a->alive||a->channel!=p6::Channel::none||!alternative(a->id))return out;
        if(organization==Organization::H&&i&&a->bag!=0)return out;
        for(const auto& m:site.group)
            if(a->attack_range<0 || distance(plan.pads[i],point(m.spawn_position()))>a->attack_range ||
               !cambc::map::walls().shadow_cast(tile(plan.pads[i]),a->attack_range).is_set(m.spawn_position()))return out;
    }
    auto targets=std::make_unique<std::array<p6::MonsterWork,32>>();int count=0;double incoming=0;
    for(const auto& m:site.group) {
        if(count==32)return out;const auto stats=m.base_stats();
        const double hp=m.is_visible()?(m.is_alive()?double(m.health()):0.):double(stats.base_health);
        const double strength=m.is_visible()?double(m.strength()):double(stats.base_strength);
        (*targets)[count++]={sdk::identity(m),hp,strength};if(hp>0)incoming+=strength;
    }
    std::array<p6::Productivity,3> workers{};
    auto models=std::make_unique<std::array<Solo,3>>();
    auto plans=std::make_unique<std::array<Plan,3>>();int assembly=0;
    for(int i=0;i<plan.crew_count;++i) {
        const auto* a=frame.visible(plan.crew[i]);workers[i]={a->strength,0,a->ranged};
        const auto local=subject(frame,*a);(*models)[i]=solo(routes,s,b,local,spawn_visit);
        if(!(*models)[i].feasible)return out;normalize((*models)[i],frame);
        (*plans)[i]=formation_plan(routes,s,b,local,draft,organization,spawn_visit,alternative(a->id));
        if(!plan_valid((*plans)[i],a->id))return out;
        if(walking((*plans)[i].ingress)>8)return out;
        assembly=std::max(assembly,walking((*plans)[i].ingress));
    }
    const auto clear=p6::combat_work({targets->data(),std::size_t(count)},{workers.data(),std::size_t(plan.crew_count)});
    if(!clear.resolved)return out;
    const int clear_finish=assembly+8+clear.rounds;
    auto& offer=out.offer;offer.plan=plan;offer.schedule.target_id=plan.group;
    int joint_payout=clear_finish;
    for(int i=0;i<plan.crew_count;++i)joint_payout=std::max(joint_payout,
        clear_finish+walking((*plans)[i].capture)+cambc::constants::capture_time+1);
    if(organization==Organization::J) {
        auto production=(*models)[0].model.schedule.productions[0];
        production.event_id=4096;production.tick=joint_payout;production.holder_count=plan.crew_count;
        production.holders=plan.crew;offer.schedule.productions[offer.schedule.production_count++]=production;
    }
    int event_prefix=0;
    std::array<economy::BankVisit,p6::max_actors> bank_visits{};
    for(const auto& baseline:alternatives) {
        if(!baseline.feasible)return out;const int actor=baseline.model.schedule.actors[0].actor_id;
        const int member=crew_index(plan,actor);p6::CycleCertificate certificate;
        if(member<0) {
            auto copy=baseline;normalize(copy,frame);
            if(!append(offer.schedule,copy.model.schedule,0,event_prefix))return out;
            certificate=copy.model.certificate;
        } else if(organization==Organization::H&&member!=0) {
            auto copy=baseline;normalize(copy,frame);
            const int support=clear_finish+walking((*plans)[0].capture)+2+walking((*plans)[member].release);
            const int delay=std::max(0,support-walking(baseline.plan.ingress));
            if(!append(offer.schedule,copy.model.schedule,delay,event_prefix))return out;
            certificate=copy.model.certificate;certificate.group=plan.group;
            for(auto* e:{&certificate.clear,&certificate.hold,&certificate.bank,&certificate.recovery})
                e->incoming_total+=(incoming+std::max(copy.ambient_incoming,(*models)[member].ambient_incoming))*support;
        } else {
            auto& target=(*models)[member];auto schedule=target.model.schedule;
            const int old_payout=schedule.productions[0].tick;
            const int payout=organization==Organization::J?joint_payout:
                clear_finish+walking((*plans)[member].capture)+cambc::constants::capture_time+1;
            const int delta=payout-old_payout;
            const int bank_extra=std::max(0,walking((*plans)[member].bank)-walking(target.plan.bank));
            for(int e=0;e<schedule.actors[0].event_count;++e) {
                auto& event=schedule.actors[0].events[e];event.tick+=delta;
                if(event.kind!=p6::EventKind::production)event.tick+=bank_extra;
            }
            schedule.actors[0].occupied_turns.value+=delta+bank_extra;schedule.productions[0].tick+=delta;
            if(organization==Organization::H) {
                if(!append(offer.schedule,schedule,0,event_prefix))return out;
            } else {
                auto a=schedule.actors[0];
                for(int e=0;e<a.event_count;++e) {
                    auto& event=a.events[e];
                    if(event.kind==p6::EventKind::production){event.production_index=0;event.event_id=4096;}
                    else event.event_id+=event_prefix;
                }
                offer.schedule.actors[offer.schedule.actor_count++]=a;
            }
            certificate=target.model.certificate;
            for(auto* e:{&certificate.clear,&certificate.hold,&certificate.bank,&certificate.recovery})
                e->incoming_total+=incoming*(assembly+8+bank_extra+
                    std::max(0,walking((*plans)[member].capture)-walking(target.plan.capture)))+
                    target.ambient_incoming*std::max(0,delta+bank_extra);
        }
        if(!certificate.valid(frame,actor,certificate.group))return out;
        const bool target_channel=member>=0&&(organization==Organization::J||member==0);
        const auto& channel_model=target_channel?(*models)[member]:baseline;
        const auto& channel_plan=target_channel?(*plans)[member]:baseline.plan;
        if(!route_valid(channel_plan.bank)||!route_valid(channel_plan.renewal))return out;
        bank_visits[offer.safety_count]={actor,channel_plan.bank.tiles[channel_plan.bank.count-1],
            solo_bank_bag(channel_model),channel_model.ambient_incoming,
            channel_plan.renewal.tiles[std::min(1,channel_plan.renewal.count-1)],
            {channel_plan.bank.tiles.data(),std::size_t(channel_plan.bank.count)}};
        offer.safety[offer.safety_count++]=certificate;event_prefix+=64;
    }
    const int self=p6::actor_index(offer.schedule,frame.clock.self);if(self<0)return out;
    offer.plan.forecast_rounds=int(offer.schedule.actors[self].occupied_turns.value);
    out.feasible=plan_valid(offer.plan,frame.clock.self)&&apply_bank_queue(offer,
        {bank_visits.data(),std::size_t(offer.safety_count)},frame);return out;
}
} // namespace p8::native
