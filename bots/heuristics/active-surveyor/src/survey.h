#pragma once
#include "policy.h"
#include <array>

namespace h12_active_surveyor {
// Geometry is only an admission prediction. Fresh is an owner observation,
// stamped after a successful ward action; it is never a predicted live fact.
struct Probe {int id=-1,x=0,y=0;double lost=0,cost=0;bool reveals=false;};
struct Fresh {std::vector<heuristics::Job> jobs;int round=-1;bool newly_seen=false,complete=true;};
struct Survey {
    int epoch_round=-100,last_pair_a=-1,last_pair_b=-1;
    unsigned attempts=0,changed=0,inconclusive=0,failed=0,refused=0;
    unsigned low_fuel=0,unready=0,no_value=0,no_geometry=0,incomplete=0,stale=0;
    template<class World> int tick(World&w,Policy&policy,const heuristics::State&s,
                                   std::vector<heuristics::Job>&jobs) {
        if(!w.alive()) {epoch_round=-100;last_pair_a=last_pair_b=-1;return -1;}
        if(w.channeling())return -1;
        int before=heuristics::choose(policy,s,jobs);
        if(before<0)return before;
#if SURVEY_SUPPRESS_PROBE
        // Same-policy counterfactual: retain ordinary work and refuse only
        // optional information acquisition, including its refresh cost.
        return before;
#endif
        if(w.fuel()<76000){++refused;++low_fuel;return before;}
        if(s.round-epoch_round<10||!w.ready()){++refused;++unready;return before;}
        // Only two executable jobs participate: the incumbent and the most
        // useful uncertain challenger (or the uncertain incumbent itself).
        const heuristics::Job*base=nullptr;
        for(const auto&j:jobs)if(j.id==before)base=&j;
        if(!base)return before;
        int candidate=-1;double gain=0;
        for(const auto&j:jobs)if(w.unknown(j.id)) {
            if constexpr(requires{w.probeable(j.id);})if(!w.probeable(j.id))continue;
            auto optimistic=j;optimistic.work=0;optimistic.loss=0;
            optimistic.ready=true;optimistic.affordable=true;
            const double best=policy.merit(s,optimistic);
            double benefit=best-policy.merit(s,*base);
            if(j.id==before) {
                // Discovering respawn invalidates the current work choice.
                double alternative=-1e100;
                for(const auto&k:jobs)if(k.id!=before&&k.affordable)
                    alternative=std::max(alternative,policy.merit(s,k));
                benefit=alternative>-1e90?std::max(benefit,.25):0;
            }
            if(benefit>gain){gain=benefit;candidate=j.id;}
        }
        if(candidate<0||gain<=.02) {++refused;++no_value;return before;}
        const int other=candidate==before?second(policy,s,jobs,before):candidate;
        if(other<0){++refused;return before;}
        // At most two terrain predictions, supplied for this one hidden job.
        const auto probes=w.probes(candidate,before,other);
        Probe best;double net=0;
        for(const auto&p:probes)if(p.reveals&&gain-p.lost-p.cost>net) {
            net=gain-p.lost-p.cost;best=p;
        }
        if(best.id<0||w.fuel()<59000){++refused;++no_geometry;return before;}
        if constexpr(requires{w.current_round();})if(w.current_round()!=s.round) {
            ++refused;return -1;
        }
        if(!w.alive()||w.channeling())return -1;
        if(!w.ready()){++refused;++unready;return before;}
        epoch_round=s.round;last_pair_a=before;last_pair_b=other;++attempts;
        w.event("probe",before,candidate,best.x,best.y);
        if(!w.place(best)) {++failed;w.event("failed",before,before,best.x,best.y);return before;}
        auto fresh=w.refresh(before,other);
        // The owning refresh replaces the entire bounded shortlist. A stale
        // or incomplete sample cannot continue using any previous live facts.
        if(!fresh.complete) {
            ++inconclusive;++incomplete;w.event("incomplete",before,-1,best.x,best.y);return -1;
        }
        if(fresh.round!=s.round) {
            ++inconclusive;++stale;w.event("stale",before,-1,best.x,best.y);return -1;
        }
        jobs=std::move(fresh.jobs);
        const int after=heuristics::choose(policy,s,jobs);
        if(!fresh.newly_seen) {++inconclusive;w.event("inconclusive",before,after,best.x,best.y);}
        else if(after!=before&&after>=0) {++changed;w.event("changed",before,after,best.x,best.y);}
        else {++inconclusive;w.event("same",before,after,best.x,best.y);}
        return after;
    }
    static int second(const Policy&p,const heuristics::State&s,std::span<const heuristics::Job>jobs,int except) {
        int id=-1;double best=-1e100;
        for(const auto&j:jobs)if(j.id!=except&&j.affordable&&p.merit(s,j)>best){id=j.id;best=p.merit(s,j);}
        return id;
    }
};
}
