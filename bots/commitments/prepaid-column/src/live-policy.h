#pragma once
#include "live-profile.h"
namespace p9::prepaid_column {
class Policy {
    live::Campaign campaign_{};
    int home_=-1,group_=-1,heal_round_=-1,staged_round_=-1;
    bool healed_=false,released_=false;
    live::Point staging_{};
    int varg_distance_=-1,collector_distance_=-1;
public:
    live::Directive step(const live::World& w,live::Arm arm) {
        using namespace live;
        if(home_<0&&w.productive_group>=0) home_=w.productive_group;
        if(w.immediate_bank()) return bank();
        if(!w.self.alive||!w.safe_now) {campaign_.finish();return {};}
        if(w.self.pick>2) return {};
        const auto* willow=w.pick(0);const auto* varg=w.pick(1);const auto* collector=w.pick(2);
        const auto* g=w.group(w.verified_column_group);
        const bool pair=ready(varg)&&ready(collector)&&distance(varg->position,collector->position)<=4;
        const bool unsupported=pair&&g&&w.column_without_healer_verified&&!w.extra_opposition&&
            !g->respawning&&safe_for(*g,*varg)&&safe_for(*g,*collector)&&
            w.group_travel(varg->id,g->id)>=0&&w.group_travel(collector->id,g->id)>=0;
        if(w.self.pick==0) {
            if(released_) {
                const auto* home=w.group(home_);
                return home&&home->safe_solo&&home->observed&&home->route_known&&!home->respawning?
                    farm(home_,"healer resumes independent production after witnessed departure"):Directive{};
            }
            if(!pair) {campaign_.finish();return {};}
            if(w.own_heal_receipt&&w.last_heal_actor==varg->id&&w.last_heal_amount>0&&
                w.last_heal_round>heal_round_&&w.last_heal_round<=w.round&&w.round-w.last_heal_round<=1) {
                healed_=true;heal_round_=w.last_heal_round;
                staging_=w.self.position;staged_round_=w.round;
                if(g) {group_=g->id;varg_distance_=distance(varg->position,g->position);
                    collector_distance_=distance(collector->position,g->position);}
            }
            if(!healed_) {
                if(unsupported&&varg->hp<varg->max_hp&&w.self.can_cast&&w.self.cooldown[0]==0&&
                    distance(varg->position,w.self.position)<=4)
                    return {Task::heal,-1,varg->id,-1,-1,true,"restore actual missing health before departure"};
                return {};
            }
            if(!unsupported||g->id!=group_) {campaign_.finish();return {};}
            if(!campaign_.active&&!campaign_.start(w)) return {};
            campaign_.tick(w,true);
            if(campaign_.expired(w)) {campaign_.finish();return {};}
            const bool departed=w.round>staged_round_&&distance(varg->position,g->position)<varg_distance_&&
                distance(collector->position,g->position)<collector_distance_&&
                distance(varg->position,staging_)>0&&distance(collector->position,staging_)>0;
            if(arm==Arm::active&&departed) {
                released_=true;campaign_.finish();
                const auto* home=w.group(home_);
                return home&&home->safe_solo&&home->observed&&home->route_known&&!home->respawning?
                    farm(home_,"prepaid actual health frees Willow's production"):Directive{};
            }
            if(arm==Arm::control&&varg->hp<varg->max_hp&&w.self.can_cast&&w.self.cooldown[0]==0&&
                distance(varg->position,w.self.position)<=4)
                return {Task::heal,-1,varg->id,-1,-1,true,"attached control retains currently legal support"};
            return support(varg->id,g->id,"Willow remains present until observed safe departure");
        }
        if(!unsupported) {campaign_.finish();return {};}
        if(!campaign_.active&&(!ready(willow)||distance(willow->position,varg->position)>4)) return {};
        if(!campaign_.active&&!campaign_.start(w)) return {};
        campaign_.tick(w,pair);if(campaign_.expired(w)) {campaign_.finish();return {};}
        if(w.self.pick==1) return support(collector->id,g->id,"unsupported viable clearer; no future Willow cast assumed");
        if(w.self.bag>0) return bank();
        return {g->cleared?Task::capture:Task::farm,g->id,-1,-1,-1,true,"Marcella owns future column payout"};
    }
};
}
