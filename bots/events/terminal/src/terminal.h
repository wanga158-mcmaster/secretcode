#pragma once
#include "../../common/src/events.h"

namespace p11_terminal {
struct RouteJob {
    p11::Candidate candidate;
    int x=0,y=0;
};

inline int finishing_job(const p11::State&s,std::span<const RouteJob>jobs) {
    // A one-job certificate conditional on the supplied fixed costs and the
    // shared health model. No allied bag, future capture or chance of success
    // is credited. Production supplies at most three completed candidates.
    const int count=int(std::min(jobs.size(),std::size_t(3)));
    int selected=-1,best=std::numeric_limits<int>::max();
    for(int i=0;i<count;++i) {
        const auto&c=jobs[i].candidate;
        if(c.id<0 || !p11::feasible(s,c) || c.gems+s.bag<s.needed_gems)continue;
        // Overshooting the target still carries and channels every actual gem.
        const int bank=c.bank_time(s);
        if(bank>s.limit-s.round)continue;
        if(bank<best || (bank==best && (selected<0 || c.id<selected))) {
            best=bank;selected=c.id;
        }
    }
    return selected;
}
} // namespace p11_terminal
