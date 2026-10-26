#pragma once
#include "profile.h"
#include "../../common/src/draft.h"
namespace p8::assay_guild {
using common::DraftInputs; using common::DraftCandidate; using common::DraftDecision; using common::DraftRole;
inline DraftDecision select_draft(const DraftInputs& in,std::span<const DraftCandidate> candidates) {
    auto out=common::select_draft(in,candidates,&draft_priority(in.ordinal));
    out.role=in.ordinal==0?DraftRole::home_production:in.ordinal==1?DraftRole::collector:DraftRole::independent;
    return out;
}
}
