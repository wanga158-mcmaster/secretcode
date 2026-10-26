#pragma once
#include "live-profile.h"
namespace p9::spent_response_exchange {
class Policy {
    live::Campaign campaign_{};
    int enemy_=-1,seen_=-1;
    std::array<int,2> cooldown_{-1,-1};
    static int conversion(const live::World& w,const live::Actor& collector) {
        int best=121;
        for(int i=0;i<w.altar_count;++i) {
            const auto& a=w.altars[i]; const int travel=w.altar_travel(collector.id,a.id);
            if(a.visible&&a.route_known&&!a.enemy_lock&&travel>=0)
                best=std::min(best,travel+w.ticks(collector.bag)+1);
        }
        return best;
    }
public:
    live::Directive step(const live::World& w,live::Arm arm) {
        using namespace live;
        const Actor* enemy=nullptr;
        if(w.enemy_count==1&&!w.extra_opposition&&w.enemies[0].visible&&w.enemies[0].alive) enemy=&w.enemies[0];
        bool spent=false;
        if(enemy&&enemy->type!=Champion::vendetta&&enemy->cooldown[0]>=0&&enemy->cooldown[1]>=0) {
            if(enemy_==enemy->id&&seen_==w.round-1)
                for(int slot=0;slot<2;++slot) spent|=cooldown_[slot]==0&&enemy->cooldown[slot]>0;
            enemy_=enemy->id; seen_=w.round;cooldown_=enemy->cooldown;
        } else {enemy_=-1;seen_=-1;cooldown_={-1,-1};}
        if(w.immediate_bank()) return bank();
        if(w.self.pick>1||!w.self.alive||!w.safe_now) {campaign_.finish();return {};}
        const auto* ren=w.pick(0); const auto* collector=w.pick(1);
        const bool pair=ready(ren)&&ready(collector)&&collector->bag>0&&
            distance(ren->position,collector->position)<=4&&ren->hp>=.35*ren->max_hp&&
            collector->hp>=.35*collector->max_hp;
        const int stage=pair?conversion(w,*collector):121;
        const bool viable=pair&&stage<=120;
        const bool protected_window=viable&&enemy&&enemy->type!=Champion::vendetta&&
            w.response_window_verified&&w.verified_response_enemy==enemy->id&&
            w.verified_response_until>=w.round&&w.verified_response_until-w.round>stage&&
            enemy->cooldown[0]>stage&&enemy->cooldown[1]>stage;
        if(campaign_.active) {
            campaign_.tick(w,pair);
            if(!viable||campaign_.expired(w)||(arm==Arm::active&&!protected_window)) {
                campaign_.finish();return {};
            }
        } else if(!viable||(arm==Arm::active&&(!spent||!protected_window))||!campaign_.start(w)) return {};
        if(w.self.pick==1) return {Task::bank,-1,-1,-1,-1,true,"collector executes currently feasible conversion"};
        return support(collector->id,-1,arm==Arm::active?
            "fresh queried expenditure admits visible conversion protection":"same ready organization without expenditure gate");
    }
};
}
