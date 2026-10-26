#pragma once
#include "profile.h"
#include "../../common/src/draft.h"

namespace p8::reversible_workshop {
using common::DraftInputs;
using common::DraftCandidate;
using common::DraftRole;
using common::DraftDecision;
inline DraftRole draft_role(int ordinal) {
    return ordinal == 0 ? DraftRole::collector : ordinal == 1 ? DraftRole::home_production :
        ordinal == 2 ? DraftRole::safe_helper : DraftRole::independent;
}
inline DraftDecision select_draft(const DraftInputs& in, std::span<const DraftCandidate> candidates) {
    if (in.ordinal==0) {
        DraftDecision unavailable; unavailable.role=DraftRole::collector;
        if (candidates.size()>champion_pool.size()) return unavailable;
        for (std::size_t i=0;i<candidates.size();++i) if (reversible_collector(candidates[i].champion)) {
            auto decision=common::select_draft(in,candidates.subspan(i,1),&draft_priority(0));
            decision.role=DraftRole::collector;
            if (decision.candidate>=0) { decision.candidate=static_cast<int>(i); return decision; }
        }
        return unavailable;
    }
    auto decision = common::select_draft(in,candidates,&draft_priority(in.ordinal));
    decision.role = draft_role(in.ordinal);
    return decision;
}
} // namespace p8::reversible_workshop
