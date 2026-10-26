#pragma once
#include "config.h"
#include "flow.h"
#include "../../common/src/equipment.h"
namespace h12_learned_circuit {
struct Policy {
    static constexpr bool enabled=HEURISTICS_ENABLED,trace=HEURISTICS_TRACE;
    static constexpr std::array roster{3,4,0,10,8};
    Circuit circuit;
    bool forced_bank=false;
    int select(const heuristics::State&s,std::span<const heuristics::Job>jobs,bool partial) {
        if(partial)return -1; // planning already admits healthy bounded partial work
        circuit.plan(s,jobs);
        const int wanted=circuit.current();
        for(const auto&j:jobs)if(j.id==wanted) {
            circuit.observe_progress(s.round,j.travel,j.remaining_hp);
            if(j.affordable||s.hp>=s.maximum*.75f||j.committed)return wanted;
        }
        // Omitted expensive detail is unknown, never a failed stop. Runtime
        // directly admits its cheap public geometry before running this hook.
        return -1;
    }
    bool bank(const heuristics::State&s)const {
        return s.bag>0&&(forced_bank||circuit.stage==Stage::bank||heuristics::deadline(s)||
            s.hp<s.maximum*.52f||s.bag>=24);
    }
    bool retreat(float hp,float maximum,float incoming,int bag)const {
        return hp<=maximum*(bag>0?.34f:.27f)+incoming*2;
    }
    static heuristics::Purchase purchase(int identity,unsigned monster,unsigned jungle) {
        using P=heuristics::Purchase;
        if(monster<5)return identity==7||identity==9||identity==5?P::monster_focus:P::monster_strength;
        if(jungle<5)return identity==1?P::jungle_strength:P::jungle_health;
        return identity==1?P::power_health:identity==7||identity==9||identity==5?P::power_focus:P::power_strength;
    }
    fundamentals::BuildStage build(unsigned monster,unsigned jungle)const {
        return fundamentals::build_stage(monster,jungle);
    }
};
} // namespace h12_learned_circuit
