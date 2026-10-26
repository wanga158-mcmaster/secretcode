#pragma once
#include "../common/runtime.h"
#include "viability.h"

namespace plan3_lifeline {
struct PolicyState {
    plan3_runtime::Campaign* campaign=nullptr;
    Assumptions assumptions{true,true};
    std::array<bool,2> reported{};
};
inline plan3::JobIntent select(void* opaque,bool control,const plan3::InformationState& info,
                              const std::vector<plan3::JobForecast>& jobs,
                              const plan3::ControllerContext&) {
    auto& state=*static_cast<PolicyState*>(opaque);
    auto result=evaluate(info,jobs,state.assumptions,[] {return cambc::fuel_remaining()>=45000;});
    // The common callback wrapper owns priorities and executable-job validation.
    // These are conditional model assumptions, not verified native safety facts.
    if(!state.reported[control] && cambc::fuel_remaining()>=65000) {
        plan3_runtime::TelemetryFields fields;
        fields<<",\"family\":\"lifeline\",\"arm\":\""<<(control?"control":"active")
            <<"\",\"quiet_assumed\":"<<state.assumptions.quiet
            <<",\"safe_recall_assumed\":"<<state.assumptions.safeRecall
            <<",\"status\":"<<static_cast<unsigned>(result.status)
            <<",\"actions\":"<<result.actions.size()<<",\"states\":"<<result.states.size()
            <<",\"omitted\":"<<result.omitted;
        state.campaign->report_policy(fields.view());state.reported[control]=true;
    }
    if(result.status!=Status::known)return {};
    const auto& choice=control?result.control:result.active;
    if(!choice.present)return {};
    return {plan3::IntentStatus::selected,choice.job,
        double(choice.score.converted)/double(choice.score.occupied),plan3::DecisionReason::objective};
}
inline int run(bool control,const char* label) {
    plan3_runtime::Campaign campaign(plan3::Controller::ledger,control,label);
    PolicyState state;state.campaign=&campaign;
    campaign.supply_selector(select,&state);
    while(true) {
        try {
            if(cambc::is_draft())plan3_runtime::fixed_draft();
            else campaign.turn();
        } catch(const std::exception& e) {
            std::fprintf(stdout,"p3 error: %.240s\n",e.what());std::fflush(stdout);
        } catch(...) {
            std::fputs("p3 error: unknown exception\n",stdout);std::fflush(stdout);
        }
        cambc::end_turn();
    }
}
} // namespace plan3_lifeline
