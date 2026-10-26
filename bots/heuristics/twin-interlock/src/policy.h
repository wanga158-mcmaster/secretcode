#pragma once
#include "config.h"
#include "../../common/src/decision.h"
#include "../../common/src/equipment.h"
namespace h12_twin_interlock {
struct Policy {
    static constexpr bool enabled=HEURISTICS_ENABLED,trace=HEURISTICS_TRACE;
    static constexpr std::array roster{11,7,0,9,4};
    int next_price=30;
    double merit(const heuristics::State&s,const heuristics::Job&j)const {
        const bool twin=s.identity==3||s.identity==4;
        const double cash=s.mature?.10:std::clamp(1.-double(s.gold)/(5*next_price),.15,1.);
        const double cycle=2+j.travel+j.work+s.capture+.7*j.altar+2*j.allies;
        const double exposure=j.loss/std::max(1.f,s.hp);
        const double role=s.identity==3?std::min(2.,double(j.work)/7):s.identity==4?1./(1+j.work*.1):1.;
        return (2.5*j.gems+.55*cash*j.gold+role)/cycle-(twin?3.8:1.8)*exposure+(j.ready?1.2:0)+(j.committed?.22:0);
    }
    int select(const heuristics::State&s,std::span<const heuristics::Job>jobs,bool partial)const {
        return heuristics::maximum(jobs,partial,[&](const auto&j){return merit(s,j);});
    }
    bool bank(const heuristics::State&s)const {
        const bool twin=s.identity==3||s.identity==4;
        return s.bag>0&&(twin||heuristics::deadline(s)||s.bag>=16||s.altar<=2||s.hp<s.maximum*.58f);
    }
    bool retreat(float hp,float maximum,float incoming,int bag)const {
        return hp<=maximum*(bag?.48f:.36f)+incoming*(bag?3.5f:3.f);
    }
    static heuristics::Purchase purchase(int identity,unsigned monster,unsigned jungle) {
        using P=heuristics::Purchase;
        if(monster<5)return identity==3||identity==8?P::monster_focus:P::monster_strength;
        if(jungle<5)return P::jungle_health;
        return identity==3||identity==8?P::power_focus:identity==4?P::power_health:P::power_strength;
    }
    fundamentals::BuildStage build(unsigned m,unsigned j)const{return fundamentals::build_stage(m,j);}
};
}
