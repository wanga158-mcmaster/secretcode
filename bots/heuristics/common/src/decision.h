#pragma once
#include "baseline.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <vector>

namespace heuristics {
// Coarse public/observed features, deliberately cheap rather than a complete
// game simulation. Every controller owns its choice, bank and risk rules.
struct State {
    int round=0, limit=2000, bag=0, altar=0, identity=0, gold=0;
    int capture=5, recall=5, monster=0, jungle=0;
    double tribute=.1;
    float hp=1000, maximum=1000;
    bool mature=false;
};
struct Job {
    int id=-1, travel=0, altar=0, allies=0, enemy_eta=10000;
    int gems=0, gold=0, home=0, age=0;
    float work=0, loss=0, incoming=0, original_hp=0, remaining_hp=0;
    bool ready=false, known=false, committed=false, affordable=false;
};
struct Detail {int id,travel;bool reachable=true,harmless=false;};
inline std::vector<int> detail_order(std::vector<Detail> items,int incumbent) {
    // A small insertion shortlist avoids libc++ stable_sort's fuel overhead.
    std::array<Detail,12> rank{};int count=0;Detail fallback{-1,100000,false,false};
    auto cost=[&](const Detail&d){return d.travel-(d.id==incumbent?8:0);};
    for(const auto&item:items)if(item.reachable) {
        if(item.harmless&&item.travel<fallback.travel)fallback=item;
        int at=0;while(at<count&&cost(rank[at])<=cost(item))++at;
        if(at>=12)continue;
        for(int k=std::min(count,11);k>at;--k)rank[k]=rank[k-1];
        rank[at]=item;if(count<12)++count;
    }
    std::vector<int> result;result.reserve(12);
    if(fallback.id>=0)result.push_back(fallback.id);
    for(int i=0;i<count&&result.size()<12;++i)if(rank[i].id!=fallback.id)result.push_back(rank[i].id);
    return result;
}
enum class CaptureAction {hold,engage,leave};
constexpr CaptureAction capture_action(bool contested,bool can_hit,int elapsed) {
    return !contested?CaptureAction::hold:(can_hit&&elapsed<8?CaptureAction::engage:CaptureAction::leave);
}
inline float reward(const State&s,const Job&j) {
    return j.gems*2.f+j.gold*(s.mature?.08f:.4f);
}
inline float cycle(const State&s,const Job&j) {
    return 1+j.travel+j.work+s.capture+1+j.altar+std::ceil((s.bag+j.gems)*s.tribute);
}
inline float pressure(const Job&j) {return std::max(0.f,12.f+j.travel+j.work-j.enemy_eta);}
inline bool allowed(const Job&j,bool partial) {return partial?!j.affordable:j.affordable;}
template<class Score> int maximum(std::span<const Job>jobs,bool partial,Score score) {
    int best=-1;double value=-std::numeric_limits<double>::infinity();
    for(const auto&j:jobs) if(allowed(j,partial)) {
        const double v=score(j);
        if(std::isfinite(v)&&v>value){best=j.id;value=v;}
    }
    return best;
}
// This is the complete production selector called by runtime.h, also exercised
// by fixtures. No controller receives a preselected winner.
template<class Policy> int choose(Policy&policy,const State&s,std::span<const Job>jobs) {
    int id=policy.select(s,jobs,false);
    if(id>=0)return id;
    if(s.hp>=s.maximum*.75f)return policy.select(s,jobs,true);
    std::vector<Job> continuing;
    for(const auto&j:jobs)if(j.committed)continuing.push_back(j);
    if(!continuing.empty())return policy.select(s,continuing,true);
    return -1;
}
inline bool deadline(const State&s) {
    return s.bag>0&&s.limit-s.round<=s.altar+int(std::ceil(s.bag*s.tribute))+8;
}
} // namespace heuristics
