#pragma once
#include "live-profile.h"
namespace p9::counterclaim {
class Policy {
    live::Campaign campaign_{};
    int converter_=-1,rival_=-1,source_=-1,branch_=0;
    int prior_round_=-1,prior_distance_=-1;
    static live::Directive ordinary(const live::World& w,const char* why="counterclaim-independent") {
        if(!w.self.alive||!w.safe_now) return {live::Task::recover,-1,-1,-1,-1,false,why};
        return {live::Task::independent,-1,-1,-1,-1,false,why};
    }
    static bool threatening(const live::Actor& rival,const live::Actor& converter) {
        return rival.can_attack&&rival.range>=0&&live::distance(rival.position,converter.position)<=rival.range;
    }
    live::Directive choose(const live::World& w,live::Arm arm,const live::Actor& converter,const live::Actor& rival) {
        using namespace live;
        const int current=distance(rival.position,converter.position);
        const bool consecutive=prior_round_>=0&&w.round-prior_round_==1;
        if(arm==Arm::active&&branch_==0) {
            if(threatening(rival,converter)||(consecutive&&current<prior_distance_)) branch_=1;
            else if(consecutive&&current>prior_distance_) branch_=2;
        }
        prior_round_=w.round;prior_distance_=current;
        if(branch_==1) return support(converter.id,source_,"counterclaim-observed-approach-escort");
        if(branch_==2) return {Task::intercept,-1,rival.id,-1,-1,true,"counterclaim-observed-departure-intercept"};
        auto out=farm(source_,"counterclaim-productive-regional-option");out.strategic=true;return out;
    }
public:
    live::Directive step(const live::World& w,live::Arm arm) {
        using namespace live;
        if(campaign_.active) campaign_.tick(w,false);
        if(w.self.channel!=Channel::none||w.capture_active) return {};
        if(!w.self.alive||!w.safe_now||w.extra_opposition||w.self.pick<0||w.self.pick>1||w.self.bag>0) {
            campaign_.finish();return ordinary(w,"counterclaim-no-safe-regional-role");
        }
        if(campaign_.active) {
            const auto* converter=w.actor(converter_);const auto* rival=w.actor(rival_);const auto* group=w.group(source_);
            const bool fresh=converter&&converter->visible&&converter->alive&&converter->ally&&converter->bag>0&&rival&&rival->visible&&
                rival->alive&&!rival->ally&&group&&group->observed&&group->route_known;
            campaign_.tick(w,fresh);
            if(!fresh||campaign_.expired(w)||distance(rival->position,w.self.position)>4||
                rival->bag<=0||rival->hp>3*std::max(1.,w.self.strength)||
                w.self.hp<std::max(.35*w.self.max_hp,3*rival->strength)||!safe_for(*group,w.self)) {
                campaign_.finish();return ordinary(w,"counterclaim-dependency-or-exposure-lost");
            }
            return choose(w,arm,*converter,*rival);
        }
        const auto* converter=w.pick(2);const auto* rival=favorable_rival(w);
        const auto* group=nearest_safe(w);
        if(!ready(converter)||!converter->ally||converter->bag<=0||!rival||!group||
            distance(converter->position,w.self.position)>4) return ordinary(w,"counterclaim-no-current-two-purpose-opportunity");
        bool service=false;
        for(int i=0;i<w.altar_count;++i) {
            const auto& altar=w.altars[i];
            if(altar.visible&&altar.route_known&&!altar.enemy_lock&&w.altar_travel(converter->id,altar.id)>=0)
                service=true;
        }
        int exclusive=0;
        if(arm==Arm::control) {
            // Charge the full bounded regional segment to each exclusive
            // package, then the same complete independent conversion/renewal.
            // There is no guaranteed future loot or ally score in either row.
            auto intercept=cycle_value(w,*group,16),escort=intercept;
            intercept.id=0;escort.id=1;
            if(!intercept.feasible||!escort.feasible) return ordinary(w,"counterclaim-no-complete-control-continuation");
            exclusive=threatening(*rival,*converter)?1:(better(intercept,escort)?2:1);
        }
        if(!service||!campaign_.start(w)) return ordinary(w,"counterclaim-service-or-campaign-unavailable");
        converter_=converter->id;rival_=rival->id;source_=group->id;prior_round_=-1;prior_distance_=-1;branch_=0;
        // Current safety precedes the common schedule ranking. Stable package
        // IDs resolve otherwise equal complete exclusive continuations.
        if(arm==Arm::control) branch_=exclusive;
        return choose(w,arm,*converter,*rival);
    }
};
}
