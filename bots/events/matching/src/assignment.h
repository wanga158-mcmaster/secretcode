#pragma once
#include "../../tempo/src/policy.h"

namespace p11_matching {
struct Worker {
    int id=-1,x=0,y=0;
    float hp=0,maximum=0,strength=0;
    bool available=true;
    int score_identity=-1;
};
struct Job { p11::Candidate candidate; int x=0,y=0; };

inline constexpr int capacity=3;
using Matrix=std::array<std::array<double,capacity>,capacity>;
struct Assignment {
    std::array<int,capacity> job{{-1,-1,-1}};
    double value=0;
    int transitions=0;
    bool bounded=true;
};

// Exact bitmask DP for three real unit-capacity jobs. A wait transition leaves
// the mask alone, so each worker has its own private zero-value wait dummy.
// At most 36 transitions are visited (well below 6P3 = 120).
inline Assignment maximize(const Matrix&utility,int rows,int columns) {
    Assignment result;
    if(rows<0||rows>capacity||columns<0||columns>capacity) {
        result.bounded=false;return result;
    }
    struct Cell {
        double value=-std::numeric_limits<double>::infinity();
        std::array<int,capacity> job{{-1,-1,-1}};
    };
    std::array<Cell,1<<capacity> current{},next{};
    current[0].value=0;
    auto earlier=[&](const auto&a,const auto&b) {
        for(int i=0;i<rows;++i) {
            const int x=a[i]<0?columns:a[i],y=b[i]<0?columns:b[i];
            if(x!=y)return x<y;
        }
        return false;
    };
    auto improve=[&](Cell&to,const Cell&from) {
        if(from.value>to.value||(from.value==to.value&&earlier(from.job,to.job)))to=from;
    };
    for(int row=0;row<rows;++row) {
        next.fill(Cell{});
        for(int mask=0;mask<(1<<columns);++mask)if(std::isfinite(current[mask].value)) {
            ++result.transitions;improve(next[mask],current[mask]);
            for(int job=0;job<columns;++job) {
                const double value=utility[row][job];
                // Nonpositive work is weakly dominated by this row's wait.
                if((mask&(1<<job))||!std::isfinite(value)||value<=0)continue;
                ++result.transitions;
                auto candidate=current[mask];candidate.value+=value;candidate.job[row]=job;
                improve(next[mask|(1<<job)],candidate);
            }
        }
        current=next;
    }
    Cell best;best.value=0;
    for(int mask=0;mask<(1<<columns);++mask)improve(best,current[mask]);
    result.value=best.value;result.job=best.job;return result;
}

inline double row_utility(const p11::State&local,const Job&job,const Worker&worker,int self_id) {
    const auto&source=job.candidate;
    if(!worker.available||worker.id<0||worker.score_identity<0||source.id<0||!std::isfinite(worker.hp)||!std::isfinite(worker.maximum)
        ||!std::isfinite(worker.strength)||worker.hp<=0||worker.maximum<=0
        ||!std::isfinite(source.hp)||source.hp<0||!std::isfinite(source.incoming)||source.incoming<0)
        return -std::numeric_limits<double>::infinity();
    auto state=local;
    state.hp=worker.hp;state.maximum=worker.maximum;state.strength=worker.strength;
    // Physical handles identify rows; every observer must score a worker with
    // that worker's canonical champion identity, including its own self row.
    state.identity=worker.score_identity;
    auto candidate=source;
    if(worker.id!=self_id) {
        candidate.travel=std::max(std::abs(worker.x-job.x),std::abs(worker.y-job.y));
        candidate.clear=source.ready?0:p11::attacks(source.hp,worker.strength);
        candidate.loss=source.ready?0:source.incoming*(candidate.clear+2);
    }
    candidate.allies=0;
    candidate.committed=source.committed&&worker.id==self_id;
    if(!p11::feasible(state,candidate))return -std::numeric_limits<double>::infinity();
    // The self row keeps the completed detailed travel, clear and exposure,
    // together with the local financial state and canonical worker identity.
    // Other rows reuse that financial state (bag, equipment maturity, shop
    // access and altar proxy). Only visible worker stats and identity vary.
    // Full public camp HP and raw visible Strength receive no allied damage or
    // mitigation credit. Travel is optimistic static Chebyshev to the anchor.
    // This solves that small local proxy exactly, not a global fog-aware plan.
    return p11_tempo::Policy::score(state,candidate);
}

inline int assign(const p11::State&state,std::span<const Job>jobs,std::span<const Worker>workers,int self_id) {
    if(jobs.size()>capacity||workers.empty()||workers.size()>capacity)return -1;
    std::array<Worker,capacity> rows{};std::array<Job,capacity> columns{};
    std::copy(workers.begin(),workers.end(),rows.begin());
    std::copy(jobs.begin(),jobs.end(),columns.begin());
    std::sort(rows.begin(),rows.begin()+workers.size(),[](const auto&a,const auto&b){return a.id<b.id;});
    std::sort(columns.begin(),columns.begin()+jobs.size(),[](const auto&a,const auto&b){return a.candidate.id<b.candidate.id;});
    int self=-1;
    for(int i=0;i<int(workers.size());++i) {
        if(i&&rows[i].id==rows[i-1].id)return -1;
        if(rows[i].id==self_id)self=i;
    }
    if(self<0)return -1;
    for(int i=1;i<int(jobs.size());++i)if(columns[i].candidate.id==columns[i-1].candidate.id)return -1;
    Matrix utility{};
    for(int row=0;row<int(workers.size());++row)
        for(int job=0;job<int(jobs.size());++job)
            utility[row][job]=row_utility(state,columns[job],rows[row],self_id);
    const auto result=maximize(utility,int(workers.size()),int(jobs.size()));
    const int chosen=result.job[self];
    return chosen<0?-1:columns[chosen].candidate.id;
}
} // namespace p11_matching
