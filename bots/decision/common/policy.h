#pragma once
#include "model.h"

namespace plan3 {
// A policy ranks the current legal job forecasts. It does not issue SDK actions.
using JobSelector = JobIntent (*)(void*, bool, const InformationState&,
    const std::vector<JobForecast>&, const ControllerContext&);
struct PolicyEvent {
    enum class Kind { committed, completed, aborted };
    Kind kind=Kind::committed;
    std::uint64_t token=0;
    JobId job;
    int startedRound=0,round=0;
    std::optional<unsigned> exactConverted;
    bool conversion=false;
};
using PolicyObserver = void (*)(void*,const PolicyEvent&);

inline JobIntent select_policy(JobSelector selector, void* state, bool control,
    const InformationState& info, const std::vector<JobForecast>& input,
    const ControllerContext& context,Controller priorityController=Controller::ledger) {
    const auto baseline=select(priorityController,false,info,input,context);
    // Every controller shares obligations, complete-menu admission and the
    // immediate winning conversion. New objectives cannot override these.
    if (!selector || baseline.reason==DecisionReason::protected_obligation ||
        baseline.reason==DecisionReason::incomplete_menu ||
        baseline.reason==DecisionReason::immediate_winning_conversion ||
        baseline.reason==DecisionReason::mandatory_recovery ||
        baseline.reason==DecisionReason::gold_fallback) return baseline;
    auto result=selector(state,control,info,input,context);
    result.candidateTruncated=result.candidateTruncated || baseline.candidateTruncated;
    if (result.status==IntentStatus::yield) {
        result.job.reset(); result.objective=0;result.policyToken=0;return result;
    }
    if (result.status==IntentStatus::preserve && context.activeJob &&
        context.activeJobValid && !context.sharedAbort && result.job==context.activeJob)
        return result;
    if (result.status==IntentStatus::selected && result.job && std::isfinite(result.objective)) {
        const auto menu=canonical_menu(input);
        for (const auto* job:menu.jobs)
            if (job->id==*result.job && admitted(info,*job,false)) return result;
    }
    return {IntentStatus::yield,std::nullopt,0,DecisionReason::no_admitted_job,
        false,result.candidateTruncated};
}
} // namespace plan3
