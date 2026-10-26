#pragma once
#include "config.h"
#include "../../common/src/decision.h"
#include "../../common/src/equipment.h"
namespace h12_capability_portfolio {
struct Policy {
    static constexpr bool enabled=HEURISTICS_ENABLED,trace=HEURISTICS_TRACE;
    static constexpr std::array roster{3,2,4,1,7};
    int next_price=30;
    double merit(const heuristics::State&s,const heuristics::Job&j)const {
        // Independent useful work remains the outside option of every role.
        const double liquid=std::clamp(1.-double(s.gold)/(4*next_price),.1,1.);
        const double cycle=1+j.travel+j.work+s.capture+.8*j.altar;
        const double payout=(2.3*j.gems+.6*liquid*j.gold)/cycle;
        const double reserve=j.loss/std::max(1.f,s.hp);
        return payout-2.6*reserve+(j.ready?1.5:0)+(j.committed?.3:0)-.12*j.allies;
    }
    int select(const heuristics::State&s,std::span<const heuristics::Job>jobs,bool partial)const {
        return heuristics::maximum(jobs,partial,[&](const auto&j){return merit(s,j);});
    }
    bool bank(const heuristics::State&s)const {
        return s.bag>0&&(heuristics::deadline(s)||s.bag>=14||s.altar<=3||s.hp<s.maximum*.62f);
    }
    bool retreat(float hp,float maximum,float incoming,int bag)const {
        return hp<=maximum*(bag?.46f:.32f)+incoming*(bag?3.f:2.5f);
    }
    static heuristics::Purchase purchase(int identity,unsigned monster,unsigned jungle) {
        using P=heuristics::Purchase;
        if(monster<5)return identity==11?P::monster_focus:P::monster_strength;
        if(jungle<5)return P::jungle_health;
        return identity==11?P::power_focus:identity==7?P::power_health:P::power_strength;
    }
    fundamentals::BuildStage build(unsigned m,unsigned j)const{return fundamentals::build_stage(m,j);}
};
}
