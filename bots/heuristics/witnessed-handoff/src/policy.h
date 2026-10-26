#pragma once
#include "config.h"
#include "../../common/src/decision.h"
#include "../../common/src/equipment.h"
namespace h12_witnessed_handoff {
// Executable work auction: each worker prices its own completion loss, crowding
// and conversion trip. Varg is the designated clearer; no private route program.
struct Policy {
    static constexpr bool enabled=HEURISTICS_ENABLED,trace=HEURISTICS_TRACE;
    static constexpr std::array roster{1,4,0,2,7};
    bool forced_bank=false;
    int select(const heuristics::State&s,std::span<const heuristics::Job>jobs,bool partial) {
        return heuristics::maximum(jobs,partial,[&](const heuristics::Job&j) {
            const double damage=std::max(0.f,j.loss-s.hp*.32f)/std::max(1.f,s.maximum);
            const double reward=j.gems*3.0+j.gold*(s.monster<5?.55:.12);
            const double completion=j.travel+j.work+s.capture+2;
            const double conversion=j.gems?j.altar*.65:0;
            const double crowd=1.0+j.allies*.85;
            const double certainty=j.known?1.08:.90;
            const double continuation=j.committed?1.12:1.0;
            return reward*certainty*continuation/(completion+conversion+damage*30)/crowd
                -heuristics::pressure(j)*.045;
        });
    }
    bool bank(const heuristics::State&s)const {
        return s.bag>0&&(forced_bank||heuristics::deadline(s)||s.hp<s.maximum*.60f||
            s.bag>=12||s.altar<=3||s.round%48>=40);
    }
    bool retreat(float hp,float maximum,float incoming,int bag)const {
        return hp<=maximum*(bag>0?.38f:.25f)+incoming*2.5f;
    }
    static heuristics::Purchase purchase(int identity,unsigned monster,unsigned jungle) {
        using P=heuristics::Purchase;
        // Strength immediately shortens ordinary clears. Durable holders then
        // buy Health; Varg buys Strength in both families for outside work.
        if(monster<5)return P::monster_strength;
        if(jungle<5)return identity==10?P::jungle_strength:P::jungle_health;
        return identity==10?P::power_strength:P::power_health;
    }
    fundamentals::BuildStage build(unsigned monster,unsigned jungle)const {
        return fundamentals::build_stage(monster,jungle);
    }
};
}
