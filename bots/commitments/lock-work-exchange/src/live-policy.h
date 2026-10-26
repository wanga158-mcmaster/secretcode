#pragma once
#include "live-profile.h"
namespace p9::lock_work_exchange {
using namespace p9::live;
class Policy {
    Campaign campaign_{};
    int altar_=-1, source_=-1;
    bool placed_=false;
    int placement_round_=-1;
    const Altar* service(const World& w,const Actor& collector) const {
        const Altar* best=nullptr;
        for(int i=0;i<w.altar_count;++i) {
            const auto& a=w.altars[i];const int route=w.altar_travel(collector.id,a.id);
            if(!a.visible||!a.route_known||a.enemy_lock||a.travel<0||route<0||route+w.ticks(collector.bag)>120) continue;
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
        if(w.self.bag>0) return bank(-1,"ordinary_actual_conversion");
        const auto* collector=w.pick(1);
        if(w.self.pick==2) {
            const auto* g=nearest_safe(w);
            if(g&&ready(collector)&&collector->bag>0&&safe_for(*g,w.self))
                return support(collector->id,g->id,"same_collector_protection_both_arms");
            return {};
        }
        if(w.self.pick!=0) return {};
        if(placed_) {
            campaign_.finish();const auto* g=w.group(source_);
            return g&&g->safe_solo&&safe_for(*g,w.self)?farm(g->id,"lock_releases_producer"):Directive{};
        }
        const auto* a=w.altar(altar_);
        if(campaign_.active) {
            campaign_.tick(w,a&&a->visible);
            if(campaign_.expired(w)||!a||!a->visible||!ready(collector)||collector->bag<=0) {
                campaign_.finish();return {};
            }
        } else {
            if(!ready(collector)||collector->bag<=0) return {};
            a=service(w,*collector);const auto* source=nearest_safe(w);
            if(!a||!source||!cycle_value(w,*source).feasible) return {};
            if(arm==Arm::active && !w.has_lock && (w.lock_cost<=0||w.own_gold<w.lock_cost)) return {};
            if(!campaign_.start(w)) return {};
            altar_=a->id;source_=source->id;
        }
        if(arm==Arm::control) {
            if(w.self.can_cast&&w.self.cooldown[0]==0&&!w.extra_opposition)
                for(int i=0;i<w.enemy_count;++i) {
                    const auto& e=w.enemies[i];
                    if(e.visible&&e.alive&&e.type!=Champion::marcella&&e.channel==Channel::tribute&&
                        e.bag>0&&distance(e.position,a->position)<=1)
                        return {Task::intercept,-1,e.id,a->id,-1,true,"mobile_ready_stun_service_labor"};
                }
            return source_>=0?farm(source_,"no_applicable_interruption_target"):Directive{};
        }
        if(placement_round_>=0&&!w.has_lock) {
            if(!a->friendly_lock||w.round-placement_round_>1) {campaign_.finish();return {};}
            placed_=true;campaign_.finish();return farm(source_,"observed_lock_replaces_service_labor");
        }
        if(a->friendly_lock&&!w.has_lock) {campaign_.finish();return farm(source_,"existing_service_lock_needs_no_duplicate");}
        if(!w.has_lock) {
            if(w.own_gold<w.lock_cost||w.lock_cost<=0) {campaign_.finish();return {};}
            if(!w.self.at_spawn) return {Task::recover,-1,-1,-1,-1,true,"funded_lock_shop_visit"};
            return {Task::buy_lock,-1,-1,a->id,-1,true,"one_funded_service_lock"};
        }
        placement_round_=w.round;
        return {Task::place_lock,-1,-1,a->id,-1,true,"place_once_then_leave_service"};
    }
};
}
