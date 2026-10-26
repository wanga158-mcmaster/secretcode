#pragma once
#include "../common/runtime.h"
#include "feedback.h"

namespace plan3_adapter {
struct Runtime {
    Bridge bridge;
    plan3_runtime::Campaign* campaign=nullptr;
    bool governingControl=false,feedbackDirty=false;
    std::array<bool,2> reported{};
    explicit Runtime(bool control):bridge(control),governingControl(control) {}
};
inline plan3::JobIntent select(void* opaque,bool control,const plan3::InformationState& info,
                              const std::vector<plan3::JobForecast>& jobs,const plan3::ControllerContext& context) {
    auto& runtime=*static_cast<Runtime*>(opaque);
    auto selection=runtime.bridge.preview(control,info,jobs,context,[] {return cambc::fuel_remaining()>=45000;});
    if((!runtime.reported[control] || (control==runtime.governingControl && runtime.feedbackDirty)) &&
       cambc::fuel_remaining()>=65000) {
        unsigned exact=0,censored=0,costs=0;
        for(const auto& stats:runtime.bridge.bandit().statistics()) {
            exact+=stats.rewardSamples;censored+=stats.censored;costs+=stats.costSamples;
        }
        plan3_runtime::TelemetryFields fields;
        fields<<",\"family\":\"adapter\",\"arm\":"<<control<<",\"state\":"<<int(selection.status)
            <<",\"expert\":"<<selection.expert<<",\"episodes\":"<<runtime.bridge.bandit().episodes()
            <<",\"exact_samples\":"<<exact<<",\"censored\":"<<censored<<",\"cost_samples\":"<<costs
            <<",\"gamma\":0.125,\"eta\":0.03125,\"rate_scale\":64,\"switch_cost\":0.05,\"abort_cost\":0.1"
            <<",\"model_assumed\":true,\"regret_guarantee\":false";
        runtime.campaign->report_policy(fields.view());runtime.reported[control]=true;
        if(control==runtime.governingControl)runtime.feedbackDirty=false;
    }
    if(selection.status!=Status::known && selection.status!=Status::pending)return {};
    selection.intent.policyToken=selection.token;
    return selection.intent;
}
inline void observer(void* opaque,const plan3::PolicyEvent& event) {
    auto& runtime=*static_cast<Runtime*>(opaque);
    const auto result=observe(runtime.bridge,event);
    if(result.status==Status::invalid)throw std::logic_error("Adapter job feedback mismatch");
    if(result.status==Status::known && event.kind!=plan3::PolicyEvent::Kind::committed)
        runtime.feedbackDirty=true;
}
inline int run(bool control,const char* label) {
    plan3_runtime::Campaign campaign(plan3::Controller::ledger,control,label);
    Runtime runtime(control);runtime.campaign=&campaign;
    campaign.supply_selector(select,&runtime,false,observer);
    while(true) {
        try {if(cambc::is_draft())plan3_runtime::fixed_draft();else campaign.turn();}
        catch(const std::exception& error) {
            std::fprintf(stdout,"p3 error: %.240s\n",error.what());std::fflush(stdout);
        } catch(...) {
            std::fputs("p3 error: unknown exception\n",stdout);std::fflush(stdout);
        }
        cambc::end_turn();
    }
}
} // namespace plan3_adapter
