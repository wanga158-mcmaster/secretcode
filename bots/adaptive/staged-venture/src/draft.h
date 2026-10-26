#pragma once
#include "profile.h"
#include "../../common/src/draft.h"
namespace p8::staged_venture {
using common::DraftInputs;using common::DraftCandidate;using common::DraftDecision;
inline DraftDecision select_draft(const DraftInputs& in,std::span<const DraftCandidate> candidates) {
    auto selected=common::select_draft(in,candidates,&draft_priority(in.ordinal));
    selected.role=in.ordinal==0?common::DraftRole::safe_helper:in.ordinal==1?
        common::DraftRole::collector:common::DraftRole::independent;
    return selected;
}
} // namespace p8::staged_venture
