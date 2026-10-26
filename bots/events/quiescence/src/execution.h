#pragma once
#include "micro.h"

namespace p11_quiescence {
// Execute only the admitted, bounded current-turn choice. The SDK adapter
// validates each primitive freshly and may complete reset/basic atomically.
// Do not spend the original input-query cost again between its primitives.
template<class Now,class Fuel,class Perform>
int execute(const Choice&choice,int observed_round,Now now,Fuel fuel,Perform perform) {
    const int count=std::clamp(choice.count,0,3);
    int completed=0;
    while(completed<count) {
        if(now()!=observed_round)break;
        // Preserve the first legal fallback. Further callbacks need the
        // adapter's existing continuation reserve, not a fresh-search reserve.
        if(completed&&fuel()<=10000)break;
        const auto action=choice.actions[completed];
        if(action==Action::wait)break;
        const auto next=completed+1<count?choice.actions[completed+1]:Action::wait;
        const int consumed=perform(completed,action,next);
        if(consumed<1||consumed>2||consumed>count-completed)break;
        completed+=consumed;
    }
    return completed;
}
} // namespace p11_quiescence
