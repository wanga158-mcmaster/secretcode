#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace catalyst_forge {
struct Point { int x=0,y=0; };
inline int distance(Point a,Point b) { return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y)); }
enum class Kit { varg, stellar, vendetta, other };
struct Monster { int id=-1,group=-1; Point tile{}; float hp=0,damage=0; int range=0; bool awake=false,visible=true; };
struct Actor { int id=-1; Kit kit=Kit::other; Point tile{}; float hp=0,strength=0,focus=0; int range=1; bool ranged=false; std::array<int,2> cooldown{}; };
// A hit set includes collateral identities; a missing/hidden member invalidates it.
struct HitSet { std::uint16_t members=0; bool complete=true; };
enum class Op { move, basic, reset, single, area, hold, bank, release };
struct ForgeStep { int actor=0,round=0; Op op=Op::basic; int target=-1; Point tile{}; HitSet hits{}; };
struct ForgeSuffix {
    std::array<Monster,12> monsters{}; int count=0;
    std::array<Actor,3> actors{}; int actors_count=0;
    std::array<ForgeStep,96> steps{}; int step_count=0;
    int holder=0,bank_owner=0,capture_ticks=5,altar_travel=0,tribute_ticks=1,next_source_travel=0;
    int observed_round=0; bool contested=false,collateral_complete=true;int(*fuel_reader)()=nullptr;int fuel_reserve=16000;
};
struct Result { bool legal=false,safe=false,complete=false; int clear_round=0,bank_round=0,renew_round=0,kills=0,casts=0,released_turns=0; std::array<float,3> hp{}; std::array<float,12> remaining{}; };
inline HitSet hit_set(const ForgeSuffix& s,Point center,int radius,int primary=-1) {
    HitSet h;
    for(int i=0;i<s.count;++i) if(s.monsters[i].hp>0 && (i==primary || distance(center,s.monsters[i].tile)<=radius)) {
        h.members|=std::uint16_t(1u<<i); h.complete&=s.monsters[i].visible;
    }
    h.complete &= s.collateral_complete;
    return h;
}
inline Result execute(ForgeSuffix s) {
    Result r; if(s.count<1||s.count>12||s.actors_count<1||s.actors_count>3||s.step_count>96||s.contested) return r;
    std::array<int,3> last_round{-1,-1,-1}; std::array<bool,3> moved{},attacked{},cast{};
    int phase=-1;
    auto camp_phase=[&]() {
        for(int i=0;i<s.count;++i) if(s.monsters[i].hp>0&&s.monsters[i].awake&&s.monsters[i].damage>0) {
            int nearest=999,who=-1; bool tie=false;
            for(int a=0;a<s.actors_count;++a) if(s.actors[a].hp>0) {
                const int d=distance(s.actors[a].tile,s.monsters[i].tile);
                if(d<=s.monsters[i].range&&d<nearest) { nearest=d;who=a;tie=false; }
                else if(d<=s.monsters[i].range&&d==nearest) tie=true;
            }
            // Unknown target ties cannot certify a positioned crew trajectory.
            if(tie) {
                // Preserve every nearest-target branch: charge the volley to
                // each eligible tied body, rather than inventing a tie winner.
                for(int a=0;a<s.actors_count;++a) if(s.actors[a].hp>0&&distance(s.actors[a].tile,s.monsters[i].tile)==nearest) {
                    s.actors[a].hp-=s.monsters[i].damage;if(s.actors[a].hp<=0)return false;
                }
                continue;
            }
            if(who>=0) { s.actors[who].hp-=s.monsters[i].damage; if(s.actors[who].hp<=0) return false; }
        }
        return true;
    };
    for(int k=0;k<s.step_count;++k) {
        if(s.fuel_reader&&s.fuel_reader()<s.fuel_reserve)return r;
        const auto& step=s.steps[k]; const int a=step.actor;
        if(a<0||a>=s.actors_count||step.round<phase||step.round>=16) return r;
        while(phase<step.round) { if(phase>=0&&!camp_phase()) return r; ++phase; }
        if(last_round[a]!=phase) {
            if(last_round[a]>=0) for(auto& cd:s.actors[a].cooldown) cd=std::max(0,cd-(phase-last_round[a]));
            moved[a]=attacked[a]=cast[a]=false; last_round[a]=phase;
        }
        auto& actor=s.actors[a];
        if(step.op==Op::move) {
            if(moved[a]||(actor.ranged&&attacked[a])||distance(actor.tile,step.tile)>1) return r;
            for(int b=0;b<s.actors_count;++b) if(a!=b&&distance(s.actors[b].tile,step.tile)==0) return r;
            actor.tile=step.tile;moved[a]=true;continue;
        }
        if(step.op==Op::hold||step.op==Op::bank||step.op==Op::release) continue;
        const bool basic=step.op==Op::basic;
        const int slot=step.op==Op::area?1:0;
        if(basic) { if(attacked[a]||(actor.ranged&&moved[a])) return r; }
        else { if(cast[a]||actor.cooldown[slot]>0) return r; cast[a]=true;actor.cooldown[slot]=8;++r.casts; }
        if(step.op==Op::reset) { if(actor.kit!=Kit::varg&&actor.kit!=Kit::vendetta)return r;attacked[a]=false;continue; }
        if((step.op==Op::area||step.op==Op::single)&&actor.kit!=Kit::stellar)return r;
        if(step.target<0||step.target>=s.count||!step.hits.complete) return r;
        const auto& primary=s.monsters[step.target];
        if(!primary.visible||primary.hp<=0) return r;
        if(distance(actor.tile,step.op==Op::area?step.tile:primary.tile)>(basic?actor.range:4)) return r;
        if(!basic&&moved[a]) return r;
        HitSet actual=basic ? (actor.kit==Kit::varg?hit_set(s,primary.tile,1,step.target):HitSet{std::uint16_t(1u<<step.target),true})
            : step.op==Op::area?hit_set(s,step.tile,1):HitSet{std::uint16_t(1u<<step.target),true};
        if(actual.members!=step.hits.members||!actual.complete) return r;
        const float damage=basic?actor.strength:actor.focus*(step.op==Op::area?1.4f:2.f);
        if(basic) attacked[a]=true;
        for(int i=0;i<s.count;++i) if(actual.members&(1u<<i)) {
            const int group=s.monsters[i].group;
            for(int j=0;j<s.count;++j) if(s.monsters[j].group==group) s.monsters[j].awake=true;
            s.monsters[i].hp-=damage;
            if(s.monsters[i].hp<=0) { ++r.kills; if(actor.kit==Kit::vendetta) actor.cooldown={0,0}; }
        }
    }
    // Explicit two next camp phases include any surviving represented collateral.
    if(!camp_phase()||!camp_phase()) return r;
    r.legal=r.safe=true;r.complete=true;
    for(int i=0;i<s.count;++i) { r.remaining[i]=std::max(0.f,s.monsters[i].hp);r.complete&=s.monsters[i].hp<=0; }
    for(int a=0;a<s.actors_count;++a) r.hp[a]=s.actors[a].hp;
    r.clear_round=phase+1;
    // The phase starting capture precedes its configured countdown phases.
    r.bank_round=r.clear_round+1+s.capture_ticks+s.altar_travel+s.tribute_ticks;
    r.renew_round=r.bank_round+s.next_source_travel;
    r.complete&=r.renew_round<=96;
    r.released_turns=(s.actors_count-1)*(s.capture_ticks+s.altar_travel+s.tribute_ticks);
    return r;
}
inline bool better(const Result& a,const Result& b) {
    return a.legal&&a.safe&&a.complete&&(!b.legal||!b.safe||!b.complete||a.renew_round<b.renew_round||(a.renew_round==b.renew_round&&a.released_turns>b.released_turns));
}
inline bool same_premises(const ForgeSuffix& old,const ForgeSuffix& now) {
    if(old.count!=now.count||old.actors_count!=now.actors_count||now.contested||!now.collateral_complete) return false;
    for(int i=0;i<old.count;++i) if(old.monsters[i].id!=now.monsters[i].id||old.monsters[i].hp!=now.monsters[i].hp||!now.monsters[i].visible) return false;
    for(int i=0;i<old.actors_count;++i) if(old.actors[i].cooldown!=now.actors[i].cooldown||old.actors[i].hp!=now.actors[i].hp||distance(old.actors[i].tile,now.actors[i].tile)) return false;
    return true;
}
inline int personal_threshold_shards(float hp,float current,float growth,int gold) {
    if(hp<=current||growth<=0) return 0;
    const int shards=static_cast<int>(std::ceil((hp-current)/growth));return shards*30<=gold?shards:0;
}
} // namespace catalyst_forge
