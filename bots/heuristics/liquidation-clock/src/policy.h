#pragma once
#include "config.h"
#include "clock.h"
#include "../../common/src/decision.h"
#include "../../common/src/equipment.h"
namespace h12_liquidation_clock {
struct Policy {
    static constexpr bool enabled=HEURISTICS_ENABLED,trace=HEURISTICS_TRACE;
    static constexpr std::array roster{0,7,3,4,2};
    int next_price=30;
    double merit(const heuristics::State&s,const heuristics::Job&j)const {
        const double liquid=std::clamp(1.-double(s.gold)/(4*next_price),.1,1.);
        return (2*j.gems+.5*liquid*j.gold)/(1+j.travel+j.work+s.capture+j.altar)
            +2*int(j.ready)-2*j.loss/std::max(1.f,s.hp)-.4*j.allies+.15*int(j.committed);
    }
    int select(const heuristics::State&s,std::span<const heuristics::Job>jobs,bool partial)const {
        return heuristics::maximum(jobs,partial,[&](const auto&j){return merit(s,j);});
    }
    bool bank(const heuristics::State&s)const {
        return s.bag>0&&(heuristics::deadline(s)||s.bag>=12||s.altar<=2||s.hp<s.maximum*.62f);
    }
    bool retreat(float hp,float maximum,float incoming,int bag)const {
        return hp<=maximum*(bag?.48f:.34f)+incoming*(bag?2.5f:2.f);
    }
    static heuristics::Purchase purchase(int identity,unsigned monster,unsigned jungle) {
        using P=heuristics::Purchase;
        if(monster<5)return identity==11?P::monster_focus:P::monster_strength;
        if(jungle<5)return P::jungle_health;
        return identity==11?P::power_focus:identity==8?P::power_health:P::power_strength;
    }
    fundamentals::BuildStage build(unsigned m,unsigned j)const{return fundamentals::build_stage(m,j);}
};
}
