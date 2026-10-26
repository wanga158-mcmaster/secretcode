#pragma once
#include "executor.h"
#include "information.h"
#include "model.h"

namespace plan3_runtime::opening_bootstrap {
// This is the existing fixed B0 opening, not a learned spending objective.
inline bool unfinished(const Executor& executor) {
    return executor.monster_shards()<5 || executor.jungle_shards()<5;
}

// Whole-job eligibility only; this neither selects gold nor proves that gem
// opportunities are absent.
inline bool single_gold_eligible(const plan3::InformationState& state,
                                 const plan3::JobForecast& job,bool yBased=false) {
    if(state.carriedGems || job.id.kind!=plan3::JobKind::cycle || job.id.secondGroup>=0 ||
       !job.rewardOnlyEndpoint || job.campGold<=0 || !plan3::admitted(state,job,yBased))return false;
    const auto* nominal=job.nominal();
    return nominal && !nominal->deltaS && nominal->tau>=0 && job.projectedRenewalAndReturnTime>=0;
}

// Call only inside a selector's common gold-fallback prefix. Keep the same
// canonical candidate prefix and complete Single Cycle on both paired arms.
template<class Jobs>
const plan3::JobForecast* single_gold(const plan3::InformationState& state,const Jobs& jobs,bool yBased=false) {
    if(state.carriedGems)return nullptr;
    const plan3::JobForecast* best=nullptr;
    double rate=-1;
    for(const auto* job:jobs) {
        if(!single_gold_eligible(state,*job,yBased))continue;
        const auto* nominal=job->nominal();
        const double value=double(job->campGold)/
            std::max<std::int64_t>(1,std::int64_t(nominal->tau)+job->projectedRenewalAndReturnTime);
        if(!best || plan3::strictly_better(value,rate) ||
           (!plan3::strictly_better(rate,value) && job->id<best->id)) {
            best=job;rate=value;
        }
    }
    return best;
}

inline bool prefer_single(const plan3::InformationState& state,
                          const std::vector<plan3::JobForecast>& jobs,
                          plan3::JobIntent& intent,plan3::JobIntent& companion,bool yBased=false) {
    if(intent.status!=plan3::IntentStatus::selected || companion.status!=plan3::IntentStatus::selected ||
       intent.reason!=plan3::DecisionReason::gold_fallback ||
       companion.reason!=plan3::DecisionReason::gold_fallback || intent.job!=companion.job)return false;
    const auto menu=plan3::canonical_menu(jobs);
    const auto* job=single_gold(state,menu.jobs,yBased);
    if(!job)return false;
    const double rate=double(job->campGold)/std::max<std::int64_t>(1,
        std::int64_t(job->nominal()->tau)+job->projectedRenewalAndReturnTime);
    intent={plan3::IntentStatus::selected,job->id,rate,plan3::DecisionReason::gold_fallback,false,menu.truncated};
    companion=intent;
    return true;
}

// A completed gold-only capture may fund an opening purchase without leaving a
// healthy non-Tavi actor at the shop. Pending state is set only on commitment;
// never replace a capture, carried bag, channel or another active job.
inline bool return_needed(bool& pending,const Executor& executor,
                          const PersonalInformation& personal,bool active) {
    if(!pending)return false;
    auto& me=cambc::self();
    if(!unfinished(executor) || personal.own_spawn.is_set(me.position())) {
        pending=false;return false;
    }
    if(active || me.gem_count() || cambc::fuel_remaining()<45000 ||
       cambc::round_number()+1>=cambc::constants::max_rounds)return false;
    const auto item=executor.monster_shards()<5?cambc::item_type::monster_shard_of_strength:
        cambc::item_type::jungle_shard_of_health;
    const unsigned cost=item.cost();
    return cost && me.gold_count()>=cost && !me.can_buy(item,1);
}
} // namespace plan3_runtime::opening_bootstrap
