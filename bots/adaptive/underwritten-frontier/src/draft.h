#pragma once
#include "profile.h"
#include "../../common/src/draft.h"
namespace p8::underwritten_frontier {
using common::DraftInputs; using common::DraftCandidate; using common::DraftDecision; using common::DraftRole;
inline DraftDecision select_draft(const DraftInputs& in,std::span<const DraftCandidate> candidates) {
    auto selected=common::select_draft(in,candidates,&draft_priority(in.ordinal));
    selected.role=in.ordinal==0?DraftRole::home_production:in.ordinal==2?DraftRole::collector:
        in.ordinal==3?DraftRole::safe_helper:DraftRole::independent;
    return selected;
}
} // namespace p8::underwritten_frontier
