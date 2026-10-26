#include "availability.h"
#include <cassert>

int main()
{
    bool dead = false, reward = false;
    int due = -1;
    tt::observe_lifecycle(dead, reward, due, tt::Lifecycle::reward, 10);
    assert(dead && reward && due == -1);
    // A blocked group remains unavailable at timer zero, including repeated observations.
    tt::observe_lifecycle(dead, reward, due, tt::Lifecycle::respawning, 20, 0);
    assert(dead && !reward && due == 20 && tt::observed_unavailable(dead, reward, 20, 20));
    tt::observe_lifecycle(dead, reward, due, tt::Lifecycle::respawning, 200, 0);
    assert(dead && !reward && due == 200);
    assert(!tt::observed_unavailable(dead, reward, 200, 201)); // stale is unknown, not proof
    tt::observe_lifecycle(dead, reward, due, tt::Lifecycle::alive, 201);
    assert(!dead && !reward && due == -1);
    // Abandoning a capture leaves the last observed reward intact until new evidence arrives.
    tt::observe_lifecycle(dead, reward, due, tt::Lifecycle::reward, 210);
    assert(dead && reward);
    assert(!tt::channel_actions_available(false, false, false));
    assert(tt::channel_actions_available(true, false, false));
    assert(tt::channel_actions_available(false, true, false));
    assert(tt::channel_actions_available(false, false, true));
}
