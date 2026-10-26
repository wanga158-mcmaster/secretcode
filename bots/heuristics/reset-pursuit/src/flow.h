#pragma once
#include <array>
#include <cstddef>

namespace h12_reset_pursuit {
struct FlowResult {
    unsigned basics=0,casts=0,attempts=0,refreshes=0,different_hits=0;
    bool first_refresh=false;
    int first=-1,second=-1;
};
// Both the live SDK adapter and fixtures implement this small observation/action
// interface. Targets are a six-entry local menu, refreshed after every action.
// legal() includes visibility, life, HP and a fresh attack mask; it does NOT
// require the attack allowance, which a reset can restore.
template<class World> FlowResult execute(World&w) {
    FlowResult result;
    if(!w.enough_fuel()||!w.can_attack())return result;
    const int first=w.choose(-1);
    if(first<0||!w.legal(first))return result;
    const int before=w.cooldown();
    ++result.attempts;
    if(!w.basic(first))return result;
    result.first=first;++result.basics;
    const bool dead=!w.alive(first);
    const int after=w.cooldown();
    if(dead&&before>0&&after==0){++result.refreshes;result.first_refresh=true;}
    if(!w.enough_fuel()||w.can_attack()||!w.can_cast()||!w.reset_ready())return result;
    // A kill must retarget a DIFFERENT freshly legal unit. A surviving target
    // remains eligible for an ordinary two-hit reset when the reset was ready.
    const int next=w.choose(dead?first:-1);
    if(next<0||!w.legal(next))return result;
    ++result.attempts;
    if(!w.reset())return result;
    ++result.casts;
    if(!w.enough_fuel()||!w.can_attack()||!w.legal(next))return result;
    const int second_before=w.cooldown();
    ++result.attempts;
    if(w.basic(next)) {
        ++result.basics;result.second=next;result.different_hits+=next!=first;
        if(!w.alive(next)&&second_before>0&&w.cooldown()==0)++result.refreshes;
    }
    // Never spend a second cast, even if B's death refreshes cooldown again.
    return result;
}
} // namespace h12_reset_pursuit
