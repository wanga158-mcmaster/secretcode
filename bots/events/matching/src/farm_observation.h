#pragma once
#include <optional>
#include "../../common/src/events.h"

namespace p11_matching {
struct FarmObservation {
    int x=0,y=0,range=0,type=0,state=0;
    float hp=0,maximum=0,strength=0,focus=0;
};

// The caller supplies its checked current self-record reader. Every field is
// copied from that one record; no action availability is cached for execution.
template<class Read>
FarmObservation observe_self(Read read) {
    const auto record=read();
    return {int(record.x),int(record.y),int(record.attack_range),int(record.type),int(record.state),
        record.health,record.max_health,record.strength,record.focus};
}

struct BasicTargetView {
    int id=-1;
    float hp=0,incoming=0;
};

// Runtime supplies only legally observed alive members of the selected group,
// in their original vision order. Fresh empty means no visible target; stale
// means no decision at all. Equal ranks preserve the first observation.
inline std::optional<int> baseline_from_detail(std::span<const BasicTargetView> views,
        float damage,int observed_round,int current_round) {
    if(observed_round!=current_round)return std::nullopt;
    int selected=-1;
    float weakest=std::numeric_limits<float>::max();
    for(const auto&view:views) {
        const float rank=p11::target_rank(view.hp,view.incoming,damage);
        if(rank<weakest){weakest=rank;selected=view.id;}
    }
    return selected;
}
}
