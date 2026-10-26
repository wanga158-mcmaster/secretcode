#pragma once
#include "../../earned-center/src/trace.h"

namespace p8::circuitbreaker {
// One persistent object per actual actor, including across U/J, epochs and
// deaths. J members establish their own shares, never the collector's receipt.
struct JTrace:earned_center::CycleTrace {
    std::array<bool,6> captured{},departed{};
    bool begin(const EvidenceLedger& ledger,const Work& work) {
        if(work.plan.organization!=Organization::J) {
            captured={};departed={};return CycleTrace::begin(ledger,work);
        }
        const auto* e=ledger.active();const auto& p=work.plan;
        if(active||!e||!work.active||work.cycle_completed||!work.generation||
            work.started_clock.round!=e->issue_round||work.started_clock.self!=e->premises.key.actor_id||
            work.last_round>=e->issue_round||work.productive_action_seen||work.payout_seen||work.bank_seen||
            crew_index(p,e->premises.key.actor_id)<0||p.crew_count!=3||
            e->premises.key.source_group!=p.group||e->premises.key.route_hash!=plan_route_hash(p))return false;
        serial=e->serial;owner=e->premises.key.actor_id;group=p.group;issue_round=e->issue_round;
        last_round=-1;plan=p;route=plan_route_hash(p);work_generation=work.generation;work_start=work.started_clock;
        production_round=payout_round=bank_round=spawn_round=renewal_round=-1;
        payout_event=bank_event=renewal_event=-1;
        active=true;completed=spawn_shop=failed=purchase_attempted=purchase_confirmed=false;
        payout_lower=bank_lower=0;helper_work={};helper_released={};captured={};departed={};return true;
    }
    void observe(const LocalFrame& local,const Work& work,const p6::Observation<bool>& shop_legal) {
        const bool before_payout=payout_round<0;
        CycleTrace::observe(local,work,shop_legal);
        if(!active||plan.organization!=Organization::J||!local.fresh()||
            local.frame.clock.self!=owner||local.frame.clock.round!=last_round)return;
        const auto* g=local.frame.live_group(group);
        for(int i=0;i<plan.crew_count;++i) {
            const int id=plan.crew[i];const auto* actor=local.frame.visible(id);
            if(!actor||!actor->alive||actor->team!=p6::Team::ally) {
                if(!departed[i])failed=true;
                continue;
            }
            // The public clear is insufficient: this actor must personally see
            // each member occupying a source tile before its OWN payout.
            if(before_payout&&actor->spawn_group==group&&g&&
                g->lifecycle==p6::Lifecycle::reward_available)captured[i]=true;
            if(captured[i]&&actor->spawn_group!=group) {
                if(payout_round<0)failed=true;
                else departed[i]=true;
            }
            if(id!=owner)helper_released[i]=captured[i]&&departed[i];
        }
        if(payout_round>=0)for(int i=0;i<plan.crew_count;++i)if(!captured[i])failed=true;
    }
};
} // namespace p8::circuitbreaker
