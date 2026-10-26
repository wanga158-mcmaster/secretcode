#pragma once
#include "../policy.h"
#include "../../../common/src/coalition.h"
namespace p8::disagreement_partnership::native {
using namespace p8::native;
#ifndef P8_CONTROL
#define P8_CONTROL 0
#endif
inline constexpr Arm arm=P8_CONTROL?Arm::control:Arm::active;
inline DraftRoster geometry_roster(DraftRoster draft) {
    if(draft.picks>=3){std::swap(draft.actors[0],draft.actors[2]);std::swap(draft.champions[0],draft.champions[2]);}
    return draft;
}
inline bool independent_offer(Offer& out,const Solo& own,std::span<const Solo> models,const p6::Frame& frame) {
    out={};out.plan=own.plan;out.schedule.target_id=own.plan.group;int prefix=0;bool self=false;
    for(const auto& baseline:models) {
        if(!baseline.feasible||out.safety_count==p6::max_actors)return false;
        auto copy=baseline;normalize(copy,frame);self|=copy.model.schedule.actors[0].actor_id==frame.clock.self;
        if(!append(out.schedule,copy.model.schedule,0,prefix))return false;
        out.safety[out.safety_count++]=copy.model.certificate;prefix+=64;
    }
    return self&&queue_alternatives(out,models,frame);
}
inline bool full_work(const Site& site) {
    for(const auto& camp:site.group) {
        const auto stats=camp.base_stats();
        if(!camp.is_visible()||!camp.is_alive()||double(camp.health())!=double(stats.base_health))return false;
    }
    return true;
}
inline Solo retained_solo(const Routes& routes,const Solo& original,const Plan& retained,const p6::Frame& frame) {
    auto out=original;if(!out.feasible||!plan_valid(retained,frame.clock.self))return {};
    const auto pad=retained.ingress.tiles[retained.ingress.count-1];double incoming=out.ambient_incoming;
    for(const auto& camp:routes.sites[out.site].group) {
        if(distance(pad,point(camp.spawn_position()))>frame.own.actor.attack_range||
            !cambc::map::walls().shadow_cast(tile(pad),frame.own.actor.attack_range).is_set(camp.spawn_position()))return {};
        incoming+=camp.is_visible()?double(camp.strength()):double(camp.base_stats().base_strength);
    }
    const int before=std::max(0,walking(retained.ingress)-walking(out.plan.ingress))+std::max(0,walking(retained.capture)-walking(out.plan.capture));
    const int bank=std::max(0,walking(retained.bank)-walking(out.plan.bank));
    const int renew=std::max(0,walking(retained.renewal)-walking(out.plan.renewal));
    for(int p=0;p<out.model.schedule.production_count;++p)out.model.schedule.productions[p].tick+=before;
    auto& actor=out.model.schedule.actors[0];actor.occupied_turns.value+=before+bank+renew;
    for(int e=0;e<actor.event_count;++e) {
        auto& event=actor.events[e];event.tick+=before;
        if(event.kind!=p6::EventKind::production)event.tick+=bank;
        if(event.kind==p6::EventKind::renewal)event.tick+=renew;
    }
    for(auto* exposure:{&out.model.certificate.clear,&out.model.certificate.hold,&out.model.certificate.bank,&out.model.certificate.recovery})
        exposure->incoming_total+=incoming*(before+bank+renew);
    out.plan=retained;out.plan.organization=Organization::U;out.plan.crew={frame.clock.self,-1,-1,-1,-1,-1};out.plan.crew_count=1;out.plan.release={};
    out.plan.forecast_rounds=int(actor.occupied_turns.value);
    out.feasible=plan_valid(out.plan,frame.clock.self)&&out.model.certificate.valid(frame,frame.clock.self,out.plan.group);return out;
}
struct Corridor {
    int actor=-1,index=-1,forced=0; p6::Point tile{};
    bool valid=false;
};
// Only an actually observed nonparticipant on an interior corridor tile
// creates this residual question. This subset declines any known current
// channel/stun/spent-move coupling instead of pretending to learn its timer.
inline Corridor corridor(const Plan& plan,const p6::Schedule& first,int owner,int helper) {
    Corridor result;const int own=p6::actor_index(first,owner);if(own<0)return result;
    int payout=-1;const auto& a=first.actors[own];
    for(int e=0;e<a.event_count;++e)if(a.events[e].kind==p6::EventKind::production){payout=a.events[e].tick;break;}
    if(payout<0)return result;
    for(const auto& ally:cambc::vision::champions(cambc::team::ally)) {
        const int id=sdk::identity(ally);if(id==owner||id==helper||!ally.is_alive())continue;
        const auto position=point(ally.position());
        for(int i=1;i+1<plan.bank.count;++i)if(position==plan.bank.tiles[i]) {
            const bool channel=ally.is_recalling()||ally.is_submitting_gems()||ally.is_teleporting();
            if(channel||ally.buffs().has_stun()||!ally.can_move())return {};
            // It can leave now, before a later five-tick capture pays out.
            // Its unknown future choice to hold this tile is the residual.
            if(result.valid)return {};
            result={id,i,std::max(0,1-(payout+i)),position,true};
        }
    }
    return result;
}
// Two fully charged recurrences of the SAME source after its queried respawn.
// The first templates must represent public full HP already; the second does
// not reuse partly damaged camp work or credit spawn healing. Every actor
// survives both complete jobs and all intervening waits from current lower HP.
inline bool recurrence(Offer& out,const Offer& first,const Offer& second,std::span<const Solo> baselines,const Routes& routes,
                       const p6::Frame& frame,int owner,bool supported) {
    out=first;out.schedule={};out.schedule.target_id=first.schedule.target_id;
    std::array<int,p6::max_actors> offsets{};int shared=0;
    for(int i=0;i<first.schedule.actor_count;++i) {
        const auto& actor=first.schedule.actors[i];const Solo* baseline=nullptr;
        for(const auto& model:baselines)if(model.model.schedule.actors[0].actor_id==actor.actor_id)baseline=&model;
        if(!baseline||!full_work(routes.sites[baseline->site]))return false;
        int payout=-1;for(int e=0;e<actor.event_count;++e)if(actor.events[e].kind==p6::EventKind::production){payout=actor.events[e].tick;break;}
        if(payout<0||actor.occupied_turns.value<0||actor.occupied_turns.value>120)return false;
        const int source=actor.actor_id==owner&&supported?first.plan.group:baseline->plan.group;
        int cooldown=-1;for(const auto& site:routes.sites)if(group_id(site.group)==source)cooldown=site.group.max_respawn_timer();
        if(cooldown<0)return false;
        offsets[i]=std::max(int(actor.occupied_turns.value),payout+cooldown+1);shared=std::max(shared,offsets[i]);
    }
    for(int i=0;i<first.schedule.actor_count;++i) {
        const auto& old=first.schedule.actors[i];const int next_index=p6::actor_index(second.schedule,old.actor_id);
        if(next_index<0)return false;const auto& next=second.schedule.actors[next_index];auto actor=old;actor.event_count=0;
        const int offset=supported?shared:offsets[i];const int buffer=supported&&old.actor_id!=owner?2:0;
        const int finish=offset+int(next.occupied_turns.value)+buffer;
        if(finish+4>std::min(common::forecast_window,int(cambc::constants::max_rounds)-frame.clock.round))return false;
        actor.occupied_turns={finish,p6::Source::conservative};
        for(int cycle=0;cycle<2;++cycle)for(int e=0;e<(cycle?next:old).event_count;++e) {
            auto event=(cycle?next:old).events[e];
            // No healing benefit is used. Keep its charged time but omit the
            // recovery event, so a delayed bank never precedes a false heal.
            if(event.kind==p6::EventKind::recovery||(!cycle&&event.kind==p6::EventKind::renewal))continue;
            if(actor.event_count==p6::max_actor_events)return false;
            event.tick+=cycle?offset+buffer:0;event.event_id+=cycle?8192:0;
            if(event.kind==p6::EventKind::production) {
                const auto& source=cycle?second.schedule:first.schedule;
                if(event.production_index<0||event.production_index>=source.production_count||out.schedule.production_count==p6::max_productions)return false;
                auto production=source.productions[event.production_index];production.tick=event.tick;production.event_id=event.event_id;
                event.production_index=out.schedule.production_count;out.schedule.productions[out.schedule.production_count++]=production;
            }
            actor.events[actor.event_count++]=event;
        }
        out.schedule.actors[out.schedule.actor_count++]=actor;
        const auto* seen=frame.visible(old.actor_id);if(!seen)return false;
        for(int c=0;c<out.safety_count;++c)if(out.safety[c].actor==old.actor_id) {
            auto& certificate=out.safety[c];double ambient=0;
            for(const auto& model:baselines)if(model.model.schedule.actors[0].actor_id==old.actor_id)ambient=model.ambient_incoming;
            const double waiting=offset-double(old.occupied_turns.value)+buffer+4;
            const auto* next_certificate=common::certificate(second,old.actor_id);if(!next_certificate)return false;
            const double incoming=certificate.recovery.incoming_total+next_certificate->recovery.incoming_total+ambient*waiting;
            const double next_rate=std::max({certificate.clear.next_phase_incoming,certificate.hold.next_phase_incoming,
                certificate.bank.next_phase_incoming,certificate.recovery.next_phase_incoming,
                next_certificate->clear.next_phase_incoming,next_certificate->hold.next_phase_incoming,
                next_certificate->bank.next_phase_incoming,next_certificate->recovery.next_phase_incoming});
            for(auto* exposure:{&certificate.clear,&certificate.hold,&certificate.bank,&certificate.recovery})
                *exposure={seen->health,seen->max_health,incoming,next_rate,0,0,true};
            if(!certificate.valid(frame,old.actor_id,certificate.group))return false;
        }
    }
    return true;
}
inline bool delayed(p6::Schedule& out,const p6::Schedule& common,int owner,int delay=2) {
    out=common;const int a=p6::actor_index(out,owner);if(a<0)return false;
    auto& actor=out.actors[a];actor.occupied_turns.value+=2*delay;int productions=0,tributes=0;
    for(int e=0;e<actor.event_count;++e) {
        auto& event=actor.events[e];int shift=0;
        if(event.kind==p6::EventKind::production&&++productions==2){shift=delay;out.productions[event.production_index].tick+=delay;}
        if(event.kind==p6::EventKind::tribute)shift=++tributes*delay;
        if(event.kind==p6::EventKind::renewal)shift=2*delay;
        event.tick+=shift;
    }
    return productions==2&&tributes==2&&route_models_match(common,out,owner,delay);
}
inline ContextDeclaration declaration(const Plan& plan,const sdk::Snapshot& snapshot,const Inputs& in,bool full,p6::Gems maximum_bag) {
    ContextDeclaration d;auto& k=d.key;const auto& state=snapshot.own;
    k.settings_id=in.settings_identity.value;k.route_hash=plan_route_hash(plan);k.return_route_hash=plan_return_hash(plan);
    k.actor_id=snapshot.frame.clock.self;k.champion=int(snapshot.frame.own.actor.type);k.role=full?1:0;k.source_group=plan.group;
    k.route_template=0;k.endpoint=plan.altar;k.threat_regime=in.threat_regime.value;k.participants=plan.crew;k.participant_count=plan.crew_count;
    std::sort(k.participants.begin(),k.participants.begin()+k.participant_count);
    k.build={state.equipment_hash,state.max_health,state.strength,state.focus,state.growth};k.hp_band=health_band(state.health,state.max_health);
    const double focus=state.focus+(snapshot.frame.own.actor.type==p6::Champion::prism?
        double(cambc::constants::passive_abilities::prism_focus_per_gem)*double(maximum_bag):0.);
    d.envelope={{std::nextafter(0.,1.),state.max_health},{state.max_health,state.max_health},{state.strength,state.strength},
        {state.focus,focus},{state.growth,state.growth},{0,double(maximum_bag)},0};
    d.kind=full?EpisodeKind::full_organization:EpisodeKind::return_component;d.require_supported_work=full;return d;
}
} // namespace p8::disagreement_partnership::native
