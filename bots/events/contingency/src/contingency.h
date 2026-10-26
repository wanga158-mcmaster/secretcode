#pragma once
#include "../../tempo/src/policy.h"

namespace p11_contingency {
struct RouteJob {
    p11::Candidate candidate;
    int x=0,y=0;
};

namespace detail {
inline double utility(const p11::State&s,const p11::Candidate&c) {
    if(!p11::feasible(s,c))return 0;
    const double value=p11_tempo::Policy::score(s,c);
    return std::isfinite(value)&&value>0?value:0;
}
} // namespace detail

// Exact minimax regret for a declared finite sensitivity problem, not a bound
// on actual uncertainty or a survival certificate. Scenarios are baseline and
// one isolated extra event for each job; no probability or opponent model is
// assigned. The runtime executes only the selected unmodified job.
inline int select_route(const p11::State&s,std::span<const RouteJob>jobs) {
    if(jobs.size()>3)return -1;
    const int count=int(jobs.size());
    // The ID-only caller cannot distinguish duplicate/negative job identities.
    for(int i=0;i<count;++i) {
        if(jobs[i].candidate.id<0)return -1;
        for(int j=0;j<i;++j)if(jobs[i].candidate.id==jobs[j].candidate.id)return -1;
    }
    // Action zero is explicit wait, utility zero in every scenario. Each job
    // supplies one additional scenario; all other jobs retain baseline costs.
    std::array<std::array<double,4>,4> value{};
    std::array<double,4> best{};
    for(int scenario=0;scenario<=count;++scenario) {
        for(int action=1;action<=count;++action) {
            auto c=jobs[action-1].candidate;
            if(scenario==action) {
                if(!c.ready&&c.clear>0) {
                    ++c.clear;
                    c.loss+=c.incoming*(s.jungle_shards>=5?.7f:1.f);
                } else ++c.capture;
            }
            value[action][scenario]=detail::utility(s,c);
            best[scenario]=std::max(best[scenario],value[action][scenario]);
        }
    }
    int selected=-1;
    double worst=*std::max_element(best.begin(),best.begin()+count+1),baseline=0;
    for(int action=1;action<=count;++action) {
        double regret=0;
        for(int scenario=0;scenario<=count;++scenario)
            regret=std::max(regret,best[scenario]-value[action][scenario]);
        const int id=jobs[action-1].candidate.id;
        if(regret<worst || (regret==worst && (value[action][0]>baseline
                || (value[action][0]==baseline&&id<selected)))) {
            selected=id;worst=regret;baseline=value[action][0];
        }
    }
    return selected;
}
} // namespace p11_contingency
