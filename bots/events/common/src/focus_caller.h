#pragma once
#include "events.h"

namespace p11::focus {
struct View {
    int id=-1;
    float hp=0,incoming=0;
    bool in_group=true,attackable=false;
};
enum class Reason { stale, unsupported, empty, no_attack, reserve, planned, rejected };
struct Decision {
    int selected=-1,baseline=-1,jobs=0,visible=0;
    Reason reason=Reason::empty;
};

// The SDK caller and fixtures share this entire admission/selection path.
// Live observations are owned by one round. No host query occurs here.
template<class Policy>
Decision select(std::span<const View> views,float damage,bool attack_ready,
                int fuel,int observed_round,int current_round) {
    Decision result;
    if(observed_round!=current_round){result.reason=Reason::stale;return result;}
    if(views.size()>512||!std::isfinite(damage)) {
        result.reason=Reason::unsupported;return result;
    }
    struct Ranked {typename Policy::FocusJob job;float rank;};
    std::array<Ranked,3> pool{};
    float baseline_rank=std::numeric_limits<float>::infinity();
    for(const auto&v:views) {
        if(!v.in_group||v.hp<=0)continue;
        if(v.id<0||!std::isfinite(v.hp)||!std::isfinite(v.incoming)||v.incoming<0) {
            result.reason=Reason::unsupported;result.selected=-1;return result;
        }
        ++result.visible;
        const float rank=p11::target_rank(v.hp,v.incoming,damage);
        if(rank<baseline_rank||(rank==baseline_rank&&(result.baseline<0||v.id<result.baseline))) {
            baseline_rank=rank;result.baseline=v.id;
        }
        if(!v.attackable||damage<=0)continue;
        const Ranked next{{v.id,p11::attacks(v.hp,damage),double(v.incoming)},rank};
        int at=0;
        while(at<result.jobs&&(pool[at].rank<rank
              ||(pool[at].rank==rank&&pool[at].job.id<v.id)))++at;
        if(at==3)continue;
        for(int j=std::min(result.jobs,2);j>at;--j)pool[j]=pool[j-1];
        pool[at]=next;result.jobs=std::min(3,result.jobs+1);
    }
    result.selected=result.baseline;
    // No positive basic work model is available, but the baseline target can
    // still be useful to the live spell executor. Do not turn this into wait.
    if(damage<=0){result.reason=Reason::unsupported;return result;}
    if(!attack_ready){result.reason=Reason::no_attack;return result;}
    if(fuel<=45000){result.reason=Reason::reserve;return result;}
    if(result.jobs==0)return result;
    std::array<typename Policy::FocusJob,3> jobs{};
    for(int i=0;i<result.jobs;++i)jobs[i]=pool[i].job;
    const int selected=Policy::choose_focus(std::span(jobs.data(),std::size_t(result.jobs)));
    for(int i=0;i<result.jobs;++i)if(jobs[i].id==selected) {
        result.selected=selected;result.reason=Reason::planned;return result;
    }
    result.reason=Reason::rejected;return result;
}
}
