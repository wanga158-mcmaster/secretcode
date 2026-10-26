#pragma once
// SDK-free production candidate dispatcher, called by Bot::choose_target_safe.
// Full score callbacks may be expensive; prune only by an admissible upper bound.
#include <algorithm>

namespace tt {

struct DispatchCandidate { int group; float upper; };
struct DispatchChoice { int group = -1; float rate = 0; bool party = false; int scored = 0; };

inline float dispatch_upper(float value, float travel, float capture)
{
    // Current rate_n can subtract half of travel when charging an initial heal trip.
    // All other labour terms are nonnegative. The largest party preference is1.3.
    return value * 1.3f / (capture + 0.5f * std::max(0.0f, travel));
}

template<class Score, class Budget>
DispatchChoice ordered_dispatch(DispatchCandidate* candidates, int count, Score score, Budget budget)
{
    for (int i = 1; i < count; ++i) {
        const auto candidate = candidates[i];
        int at = i;
        while (at > 0 && candidates[at-1].upper < candidate.upper) {
            candidates[at] = candidates[at-1];
            --at;
        }
        candidates[at] = candidate;
    }
    DispatchChoice best;
    for (int i = 0; i < count; ++i) {
        if (candidates[i].upper <= best.rate || !budget()) break;
        bool party = false;
        const float rate = score(candidates[i].group, &party);
        ++best.scored;
        if (rate > best.rate) {
            best.group = candidates[i].group;
            best.rate = rate;
            best.party = party;
        }
    }
    return best;
}

} // namespace tt
