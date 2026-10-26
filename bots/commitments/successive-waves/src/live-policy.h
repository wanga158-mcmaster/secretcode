#pragma once
#include "live-profile.h"
namespace p9::successive_waves {
using namespace p9::live;
class Policy {
    Campaign campaign_{};
    int project_=-1, rival_=-1;
    int leader_round_=-1, leader_group_=-1, leader_distance_=-1;
    bool saw_first_approach_=false;
    const Group* project(const World& w) const {
        const Group* best=nullptr;
        for(int i=0;i<w.group_count;++i) {
            const auto& g=w.groups[i];
            if(!g.contested||g.respawning||!g.safe_solo||!safe_for(g,w.self)||!cycle_value(w,g).feasible) continue;
            if(!best||g.id<best->id) best=&g;
        }
        return best;
    }
    const Actor* window(const World& w,const Group& g,int id=-1) const {
        if(w.extra_opposition) return nullptr;
        for(int i=0;i<w.enemy_count;++i) {
            const auto& e=w.enemies[i];
            if((id>=0&&e.id!=id)||!e.visible||!e.alive||e.max_hp<=0||e.cooldown[0]<0||e.cooldown[1]<0||distance(e.position,g.position)>5) continue;
            if(e.hp<.65*e.max_hp||e.cooldown[0]>0||e.cooldown[1]>0) return &e;
        }
        return nullptr;
    }
public:
    Directive step(const World& w,Arm arm) {
        campaign_.tick(w,false);
        if(w.self.channel!=Channel::none) return {Task::wait,-1,-1,-1,-1,false,"preserve_channel"};
        if(w.capture_active&&w.safe_now) return {Task::capture,w.productive_group};
        if(!w.safe_now) {campaign_.finish();return {Task::recover};}
        if(w.self.bag>0) {campaign_.finish();return bank(-1,"wave_actual_conversion");}
        if(w.self.pick>1) return {};
        if(w.self.pick==0) {
            const auto* g=w.group(project_);
            if(!campaign_.active) {
                g=project(w);
                if(!g||!campaign_.start(w)) return {};
                project_=g->id;
            }
            campaign_.tick(w,g&&g->observed);
            const int boundary=arm==Arm::active?8:16;
            if(campaign_.turns>=boundary||campaign_.expired(w)||!g||!safe_for(*g,w.self)) {
                campaign_.finish();return {Task::recover,-1,-1,-1,-1,false,"first_wave_declared_exit"};
            }
            return farm(project_,"first_wave_productive_contest");
        }
        if(arm==Arm::control) return {};
        if(campaign_.active) {
            const auto* g=w.group(project_);
            const auto* e=g?window(w,*g,rival_):nullptr;
            campaign_.tick(w,e!=nullptr);
            if(!g||!e||campaign_.expired(w)||!safe_for(*g,w.self)) {
                campaign_.finish();return {};
            }
            return farm(project_,"independent_fresh_second_wave");
        }
        const auto* g=project(w); const auto* first=w.pick(0);
        if(!g||!first||!first->visible||!first->alive||distance(first->position,w.self.position)>6) {
            leader_round_=-1;saw_first_approach_=false;return {};
        }
        const int d=w.group_travel(first->id,g->id);
        if(d<0) {leader_round_=-1;saw_first_approach_=false;return {};}
        const bool departed=first->channel==Channel::none&&saw_first_approach_&&leader_group_==g->id&&
            w.round==leader_round_+1&&leader_distance_<=3&&d>leader_distance_;
        const auto* enemy=window(w,*g);
        if(departed&&enemy&&campaign_.start(w)) {
            project_=g->id;rival_=enemy->id;
            return farm(project_,"witnessed_departure_current_window");
        }
        saw_first_approach_=(leader_group_==g->id&&saw_first_approach_)||d<=3;
        leader_round_=w.round;leader_group_=g->id;leader_distance_=d;
        const auto* own=nearest_safe(w,g->id);
        return own?farm(own->id,"separate_second_wave_circuit"):Directive{};
    }
};
}
