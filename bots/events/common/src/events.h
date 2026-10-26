#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace p11 {
// Round phase and owner-turn phase are deliberately separate clocks.
enum class Phase { respawn, spell_update, capture, camp_attack, owner_start, action, owner_end };
struct Event {
    int round; Phase phase; int actor=0;
    auto key()const {
        const int p=int(phase);const bool turn=p>=int(Phase::owner_start);
        return std::array{round,turn?int(Phase::owner_start):p,turn?actor:0,turn?p:0};
    }
    auto operator<=>(const Event&other)const{return key()<=>other.key();}
    bool operator==(const Event&other)const{return key()==other.key();}
};
inline int tribute_ticks(int bag, double multiplier=.1, int base=0, int minimum=1) {
    return bag>0 ? std::max(minimum, base+int(std::ceil(bag*multiplier))) : 0;
}
struct HitCount { int hits=0,iterations=0; };
inline HitCount hit_count(float hp, float damage) {
    static_assert(sizeof(float)==sizeof(std::uint32_t) && std::numeric_limits<float>::is_iec559);
    constexpr int cap=10000,iteration_cap=510;
    if(hp<=0) return {};
    if(!std::isfinite(hp) || !(damage>0)) return {cap,0};
    if(!std::isfinite(damage)) return {1,0};
    int hits=0,iterations=0;
    while(hits<cap && iterations<iteration_cap) {
        // Match the engine's per-hit float32 subtraction. One actual step also
        // settles tie-to-even parity before a uniform-spacing jump.
        const float next=hp-damage;
        ++hits;++iterations;
        if(next<=0) return {hits,iterations};
        if(next==hp) return {cap,iterations};
        hp=next;
        if(hits==cap) break;
        const float following=hp-damage;
        if(following<=0) return {hits+1,iterations};
        if(following==hp) return {cap,iterations};
        const auto from=std::bit_cast<std::uint32_t>(hp);
        const auto to=std::bit_cast<std::uint32_t>(following);
        if((from>>23)!=(to>>23)) continue;
        const auto floor=(from>>23)<<23;
        if(from==floor) continue;
        const auto step=from-to;
        // Strictly inside this binade, each subtraction has the same integer
        // ULP decrement. The power-of-two floor has a narrower rounding cell
        // below it, so cross that boundary with a real subtraction instead.
        const int jumps=std::min(cap-hits,int((from-floor-1)/step));
        hp=std::bit_cast<float>(from-std::uint32_t(jumps)*step);
        hits+=jumps;
    }
    // At most two iterations enter each of255 positive exponent regions;
    // subnormal spacing is uniform too. Both work and returned hits are capped.
    return {cap,iterations};
}
inline int attacks(float hp, float damage) {return hit_count(hp,damage).hits;}
struct Monster { float hp=0,strength=0; int approach=0; };
struct Clear { int turns=0; float loss=0; bool bounded=true; };
// Single-basic-attack schedule. No speculative reset, shield, allied help or
// spell damage is credited. Geometry/targeting remain model assumptions.
inline Clear clear_schedule(std::span<const Monster> monsters,float damage,float mitigation=1) {
    if(monsters.size()>32) return {10000,std::numeric_limits<float>::max(),false};
    struct Work { Monster monster;int hits; };
    std::array<Work,32> order{};int count=0;
    // Smith's rule minimizes weighted completion for independent jobs; use
    // integer hit work, not continuous health/DPS. Stable deterministic ties.
    for(auto m:monsters) if(m.hp>0) {
        const Work next{m,attacks(m.hp,damage)};
        int at=count;
        while(at>0&&next.monster.strength*order[at-1].hits>order[at-1].monster.strength*next.hits) {
            order[at]=order[at-1];--at;
        }
        order[at]=next;++count;
    }
    Clear c;
    for(int i=0;i<count;++i) {
        c.turns+=order[i].hits+order[i].monster.approach;
        // The lethal action precedes the next camp phase. Two additional
        // exposure phases are reserved for engagement/escape uncertainty.
        c.loss+=order[i].monster.strength*std::max(0,c.turns-1+2)*mitigation;
    }
    return c;
}
inline float exposure_reserve(std::span<const Monster> monsters,int turns,float mitigation=1) {
    float incoming=0;for(const auto&m:monsters) if(m.hp>0) incoming+=m.strength;
    return incoming*(turns+2*monsters.size())*mitigation;
}
inline float target_rank(float hp,float strength,float damage) {
    return float(attacks(hp,damage))/std::max(.001f,strength);
}
inline float ordered_exposure(std::span<const Monster> monsters,const Clear&clear,float mitigation=1) {
    float incoming=0;for(const auto&m:monsters) if(m.hp>0) incoming+=m.strength;
    // Conditional on executing the same kill order. One further whole-group
    // phase per member reserves local repositioning beyond the two phases in
    // clear_schedule. Walls, enemies and failed movement still require live
    // retreat; this is not a proof about arbitrary path geometry.
    return clear.loss+incoming*monsters.size()*mitigation;
}
struct State {
    int round=0,limit=2000,bag=0,altar=0,capture=5,recall=5,identity=0,gold=0;
    float hp=1000,maximum=1000,strength=100,focus=0;
    int monster_shards=0,jungle_shards=0,needed_gems=1000;
    bool mature=false,field_shop=false; double tribute_multiplier=.1;int tribute_base=0,tribute_minimum=1;
};
struct Candidate {
    int id=-1,travel=0,clear=0,capture=6,altar=0,gems=0,gold=0,allies=0,enemy_eta=10000;
    int renewal=0,age=0;float loss=0,hp=0,incoming=0;bool ready=false,known=false,committed=false;
    int payout()const{return travel+clear+capture;}
    int bank_time(const State&s)const{return payout()+altar+tribute_ticks(s.bag+gems,s.tribute_multiplier,s.tribute_base,s.tribute_minimum);}
};
inline bool continue_partial(float hp,float maximum,bool committed) {return committed || hp>=maximum*.80f;}
enum class CaptureAction { hold, contest };
inline CaptureAction capture_action(bool enemy_on_spawn) {return enemy_on_spawn?CaptureAction::contest:CaptureAction::hold;}
template<class Policy> std::vector<int> shortlist(const State&s,std::span<const Candidate> candidates,int limit,int incumbent,int explore) {
    limit=std::clamp(limit,2,32);
    struct Rank {double value;int id;};
    std::array<Rank,32> prefix{},unique{};int prefix_size=0,unique_size=0;
    bool incumbent_finite=false,explore_finite=false;
    // Keep the raw first(limit-2) entries separately: negative and duplicate
    // IDs consume old stable-sort prefix slots even though add() ignores them.
    // The best limit distinct valid IDs suffice for the final rank-fill phase.
    auto insert=[](auto&rank,int&size,int capacity,Rank next) {
        int at=0;while(at<size&&!(next.value>rank[at].value))++at;
        if(at>=capacity)return;
        for(int i=std::min(size,capacity-1);i>at;--i)rank[i]=rank[i-1];
        rank[at]=next;if(size<capacity)++size;
    };
    for(const auto&c:candidates) {
        const double value=Policy::screen(s,c);if(!std::isfinite(value))continue;
        incumbent_finite|=c.id==incumbent;explore_finite|=c.id==explore;
        insert(prefix,prefix_size,limit-2,Rank{value,c.id});
        if(c.id<0)continue;
        int previous=-1;
        for(int i=0;i<unique_size;++i)if(unique[i].id==c.id){previous=i;break;}
        if(previous>=0) {
            if(!(value>unique[previous].value))continue;
            for(int i=previous+1;i<unique_size;++i)unique[i-1]=unique[i];
            --unique_size;
        }
        insert(unique,unique_size,limit,Rank{value,c.id});
    }
    std::vector<int> order;order.reserve(std::min(std::size_t(limit),candidates.size()));
    auto add=[&](int id){
        if(id<0||int(order.size())>=limit||std::find(order.begin(),order.end(),id)!=order.end())return;
        order.push_back(id);
    };
    for(int i=0;i<prefix_size;++i)add(prefix[i].id);
    if(incumbent_finite)add(incumbent);if(explore_finite)add(explore);
    for(int i=0;i<unique_size;++i)add(unique[i].id);
    return order;
}
inline bool feasible(const State&s,const Candidate&c) {
    return c.ready || (c.clear<10000 && s.hp-c.loss>=s.maximum*.25f);
}
// This is the selector called by the real SDK farm path. Fixtures present all
// alternatives to it; policies cannot supply a preselected candidate.
template<class Policy> int choose(const State&s,std::span<const Candidate> candidates) {
    int selected=-1;double best=-std::numeric_limits<double>::infinity();
    for(const auto&c:candidates) {
        if constexpr(requires{Policy::admissible(s,c);}) {if(!Policy::admissible(s,c)) continue;}
        else {if(!feasible(s,c)) continue;}
        const double value=Policy::score(s,c);
        if(std::isfinite(value) && value>best) {best=value;selected=c.id;}
    }
    return selected;
}
template<class Policy> int choose_partial(const State&s,std::span<const Candidate> candidates) {
    int selected=-1;double best=-std::numeric_limits<double>::infinity();
    for(const auto&c:candidates) {
        if(!continue_partial(s.hp,s.maximum,c.committed)) continue;
        const double value=Policy::score(s,c);
        if(std::isfinite(value)&&value>best){best=value;selected=c.id;}
    }
    return selected;
}
}
