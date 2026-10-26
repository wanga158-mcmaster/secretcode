#pragma once
#include "live-profile.h"
namespace p9::capped_contest_investment {
using namespace p9::live;
class Policy {
    Campaign campaign_{};
    int project_=-1, released_=-1;
    bool compared_=false;
    const Group* candidate(const World& w) const {
        const Group* best=nullptr;
        for(int i=0;i<w.group_count;++i) {
            const auto& g=w.groups[i];
            if(!g.contested||g.respawning||!g.safe_solo||!safe_for(g,w.self)||!cycle_value(w,g).feasible) continue;
            if(!best||g.id<best->id) best=&g;
        }
        return best;
    }
public:
    Directive step(const World& w,Arm arm) {
        campaign_.tick(w,false);
        if(w.self.channel!=Channel::none) return {Task::wait,-1,-1,-1,-1,false,"preserve_channel"};
        if(!w.safe_now) {campaign_.finish();return {Task::recover};}
        if(w.self.bag>0) {campaign_.finish();return bank(-1,"contest_real_bag");}
        if(w.self.pick>1) return {};
        if(w.capture_active) return {Task::capture,w.productive_group};
        if(released_>=0) {
            const auto* alt=w.group(released_);
            return alt&&alt->safe_solo&&safe_for(*alt,w.self)?farm(released_,"released_productive_alternative"):Directive{};
        }
        const auto* g=w.group(project_);
        if(!campaign_.active) {
            g=candidate(w);const auto* peer=w.pick(1-w.self.pick);
            if(!g||!ready(peer)||distance(peer->position,w.self.position)>6||
                w.group_travel(peer->id,g->id)<0||!safe_for(*g,*peer)||!campaign_.start(w)) return {};
            project_=g->id;compared_=false;
        }
        campaign_.tick(w,g&&g->observed);
        // Existing safe occupancy completes even if its segment reaches the cap.
        if(w.capture_active) return {Task::capture,w.productive_group};
        if(campaign_.expired(w)||!g||!safe_for(*g,w.self)) {campaign_.finish();return {};}
        if(arm==Arm::active&&!compared_&&campaign_.turns>=8) {
            compared_=true;
            const auto* alternate=nearest_safe(w,project_);
            if(alternate) {
                // Current HP, remaining work and current travel are used here.
                // No initial HP, past travel or damage credit enters either side.
                const auto staying=cycle_value(w,*g), leaving=cycle_value(w,*alternate);
                if(better(leaving,staying)) {
                    released_=alternate->id;campaign_.finish();
                    return farm(released_,"marginal_remaining_work_reallocation");
                }
            }
        }
        if(w.self.pick==1) {
            const auto* holder=w.pick(0);
            if(!ready(holder)) {campaign_.finish();return {};}
            return support(holder->id,project_,"bounded_contest_clearer");
        }
        return farm(project_,"complete_contest_until_stage_or_cap");
    }
};
}
