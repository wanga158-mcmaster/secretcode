#pragma once
#include "risk.h"
#include "../common/model.h"
#include <cmath>
#include <limits>

namespace plan3_risk {
enum class Family {ambiguity,ruin};
enum class Control {belief,minimax};
struct Model {
    Family family=Family::ambiguity;
    Control control=Control::belief;
    Fraction32 radius{1,8},budget{1,8};
    unsigned valueUnits=1024,probabilityTotal=4096;
};
enum class MapReason {none,configuration,snapshot,probabilities,cutoff,no_supported_jobs};
struct Mapping {
    State state=State::unavailable;
    MapReason reason=MapReason::none;
    Input input;
    // Full Robust contrast: minimum individual outcome before lifecycle means.
    // Aligned with input.jobs, not the source vector or canonical rejected rows.
    std::array<std::uint64_t,maximum_jobs> fullMinimaxValues{};
    unsigned canonicalCount=0,admittedCount=0,omittedUnknown=0;
    bool candidatePrefix=false;
};
inline JobId key(const plan3::JobId& id) {
    return {static_cast<std::int32_t>(id.kind),id.firstGroup,id.secondGroup,id.altarTile,id.equipmentBlock};
}
inline plan3::JobId job_id(const JobId& id) {
    return {static_cast<plan3::JobKind>(id[0]),id[1],id[2],id[3],id[4]};
}
inline bool probability_counts(const std::array<double,4>& probabilities,unsigned total,
                               std::array<std::uint32_t,4>& counts) {
    if(!total || total>maximum_weight)return false;
    double sum=0;
    for(const auto p:probabilities) {if(!std::isfinite(p) || p<0 || p>1)return false;sum+=p;}
    if(!std::isfinite(sum) || std::abs(sum-1)>1e-9)return false;
    std::array<double,4> remainder{};std::array<unsigned,4> order{0,1,2,3};unsigned assigned=0;
    for(unsigned w=0;w<4;++w) {
        const double exact=probabilities[w]/sum*total;
        counts[w]=static_cast<unsigned>(std::floor(exact));assigned+=counts[w];
        remainder[w]=exact-counts[w];
    }
    if(assigned>total || total-assigned>4)return false;
    std::sort(order.begin(),order.end(),[&](unsigned a,unsigned b) {
        return remainder[a]==remainder[b]?a<b:remainder[a]>remainder[b];
    });
    for(unsigned n=0;n<total-assigned;++n)++counts[order[n]];
    return true;
}
namespace detail {
inline bool outcome_supported(const plan3::InformationState& info,const plan3::Outcome& outcome,
                              Family family,double& value,bool& loss) {
    if(outcome.tau<0 || outcome.deltaS<0 || !std::isfinite(outcome.healthLoss) || outcome.healthLoss<0 ||
       outcome.terminal.bag<0 || !std::isfinite(outcome.terminal.health) ||
       !std::isfinite(outcome.terminal.maximumHealth) || outcome.terminal.health<0 || outcome.terminal.maximumHealth<0 ||
       (outcome.minimumHealth && (!std::isfinite(*outcome.minimumHealth) ||
        *outcome.minimumHealth<0 || *outcome.minimumHealth>info.maximumHealth)) ||
       outcome.conversions.size()>8 || std::int64_t(info.ownPermanentScore)+outcome.deltaS>INT32_MAX)return false;
    const int time_bound=std::numeric_limits<int>::max()-128;
    for(const auto time:{outcome.terminal.cashoutTurns,outcome.terminal.preparationTurns})
        if(time && (*time<0 || *time>time_bound))return false;
    std::int64_t credit=0,in_time=0;int previous=-1;
    for(const auto& conversion:outcome.conversions) {
        if(conversion.completedAt<previous || conversion.completedAt<0 || conversion.completedAt>outcome.tau || conversion.gems<0)return false;
        previous=conversion.completedAt;credit+=conversion.gems;
        if(credit>INT32_MAX)return false;
        if(conversion.completedAt<=info.horizon())in_time+=conversion.gems;
    }
    if(credit!=outcome.deltaS)return false;
    // A complete phase schedule can heal between damage episodes. Its stated
    // minimum establishes survival; a healed/respawn terminal never does.
    loss=(outcome.minimumHealth ? *outcome.minimumHealth<=0 : outcome.healthLoss>=info.health) ||
         std::int64_t(info.carriedGems)>credit+outcome.terminal.bag;
    value=family==Family::ambiguity?plan3::outcome_value(info,outcome):double(in_time);
    return std::isfinite(value) && value>=0;
}
} // namespace detail
// Scores concern this complete, admitted, represented subset only. Unknown jobs
// are omitted and counted; they never become zero-valued worlds. No forecast,
// observation, probability estimate or selection is cached across callbacks.
template<class Ready>
Mapping map_scenarios(const plan3::InformationState& info,const std::vector<plan3::JobForecast>& jobs,
                      const plan3::ControllerContext& context,const Model& model,Ready&& ready) {
    Mapping mapped;
    const auto stop=[&](MapReason reason) {
        mapped.reason=reason;mapped.state=State::unavailable;mapped.input.jobCount=0;
    };
    if(!ready()) {stop(MapReason::cutoff);return mapped;}
    if(!model.valueUnits || model.valueUnits>1000000 || !detail::probability(model.radius) ||
       !detail::probability(model.budget) || (model.family!=Family::ambiguity && model.family!=Family::ruin) ||
       (model.control!=Control::belief && model.control!=Control::minimax)) {
        stop(MapReason::configuration);return mapped;
    }
    if(info.round<0 || info.roundLimit<=info.round || info.carriedGems<0 || info.ownPermanentScore<0 ||
       !std::isfinite(info.health) || !std::isfinite(info.maximumHealth) || info.health<=0 ||
       info.maximumHealth<info.health || info.maximumHealth>1e12 || info.winningGems<=0) {
        stop(MapReason::snapshot);return mapped;
    }
    auto& input=mapped.input;input.worldCount=4;input.radius=model.radius;input.budget=model.budget;
    if(!probability_counts(context.belief.weights,model.probabilityTotal,input.probabilityWeights)) {
        stop(MapReason::probabilities);return mapped;
    }
    const auto menu=plan3::canonical_menu(jobs);mapped.canonicalCount=unsigned(menu.jobs.size());
    mapped.candidatePrefix=menu.truncated;
    for(const auto* job:menu.jobs) {
        if(!ready()) {stop(MapReason::cutoff);return mapped;}
        if(!plan3::admitted(info,*job,false))continue;
        ++mapped.admittedCount;
        std::array<double,4> weighted{},weight{};
        std::array<unsigned,4> lifecycles{};std::array<bool,4> loss{};
        bool supported=true;double full_minimum=std::numeric_limits<double>::infinity();
        for(const auto& outcome:job->outcomes) {
            if(!ready()) {stop(MapReason::cutoff);return mapped;}
            const unsigned world=static_cast<unsigned>(outcome.response);
            if(world>=4 || outcome.lifecycle<0 || outcome.lifecycle>2 ||
               !std::isfinite(outcome.conditionalLifecycleWeight) || outcome.conditionalLifecycleWeight<0 ||
               outcome.conditionalLifecycleWeight>1) {supported=false;break;}
            const unsigned lifecycle=1u<<unsigned(outcome.lifecycle);
            if(lifecycles[world]&lifecycle) {supported=false;break;}
            lifecycles[world]|=lifecycle;
            double value=0;bool lost=false;
            if(!detail::outcome_supported(info,outcome,model.family,value,lost)) {supported=false;break;}
            full_minimum=std::min(full_minimum,value);
            weighted[world]+=outcome.conditionalLifecycleWeight*value;
            weight[world]+=outcome.conditionalLifecycleWeight;loss[world]=loss[world] || lost;
        }
        Job converted;converted.key=key(job->id);converted.complete=true;converted.outcomeCount=4;
        for(unsigned w=0;w<4 && supported;++w) {
            if(!lifecycles[w] || lifecycles[w]!=lifecycles[0] || !(weight[w]>0)) {supported=false;break;}
            const double units=weighted[w]/weight[w]*model.valueUnits;
            if(!std::isfinite(units) || units<0 || units>double(maximum_value)) {supported=false;break;}
            // The exact kernel governs this explicitly floored integer table,
            // not an assertion of exactness for the original floating model.
            converted.outcomes[w]={static_cast<std::uint64_t>(std::floor(units)),true,true,loss[w]};
        }
        if(!supported) {++mapped.omittedUnknown;mapped.candidatePrefix=true;continue;}
        const double floor_units=full_minimum*model.valueUnits;
        if(!std::isfinite(floor_units) || floor_units<0 || floor_units>double(maximum_value)) {
            ++mapped.omittedUnknown;mapped.candidatePrefix=true;continue;
        }
        mapped.fullMinimaxValues[input.jobCount]=static_cast<std::uint64_t>(std::floor(floor_units));
        input.jobs[input.jobCount++]=converted;
    }
    if(!input.jobCount) {stop(MapReason::no_supported_jobs);return mapped;}
    mapped.state=State::known;return mapped;
}
inline Choice full_minimax(const Mapping& mapped) {
    Choice result;
    if(mapped.state!=State::known)return result;
    for(unsigned j=0;j<mapped.input.jobCount;++j) {
        const auto value=mapped.fullMinimaxValues[j];const auto& id=mapped.input.jobs[j].key;
        if(!result.present || value>result.score.numerator ||
           (value==result.score.numerator && id<result.key))result={true,j,id,{value,1}};
    }
    return result;
}
inline Mapping map_scenarios(const plan3::InformationState& info,const std::vector<plan3::JobForecast>& jobs,
                             const plan3::ControllerContext& context,const Model& model) {
    return map_scenarios(info,jobs,context,model,[] {return true;});
}
} // namespace plan3_risk
