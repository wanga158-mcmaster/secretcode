#pragma once
#include "profile.h"
#include "../../common/src/draft.h"
namespace p8::requalification_reserve {
inline common::DraftDecision select_draft(const common::DraftInputs& in,std::span<const common::DraftCandidate> candidates) {
    auto d=common::select_draft(in,candidates,&draft_priority(in.ordinal));
    d.role=in.ordinal==0?common::DraftRole::collector:in.ordinal==1?common::DraftRole::home_production:
        in.ordinal==2?common::DraftRole::home_conversion:common::DraftRole::independent; return d;
}
} // namespace p8::requalification_reserve
