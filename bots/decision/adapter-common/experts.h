#pragma once
#include "bandit.h"
#include "../common/model.h"

namespace plan3_adapter {
inline constexpr std::array<plan3::Controller,expert_count> expert_controllers{
    plan3::Controller::ledger,plan3::Controller::atlas,plan3::Controller::tempo,plan3::Controller::insurance};
inline constexpr std::array<const char*,expert_count> expert_names{"ledger","atlas","tempo","insurance"};
struct ExpertMenu {
    Status status=Status::unavailable;
    std::array<plan3::JobIntent,expert_count> intents{};
    unsigned available=0;
};
inline JobKey key(const plan3::JobId& id) {
    return {static_cast<std::int32_t>(id.kind),id.firstGroup,id.secondGroup,id.altarTile,id.equipmentBlock};
}
template<class Ready>
ExpertMenu experts(const plan3::InformationState& info,const std::vector<plan3::JobForecast>& jobs,
                   const plan3::ControllerContext& context,Ready&& ready) {
    if(jobs.size()>28)return {Status::invalid};
    ExpertMenu result;
    for(unsigned i=0;i<expert_count;++i) {
        if(!ready())return {Status::cutoff};
        // Same complete forecast table; predictions select an expert's job but
        // NEVER become bandit rewards for selected or unselected experts.
        result.intents[i]=plan3::select(expert_controllers[i],false,info,jobs,context);
        if(!ready())return {Status::cutoff};
        const auto& intent=result.intents[i];
        if(intent.status==plan3::IntentStatus::selected && intent.job && std::isfinite(intent.objective))
            result.available|=1u<<i;
    }
    result.status=Status::known;return result;
}
inline ExpertMenu experts(const plan3::InformationState& info,const std::vector<plan3::JobForecast>& jobs,
                          const plan3::ControllerContext& context={}) {
    return experts(info,jobs,context,[] {return true;});
}
// A replayable ticket source, not secret randomness or an adversary guarantee.
inline std::uint32_t ticket(unsigned actor,unsigned round,unsigned episodes) {
    std::uint32_t value=0x9e3779b9u^(actor*0x85ebca6bu)^(round*0xc2b2ae35u)^episodes;
    value^=value>>16;value*=0x7feb352du;value^=value>>15;value*=0x846ca68bu;
    return value^(value>>16);
}
} // namespace plan3_adapter
