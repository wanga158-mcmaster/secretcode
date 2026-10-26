#pragma once
#include "../../common/src/events.h"

namespace p11_itinerary {
struct RouteJob {
    p11::Candidate candidate;
    int x=0,y=0;
};

namespace detail {
inline bool valid(const p11::Candidate&c) {
    return c.id>=0 && c.travel>=0 && c.clear>=0 && (c.ready||c.clear<10000)
        && c.capture>=0 && c.altar>=0 && c.gems>=0 && c.gold>=0 && c.allies>=0
        && std::isfinite(c.loss) && c.loss>=0;
}

// A conditional short SMDP rollout: each job has fixed travel, clear, capture
// and loss costs. The first leg retains the runtime's origin-to-entry cost;
// the second uses the full public-anchor Chebyshev distance. No attack-range
// subtraction, ability overlap, healing or future equipment benefit is added.
inline double route_value(const p11::State&s,const RouteJob&first,const RouteJob*second=nullptr) {
    constexpr double impossible=-std::numeric_limits<double>::infinity();
    if(second&&first.candidate.id==second->candidate.id)return impossible;
    const std::array<const RouteJob*,2> route{&first,second};
    const int count=second?2:1;
    const long long horizon=static_cast<long long>(s.limit)-s.round;
    long long clock=0;
    int gems=0;
    double loss=0,gold_value=0;
    for(int i=0;i<count;++i) {
        const auto&job=*route[i];const auto&c=job.candidate;
        if(!valid(c))return impossible;
        const long long travel=i==0?c.travel:std::max(
            std::abs(static_cast<long long>(job.x)-route[i-1]->x),
            std::abs(static_cast<long long>(job.y)-route[i-1]->y));
        clock+=travel+c.clear+c.capture;
        loss+=c.loss;
        if(clock>horizon || s.hp-loss<s.maximum*.25)return impossible;
        gems+=c.gems;
        const int spread=(c.id*3+s.identity*11)%7;
        // Gold pays at THIS capture, before any later job or altar leg.
        // Keep Tempo's financial constants, ally/spread costs, commitment and
        // non-field shop delay; no second-job prediction changes the gear.
        gold_value+=(s.mature?.04:3.)*c.gold/(1.+clock+(s.field_shop?0:s.recall+1)
            +12*c.allies+spread)*(c.committed?1.10:1.);
    }
    const auto&last=route[count-1]->candidate;
    const long long bank=clock+last.altar+p11::tribute_ticks(s.bag+gems,
        s.tribute_multiplier,s.tribute_base,s.tribute_minimum);
    if(gems>0&&bank>horizon)return impossible;
    double value=gold_value;
    for(int i=0;i<count;++i) {
        const auto&c=route[i]->candidate;
        const int spread=(c.id*3+s.identity*11)%7;
        // All new gems are carried to the last job's altar and complete one
        // tribute whose duration includes the existing bag. The bag is a
        // timing obligation, not a reward earned again by this route.
        value+=3.*c.gems/(1.+bank+12*c.allies+spread)*(c.committed?1.10:1.);
    }
    return value;
}
} // namespace detail

inline int select_route(const p11::State&s,std::span<const RouteJob>jobs) {
    // Production supplies at most three completed frontier candidates. Bound
    // defensive callers to the same prefix: <=3 singles and <=6 ordered pairs.
    const int count=int(std::min(jobs.size(),std::size_t(3)));
    int first=-1,second=-1;
    double best=-std::numeric_limits<double>::infinity();
    auto consider=[&](int a,int b) {
        const double value=detail::route_value(s,jobs[a],b<0?nullptr:&jobs[b]);
        const int aid=jobs[a].candidate.id,bid=b<0?-1:jobs[b].candidate.id;
        const bool tie=value==best && (aid<first||(aid==first&&bid<second));
        if(std::isfinite(value)&&(value>best||tie)) {
            best=value;first=aid;second=bid;
        }
    };
    for(int a=0;a<count;++a) {
        consider(a,-1);
        for(int b=0;b<count;++b)if(a!=b)consider(a,b);
    }
    return first;
}
} // namespace p11_itinerary
