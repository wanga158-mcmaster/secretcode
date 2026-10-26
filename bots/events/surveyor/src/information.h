#pragma once
#include "../../common/src/events.h"

namespace p11_surveyor {
// A completed legal detail observation, rather than elapsed time or a planned
// visit, pays for advancing the information frontier. With a fixed eligible
// menu and room for three distinct entries, every ID is probed within one
// cycle, even while both the incumbent and conservative fallback persist.
class InformationClock {
    int next_ = 0;
    int pending_ = -1;
public:
    void reset() { next_=0;pending_=-1; }

    template<class Policy>
    std::vector<int> frontier(const p11::State&state,
            std::span<const p11::Candidate> candidates,int limit,
            int incumbent,int fallback) {
        struct Rank { double value;int id; };
        std::vector<Rank> ranked;ranked.reserve(candidates.size());
        int first=-1,following=-1;
        for(const auto&candidate:candidates) {
            const double value=Policy::screen(state,candidate);
            if(candidate.id<0||!std::isfinite(value))continue;
            ranked.push_back({value,candidate.id});
            if(first<0||candidate.id<first)first=candidate.id;
            if(candidate.id>=next_&&(following<0||candidate.id<following))following=candidate.id;
        }
        // Planning can replace a probe that disappeared from the eligible
        // menu, but cannot consume its cursor position. An unobserved probe in
        // a fixed menu is therefore proposed again after fuel or epoch cuts.
        pending_=following>=0?following:first;
        std::stable_sort(ranked.begin(),ranked.end(),
            [](const Rank&a,const Rank&b){return a.value>b.value;});
        limit=std::clamp(limit,0,32);
        std::vector<int> order;order.reserve(std::min(std::size_t(limit),ranked.size()));
        auto add=[&](int id) {
            if(id<0||int(order.size())>=limit||std::find(order.begin(),order.end(),id)!=order.end())return;
            if(std::none_of(ranked.begin(),ranked.end(),[&](const Rank&row){return row.id==id;}))return;
            order.push_back(id);
        };
        add(fallback);add(incumbent);add(pending_);
        for(const auto&row:ranked)add(row.id);
        return order;
    }

    void observed(int id) {
        if(id<0||id!=pending_)return;
        next_=id==std::numeric_limits<int>::max()?0:id+1;
        pending_=-1;
    }
};
}
