#pragma once
#include "config.h"
#include "../../common/src/decision.h"
#include "../../common/src/equipment.h"
namespace h12_counterpunch {
struct Policy {
    static constexpr bool enabled=HEURISTICS_ENABLED,trace=HEURISTICS_TRACE;
    static constexpr std::array roster{10,7,0,3,4};
    int next_price=30;
    double merit(const heuristics::State&s,const heuristics::Job&j)const {
        const double liquid=std::clamp(1.-double(s.gold)/(4*next_price),.1,1.);
        const double cycle=1+j.travel+j.work+s.capture+j.altar;
        const double wounded=j.known&&j.original_hp>0?std::clamp(1.f-j.remaining_hp/j.original_hp,0.f,1.f):0;
        return (2.3*j.gems+.6*liquid*j.gold)/cycle+2.5*int(j.ready)+wounded
            -3*j.loss/std::max(1.f,s.hp)+.25*int(j.committed)-.15*j.allies;
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
        if(identity==9){if(jungle<5)return P::jungle_focus;if(monster<5)return P::monster_health;return P::power_focus;}
        if(monster<5)return identity==11?P::monster_focus:P::monster_strength;
        if(jungle<5)return P::jungle_health;
        return identity==11?P::power_focus:identity==7?P::power_health:P::power_strength;
    }
    fundamentals::BuildStage build(unsigned m,unsigned j)const{return fundamentals::build_stage(m,j);}
};
}
