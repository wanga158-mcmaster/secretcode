#pragma once
// Optional computation-only BUY builder. Complete rows and the student feature ABI
// match argument_candidates; other action families delegate unchanged.
#include "student.h"

namespace zero::student {

ActionBuild argument_candidates_buy_fast(const Context& ctx, ActionType type,
                                         std::vector<Cand>& out);

}  // namespace zero::student
