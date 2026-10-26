#pragma once
#include <array>
#include <cmath>
#include <limits>
#include <span>

namespace p11_triage {
struct FocusJob {
    int id=-1;
    int hits=0;
    double incoming=0;
};
inline constexpr int horizon=3;
inline constexpr std::size_t max_jobs=3;

// Three future basics, each followed by the next camp-attack phase. Jobs are
// already visible, alive and in basic range. Future basics are conditional on
// retaining that range; the group is assumed engaged with constant incoming
// weights. Other threats, abilities, resets, splash, shields, healing and
// retreat receive no credit. Only the first target is returned for fresh
// runtime observation and legality checks; this is not an action sequence.
inline int choose_focus(std::span<const FocusJob> jobs) {
    if(jobs.empty()||jobs.size()>max_jobs)return -1;
    std::array<FocusJob,max_jobs> ordered{};
    for(std::size_t i=0;i<jobs.size();++i) {
        const auto&job=jobs[i];
        // The upper weight bound keeps all nine possible survivor terms
        // representable without rescaling ordinary, exactly tied strengths.
        if(job.id<0||job.hits<=0||!std::isfinite(job.incoming)||job.incoming<0||
           job.incoming>std::numeric_limits<double>::max()/16)return -1;
        std::size_t at=i;
        while(at>0&&job.id<ordered[at-1].id){ordered[at]=ordered[at-1];--at;}
        ordered[at]=job;
    }
    for(std::size_t i=1;i<jobs.size();++i)if(ordered[i-1].id==ordered[i].id)return -1;

    std::array<int,max_jobs> remaining{},phases{};
    for(std::size_t i=0;i<jobs.size();++i)remaining[i]=ordered[i].hits;
    int best=-1,best_kills=-1;
    double best_exposure=0;
    const auto search=[&](auto&&self,int depth,int first)->void {
        int killed=0;
        for(std::size_t i=0;i<jobs.size();++i)killed+=remaining[i]==0;
        if(depth==horizon||killed==int(jobs.size())) {
            // Sum canonical job contributions at the leaf, so equal survivor
            // counts have identical arithmetic regardless of action order.
            double exposure=0;
            for(std::size_t i=0;i<jobs.size();++i)exposure+=ordered[i].incoming*phases[i];
            if(best<0||exposure<best_exposure||
               (exposure==best_exposure&&(killed>best_kills||
                (killed==best_kills&&first<best)))) {
                best=first;best_exposure=exposure;best_kills=killed;
            }
            return;
        }
        for(std::size_t target=0;target<jobs.size();++target)if(remaining[target]>0) {
            --remaining[target];
            for(std::size_t i=0;i<jobs.size();++i)phases[i]+=remaining[i]>0;
            self(self,depth+1,depth==0?ordered[target].id:first);
            for(std::size_t i=0;i<jobs.size();++i)phases[i]-=remaining[i]>0;
            ++remaining[target];
        }
    };
    search(search,0,-1);
    return best;
}
} // namespace p11_triage
