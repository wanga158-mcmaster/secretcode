#pragma once
#include "student.h"

namespace zero::student {

// Opt-in computation-only dash builder. It preserves argument_candidates' status,
// complete payload order and all raw feature bits; every other family delegates.
// The existing builder remains the default and the reference implementation.
ActionBuild argument_candidates_dash_fast(const Context& ctx, ActionType type, std::vector<Cand>& out);

}  // namespace zero::student
