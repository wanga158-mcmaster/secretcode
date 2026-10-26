#pragma once
#include "config.h"
#include "../../common/src/decision.h"
namespace h12_satisficer {
struct Policy {
    int current=-1,progress_round=0,last_travel=10000;
    float last_hp=1e30f;
    static constexpr bool enabled=HEURISTICS_ENABLED;
    static constexpr bool trace=HEURISTICS_TRACE;
    static constexpr std::array roster{0,1,2,3,4};
    // Bounded rationality: first satisfy safety and useful income, then minimise
    // time. Aspirations rise with equipment; completed rewards outrank forecasts.
    int select(const heuristics::State&s,std::span<const heuristics::Job>jobs,bool partial) {
#if !SATISFICER_ASPIRATION_CONTROL
        for(const auto&j:jobs)if(j.id==current&&heuristics::allowed(j,partial)) {
            if(j.travel<last_travel||j.remaining_hp<last_hp)progress_round=s.round;
            last_travel=std::min(last_travel,j.travel);last_hp=std::min(last_hp,j.remaining_hp);
            if(s.round-progress_round<8&&(partial||s.hp-j.loss>=s.maximum*.25f))return current;
        }
#endif
        int best=-1;std::array<float,4> key{-1e9f,-1e9f,-1e9f,-1e9f};
        for(const auto&j:jobs)if(heuristics::allowed(j,partial)) {
            const float reserve=(s.hp-j.loss)/std::max(1.f,s.maximum);
            const bool useful=s.mature?j.gems>=8:j.gold>=30;
#if SATISFICER_ASPIRATION_CONTROL
            // A narrow experimental ablation: keep the shared executor, ready
            // rewards, congestion, cycle, finance and retreat; remove only the
            // useful/survival rank and progress lease for native attribution.
            const std::array next{j.ready?3.f:1.f,
#else
            const std::array next{j.ready?3.f:(useful&&reserve>=.40f?2.f:1.f),
#endif
                -float(j.allies),-heuristics::cycle(s,j),j.committed?1.f:0.f};
            if(next>key){key=next;best=j.id;}
        }
        if(best>=0&&best!=current){current=best;progress_round=s.round;last_travel=10000;last_hp=1e30f;}
        return best;
    }
    bool bank(const heuristics::State&s) const {
        return s.bag>0&&(heuristics::deadline(s)||s.altar<=2||s.bag>=20||(s.bag>=10&&s.hp<s.maximum*.5f));
    }
    bool retreat(float hp,float maximum,float incoming,int bag) const {
        return hp<maximum*(bag>=20?.4f:.32f)||hp<=incoming*2+60;
    }
    fundamentals::BuildStage build(unsigned monster,unsigned jungle) const {
        return fundamentals::build_stage(monster,jungle);
    }
};
}
