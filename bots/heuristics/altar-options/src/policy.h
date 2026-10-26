#pragma once
#include "config.h"
#include "../../common/src/decision.h"
#include "../../common/src/equipment.h"

namespace h12_altar_options {
// A financing lot, rather than a completion auction: first choose whether a
// source can pay the next useful purchase, then minimise its own work burden.
struct Policy {
    static constexpr bool enabled=HEURISTICS_ENABLED,trace=HEURISTICS_TRACE;
    static constexpr std::array roster{4,0,3,1,9};
    int next_price=30; // Fixture default; SDK shop refreshes the queried item cost.
    int ticket=-1,start=0,progress=0,best_travel=10000;
    float best_hp=std::numeric_limits<float>::infinity();
    int select(const heuristics::State&s,std::span<const heuristics::Job>jobs,bool partial) {
        const double needed=s.mature?0:std::max(0,next_price-s.gold);
        const heuristics::Job*incumbent=nullptr;
        for(const auto&j:jobs)if(j.id==ticket&&heuristics::allowed(j,partial))incumbent=&j;
        if(incumbent) {
            if(incumbent->travel<best_travel||incumbent->remaining_hp<best_hp)progress=s.round;
            best_travel=std::min(best_travel,incumbent->travel);
            best_hp=std::min(best_hp,incumbent->remaining_hp);
            if(s.round-start<90&&s.round-progress<12)return ticket;
        }
        std::array<double,4> best{1e100,1e100,1e100,1e100};int chosen=-1;
        for(const auto&j:jobs)if(heuristics::allowed(j,partial)) {
            // Marcella's gems become spendable gold only after service. Other
            // workers cannot finance a shop from cargo alone.
            const double cash=j.gold+(s.identity==7?2*j.gems:0);
            const double useful=needed>0?std::min(needed,cash):j.gems;
            const double safety=j.loss<=std::max(0.f,s.hp-s.maximum*.35f)?0:1;
            const double funded=needed>0?(cash>=needed?0:1):(j.gems>0?0:1);
            const double work=j.travel+std::ceil(j.work)+s.capture+1;
            const double burden=work/std::max(1.,useful)+j.allies*2+(j.known?0:2)
                +(s.identity==7&&j.gems>0?j.altar*.15:0);
            const std::array key{safety,funded,burden,double(j.id)};
            if(key<best){best=key;chosen=j.id;}
        }
        if(chosen!=ticket||!incumbent||s.round-start>=90||s.round-progress>=12) {
            ticket=chosen;start=progress=s.round;best_travel=10000;
            best_hp=std::numeric_limits<float>::infinity();
            for(const auto&j:jobs)if(j.id==ticket){best_travel=j.travel;best_hp=j.remaining_hp;break;}
        }
        return chosen;
    }
    bool bank(const heuristics::State&s)const {
        const bool funds=s.identity==7&&!s.mature&&s.gold<next_price&&s.gold+2*s.bag>=next_price;
        return s.bag>0&&(heuristics::deadline(s)||s.altar<=2||funds||
            s.bag>=(s.identity==7?10:18)||s.hp<s.maximum*.55f);
    }
    bool retreat(float hp,float maximum,float incoming,int bag)const {
        // Cashable inventory raises the life reserve; an empty worker can
        // accept a longer partial clear without an artificial banking phase.
        return hp<=maximum*(bag>0?.34f:.22f)+incoming*(bag>0?3.f:2.f);
    }
    static heuristics::Purchase purchase(int identity,unsigned monster,unsigned jungle) {
        using P=heuristics::Purchase;
        if(monster<5)return identity==1?P::monster_focus:P::monster_strength;
        if(jungle<5)return identity==10?P::jungle_strength:P::jungle_health;
        return identity==1?P::power_focus:(identity==7||identity==0?P::power_health:P::power_strength);
    }
    fundamentals::BuildStage build(unsigned monster,unsigned jungle)const {
        return fundamentals::build_stage(monster,jungle);
    }
};
}
