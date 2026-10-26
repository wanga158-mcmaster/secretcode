#pragma once
// Pure control-history updates shared by production and explicitly declared source augmentation.
// These updates do not recover an expert's private state. A source producer must declare its initial
// condition and distinguish observed attempts from any derived goal-conditioning transitions.
#include "cand.h"

namespace zero::student {

inline void begin_commands(Obs& observation) { observation.command_index = 0; }
inline void record_command(Obs& observation) { ++observation.command_index; }

inline void set_goal(Memory& memory, const Cand& goal, int round)
{
    if (goal.kind != memory.goal_kind || goal.target != memory.goal_target) memory.goal_since = round;
    memory.goal_kind = goal.kind;
    memory.goal_target = goal.target;
    memory.goal_decided = round;
}

}  // namespace zero::student
