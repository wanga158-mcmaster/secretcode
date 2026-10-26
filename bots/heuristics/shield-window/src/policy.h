#pragma once
#include "config.h"
#include "flow.h"
#include "../../common/src/decision.h"
#include "../../common/src/equipment.h"
#include <tuple>

namespace h12_shield_window {
struct Policy {
    static constexpr bool enabled=HEURISTICS_ENABLED,trace=HEURISTICS_TRACE;
    static constexpr std::array roster{6,1,7,10,0};
    Window window;
    bool forced_bank=false,shield_ready=false;
    float predicted_shield=0;
    int current=-1,progress_round=0,best_distance=10000;
    float best_hp=std::numeric_limits<float>::infinity();
    int select(const heuristics::State&s,std::span<const heuristics::Job>jobs,bool partial) {
        int best=-1;std::tuple<int,int,double,int> rank{99,99,1e30,99999};
        const bool shield_role=s.identity==12||s.identity==10;
        const heuristics::Job*incumbent=nullptr;
        for(const auto&j:jobs)if(heuristics::allowed(j,partial)) {
            const double share=1+std::max(0,j.allies);
            const bool window_option=shield_role&&shield_ready&&predicted_shield>=j.incoming&&
                s.hp>s.maximum*(s.bag>0?.38f:.26f)+2*j.incoming&&j.incoming>0;
            const bool normal_partial=!shield_role&&(s.hp>=s.maximum*.75f||j.committed);
            if(partial&&!window_option&&!normal_partial)continue;
            if(j.id==window.source&&window.renewing()&&!shield_ready&&j.incoming>0)continue;
            const double horizon=std::min(2.f,j.work);
            const double progress=j.ready?1:std::min(1.,horizon/std::max(1.f,j.work));
            const double payout=(j.gems*2.+j.gold*(s.mature?.08:.45))/share;
            const double switches=current>=0&&j.id!=current?3:0;
            const double exposure=std::max(0.,double(j.incoming)*horizon-(window_option?predicted_shield:0));
            const double reserve_ratio=exposure/std::max(1.,double(s.hp-s.maximum*.26f));
            const double distance=j.travel+switches+2+(j.ready?s.capture:0);
            const double priority=payout*progress/distance;
            const bool cashable=s.limit-s.round>j.travel+j.work+s.capture+j.altar+std::ceil((s.bag+j.gems/share)*s.tribute)+4;
            const int risk=reserve_ratio>.5?2:reserve_ratio>.15?1:0;
            const auto r=std::tuple{cashable?0:1,j.ready?0:1, -priority/(1+risk),j.travel};
            if(r<rank){rank=r;best=j.id;}
            if(j.id==current)incumbent=&j;
        }
        // Fresh progress earns a short lease; a shield pulse never earns an
        // unbounded commitment merely by moving between equally distant tiles.
        if(incumbent&&!window.renewing()) {
            if(incumbent->travel<best_distance||incumbent->remaining_hp<best_hp)progress_round=s.round;
            best_distance=std::min(best_distance,incumbent->travel);best_hp=std::min(best_hp,incumbent->remaining_hp);
            if(s.round-progress_round<2)return current;
        }
        if(best!=current){current=best;best_distance=10000;best_hp=std::numeric_limits<float>::infinity();progress_round=s.round;}
        return best;
    }
    bool bank(const heuristics::State&s)const {
        if(s.bag<=0)return false;
        return forced_bank||heuristics::deadline(s)||s.bag>=8||s.altar<=2||
            s.hp<s.maximum*.58f||(window.renewing()&&s.bag>=4);
    }
    bool retreat(float hp,float maximum,float incoming,int bag)const {
        return hp<=maximum*(bag>0?.38f:.26f)+2*incoming;
    }
    static heuristics::Purchase purchase(int identity,unsigned monster,unsigned jungle) {
        using P=heuristics::Purchase;
        // The shield roles buy absorption throughput (Focus) after five
        // Monster Strength shards fund useful physical pulses. HP funds exits.
        if(monster<5)return identity==8?P::monster_focus:P::monster_strength;
        if(jungle<5)return identity==12||identity==10?P::jungle_focus:P::jungle_health;
        return identity==12||identity==10||identity==8?P::power_focus:P::power_strength;
    }
    fundamentals::BuildStage build(unsigned monster,unsigned jungle)const {
        if(monster<5)return fundamentals::BuildStage::monster_strength;
        if(jungle<5)return fundamentals::BuildStage::jungle_health;
        return fundamentals::BuildStage::power_strength;
    }
};
} // namespace h12_shield_window
