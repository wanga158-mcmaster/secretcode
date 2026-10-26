#pragma once
#include "live-profile.h"
#ifndef P9_FORBEARANCE_RENEWAL
#define P9_FORBEARANCE_RENEWAL 0
#endif
namespace p9::forbearance_frontier {
using namespace p9::live;
class Policy {
    Campaign campaign_{};
    int home_=-1, outer_=-1;
    const Group* home(const World& w) const {
        const Group* best=nullptr;
        for(int i=0;i<w.group_count;++i) {
            const auto& g=w.groups[i];
            if(!g.observed||!g.route_known||!g.safe_solo||g.respawning||g.travel<0||
                g.own_distance<0||g.enemy_distance<0||g.own_distance>=g.enemy_distance) continue;
            if(!best||g.travel<best->travel||(g.travel==best->travel&&g.id<best->id)) best=&g;
        }
        return best;
    }
    bool intrusion(const World& w,const Group& g) const {
        if(!g.observed||!g.cleared||!g.contested||w.productive_group!=g.id) return false;
        for(int i=0;i<w.enemy_count;++i) {
            const auto& e=w.enemies[i];
            if(e.visible&&e.alive&&distance(e.position,g.position)==0) return true;
        }
        return false;
    }
#if P9_FORBEARANCE_RENEWAL
    Directive home_work(const World& w,const Group* original,const char* reason) const {
        // Keep the original physical sphere and its intrusion premise. A
        // captured source cannot pay again while respawning; work another
        // personally observed safe source on our side during that interval.
        // This does not license an unobserved or enemy-side counterclaim.
        if(original&&original->respawning)
            if(const auto* available=home(w))
                return farm(available->id,"renew_within_original_home_sphere");
        return original&&original->safe_solo?farm(home_,reason):Directive{};
    }
#endif
public:
    Directive step(const World& w,Arm arm) {
        campaign_.tick(w,false);
        if(w.self.channel!=Channel::none) return {Task::wait,-1,-1,-1,-1,false,"preserve_channel"};
        if(!w.safe_now) {campaign_.finish();return {Task::recover};}
        const auto* holding=w.group(w.productive_group);
        if(w.capture_active&&(!holding||!holding->contested)) return {Task::capture,w.productive_group};
        if(w.self.pick!=1&&w.self.pick!=2) return {};
        if(w.self.bag>0) {campaign_.finish();return bank(-1,"counterclaim_actual_conversion");}
        if(home_<0) {const auto* g=home(w);if(g) home_=g->id;}
        const auto* original=w.group(home_);
        const auto* peer=w.pick(w.self.pick==1?2:1);
        if(campaign_.active) {
            const auto* g=w.group(outer_);campaign_.tick(w,g&&g->observed);
            if(w.own_bank_completed||campaign_.expired(w)||!g||!safe_for(*g,w.self)) {
#if P9_FORBEARANCE_RENEWAL
                campaign_.finish();return home_work(w,original,"return_original_sphere");
#else
                campaign_.finish();return original?farm(home_,"return_original_sphere"):Directive{};
#endif
            }
            if(w.capture_active) return {Task::capture,w.productive_group};
            return farm(outer_,"bounded_productive_outer_counterclaim");
        }
        if(w.capture_active&&!original) return {Task::capture,w.productive_group};
        const auto* outer=nearest_safe(w,home_,true);
        const bool joint=ready(peer)&&distance(peer->position,w.self.position)<=6&&
            outer&&w.group_travel(peer->id,outer->id)>=0&&safe_for(*outer,*peer)&&cycle_value(w,*outer).feasible;
        if(joint&&original&&(arm==Arm::control||intrusion(w,*original))&&campaign_.start(w)) {
            outer_=outer->id;return farm(outer_,arm==Arm::active?"observed_region_intrusion":"unconditional_outer_pressure");
        }
        if(w.capture_active) return {Task::capture,w.productive_group};
#if P9_FORBEARANCE_RENEWAL
        return home_work(w,original,"forbear_own_side_source");
#else
        return original&&original->safe_solo?farm(home_,"forbear_own_side_source"):Directive{};
#endif
    }
};
}
