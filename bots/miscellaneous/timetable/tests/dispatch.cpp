#include "dispatch.h"
#include <cassert>
#include <cmath>

int main()
{
    // Exercise the production dispatch loop, including >10 optimistic but unproductive jobs.
    tt::DispatchCandidate candidates[12];
    for (int i = 0; i < 11; ++i) candidates[i] = {i, 100.0f};
    candidates[11] = {11, tt::dispatch_upper(100, 10, 6)};
    int calls = 0;
    auto score = [&](int g, bool* party) {
        ++calls;
        *party = false;
        return g == 11 ? 100.0f / 16.0f : 0.0f;
    };
    const auto best = tt::ordered_dispatch(candidates, 12, score, [] { return true; });
    assert(best.group == 11 && best.rate == 6.25f && calls == 12);
    // The old full-health score is below the feasible reward rate; it cannot prune this job.
    assert(100.0f / (10 + 6 + 10000.0f / 100) < best.rate);
    tt::DispatchCandidate pruned[] = {{1, 7}, {2, 2}, {3, 1}};
    calls = 0;
    const auto early = tt::ordered_dispatch(pruned, 3, [&](int g, bool* p) {
        ++calls; *p = true; return g == 1 ? 6.0f : 0.0f;
    }, [] { return true; });
    assert(early.group == 1 && early.party && calls == 1);
    tt::DispatchCandidate budgeted[] = {{1,7}};
    const auto none = tt::ordered_dispatch(budgeted, 1, score, [] { return false; });
    assert(none.group == -1 && none.scored == 0);
    // Bound accounts for the owner value bonus and the largest party preference.
    const float owner = tt::dispatch_upper(100 * 1.15f, 10, 6);
    assert(owner >= 1.3f * 115.0f / 16.0f);
}
