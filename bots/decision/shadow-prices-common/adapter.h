#pragma once
#include "packing.h"
#include "../common/policy.h"
#include <cmath>

namespace plan3_shadow_prices {
inline constexpr unsigned reward_units=1;
inline JobKey key(const plan3::JobId& id){return {int(id.kind),id.firstGroup,id.secondGroup,id.altarTile,id.equipmentBlock};}
inline plan3::JobId job_id(const JobKey& id){return {plan3::JobKind(id[0]),id[1],id[2],id[3],id[4]};}
enum class MappingReason {none,snapshot,incomplete,admission,forecast,continuation,numeric,zero_reward,individual_budget,cutoff};
struct Candidate {JobKey key{0,-1,-1,-1,-1};MappingReason reason=MappingReason::none;};
struct Mapping {
    State state=State::unavailable;
    MappingReason reason=MappingReason::snapshot;
    Input input;
    std::array<Candidate,maximum_jobs> candidates{};
    unsigned canonicalCount=0,unknownCount=0,rejectedCount=0,overBudget=0;
    bool completeMenu=false,candidatePrefix=false;
};
// Four represented resources, not four disjoint engine currencies. R is the
// forecast recovery/return owner-turn opportunity loss; time=tau+R. With both
// budgets H, the R constraint is redundant. We do not invent a smaller budget.
// Prices concern this fractional repeated-row surrogate, not executable jobs.
template<class Ready> Mapping map_menu(const plan3::InformationState& state,
        const std::vector<plan3::JobForecast>& jobs,bool completeMenu,Ready&& ready) {
    Mapping out;out.completeMenu=completeMenu;auto& input=out.input;
    // This joins the supplied fresh decision callback. It is not a separate
    // certificate that every lifecycle in its analytical forecasts was seen.
    input.round=state.round;input.observationRound=state.round;input.roundLimit=state.roundLimit;input.carriedBag=state.carriedGems;
    if(state.round<0 || state.roundLimit<state.round || state.carriedGems!=0 || state.gold<0 || state.gold>numeric_limit ||
       state.ownPermanentScore<0 || state.ownPermanentScore>numeric_limit || state.winningGems<1 || state.winningGems>numeric_limit ||
       !std::isfinite(state.health) || !std::isfinite(state.maximumHealth) || state.maximumHealth<=0 ||
       state.health<0 || state.health>state.maximumHealth)return out;
    if(!completeMenu){out.state=State::unknown;out.reason=MappingReason::incomplete;return out;}
    const double healthBudget=std::floor(std::max(0.0,state.health-.25*state.maximumHealth));
    if(healthBudget>numeric_limit)return out;
    input.budget={std::int64_t(healthBudget),state.horizon(),state.gold,state.horizon()};
    const auto menu=plan3::canonical_menu(jobs);out.canonicalCount=unsigned(menu.jobs.size());out.candidatePrefix=menu.truncated;
    if(menu.jobs.size()>maximum_jobs)return out;
    for(const auto* source:menu.jobs) {
        if(!ready()){out.state=State::cutoff;out.reason=MappingReason::cutoff;input.cutoff=true;return out;}
        const unsigned at=input.rowCount++;auto& row=input.jobs[at];auto& candidate=out.candidates[at];
        row.key=candidate.key=key(source->id);
        const auto reject=[&](MappingReason why,bool unknown=false) {
            candidate.reason=why;row.state=unknown?State::unknown:State::unavailable;
            if(unknown){++out.unknownCount;out.candidatePrefix=true;}else ++out.rejectedCount;
        };
        if(!source->complete || !source->routeKnown){reject(MappingReason::incomplete,true);continue;}
        if(!plan3::admitted(state,*source,false)){reject(MappingReason::admission);continue;}
        const auto* nominal=source->nominal();
        if(!nominal || nominal->provenance==plan3::Provenance::unknown || nominal->tau<0 || nominal->deltaS<0 || nominal->deltaS>numeric_limit || nominal->conversions.size()>256 ||
           source->projectedRenewalAndReturnTime<0 ||
           !std::isfinite(nominal->healthLoss) || nominal->healthLoss<0 ||
           !std::isfinite(nominal->goldSpent) || nominal->goldSpent<0) {reject(MappingReason::forecast,true);continue;}
        if(nominal->terminal.loopSupportUnavailable){reject(MappingReason::continuation,true);continue;}
        bool continuationKnown=true;
        for(const auto& loop:nominal->terminal.loops)if(loop.supported && loop.provenance==plan3::Provenance::unknown)continuationKnown=false;
        if(!continuationKnown){reject(MappingReason::continuation,true);continue;}
        std::int64_t converted=0;int previous=-1;bool conversionKnown=true;
        for(const auto& conversion:nominal->conversions) {
            if(conversion.completedAt<previous || conversion.completedAt<0 || conversion.completedAt>nominal->tau || conversion.gems<0)conversionKnown=false;
            previous=conversion.completedAt;converted+=conversion.gems;
            if(converted>numeric_limit)conversionKnown=false;
        }
        if(!conversionKnown || converted!=nominal->deltaS){reject(MappingReason::forecast,true);continue;}
        if(nominal->terminal.bag<0 || !std::isfinite(nominal->terminal.health) ||
           !std::isfinite(nominal->terminal.maximumHealth) || nominal->terminal.health<0 ||
           nominal->terminal.maximumHealth<=0 || nominal->terminal.health>nominal->terminal.maximumHealth) {
            reject(MappingReason::forecast,true);continue;
        }
        if(source->id.kind==plan3::JobKind::invest && !source->investment.positive_payback()) {
            reject(MappingReason::admission);continue;
        }
        const double represented=plan3::outcome_value(state,*nominal)*reward_units;
        const double loss=std::ceil(nominal->healthLoss),spend=std::ceil(nominal->goldSpent);
        const std::int64_t recovery=source->projectedRenewalAndReturnTime;
        const std::int64_t duration=std::int64_t(nominal->tau)+recovery;
        if(!std::isfinite(represented) || represented<0 || represented>numeric_limit || loss>numeric_limit || spend>numeric_limit ||
           recovery>numeric_limit || duration>numeric_limit){reject(MappingReason::numeric);continue;}
        row.reward=std::int64_t(std::floor(represented));row.cost={std::int64_t(loss),duration,std::int64_t(spend),recovery};
        if(!row.reward){reject(MappingReason::zero_reward);continue;}
        row.costsKnown=true;
        bool over=false;for(unsigned r=0;r<resources;++r)over|=row.cost[r]>input.budget[r];
        if(over){++out.overBudget;reject(MappingReason::individual_budget);continue;}
        if(std::none_of(row.cost.begin(),row.cost.end(),[](auto c){return c>0;})){reject(MappingReason::numeric);continue;}
        row.state=State::known;candidate.reason=MappingReason::none;
    }
    out.state=State::known;out.reason=MappingReason::none;return out;
}
inline Mapping map_menu(const plan3::InformationState& state,const std::vector<plan3::JobForecast>& jobs,bool completeMenu=true) {
    return map_menu(state,jobs,completeMenu,[]{return true;});
}
inline bool same_input(const Input& a,const Input& b) {
    if(a.round!=b.round || a.observationRound!=b.observationRound || a.roundLimit!=b.roundLimit ||
       a.carriedBag!=b.carriedBag || a.budget!=b.budget || a.rowCount!=b.rowCount || a.cutoff!=b.cutoff || a.fixedPrices!=b.fixedPrices)return false;
    for(unsigned n=0;n<a.rowCount;++n)if(!(a.jobs[n]==b.jobs[n]))return false;
    return true;
}
class Policy {
    std::optional<Basis<resources>> reference_;
    std::optional<Input> cachedInput_;
    Result result_;
public:
    unsigned solves=0;
    const std::optional<Basis<resources>>& reference() const{return reference_;}
    template<class Ready> const Result& solve(const Mapping& mapped,Ready&& ready) {
        const auto reject=[&](State state,Reason reason)->const Result& {
            cachedInput_.reset();result_={};result_.state=state;result_.reason=reason;return result_;
        };
        if(!ready())return reject(State::cutoff,Reason::cutoff);
        if(mapped.state!=State::known || !mapped.completeMenu)
            return reject(mapped.state,mapped.state==State::cutoff?Reason::cutoff:Reason::unknown);
        // A complete first price vector must not be sealed from missing costs
        // or unfinished rows. Later choices explicitly retain unknown counts.
        if(!reference_ && mapped.unknownCount)return reject(State::unknown,Reason::cost_unknown);
        auto input=mapped.input;input.fixedPrices=reference_;
        if(cachedInput_ && same_input(input,*cachedInput_))return result_;
        ++solves;auto result=evaluate(input,ready);
        if(!ready())return reject(State::cutoff,Reason::cutoff);
        if(result.certified && !reference_){reference_=result.dual;input.fixedPrices=reference_;}
        result_=std::move(result);
        if(result_.certified)cachedInput_=std::move(input);else cachedInput_.reset();
        return result_;
    }
};
inline plan3::JobIntent policy_intent(const Mapping& mapped,const Result& result,bool control) {
    if(!result.certified)return {plan3::IntentStatus::yield,std::nullopt,0,plan3::DecisionReason::no_admitted_job,false,mapped.candidatePrefix};
    const auto& chosen=control?result.control:result.active;if(!chosen.present)return {};
    const double score=double(chosen.score.numerator)/double(chosen.score.denominator)/reward_units;
    return {plan3::IntentStatus::selected,job_id(chosen.key),score,plan3::DecisionReason::objective,false,mapped.candidatePrefix};
}
inline plan3::JobIntent bank_prefix(const plan3::InformationState& state,const std::vector<plan3::JobForecast>& jobs) {
    const plan3::JobForecast* bank=nullptr;const plan3::JobForecast* renewal=nullptr;
    const auto menu=plan3::canonical_menu(jobs);
    for(const auto* job:menu.jobs)if(plan3::admitted(state,*job,false)) {
        if(job->id.kind==plan3::JobKind::bank && (!bank || job->nominal()->tau<bank->nominal()->tau ||
           (job->nominal()->tau==bank->nominal()->tau && job->id<bank->id)))bank=job;
        if(job->id.kind==plan3::JobKind::renew && (!renewal || job->id<renewal->id))renewal=job;
    }
    const auto* chosen=bank?bank:renewal;
    return chosen?plan3::JobIntent{plan3::IntentStatus::selected,chosen->id,0,bank?plan3::DecisionReason::objective:plan3::DecisionReason::mandatory_recovery,false,menu.truncated}:
        plan3::JobIntent{plan3::IntentStatus::yield,std::nullopt,0,plan3::DecisionReason::no_admitted_job,false,menu.truncated};
}
} // namespace plan3_shadow_prices
