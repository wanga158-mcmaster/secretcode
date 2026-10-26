#pragma once
#include "config.h"
#include "../../common/src/decision.h"
#include "../../common/src/equipment.h"

namespace h12_act_first {
struct Policy {
    static constexpr bool enabled=HEURISTICS_ENABLED,trace=HEURISTICS_TRACE;
    static constexpr std::array roster{0,1,3,4,8};
    int next_price=30;
    // Cashable throughput, with a fixed review/switch toll and conservative
    // bag-dependent exposure. This also prices the rotating scalar audit.
    double merit(const heuristics::State&s,const heuristics::Job&j)const {
        const double gold_weight=s.mature?.06:std::clamp(1.-double(s.gold)/(6*next_price),.2,1.);
        const double pay=2.2*j.gems+.48*gold_weight*j.gold;
        const double time=3+j.travel+std::ceil(j.work)+s.capture+.5*j.altar+3*j.allies;
        const double risk=std::max(0.,double(j.loss)-std::max(0.,s.hp-s.maximum*(s.bag?.42:.29)))/std::max(1.f,s.maximum);
        return pay/std::max(1.,time)-2.8*risk-(j.known?0:.08)+(j.committed?.18:0);
    }
    int select(const heuristics::State&s,std::span<const heuristics::Job>jobs,bool partial)const {
        return heuristics::maximum(jobs,partial,[&](const auto&j){return merit(s,j);});
    }
    bool bank(const heuristics::State&s)const {
        return s.bag>0&&(heuristics::deadline(s)||s.altar<=2||s.bag>=12||s.hp<s.maximum*.62f);
    }
    bool retreat(float hp,float maximum,float incoming,int bag)const {
        return hp<=maximum*(bag?.39f:.28f)+incoming*(bag?3.2f:2.7f);
    }
    static heuristics::Purchase purchase(int identity,unsigned monster,unsigned jungle) {
        using P=heuristics::Purchase;
        if(monster<5)return identity==1||identity==5?P::monster_focus:P::monster_strength;
        if(jungle<5)return identity==10?P::jungle_strength:P::jungle_health;
        return identity==1||identity==5||identity==7?P::power_focus:(identity==10?P::power_health:P::power_strength);
    }
    fundamentals::BuildStage build(unsigned monster,unsigned jungle)const{return fundamentals::build_stage(monster,jungle);}
};
}
