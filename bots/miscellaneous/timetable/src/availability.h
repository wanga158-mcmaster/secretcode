#pragma once
// Pure lifecycle model. A countdown predicts earliest eligibility, not actual respawn.
namespace tt {

enum class Lifecycle { alive, reward, respawning };

inline void observe_lifecycle(bool& dead, bool& reward, int& due,
                              Lifecycle observed, int round, int timer = 0)
{
    dead = observed != Lifecycle::alive;
    reward = observed == Lifecycle::reward;
    due = observed == Lifecycle::respawning ? round + timer : -1;
}

inline bool observed_unavailable(bool dead, bool reward, int seen, int round)
{
    return dead && !reward && seen == round;
}

inline bool channel_actions_available(bool move, bool attack, bool cast)
{
    return move || attack || cast;
}

} // namespace tt
