#pragma once
#include "assignment.h"

namespace p11_matching {
// A three-job assignment needs three useful self edges before it can avoid
// delegating every useful job to its two peers. These are public-model edges;
// fresh detailed health/exposure can still invalidate them before assignment.
template<class Policy>
std::vector<int> detail_frontier(const p11::State&state,
        std::span<const p11::Candidate> candidates,int limit,
        int incumbent,int fallback,int explore) {
    limit=std::clamp(limit,0,32);
    if(limit==0)return {};
    std::vector<p11::Candidate> eligible;eligible.reserve(candidates.size());
    for(const auto&candidate:candidates) {
        const double value=p11_tempo::Policy::score(state,candidate);
        if(candidate.id>=0&&p11::feasible(state,candidate)&&std::isfinite(value)&&value>0)
            eligible.push_back(candidate);
    }
    auto order=p11::shortlist<Policy>(state,eligible,limit,incumbent,fallback);
    if(int(order.size())>limit)order.resize(limit);
    if(int(order.size())<limit) {
        const int owned=incumbent>=0?incumbent:fallback;
        const int alternative=fallback>=0&&fallback!=owned?fallback:explore;
        const auto legacy=p11::shortlist<Policy>(state,candidates,limit,owned,alternative);
        for(const int id:legacy) {
            if(int(order.size())>=limit)break;
            if(std::find(order.begin(),order.end(),id)==order.end())order.push_back(id);
        }
    }
    return order;
}
}
