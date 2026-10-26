#pragma once
#include "protocol.h"

namespace wb {
// One routing contract per follow action. Combat anchors describe the leader's current
// fight/retreat location; the retained production objective is not their shortcut target.
// Callbacks own the existing bounded navigation and any diagnostic recording.
template<class Anchor,class Group>
bool dispatch_follow_route(Mode mode,bool follower,int distance,int far_cutoff,
                           bool has_objective,Anchor&& anchor,Group&& group) {
    if (follower && (mode==Mode::fight || mode==Mode::retreat)) return anchor(4);
    if (distance>far_cutoff && has_objective) return group(1);
    if (anchor(2)) return true;
    return has_objective && group(3);
}
} // namespace wb
