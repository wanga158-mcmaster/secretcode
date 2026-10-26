#pragma once
#include "return_budget.h"

namespace mosaic::melee_exit {
// Actual adapter facts, all refreshed in one owner input. The admitted fuel tail
// includes the Basic, body refresh, exit reprice and one actual move attempt.
struct Ready {
    bool can_attack{}, target_in_range{}, first_step_open{}, fuel_tail{};
    int basic_range{};
};

inline return_budget::Quote admit(const return_budget::Trace& trace,
                                  const return_budget::State& state, Ready ready) {
    if (!ready.can_attack || !ready.target_in_range || !ready.first_step_open ||
        !ready.fuel_tail || ready.basic_range != 1 || !state.can_move_now ||
        state.additional_forced_waits || state.compute_waits)
        return {};
    const auto quote = trace.quote(state);
    return quote.steps ? quote : return_budget::Quote{};
}

// A refused nonordinary action does not create a new ordinary trace edge. If
// native authority says its pose is unchanged, the old geometry remains usable;
// the adapter must still reprice it and refresh occupancy before dispatch.
inline void observe_nonordinary(return_budget::Trace& trace,
                                return_budget::Point actual) {
    if (!trace.at(actual)) trace.clear();
}
}
