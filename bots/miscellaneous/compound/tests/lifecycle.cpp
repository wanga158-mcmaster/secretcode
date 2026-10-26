#include <cstdio>
#include "lifecycle.h"

int main()
{
    // Countdown predictions may reach/past eligibility without becoming either monsters or reward.
    auto observed = cl::observed_respawn(100, 5);
    if (observed.can_capture(100) || observed.can_capture(105) || observed.phase != cl::Phase::respawning) return 1;
    // A fresh zero-timer state still says unavailable. Reserving its member tile would prevent renewal.
    observed = cl::observed_respawn(105, 0);
    if (!observed.suppressed(105) || observed.can_capture(105)) return 2;
    // Fresh host reward alone authorizes capture; a stale reward observation does not.
    observed = { cl::Phase::reward, 106, 106 };
    if (!observed.can_capture(106) || observed.can_capture(107) || observed.is_respawning(106)) return 3;
    // A failed or unavailable observation grants neither capture nor a completed-reservation fact.
    observed = { cl::Phase::unknown, 107, -1 };
    if (observed.can_capture(107) || observed.is_respawning(107)) return 4;
    // Fresh native respawn proves that this capture reservation has ended, even if its payout was shared.
    observed = cl::observed_respawn(108, 60);
    if (!observed.is_respawning(108) || observed.can_capture(108) || observed.suppressed(108)) return 5;
    // Later actual monsters replace the prediction; their presence cannot authorize a capture.
    observed = { cl::Phase::alive, 168, 168 };
    if (observed.can_capture(168) || observed.is_respawning(168)) return 6;
    std::puts("lifecycle: observations, reservations and predictions remain distinct");
    return 0;
}
