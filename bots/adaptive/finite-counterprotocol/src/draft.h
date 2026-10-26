#pragma once
#include "profile.h"
#include "../../common/src/draft.h"
namespace p8::finite_counterprotocol {
inline common::DraftDecision select_draft(const common::DraftInputs& in,std::span<const common::DraftCandidate> candidates) {
    auto d=common::select_draft(in,candidates,&priority(in.ordinal));
    d.role=in.ordinal==0?common::DraftRole::collector:in.ordinal==1?common::DraftRole::safe_helper:
        in.ordinal==2?common::DraftRole::home_production:common::DraftRole::independent; return d;
}
}
