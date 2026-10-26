#pragma once
#include "policy.h"
#include "route_payload.h"

namespace p8::native {
namespace offer_storage_detail {
inline void reset_plan(Plan& plan) {
    plan.organization=Organization::U;plan.group=plan.collector=plan.altar=-1;
    plan.crew_count=0;
    for(auto* route:{&plan.ingress,&plan.capture,&plan.bank,&plan.renewal,&plan.release})reset_route_header(*route);
    plan.forecast_rounds=plan.remaining_rounds=plan.capture_window=0;
    plan.gem_bearing=plan.complete_verified=plan.rally_legal=false;
}
inline void reset_schedule(p6::Schedule& schedule) {
    schedule.actor_count=schedule.production_count=0;schedule.target_id=-1;
    schedule.immediate_win={};schedule.terminal_tiebreak={};
}
inline bool plan_bounds(const Plan& plan) {
    if(plan.crew_count<0||plan.crew_count>int(plan.crew.size()))return false;
    for(const auto* route:{&plan.ingress,&plan.capture,&plan.bank,&plan.renewal,&plan.release})
        if(route->count<0||route->count>max_route_tiles)return false;
    return true;
}
inline bool schedule_bounds(const p6::Schedule& schedule) {
    if(schedule.actor_count<0||schedule.actor_count>p6::max_actors||
        schedule.production_count<0||schedule.production_count>p6::max_productions)return false;
    for(int i=0;i<schedule.actor_count;++i)
        if(schedule.actors[i].event_count<0||schedule.actors[i].event_count>p6::max_actor_events)return false;
    for(int i=0;i<schedule.production_count;++i)
        if(schedule.productions[i].holder_count<0||schedule.productions[i].holder_count>p6::max_actors)return false;
    return true;
}
inline void copy_route(Route& to,const Route& from) {
    for(int i=0;i<from.count;++i)to.tiles[i]=from.tiles[i];
    to.count=from.count;to.blocker_assumptions=from.blocker_assumptions;to.verified=from.verified;
}
} // namespace offer_storage_detail

// All objects and array tails were initialized once by their owner. Reset every
// live header to fresh defaults; stale payload is hidden only by its count.
inline void reset_offer(common::Offer& offer) {
    offer_storage_detail::reset_plan(offer.plan);
    offer_storage_detail::reset_schedule(offer.schedule);
    offer.safety_count=0;offer.context_slot=-1;offer.entry={};
    offer.diagnostic_segment=false;offer.marked_extra_turns=0;offer.capital_cost=0;
}

// Storage copies check array bounds, not route geometry, safety or permission.
// They preserve unverified/unknown values exactly. Existing current validators
// must still admit the resulting Plan/Schedule before a forecast or command.
inline bool copy_plan_into(Plan& to,const Plan& from) {
    if(!offer_storage_detail::plan_bounds(from)) {
        offer_storage_detail::reset_plan(to);return false;
    }
    if(&to==&from)return true;
    to.organization=from.organization;to.group=from.group;to.collector=from.collector;to.altar=from.altar;
    for(int i=0;i<from.crew_count;++i){to.crew[i]=from.crew[i];to.pads[i]=from.pads[i];}
    to.crew_count=from.crew_count;
    offer_storage_detail::copy_route(to.ingress,from.ingress);
    offer_storage_detail::copy_route(to.capture,from.capture);
    offer_storage_detail::copy_route(to.bank,from.bank);
    offer_storage_detail::copy_route(to.renewal,from.renewal);
    offer_storage_detail::copy_route(to.release,from.release);
    to.forecast_rounds=from.forecast_rounds;to.remaining_rounds=from.remaining_rounds;to.capture_window=from.capture_window;
    to.gem_bearing=from.gem_bearing;to.complete_verified=from.complete_verified;to.rally_legal=from.rally_legal;
    return true;
}
inline bool copy_schedule_into(p6::Schedule& to,const p6::Schedule& from) {
    if(!offer_storage_detail::schedule_bounds(from)) {
        offer_storage_detail::reset_schedule(to);return false;
    }
    if(&to==&from)return true;
    for(int i=0;i<from.actor_count;++i) {
        auto& target=to.actors[i];const auto& source=from.actors[i];
        target.actor_id=source.actor_id;target.initial_bag=source.initial_bag;
        target.initial_gold=source.initial_gold;target.occupied_turns=source.occupied_turns;
        for(int e=0;e<source.event_count;++e)target.events[e]=source.events[e];
        target.event_count=source.event_count;
    }
    for(int i=0;i<from.production_count;++i) {
        auto& target=to.productions[i];const auto& source=from.productions[i];
        target.event_id=source.event_id;target.tick=source.tick;target.gems=source.gems;target.gold=source.gold;
        for(int h=0;h<source.holder_count;++h)target.holders[h]=source.holders[h];
        target.holder_count=source.holder_count;target.source=source.source;
    }
    to.actor_count=from.actor_count;to.production_count=from.production_count;to.target_id=from.target_id;
    to.immediate_win=from.immediate_win;to.terminal_tiebreak=from.terminal_tiebreak;
    return true;
}
} // namespace p8::native
