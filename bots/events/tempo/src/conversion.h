#pragma once
#include <algorithm>
#include <cmath>
#include <span>

namespace p11_tempo::conversion {
struct Point {int x=0,y=0;};
struct Threat {Point origin;int radius=0;float damage=0;bool next_cast_ready=false;};
struct Input {int bag=0;float hp=0,shield_upper_bound=0;int payment_ticks=0,observed_round=-1;};

inline bool exhausts_defence(const Input&input,float damage) {
    if(!std::isfinite(damage)||!std::isfinite(input.hp)||!std::isfinite(input.shield_upper_bound))return false;
    // Adding two float32 defences can round down to the candidate damage.
    // Compare the post-shield amount in wider precision instead.
    return double(damage)-double(std::max(0.f,input.shield_upper_bound))>=double(input.hp);
}

// Tempo's prospective source pays only after its clear/capture event. A
// known lethal cast before that event cannot convert the carried gem asset.
// Current shared-cast usage is separate from a zero cooldown at the enemy's
// next owner start. Shield is deliberately an optimistic defence bound.
inline bool unpaid_lethal_step(const Input&input,Point next,std::span<const Threat> threats) {
    if(input.bag<=0||input.hp<=0||input.payment_ticks<=1||!std::isfinite(input.hp)
        ||!std::isfinite(input.shield_upper_bound))return false;
    for(const auto&t:threats) {
        if(!t.next_cast_ready||t.radius<0||!exhausts_defence(input,t.damage))continue;
        const int distance=std::max(std::abs(next.x-t.origin.x),std::abs(next.y-t.origin.y));
        if(distance<=t.radius)return true;
    }
    return false;
}
}
