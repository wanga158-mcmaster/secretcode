#pragma once
#include "../../../common/src/offer_storage.h"
#include "../../../common/src/solo_storage.h"

namespace p8::reversible_workshop::native {
// Borrow only when normalize would leave both actor resources and clock intact.
// The caller keeps this current own model alive and read-only through the menu.
inline bool can_borrow_menu_own(const p8::native::Solo& own,const p6::Frame& frame) {
    return own.model.schedule.actor_count==1&&
        own.model.schedule.actors[0].actor_id==frame.clock.self&&
        own.model.certificate.clock==frame.clock;
}

// The caller owns initialized reusable storage. Counts hide inactive route,
// schedule and forecast payload; Workshop identity still reads every crew/pad.
// Invalid storage bounds keep the old whole-assignment behavior.
inline void copy_menu_baseline(p8::native::Solo& to,const p8::native::Solo& from) {
    if(&to==&from)return;
    if(from.model.phase_count<0||from.model.phase_count>int(from.model.phases.size())||
        !p8::native::copy_plan_into(to.plan,from.plan)||
        !p8::native::copy_schedule_into(to.model.schedule,from.model.schedule)) {
        to=from;return;
    }
    to.plan.crew=from.plan.crew;to.plan.pads=from.plan.pads;
    to.model.status=from.model.status;to.model.issue=from.model.issue;to.model.abort=from.model.abort;
    to.model.certificate=from.model.certificate;
    for(int i=0;i<from.model.phase_count;++i)to.model.phases[i]=from.model.phases[i];
    to.model.phase_count=from.model.phase_count;to.model.first=from.model.first;
    to.model.recovery_health_unmodeled=from.model.recovery_health_unmodeled;
    to.model.purchases_deferred=from.model.purchases_deferred;
    to.site=from.site;to.bank=from.bank;to.ambient_incoming=from.ambient_incoming;to.feasible=from.feasible;
}

// Append is the unchanged native append operation. It copies an actor before
// interpreting only occupancy/events; normalize that appended actor in place
// instead of copying and normalizing its entire Solo first. Queue admission
// remains with the native caller after this pure assembly succeeds.
template<class Append>
inline bool assemble_independent_offer(common::Offer& offer,const p8::native::Solo& own,
                                       std::span<const p8::native::Solo> baselines,
                                       const p6::Frame& frame,Append&& append) {
    p8::native::reset_offer(offer);
    if(!p8::native::copy_plan_into(offer.plan,own.plan))offer.plan=own.plan;
    offer.plan.crew=own.plan.crew;offer.plan.pads=own.plan.pads;
    offer.schedule.target_id=own.plan.group;
    int prefix=0;bool self=false;
    for(const auto& baseline:baselines) {
        if(!baseline.feasible||offer.safety_count==p6::max_actors)return false;
        self|=baseline.model.schedule.actors[0].actor_id==frame.clock.self;
        if(!append(offer.schedule,baseline.model.schedule,0,prefix))return false;
        auto& actor=offer.schedule.actors[offer.schedule.actor_count-1];
        if(actor.actor_id!=frame.clock.self) {
            actor.initial_bag.source=p6::Source::visible_subject;
            actor.initial_gold={0,p6::Source::conservative};
        }
        auto& certificate=offer.safety[offer.safety_count++];
        certificate=baseline.model.certificate;certificate.clock=frame.clock;
        prefix+=64;
    }
    return self;
}
} // namespace p8::reversible_workshop::native
