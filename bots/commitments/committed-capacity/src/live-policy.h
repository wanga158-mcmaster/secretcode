#pragma once
#include "live-profile.h"
namespace p9::committed_capacity {
using namespace p9::live;
class Policy {
    Campaign campaign_{};
    int project_=-1, collector_=-1, circuit_=-1, funded_shards_=0;
    const Group* contested(const World& w) const {
        const Group* best=nullptr;
        for(int i=0;i<w.group_count;++i) {
            const auto& g=w.groups[i];
            if(!g.contested || g.respawning || !g.safe_solo || !safe_for(g,w.self) ||
                !cycle_value(w,g).feasible) continue;
            if(!best || g.travel<best->travel || (g.travel==best->travel&&g.id<best->id)) best=&g;
        }
        return best;
    }
public:
    Directive step(const World& w, Arm arm) {
        campaign_.tick(w,false);
        if(w.self.channel!=Channel::none) return {Task::wait,-1,-1,-1,-1,false,"preserve_channel"};
        if(w.capture_active&&w.safe_now) return {Task::capture,w.productive_group};
        if(!w.safe_now) { campaign_.finish(); return {Task::recover}; }
        if(w.immediate_bank()) return bank(-1,"actual_winning_bag");
        // The collector independently adopts visible paid capacity and retains
        // her already-owned bag; only a later actual own payout ends collection.
        if(arm==Arm::active&&w.self.pick==1) {
            const auto* clearer=w.pick(0);
            const auto* g=campaign_.active?w.group(project_):contested(w);
            if(campaign_.active&&w.last_payout_group==project_&&w.last_payout_round>=campaign_.admitted) {
                campaign_.finish();return bank(-1,"capacity_collector_actual_payout");
            }
            if(!ready(clearer)||clearer->defense_shards<=0||!g||
                w.group_travel(clearer->id,g->id)<0||!safe_for(*g,*clearer)||
                distance(clearer->position,w.self.position)>5) {
                campaign_.finish();return w.self.bag>0?bank():Directive{};
            }
            if(!campaign_.active) {
                if(w.self.bag<=0||!campaign_.start(w)) return w.self.bag>0?bank():Directive{};
                project_=g->id;
            }
            campaign_.tick(w,true);
            if(campaign_.expired(w)) {campaign_.finish();return w.self.bag>0?bank():Directive{};}
            return farm(g->id,"visible_paid_capacity_collector");
        }
        if(w.self.bag>0) return bank(-1,"paid_capacity_actual_bag");
        if(arm==Arm::control || w.self.pick>1) return {};
        const auto* collector=w.actor(collector_);
        const auto* group=w.group(project_);
        if(campaign_.active) {
            campaign_.tick(w,ready(collector)&&group&&group->observed);
            if(campaign_.expired(w)||!ready(collector)||collector->bag<=0||!group||w.group_travel(collector->id,group->id)<0||
                !safe_for(*group,w.self)||!safe_for(*group,*collector)) {
                campaign_.finish();
                return circuit_>=0?farm(circuit_,"capacity_return_to_circuit"):Directive{};
            }
        } else {
            collector=w.pick(1); group=contested(w);
            if(!ready(collector)||collector->bag<=0||!group||w.group_travel(collector->id,group->id)<0||!safe_for(*group,*collector)||
                !campaign_.may_start(w)) return {};
            if(funded_shards_==0) {
                if(w.defense_cost<=0||w.own_gold<w.defense_cost) return {};
                funded_shards_=std::min(5,w.own_gold/w.defense_cost);
            }
            if(!campaign_.start(w)) return {};
            project_=group->id; collector_=collector->id;
            const auto* next=nearest_safe(w,project_); circuit_=next?next->id:-1;
        }
        if(w.self.defense_shards<funded_shards_) {
            if(w.own_gold<w.defense_cost) { campaign_.finish(); return {}; }
            if(!w.self.at_spawn) return {Task::recover,-1,-1,-1,-1,true,"funded_capacity_shop_visit"};
            return {Task::buy_defense,project_,collector_,-1,-1,true,"funded_durability_health_bundle",
                std::max(funded_shards_,w.self.defense_shards+1)};
        }
        return support(collector_,project_,"paid_capacity_contested_escort");
    }
};
}
