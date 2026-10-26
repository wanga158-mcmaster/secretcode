#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace mosaic::model {
// Restricted geometry model: one stationary actor, one group, basics and attack resets.
// No allied DPS, pooled HP, enemy absence inference, shield credit, or hidden state.
// It ranks candidate work; execution must recheck actual legal masks and incoming damage.
struct Member { int x{}, y{}; float hp{}, strength{}; int range{}; };
struct Result { int rounds{}; float damage{}; float total_hp{}; bool complete{}; };
// Occupancy is not a capture receipt. After entry, one phase starts the timer
// and capture_time later phases can finish it. Missing that expected receipt
// requests a new physical observation; it does not prove a hidden contest or
// that the task is impossible.
struct CaptureExpectation {
    int group{-1}, entered{-1};
    void begin(int current_group,int epoch) {
        if(group!=current_group){group=current_group;entered=epoch;}
    }
    void clear() {group=entered=-1;}
    bool needs_observation(int epoch,int capture_time) const {
        return group>=0&&entered>=0&&epoch-entered>=capture_time+1;
    }
};
inline int distance(int ax,int ay,int bx,int by) {
    return std::max(std::abs(ax-bx),std::abs(ay-by));
}
inline int primary(const std::vector<Member>& members,int x,int y,int range,float hit,int splash) {
    int best=-1; float best_value=-1;
    for (int i=0;i<static_cast<int>(members.size());++i) {
        const auto& a=members[i];
        if(a.hp<=0 || distance(x,y,a.x,a.y)>range) continue;
        float value=0;
        for(int j=0;j<static_cast<int>(members.size());++j) {
            const auto& b=members[j];
            if(b.hp<=0 || (j!=i && (splash==0 || distance(a.x,a.y,b.x,b.y)>splash))) continue;
            // Actual useful HP removed; finishing a threat prevents later volleys.
            value+=std::min(hit,b.hp)+(b.hp<=hit?b.strength:0);
        }
        if(value>best_value) {best_value=value;best=i;}
    }
    return best;
}
inline void basic(std::vector<Member>& members,int target,float hit,int splash) {
    if(target<0) return;
    const auto center=members[target];
    for(int j=0;j<static_cast<int>(members.size());++j) {
        auto& b=members[j];
        if(b.hp>0 && (j==target || (splash>0 && distance(center.x,center.y,b.x,b.y)<=splash)))
            b.hp=std::max(0.0f,b.hp-hit);
    }
}
inline Result forecast(std::vector<Member> members,int x,int y,int range,float hit,int splash,
                       int reset_left=-1,int reset_period=8,int max_rounds=96,bool (*budget)()=nullptr) {
    Result out;
    for(const auto& m:members) out.total_hp+=std::max(0.0f,m.hp);
    for(int r=0;r<max_rounds;++r) {
        if(budget && !budget())return out;
        if(r>0) for(const auto& m:members)
            if(m.hp>0 && distance(x,y,m.x,m.y)<=m.range) out.damage+=m.strength;
        int target=primary(members,x,y,range,hit,splash);
        if(target<0) {out.rounds=r;out.complete=false;return out;}
        basic(members,target,hit,splash);
        if(reset_left==0) {
            target=primary(members,x,y,range,hit,splash);
            if(target>=0) {basic(members,target,hit,splash);reset_left=reset_period;}
        }
        if(reset_left>0) --reset_left;
        out.rounds=r+1;
        if(std::all_of(members.begin(),members.end(),[](const Member& m){return m.hp<=0;})) {
            out.complete=true;return out;
        }
    }
    return out;
}
}
