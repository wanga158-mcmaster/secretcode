#pragma once
#include "profile.h"
#include "../../common/src/draft.h"
namespace p8::disagreement_partnership {
inline common::DraftDecision select_draft(const common::DraftInputs& in,std::span<const common::DraftCandidate> candidates) {
    auto d=common::select_draft(in,candidates,&priority(in.ordinal));
    d.role=in.ordinal==0?common::DraftRole::home_production:in.ordinal==1?common::DraftRole::home_conversion:
        in.ordinal==2?common::DraftRole::collector:common::DraftRole::independent; return d;
}
}
