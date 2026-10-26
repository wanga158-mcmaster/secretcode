#pragma once
#include <algorithm>

namespace p11_overlap {
// Correct action counts into elapsed rounds from the current action phase.
// This clock assumes fresh allowances, one basic-only stationary monster,
// a free straight route, no abilities or other units, and no surviving allied
// capture progress. It is not a forecast for arbitrary engine geometry.
inline int capture_phase_correction(bool ready,int travel,int clear,
                                    int attack_range,int origin_spawn_distance) {
    if(ready)return travel>0?-1:0;
    if(clear<1)return 0;
    if(attack_range>1)
        return std::min(attack_range,origin_spawn_distance)-1;
    // A melee approach can share its final walk with the first attack. A
    // one-hit arrival already used that walk and needs a later capture step.
    return -1-(travel>0&&clear>1?1:0);
}

// Holding a cleared spawn can also channel tribute when the same tile is in
// altar range. Channel length uses the starting bag; a later capture can grow
// the bag before owner-start completion. This gate does not infer a timer.
inline bool can_overlap_capture_tribute(bool on_spawn,bool reward,bool contested,
                                       bool channel,int bag,int altar_distance,
                                       int submit_range,bool locked) {
    return on_spawn&&reward&&!contested&&!channel&&bag>0&&!locked
        &&altar_distance>=0&&submit_range>=0&&altar_distance<=submit_range;
}
} // namespace p11_overlap
