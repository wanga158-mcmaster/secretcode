#pragma once

namespace h12_satisficer {
// Capture is an occupancy condition, not a channel or an action allowance.
// The caller retains its pad and existing fighting routine; this flow never
// requests travel and never takes over a contested or unfinished capture.
template<class Enemies, class InRange, class Fight>
bool work_while_capturing(bool own_pad,bool reward_available,bool contested,
                         const Enemies&enemies,InRange in_range,Fight fight) {
    if(!own_pad||!reward_available||contested)return false;
    for(const auto&enemy:enemies)
        if(in_range(enemy)&&fight(enemy))return true;
    return false;
}
}
