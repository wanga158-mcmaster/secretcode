#pragma once
#include "model-runtime.h"

namespace plan4::runtime {
struct GroupBoundary {
    int actor = -1, round = -1, group_id = -1, respawn_rounds = -1;
    Lifecycle lifecycle = Lifecycle::unknown;
    Provenance provenance{};
    bool actor_alive = false, personally_visible = false;
};
enum class CaptureGap { none, actor, missed_round, hidden, lifecycle, public_metadata };
struct CaptureEvidence {
    CapturedGroup receipt{};
    bool supported = false;
    CaptureGap gap = CaptureGap::none;
    // The phase event is inferred from two exact lifecycle observations.
    Status event_status = Status::derived;
    bool reward_attribution_known = false;
};
inline bool fresh_lifecycle(const GroupBoundary& b) {
    return b.personally_visible && b.provenance.observed_round == b.round &&
        b.provenance.source == Source::personal_vision && b.provenance.status == Status::exact_now;
}
inline CaptureEvidence capture_between_owner_turns(const GroupBoundary& previous_end, const GroupBoundary& current_start) {
    CaptureEvidence result;
    if (previous_end.actor < 0 || previous_end.actor != current_start.actor ||
        !previous_end.actor_alive || !current_start.actor_alive) result.gap = CaptureGap::actor;
    else if (previous_end.round < 0 || std::int64_t(previous_end.round) + 1 != current_start.round) result.gap = CaptureGap::missed_round;
    else if (!fresh_lifecycle(previous_end) || !fresh_lifecycle(current_start)) result.gap = CaptureGap::hidden;
    else if (previous_end.group_id < 0 || previous_end.group_id != current_start.group_id ||
        previous_end.respawn_rounds < 0 || previous_end.respawn_rounds != current_start.respawn_rounds) result.gap = CaptureGap::public_metadata;
    else if (previous_end.lifecycle != Lifecycle::cleared || current_start.lifecycle != Lifecycle::respawning)
        result.gap = CaptureGap::lifecycle;
    else {
        // Captures occur at round start, before actor channels. Consecutive
        // owner boundaries contain exactly one possible capture phase. Bags
        // are deliberately absent: kills and tribute can change them too.
        result.supported = true;
        result.receipt = {current_start.group_id, current_start.round, current_start.respawn_rounds,
            current_start.provenance, true};
        result.receipt.event_status = Status::derived;
        result.receipt.origin = CaptureOrigin::lifecycle_boundaries;
    }
    return result;
}
} // namespace plan4::runtime
