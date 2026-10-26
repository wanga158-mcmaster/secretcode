#pragma once
#include "../../common/src/capture.h"

namespace h12_altar_options {
// Shared by both real reward-hold paths. Quarantine does not erase the clock:
// if departure is blocked, the next owner turn must keep trying to leave.
inline bool leave_capture(heuristics::CaptureWatch&watch,int source,int round,int capture,bool quarantined) {
    return watch.expired(source,round,capture)||quarantined;
}
inline void observe_capture(heuristics::CaptureWatch&watch,bool on_source,bool reward_available) {
    if(!on_source||!reward_available)watch.reset();
}
template<class Walk,class Work> bool leave_or_work(Walk&&walk,Work&&work) {
    if(walk())return true;
    return work();
}
} // namespace h12_altar_options
