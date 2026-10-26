#pragma once
#include "model.h"

namespace plan3 {
enum class RiskGuard { world, active_arm, control_arm };
struct RiskPrefixSelection {
    JobIntent intent,companion;
    std::vector<const JobForecast*> prefix;
    std::size_t eligible=0,scored=0;
    unsigned worlds=0,checkpoints=0,aggregateCalls=0;
    bool selected=false,cutoff=false,priorityFast=false;
};
// Readiness is supplied by the runtime; this header never queries the SDK.
// Job/state storage stays immutable for this helper and both selector arms.
template<typename Ready>
RiskPrefixSelection risk_prefix_select(Controller controller,bool control,
    const InformationState& info,const std::vector<JobForecast>& jobs,
    const ControllerContext& context,EvaluationCache& cache,Ready&& ready) {
    // One fresh immutable decision; reserve-free scalar slots cover<=28 rows.
    cache.clear();
    RiskPrefixSelection result;
    result.intent.reason=result.companion.reason=DecisionReason::incomplete_menu;
    if(context.activeJob && context.activeJobValid && !context.sharedAbort) {
        result.intent.status=result.companion.status=IntentStatus::preserve;
        result.intent.job=result.companion.job=context.activeJob;
    }
    const auto canonical=canonical_menu(jobs);
    std::vector<const JobForecast*> complete,eligible;
    bool mandatory=false,winning=false,immediateGem=false,gold=false;
    for(const auto* job:canonical.jobs) {
        if(admitted(info,*job,true)) {
            eligible.push_back(job);mandatory|=job->mandatorySafety;
            immediateGem|=job->nominal()->deltaS>0;
            gold|=info.carriedGems==0 && productive(job->id.kind) && job->rewardOnlyEndpoint &&
                job->campGold>0 && job->nominal()->deltaS==0;
        }
        winning|=context.newlyFeasibleWinningBank &&
            info.carriedGems>=std::max(0,info.winningGems-info.ownPermanentScore) &&
            job->id.kind==JobKind::bank && admitted(info,*job,false) &&
            job->nominal()->deltaS>=info.winningGems-info.ownPermanentScore;
    }
    result.eligible=eligible.size();complete.reserve(eligible.size());result.prefix.reserve(eligible.size());
    result.priorityFast=!context.menuComplete ||
        (context.activeJob && context.activeJobValid && !context.sharedAbort && context.protectedChannelOrCapture) ||
        winning || mandatory || (!immediateGem && gold);
    const auto checkpoint=[&](const std::vector<const JobForecast*>* prefix) {
        // Fixed work order keeps cutoff prefixes identical for both modes.
        if(!ready(RiskGuard::active_arm)) {result.cutoff=true;return false;}
        const auto active=select(controller,false,info,jobs,context,&cache,prefix);++result.aggregateCalls;
        if(!ready(RiskGuard::control_arm)) {result.cutoff=true;return false;}
        const auto passive=select(controller,true,info,jobs,context,&cache,prefix);++result.aggregateCalls;
        result.intent=control?passive:active;result.companion=control?active:passive;
        result.selected=true;++result.checkpoints;
        if(prefix)result.prefix=*prefix;
        return true;
    };
    if(result.priorityFast) {checkpoint(nullptr);return result;}
    if(eligible.empty()) {checkpoint(&complete);return result;}
    for(const auto* job:eligible) {
        // A row is held only until this candidate's worlds finish. Checkpoint
        // selection/other candidates may insert rows and invalidate the pointer.
        EvaluationCache::Row* row=nullptr;
        for(std::size_t world=0;world<job->outcomes.size();++world) {
            if(!ready(RiskGuard::world)) {result.cutoff=true;return result;}
            if(world==0)row=outcome_cache_row(info,*job,&cache);
            cached_outcome_value_row(info,*job,world,&cache,row);++result.worlds;
        }
        ++result.scored;complete.push_back(job);
        if(!checkpoint(&complete))return result;
    }
    return result;
}
} // namespace plan3
