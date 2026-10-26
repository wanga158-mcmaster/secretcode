#pragma once
#include "config.h"
#include "flow.h"
#include "../../common/src/decision.h"
#include "../../common/src/equipment.h"
#include <tuple>

namespace h12_working_inventory {
struct Policy {
    static constexpr bool enabled=HEURISTICS_ENABLED,trace=HEURISTICS_TRACE;
    static constexpr std::array roster{5,4,3,0,7};
    Inventory inventory;
    bool forced_bank=false;
    int current=-1,progress_round=0,best_distance=10000;
    float best_hp=std::numeric_limits<float>::infinity();
    int select(const heuristics::State&s,std::span<const heuristics::Job>jobs,bool partial) {
        // Finance queues seek a bounded personal batch, with coarse downside
        // bands preceding excess production. No joint geometry/splash prior.
        int best=-1;std::tuple<int,int,int,int,int,double> rank{99,99,99,99,99999,1e30};
        const int batch=s.identity==6?8:6;
        const heuristics::Job*incumbent=nullptr;
        for(const auto&j:jobs)if(heuristics::allowed(j,partial)) {
            const double share=1+std::max(0,j.allies),gems=j.gems/share;
            const double finish=j.travel+std::ceil(j.work)+s.capture;
            const bool cashable=finish+j.altar+std::ceil((s.bag+gems)*s.tribute)+2<s.limit-s.round;
            const bool funding=!s.mature&&j.gold/share>=30;
            const int queue=j.ready?0:gems>0?1:funding?2:3;
            const int shortfall=gems>=batch?0:gems>0?1:2;
            const int downside=int(std::min(20.,4.*j.loss/std::max(1.f,s.hp)))+(j.enemy_eta<=finish?2:0);
            const int excess=int(std::max(0.,gems-batch));
            const auto r=std::tuple{cashable?0:1,queue,shortfall,downside,excess,finish+j.altar};
            if(r<rank){rank=r;best=j.id;}
            if(j.id==current)incumbent=&j;
            if(inventory.phase==Phase::work&&j.id==inventory.source&&cashable)return current=j.id;
        }
        if(incumbent) {
            if(incumbent->travel<best_distance||incumbent->remaining_hp<best_hp)progress_round=s.round;
            best_distance=std::min(best_distance,incumbent->travel);best_hp=std::min(best_hp,incumbent->remaining_hp);
            if(s.round-progress_round<s.capture)return current;
        }
        if(best!=current){current=best;best_distance=10000;best_hp=std::numeric_limits<float>::infinity();progress_round=s.round;}
        return best;
    }
    bool bank(const heuristics::State&s)const {
        if(s.bag<=0)return false;
        if(forced_bank||heuristics::deadline(s))return true;
        if(s.identity==6)return inventory.phase==Phase::liquidate;
        return s.bag>=6||s.altar<=2||s.hp<s.maximum*.6f;
    }
    bool retreat(float hp,float maximum,float incoming,int bag)const {
        return hp<=maximum*(bag>0?.37f:.24f)+3*incoming;
    }
    static heuristics::Purchase purchase(int identity,unsigned monster,unsigned jungle) {
        using P=heuristics::Purchase;
        if(identity==6||identity==8) {
            if(monster<5)return P::monster_focus;
            if(jungle<5)return identity==6?P::jungle_health:P::jungle_focus;
            return P::power_focus;
        }
        if(identity==7){if(monster<5)return P::monster_strength;if(jungle<5)return P::jungle_strength;return P::power_health;}
        if(monster<5)return P::monster_strength;
        if(jungle<5)return P::jungle_health;
        return P::power_strength;
    }
    fundamentals::BuildStage build(unsigned monster,unsigned jungle)const {
        if(monster<5)return fundamentals::BuildStage::monster_strength;
        if(jungle<5)return fundamentals::BuildStage::jungle_health;
        return fundamentals::BuildStage::power_strength;
    }
};
} // namespace h12_working_inventory
