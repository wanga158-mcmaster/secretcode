#pragma once
#include "profile.h"
#include "../../common/src/draft.h"
namespace p8::common_cause_firewall {
using common::DraftInputs; using common::DraftCandidate; using common::DraftDecision;
inline DraftDecision select_draft(const DraftInputs& in,std::span<const DraftCandidate> candidates) {
    // The current pinned SDK's field equipment shop is unique to actual Tavi;
    // the other compartment and extensions retain the full eight-pick pool.
    if (in.ordinal==0) {
        DraftDecision none; none.role=common::DraftRole::independent;
        if (candidates.size()>champion_pool.size()) return none;
        for (std::size_t i=0;i<candidates.size();++i) if (candidates[i].champion==p6::Champion::tavi) {
            auto d=common::select_draft(in,candidates.subspan(i,1),&field_priority);
            if (d.candidate>=0) { d.candidate=static_cast<int>(i); d.role=common::DraftRole::independent; return d; }
        }
        return none;
    }
    auto d=common::select_draft(in,candidates,&draft_priority(in.ordinal));
    d.role=in.ordinal==1?common::DraftRole::collector:in.ordinal==2?common::DraftRole::safe_helper:common::DraftRole::independent;
    return d;
}
} // namespace p8::common_cause_firewall
