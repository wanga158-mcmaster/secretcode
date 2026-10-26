#pragma once
#include "../common/runtime.h"
#include "policy.h"

namespace plan3_commitment {
struct Runtime {
    Planner planner;
    plan3_runtime::Campaign* campaign=nullptr;
    std::array<bool,2> reported{};
    bool governingControl=false,dirty=false;
    explicit Runtime(bool control):planner(control),governingControl(control) {}
};
inline plan3::JobIntent select(void* opaque,bool control,const plan3::InformationState& info,
                              const std::vector<plan3::JobForecast>& jobs,const plan3::ControllerContext& context) {
    auto& runtime=*static_cast<Runtime*>(opaque);
    auto selection=runtime.planner.preview(control,info,jobs,context,[] {return cambc::fuel_remaining()>=45000;});
    if((!runtime.reported[control] || (runtime.dirty && control==runtime.governingControl)) && cambc::fuel_remaining()>=65000) {
        const auto& stats=runtime.planner.statistics();plan3_runtime::TelemetryFields fields;
        fields<<",\"family\":\"commitment\",\"arm\":"<<control<<",\"state\":"<<int(selection.state)
            <<",\"best_response_mask\":"<<selection.bestResponses<<",\"omitted\":"<<selection.omitted
            <<",\"episodes\":"<<stats.committed<<",\"censored\":"<<stats.censored
            <<",\"switches\":"<<stats.switches<<",\"abort_turns\":"<<stats.abortedElapsed
            <<",\"follower_model_assumed\":true,\"effort_gems\":[0,2,4,2],\"switch_gems\":1";
        runtime.campaign->report_policy(fields.view());runtime.reported[control]=true;
        if(control==runtime.governingControl)runtime.dirty=false;
    }
    if(selection.state!=State::known)return {};
    selection.intent.policyToken=selection.token;return selection.intent;
}
inline void observer(void* opaque,const plan3::PolicyEvent& event) {
    auto& runtime=*static_cast<Runtime*>(opaque);
    if(!runtime.planner.observe(event))throw std::logic_error("Commitment job feedback mismatch");
    if(event.token && event.kind!=plan3::PolicyEvent::Kind::committed)runtime.dirty=true;
}
inline int run(bool control,const char* label) {
    plan3_runtime::Campaign campaign(plan3::Controller::ledger,control,label);
    Runtime runtime(control);runtime.campaign=&campaign;
    campaign.supply_selector(select,&runtime,false,observer);
    while(true) {
        try {if(cambc::is_draft())plan3_runtime::fixed_draft();else campaign.turn();}
        catch(const std::exception& error) {std::fprintf(stdout,"p3 error: %.240s\n",error.what());std::fflush(stdout);}
        catch(...) {std::fputs("p3 error: unknown exception\n",stdout);std::fflush(stdout);}
        cambc::end_turn();
    }
}
} // namespace plan3_commitment
