#pragma once
#include <algorithm>
#include <type_traits>
namespace p10 {
inline int action_reserve(int entry,int measured_fallback=0,int margin=4000){return std::max(entry*40/100,std::max(18000,measured_fallback+margin));}
// The clock read follows all fallible legality queries. This same boundary is
// exercised by fixtures with a query that suspends into the next owner turn.
template<class Clock,class Legal,class Action>
bool guarded_action(int round,Clock now,Legal legal,Action action){
 if(now()!=round||!legal()||now()!=round)return false;
 if constexpr(std::is_same_v<decltype(action()),bool>)return action();
 else{action();return true;}
}
}
