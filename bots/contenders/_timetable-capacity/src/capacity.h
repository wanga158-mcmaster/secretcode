#pragma once
// Production allocation seam: portfolio jobs consume named bodies, all arrival/waiting time,
// fight capacity, and one capture clock. This is a model, not a physical combat simulator.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
#include "model.h"
#ifndef CAPACITY_MEMBER_CLOCK
#define CAPACITY_MEMBER_CLOCK 0
#endif

namespace mb::capacity {
constexpr int bodies_max=8;
struct Body { float dps{}, pool{}, taken{1}; int locked{-1}; bool available{}; };
struct Job {
    int group{-1}, earliest{}, capture{5}, home{};
    float value{}, hp_scale{1};
    model::FightConstants fight;
    std::array<int,bodies_max> arrival{};
    std::array<float,bodies_max> dps{};
#if CAPACITY_MEMBER_CLOCK
    float largest_hp{};
    std::array<float,bodies_max> member_dps{};
#endif
};
struct Bid { int group{-1}, crew{}, ready{}, finish{}, clear{}; float rate{}, cost{}; };
struct Assignment { std::array<Bid,bodies_max> body; std::vector<Bid> jobs; int evaluated{}; };
struct Frontier { int cursor{};int index(int count,int step)const{return count?(cursor+step)%count:0;}void advance(int count,int visited){if(count)cursor=(cursor+std::max(1,visited))%count;} };
inline int count(int mask) { return __builtin_popcount(static_cast<unsigned>(mask)); }
// A fixed-rate lower clock, not native action admissibility: aggregate work and the slowest
// member must both fit. Sustained reset/spell rates still omit finite cadence and movement.
inline int member_clock(float hp,float largest,float aggregate,float per_member) {
    if(hp<=0)return 0;
    if(aggregate<=0 || (largest>0 && per_member<=0))return 1<<20;
    return std::max(1,static_cast<int>(std::ceil(std::max(hp/aggregate,largest>0?largest/per_member:0.f)-1e-4f)));
}
inline bool live_stats(model::ChampionSummary& summary,float strength,float focus) {
    if(!(strength>0) || !(focus>=0)) return false;
    summary.strength=strength;summary.focus=focus;return true;
}
inline void assign_commands(std::array<std::uint8_t,10>& board,const Assignment& assignment,int n,int round) {
    for(int i=0;i<n;++i) {
        const int status=board[i]>>6,group=static_cast<int>(board[i]&63)-1;
        if(group>=0 && status<2) continue; // preserve an accepted fight/capture lease
        board[n]&=static_cast<std::uint8_t>(~(1<<i));
        if(assignment.body[i].group>=0) {board[i]=static_cast<std::uint8_t>(assignment.body[i].group+1);board[n]|=static_cast<std::uint8_t>(1<<i);}
    }
    board[n+1]=static_cast<std::uint8_t>(round&255);
}

inline Bid quote(const Job& job,const std::array<Body,bodies_max>& bodies,int n,int crew,float safety=1.6f)
{
    Bid out;
    if (!crew || count(crew)>3) return out;
    float dps=0,pool=1e30f; int arrival=0,locked=0;
#if CAPACITY_MEMBER_CLOCK
    float per_member=0;
#endif
    for(int i=0;i<n;++i) {
        const auto& b=bodies[i];
        if(b.available && b.locked==job.group) locked|=1<<i;
        if(!(crew&(1<<i))) continue;
        if(!b.available || (b.locked>=0 && b.locked!=job.group) || job.arrival[i]>=16384) return out;
        dps+=job.dps[i]>0?job.dps[i]:b.dps;
#if CAPACITY_MEMBER_CLOCK
        per_member+=job.member_dps[i]>0?job.member_dps[i]:b.dps;
#endif
        // The inherited executor does not rotate tanks. Each body must cover the entire
        // predicted burden before its own recall threshold, not a fictional shared HP pool.
        pool=std::min(pool,b.pool/std::max(.1f,b.taken)); arrival=std::max(arrival,job.arrival[i]);
    }
    if((crew&locked)!=locked || dps<=0 || pool<=0) return out;
    auto f=model::forecast_from_constants(job.fight,dps,job.hp_scale);
#if CAPACITY_MEMBER_CLOCK
    const float hp=job.fight.total_hp*job.hp_scale;
    const int clock=member_clock(hp,job.largest_hp*job.hp_scale,dps,per_member);
    if(clock>=1<<20)return out;
    if(clock>f.rounds && hp>0) {
        // Apply the longer fixed-rate clock to the existing damage/recovery approximation.
        // This does not turn its uniform HP scaling or Smith ordering into a native simulator.
        f=model::forecast_from_constants(job.fight,hp/static_cast<float>(clock),job.hp_scale);
        f.rounds=std::max(f.rounds,clock);
    }
#endif
    if(f.damage*safety>pool || f.rounds>200) return out;
    const int start=std::max(arrival,job.earliest);
    // All crew waits for the slowest useful arrival. Only the chosen capturer pays capture.
    const float recovery=static_cast<float>(job.home+7)*f.damage/pool;
    const float cost=static_cast<float>(count(crew)*(start+f.rounds)+job.capture)+recovery;
    out={job.group,crew,start,start+f.rounds+job.capture,f.rounds,job.value/std::max(1.f,cost),cost};
    return out;
}

// Exact within the bounded proposal family (at most three named bodies per job), followed by
// disjoint descending-rate matching. This is deliberately not claimed a globally optimal solver.
template<class Continue> inline Assignment allocate(const std::vector<Job>& jobs,const std::array<Body,bodies_max>& bodies,int n,float safety,Continue enough)
{
    std::vector<Bid> bids;
    int evaluated=0;
    const int end=1<<n;
    for(const auto& job:jobs) {
      if(!enough()) break;
      ++evaluated;
      for(int crew=1;crew<end;++crew) {
        if(count(crew)>3) continue;
        auto b=quote(job,bodies,n,crew,safety);
        if(b.group>=0 && b.rate>0) bids.push_back(b);
      }
    }
    std::sort(bids.begin(),bids.end(),[](const Bid& a,const Bid& b) {
        if(a.rate!=b.rate) return a.rate>b.rate;
        if(a.group!=b.group) return a.group<b.group;
        return a.crew<b.crew;
    });
    Assignment out;
    out.evaluated=evaluated;
    int used=0;
    for(const auto& bid:bids) {
        if(used&bid.crew) continue;
        bool duplicate=false;
        for(const auto& old:out.jobs) if(old.group==bid.group) duplicate=true;
        if(duplicate) continue;
        out.jobs.push_back(bid); used|=bid.crew;
        for(int i=0;i<n;++i) if(bid.crew&(1<<i)) out.body[i]=bid;
        if(count(used)==n) break;
    }
    return out;
}
inline Assignment allocate(const std::vector<Job>& jobs,const std::array<Body,bodies_max>& bodies,int n,float safety=1.6f) {
    return allocate(jobs,bodies,n,safety,[]{return true;});
}

struct Calendar {
    int group{-1}, crew{}, issued{}, ready{}, assembly{}, deadline{};
    bool started{};
    void issue(const Bid& bid,int round,bool observed_active) {
        group=bid.group; crew=bid.crew; issued=round; ready=round+bid.ready;
        assembly=round+bid.ready+10; deadline=round+bid.finish+20; started=observed_active;
    }
    void release() { group=-1; crew=0; started=false; }
};
inline Calendar& calendar() { static Calendar out; return out; }
} // namespace mb::capacity
