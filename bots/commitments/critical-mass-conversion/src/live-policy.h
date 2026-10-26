#pragma once
#include "live-profile.h"
namespace p9::critical_mass_conversion {
using namespace p9::live;
class Policy {
    Campaign campaign_{};
    int project_=-1, altar_=-1;
    bool lock_done_=false;
    const Group* terminal_source(const World& w,const Actor& collector,const Actor& helper) const {
        if(w.win_gems<=0||w.score<0||w.score>=w.win_gems||w.extra_opposition) return nullptr;
        const Group* best=nullptr;
        for(int i=0;i<w.group_count;++i) {
            const auto& g=w.groups[i];
            if(g.respawning||g.gems<=0||collector.bag>=w.win_gems-w.score||
                g.gems<w.win_gems-w.score-collector.bag||!safe_for(g,collector)||!safe_for(g,helper)||
                w.group_travel(collector.id,g.id)<0||w.group_travel(helper.id,g.id)<0||
                !cycle_value(w,g).feasible) continue;
            if(!best||g.id<best->id) best=&g;
        }
        return best;
    }
    const Altar* winning_service(const World& w,const Actor& collector,const Group& g) const {
        const Altar* best=nullptr;
        for(int i=0;i<w.altar_count;++i) {
            const auto& a=w.altars[i];const int d=w.altar_travel(collector.id,a.id);
            if(!a.visible||!a.route_known||a.enemy_lock||d<0||a.travel<0||
                d+w.ticks(collector.bag+g.gems)+g.clear_turns+w.capture_turns+2>120) continue;
            if(!best||a.id<best->id) best=&a;
        }
        return best;
    }
public:
    Directive step(const World& w,Arm arm) {
        campaign_.tick(w,false);
        if(w.self.channel!=Channel::none) return {Task::wait,-1,-1,-1,-1,false,"preserve_channel"};
        if(w.capture_active&&w.safe_now) return {Task::capture,w.productive_group};
        if(!w.safe_now) {campaign_.finish();return {Task::recover};}
        if(w.immediate_bank()) return bank(-1,"current_bag_actual_winning_conversion");
        if(arm==Arm::control||w.self.pick>2) return {};
        if(w.self.pick>0&&w.self.bag>0) return bank(-1,"support_keeps_existing_bag");
        const auto* collector=w.pick(0);const auto* helper=w.pick(1);
        if(!ready(collector)||!ready(helper)||distance(collector->position,helper->position)>6) {
            campaign_.finish();return w.self.bag>0?bank():Directive{};
        }
        // Future allocation is feasible now; existing inventories stay owned.
        const auto* g=terminal_source(w,*collector,*helper);
        const auto* a=g?winning_service(w,*collector,*g):nullptr;
        if(!g||!a) {campaign_.finish();return w.self.bag>0?bank():Directive{};}
        if(w.self.pick==2&&!w.has_lock&&!a->friendly_lock&&
            (w.lock_cost<=0||w.own_gold<w.lock_cost||!w.self.at_spawn)) return {};
        if(campaign_.active) {
            campaign_.tick(w,true);
            if(campaign_.expired(w)||g->id!=project_) {campaign_.finish();return w.self.bag>0?bank():Directive{};}
        } else {
            if(!campaign_.start(w)) return {};
            project_=g->id;altar_=a->id;
        }
        if(w.self.pick==0) return farm(project_,"future_terminal_reward_holder");
        if(w.self.pick==1) {
            if(w.self.bag>0) return bank(-1,"helper_keeps_existing_bag");
            return support(collector->id,project_,"current_funded_terminal_clearance");
        }
        if(lock_done_) return {};
        const int service=w.altar_travel(w.self.id,altar_);
        const int total=service+2+g->clear_turns+w.capture_turns+w.ticks(collector->bag+g->gems);
        if(service<0||total>120||total>w.windows[0]||(!w.has_lock&&(w.lock_cost<=0||w.own_gold<w.lock_cost))) return {};
        if(a->friendly_lock&&!w.has_lock) {lock_done_=true;return {};}
        if(!w.has_lock) {
            if(!w.self.at_spawn) return {}; // Never assume future shop/tribute income.
            return {Task::buy_lock,project_,collector->id,altar_,-1,true,"current_funded_terminal_service_capital"};
        }
        return {Task::place_lock,project_,collector->id,altar_,-1,true,"optional_terminal_lock"};
    }
};
}
