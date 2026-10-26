#pragma once
#include "profile.h"

namespace p6::guilds {
struct State {
    ActorState project;
    int last_round=-1;
    bool repairing=false, logistics=false;
};
inline int visible_project(const organization::Input& in,const Actor* leader) {
    if (!organization::ready(leader)) return -1;
    for (int i=0; i<in.site_count; ++i) {
        const auto& site=in.sites[i];
        if (site.known && (leader->position==site.group.approach ||
                         organization::useful(leader,site.group.id))) return site.group.id;
    }
    return -1;
}
inline organization::Decision choose(State& state,const organization::Input& in,bool control) {
    using namespace organization;
    const bool fresh=state.last_round!=in.frame.clock.round;
    state.last_round=in.frame.clock.round;
    if (!in.frame.valid() || in.ordinal<0 || in.ordinal>=4 ||
        (in.ordinal>=2 && in.picks<4) || in.frame.own.actor.channel!=Channel::none)
        return independent(in);
    const int leader_ordinal=in.ordinal<2?0:2;
    const int partner_ordinal=in.ordinal==leader_ordinal?leader_ordinal+1:leader_ordinal;
    const auto* partner=in.ordinal_actor(partner_ordinal);
    auto& history=state.project;
    if (in.actual_payout) state.logistics=true;
    if (in.actual_bank || in.restored) {
        state.logistics=false; state.repairing=false; history.active=false;
    }
    if (state.logistics || in.phase==Phase::bank || in.phase==Phase::recover) {
        if (control) { history.active=false; return independent(in); }
        if (in.frame.own.actor.bag>0)
            return waypoint(Intent::bank,history.project.group,in.bank_approach,"guild-pair-own-bank");
        if (in.phase==Phase::recover)
            return {Intent::recover,history.project.group,-1,{},false,"guild-pair-recovery"};
        if (ready(partner) && in.safe_now)
            return {Intent::escort,history.project.group,partner->id,partner->position,true,
                    "guild-pair-bank-contact"};
        return independent(in);
    }
    int group=history.active?history.project.group:-1;
    if (group<0) group=in.ordinal==leader_ordinal?in.proposed_group:
        visible_project(in,in.ordinal_actor(leader_ordinal));
    if (group<0) return independent(in); // Other cells never wait for this pair.
    const auto* site=in.site(group);
    if (!site) return independent(in);
    if (!history.active && !history.admit(group,in.frame.clock.round,4,
            std::max(1,in.settings.max_rounds-in.frame.clock.round),
            in.frame.own.actor.bag,in.frame.own.gold)) return independent(in);
    if (!ready(partner) || in.observed_failure || in.hidden_collision) {
        if (!state.repairing) {
            if (!history.repair()) { history.retire(Failure::not_observed); return independent(in); }
            state.repairing=true;
        }
        if (fresh && !history.wait_once()) {
            history.retire(Failure::missed_appointment); return independent(in);
        }
        if (in.ordinal==leader_ordinal && site->solo(in.frame,in.frame.clock.self))
            return work(group,"guild-local-solo-rally-production");
        return waypoint(Intent::rally,group,site->group.retreat,"guild-local-eight-turn-rally");
    }
    state.repairing=false;
    if (joint(in,group) && useful(partner,group) && useful(&in.frame.own.actor,group))
        return work(group,"guild-visible-complementary-pair");
    if (site->solo(in.frame,in.frame.clock.self)) return work(group,"guild-owned-solo-stage");
    if (in.safe_now) {
        if (fresh && !history.wait_once()) {
            history.retire(Failure::missed_appointment); return independent(in);
        }
        const auto* leader=in.ordinal_actor(leader_ordinal);
        return waypoint(Intent::approach,group,in.ordinal==leader_ordinal?site->group.approach:
                        leader->position,"guild-follow-visible-pair-progress");
    }
    return independent(in);
}
struct Policy {
    State state;
    static const organization::Profile& profile() { return profile_value; }
    organization::Decision choose(const organization::Input& in,bool control) {
        return guilds::choose(state,in,control);
    }
};
}
