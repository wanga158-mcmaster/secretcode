#pragma once
#include "game.h"
#include "../common/model.h"
#include <cmath>

namespace plan3_commitment {
struct Model {
    unsigned units=1024;
    // Assumed denied-output proxy minus response effort, in gem units.
    std::array<unsigned,4> effortGems{0,2,4,2};
    unsigned switchGems=1;
};
struct Mapping {
    State state=State::unavailable;
    Input input;
    unsigned canonical=0,admitted=0,omittedUnknown=0;
    bool truncated=false;
};
inline Key key(const plan3::JobId& id) {
    return {static_cast<std::int32_t>(id.kind),id.firstGroup,id.secondGroup,id.altarTile,id.equipmentBlock};
}
inline plan3::JobId job_id(const Key& key) {
    return {static_cast<plan3::JobKind>(key[0]),key[1],key[2],key[3],key[4]};
}
template<class Ready>
Mapping map(const plan3::InformationState& info,const std::vector<plan3::JobForecast>& jobs,
            const Model& model,std::optional<Key> previous,Ready&& ready) {
    Mapping result;
    const auto stop=[&](State state) {Mapping empty;empty.state=state;return empty;};
    if(!ready())return stop(State::cutoff);
    if(!model.units || model.units>1000000 || model.switchGems>1000000 || jobs.size()>28 ||
       info.round<0 || info.roundLimit<=info.round || info.carriedGems<0 || info.ownPermanentScore<0 ||
       info.winningGems<=0 || !std::isfinite(info.health) || !std::isfinite(info.maximumHealth) ||
       info.health<=0 || info.maximumHealth<info.health)return stop(State::invalid);
    for(auto cost:model.effortGems)if(cost>1000000)return stop(State::invalid);
    result.input.previous=previous;result.input.switchCost=std::int64_t(model.switchGems)*model.units;
    const auto menu=plan3::canonical_menu(jobs);result.canonical=unsigned(menu.jobs.size());result.truncated=menu.truncated;
    for(const auto* job:menu.jobs) {
        if(!ready())return stop(State::cutoff);
        if(!plan3::admitted(info,*job,false))continue;
        ++result.admitted;
        std::array<double,4> total{},mass{};
        std::array<unsigned,4> seen{};
        std::array<std::array<double,3>,4> lifecycleWeights{};
        bool supported=job->id.kind!=plan3::JobKind::invest && job->id.kind!=plan3::JobKind::observe;
        for(const auto& outcome:job->outcomes) {
            if(!ready())return stop(State::cutoff);
            const auto response=static_cast<unsigned>(outcome.response);
            if(response>=4 || outcome.lifecycle<0 || outcome.lifecycle>2 || outcome.tau<0 || outcome.deltaS<0 ||
               !std::isfinite(outcome.conditionalLifecycleWeight) || outcome.conditionalLifecycleWeight<0 ||
               !std::isfinite(outcome.healthLoss) || outcome.healthLoss<0 || outcome.terminal.bag<0 ||
               !std::isfinite(outcome.terminal.health) || outcome.terminal.health<0 ||
               !std::isfinite(outcome.terminal.maximumHealth) || outcome.terminal.maximumHealth<outcome.terminal.health ||
               std::int64_t(info.ownPermanentScore)+outcome.deltaS>INT32_MAX || outcome.conversions.size()>8) {
                supported=false;continue;
            }
            if(seen[response]&(1u<<outcome.lifecycle)) {supported=false;continue;}
            seen[response]|=1u<<outcome.lifecycle;
            lifecycleWeights[response][unsigned(outcome.lifecycle)]=outcome.conditionalLifecycleWeight;
            for(const auto duration:{outcome.terminal.cashoutTurns,outcome.terminal.preparationTurns})
                if(duration && (*duration<0 || *duration>INT32_MAX-128))supported=false;
            std::int64_t credit=0;int last=-1;
            for(const auto& conversion:outcome.conversions) {
                if(!ready())return stop(State::cutoff);
                if(conversion.gems<0 || conversion.completedAt<last || conversion.completedAt<0 || conversion.completedAt>outcome.tau)
                    supported=false;
                last=conversion.completedAt;credit+=conversion.gems;
            }
            if(credit!=outcome.deltaS)supported=false;
            if(!supported)continue;
            // Full potential is the same assumed own-output model used by Atlas.
            const double value=plan3::outcome_value(info,outcome);
            const double weight=outcome.conditionalLifecycleWeight;
            if(!ready())return stop(State::cutoff);
            if(!std::isfinite(value) || value<0 || value>double(payoff_bound)/model.units || weight>1) {
                supported=false;continue;
            }
            total[response]+=weight*value;mass[response]+=weight;
        }
        for(unsigned r=0;r<4;++r)if(!(mass[r]>0) || !std::isfinite(total[r]) || !std::isfinite(mass[r]))supported=false;
        for(unsigned r=1;r<4;++r)for(unsigned l=0;l<3;++l)
            if(seen[r]!=seen[0] || lifecycleWeights[r][l]!=lifecycleWeights[0][l])supported=false;
        if(!supported) {++result.omittedUnknown;continue;}
        Row row;row.key=key(job->id);
        for(unsigned r=0;r<4;++r) {
            const double value=std::floor(total[r]/mass[r]*model.units);
            if(!std::isfinite(value) || value<0 || value>payoff_bound)return stop(State::invalid);
            row.cells[r]={Knowledge::known,static_cast<std::int64_t>(value),0};
        }
        for(unsigned r=0;r<4;++r)
            row.cells[r].follower=std::max<std::int64_t>(0,row.cells[0].leader-row.cells[r].leader)-
                std::int64_t(model.effortGems[r])*model.units;
        result.input.jobs[result.input.jobCount++]=row;
    }
    result.state=State::known;return result;
}
inline Mapping map(const plan3::InformationState& info,const std::vector<plan3::JobForecast>& jobs,
                   const Model& model={},std::optional<Key> previous=std::nullopt) {
    return map(info,jobs,model,previous,[] {return true;});
}
} // namespace plan3_commitment
