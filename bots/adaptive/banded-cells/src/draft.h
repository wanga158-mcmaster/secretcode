#pragma once
#include "profile.h"
#include "../../common/src/draft.h"

namespace p8::banded_cells {
using common::DraftInputs;
using common::DraftCandidate;
using common::DraftRole;
using common::DraftDecision;
inline DraftRole draft_role(int ordinal) {
    return ordinal == 0 || ordinal == 1 ? DraftRole::collector :
        ordinal == 2 ? DraftRole::home_production :
        ordinal == 3 || ordinal == 4 ? DraftRole::safe_helper : DraftRole::independent;
}
inline DraftDecision select_draft(const DraftInputs& in, std::span<const DraftCandidate> candidates) {
    auto decision = common::select_draft(in,candidates,&draft_priority(in.ordinal));
    decision.role = draft_role(in.ordinal);
    return decision;
}
} // namespace p8::banded_cells
