#pragma once
#include <optional>
#include <utility>
#include "assignment.h"

namespace p11_matching {
struct PeerObservation {
    Worker worker;
    int state=0;
};

// Read is the caller's visibility-aware host adapter. One optional record
// supplies every live field; hidden/absent records supply no worker at all.
template<class Read>
std::optional<PeerObservation> observe_worker(int handle,Read read) {
    const auto record=read(handle);
    if(!record)return std::nullopt;
    const auto&value=*record;
    return PeerObservation{Worker{handle,int(value.x),int(value.y),value.health,
        value.max_health,value.strength,true,int(value.type)},int(value.state)};
}
}
