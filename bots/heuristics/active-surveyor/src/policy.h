#pragma once
#include "config.h"
#include "../../common/src/decision.h"
#include "../../common/src/equipment.h"

namespace h12_active_surveyor {
// A cashflow horizon: already owned money gradually displaces gold with score.
// No financing-lot threshold, auction, circuit or conversion service program.
struct Policy {
    static constexpr bool enabled=HEURISTICS_ENABLED,trace=HEURISTICS_TRACE;
    static constexpr std::array roster{0,3,8,4,1};
    int next_price=30;
    double merit(const heuristics::State&s,const heuristics::Job&j)const {
        const double finance=s.mature?.04:std::clamp(1.-double(s.gold)/(next_price*8.),.12,1.);
        const double pay=j.gems*(s.mature?3.:1.6)+j.gold*.45*finance;
        const double duration=2+j.travel+std::ceil(j.work)+s.capture+j.allies*5;
        const double reserve=s.maximum*(s.bag? .38:.27);
        const double risk=std::max(0.,double(j.loss)-std::max(0.,double(s.hp)-reserve))/std::max(1.f,s.maximum);
        const double exposure=std::max(0.,8.+j.travel+j.work-j.enemy_eta)*.025;
        return pay/std::max(1.,duration)-risk*3-exposure+(j.committed?.08:0);
    }
    int select(const heuristics::State&s,std::span<const heuristics::Job>jobs,bool partial)const {
        return heuristics::maximum(jobs,partial,[&](const auto&j){return merit(s,j);});
    }
    bool bank(const heuristics::State&s)const {
        // Marcella's later gold is useful only after an actual tribute.
        const int batch=s.identity==7?8:14;
        return s.bag>0&&(heuristics::deadline(s)||s.altar<=2||s.bag>=batch||s.hp<s.maximum*.58f);
    }
    bool retreat(float hp,float maximum,float incoming,int bag)const {
        return hp<=maximum*(bag? .37f:.24f)+incoming*(bag?3.f:2.5f);
    }
    static heuristics::Purchase purchase(int identity,unsigned monster,unsigned jungle) {
        using P=heuristics::Purchase;
        if(monster<5)return identity==1||identity==5?P::monster_focus:P::monster_strength;
        if(jungle<5)return identity==5?P::jungle_focus:(identity==10||identity==11?P::jungle_strength:P::jungle_health);
        return identity==1||identity==5||identity==7?P::power_focus:(identity==10?P::power_health:P::power_strength);
    }
    fundamentals::BuildStage build(unsigned monster,unsigned jungle)const {
        return fundamentals::build_stage(monster,jungle);
    }
};
}
