#pragma once
#include "profile.h"

namespace p6::mesh {
struct Edge {
    int epoch=-1, period=-1, waits=0, misses=0;
    bool retired=false, met=false, finished=false;
};
struct State {
    std::array<Edge,2> edges{};
    int last_round=-1;
    bool produced=false;
};
inline organization::Decision region(const organization::Input& in) {
    if (in.site_count>0) {
        const auto& site=in.sites[in.ordinal%in.site_count];
        if (site.solo(in.frame,in.frame.clock.self))
            return organization::work(site.group.id,"mesh-regional-production");
    }
    return organization::independent(in);
}
inline organization::Decision choose(State& state,const organization::Input& in,bool control) {
    using namespace organization;
    const bool fresh=state.last_round!=in.frame.clock.round;
    state.last_round=in.frame.clock.round;
    if (!in.frame.valid() || in.ordinal<0) return independent(in);
    state.produced|=in.actual_payout || in.actual_bank;
    if (control || in.ordinal>=3 || !state.produced || !in.safe_now ||
        in.frame.own.actor.channel!=Channel::none || in.phase==Phase::hold ||
        in.phase==Phase::bank || in.phase==Phase::recover) return region(in);
    const int round=in.frame.clock.round;
    for (int edge_index=0; edge_index<2; ++edge_index) {
        if (in.ordinal!=edge_index && in.ordinal!=edge_index+1) continue;
        auto& edge=state.edges[edge_index];
        if (edge.epoch!=round/64) edge=Edge{round/64};
        const int residue=round%32, first=edge_index*16;
        const int period=round/32;
        const bool appointment=residue>=first && residue<first+4;
        if (!appointment || edge.retired) continue;
        if (edge.period!=period) {
            edge.period=period; edge.waits=0; edge.met=false; edge.finished=false;
        }
        if (edge.finished) continue;
        const int partner_ordinal=in.ordinal==edge_index?edge_index+1:edge_index;
        const auto* partner=in.ordinal_actor(partner_ordinal);
        if (ready(partner)) {
            edge.met=true; edge.finished=true;
            // Contact only enables aid that is personally useful right now.
            for (int i=0; i<in.site_count; ++i) {
                const int group=in.sites[i].group.id;
                if (joint(in,group) && useful(partner,group) &&
                    useful(&in.frame.own.actor,group)) return work(group,"mesh-current-contact-aid");
            }
            return region(in);
        }
        if (fresh && edge.waits>=2) {
            edge.finished=true;
            if (++edge.misses>=2) edge.retired=true;
            return region(in);
        }
        if (fresh) ++edge.waits;
        if (in.site_count>0) {
            const auto& site=in.sites[edge_index%in.site_count];
            return waypoint(Intent::approach,site.group.id,site.group.approach,
                            "mesh-two-turn-edge-checkpoint");
        }
    }
    return region(in);
}
struct Policy {
    State state;
    static const organization::Profile& profile() { return profile_value; }
    organization::Decision choose(const organization::Input& in,bool control) {
        return mesh::choose(state,in,control);
    }
};
}
