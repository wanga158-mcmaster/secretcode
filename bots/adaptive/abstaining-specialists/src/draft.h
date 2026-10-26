#pragma once
#include "profile.h"
#include "../../common/src/draft.h"
namespace p8::abstaining_specialists {
using common::DraftInputs;using common::DraftCandidate;using common::DraftDecision;
inline DraftDecision select_draft(const DraftInputs& in,std::span<const DraftCandidate> candidates){
    auto out=common::select_draft(in,candidates,&draft_priority(in.ordinal));
    out.role=in.ordinal==0?common::DraftRole::collector:common::DraftRole::independent;return out;
}
}
