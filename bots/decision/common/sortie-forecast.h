#pragma once
#include "sortie-runtime.h"

namespace plan3_runtime::sortie {
// One complete conditional quiet-model Cycle. Public full member bounds are
// assumptions about an unseen target; they do not assert current occupancy or
// probabilities of interference. Publish one nominal outcome; response-aware
// adapters must supply their own counterfactuals before using this producer.
template<class Ready>
std::optional<plan3::JobForecast> forecast(const Candidate& candidate,
                                         const ForecastContext& context,Ready&& ready) {
    if(!ready())return std::nullopt;
    const auto& p=candidate.plan;const auto& s=candidate.accounting;
    const auto& info=context.info;
    if(!candidate.assumedQuiet || !candidate.publicMemberBound ||
       p.preparedRound!=unsigned(info.round) || s.deltaS<=0 || s.tau<=0 ||
       s.tau>info.roundLimit-info.round || !s.sortieCount ||
       !std::isfinite(s.minHealth) || s.minHealth<=0 || s.minHealth>info.maximumHealth ||
       !std::isfinite(s.terminalHealth) || s.terminalHealth<0 || s.terminalHealth>info.maximumHealth)
        return std::nullopt;
    const auto found=std::find_if(context.personal.groups.begin(),context.personal.groups.end(),
        [&](const auto& group){return group.key==p.groupKey;});
    if(found==context.personal.groups.end())return std::nullopt;
    plan3::JobForecast job;
    job.id={plan3::JobKind::cycle,p.groupKey,-1,p.altarTile.y*context.personal.width+p.altarTile.x};
    job.campGems=found->gem_value;job.campGold=found->gold_value;
    job.fullClearAdmitted=true;job.routeKnown=true;
    job.travelPlusClear=s.tau;
    const auto decision=p.decisionOrigin;
    const int returnDistance=context.paths.get(context.personal.spawn_field,decision);
    if(returnDistance<0)return std::nullopt;
    job.projectedRenewalAndReturnTime=context.recall+context.healing_turns(s.terminalHealth)+returnDistance;
    plan3::Outcome nominal;
    nominal.deltaS=s.deltaS;nominal.tau=s.tau;nominal.healthLoss=s.healthLoss;
    nominal.minimumHealth=s.minHealth;
    nominal.goldEarned=std::floor(job.campGold*context.capture_gold)+cambc::constants::passive_gold*s.tau;
    nominal.conversions.push_back({s.conversion.completedAt,s.conversion.gems});
    nominal.terminal.position={p.altarApproach.y*context.personal.width+p.altarApproach.x};
    nominal.terminal.health=s.terminalHealth;
    nominal.terminal.maximumHealth=info.maximumHealth;
    nominal.terminal.equipment=info.equipment;
    const int returnToAltar=context.paths.get(context.personal.spawn_field,p.altarApproach);
    if(returnToAltar<0 || !ready())return std::nullopt;
    nominal.terminal.preparationTurns=context.recall+context.healing_turns(s.terminalHealth)+returnToAltar;
    // The completed job is scored; unbuilt renewal opportunities remain
    // unavailable rather than vetoing this independently complete endpoint.
    nominal.terminal.loopSupportUnavailable=true;
    job.outcomes.push_back(std::move(nominal));
    if(!ready())return std::nullopt;
    return job;
}
} // namespace plan3_runtime::sortie
