#pragma once
#include "../../common/src/events.h"
#include <cstdint>

namespace p11_reserve {
// Repeat the current route's clear/capture cost only as an amortization proxy.
// This does not schedule future camps or credit any healing to current health.
inline int expedition_capacity(const p11::State&s,const p11::Candidate&c) {
    if(!std::isfinite(s.hp)||!std::isfinite(s.maximum)||!std::isfinite(c.loss)
       ||s.hp<0||s.maximum<=0||c.loss<0||s.recall<0||c.travel<0||c.clear<0||c.capture<0)return 0;
    const std::int64_t horizon=std::int64_t(s.limit)-s.round;
    if(horizon<=0)return 0;
    const auto repeat=std::max<std::int64_t>(1,std::int64_t(c.travel)+c.clear+c.capture);
    const int cap=int(std::min<std::int64_t>(10000,horizon/repeat));
    if(cap==0||c.ready||c.loss<=0)return cap;

    const double available=std::max(0.,double(s.hp)-.25*double(s.maximum));
    const double loss=c.loss,ratio=available/loss;
    int count=ratio>=cap?cap:int(ratio);
    // Division can land on an integer threshold. The product decides whether
    // that many conservative clears actually fit the available health.
    while(count>0&&double(count)*loss>available)--count;
    while(count<cap&&double(count+1)*loss<=available)++count;
    return count;
}

// -1 is inadmissible. Otherwise return an integer renewal charge per clear,
// not a literal next-capture delay. Recall, one spawn healing end, and return
// travel are a conditional lower bound; current-route travel is the proxy.
inline int recovery_ticks(const p11::State&s,const p11::Candidate&c) {
    const int capacity=expedition_capacity(s,c);
    if(capacity==0)return -1;
    if(c.ready||c.loss<=0)return 0;
    const std::int64_t cycle=std::int64_t(s.recall)+1+c.travel;
    const std::int64_t tax=cycle/capacity+(cycle%capacity!=0);
    return tax<=std::numeric_limits<int>::max()?int(tax):-1;
}
} // namespace p11_reserve
