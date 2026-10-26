#pragma once
#include "../common/runtime.h"
#include "observations.h"

namespace plan3_changepoint {
struct Runtime {
    Config config;
    State inference;
    Previous previous;
    plan3_runtime::Campaign* campaign=nullptr;
    std::array<bool,2> reported{};
};
inline plan3::JobIntent select(void* opaque,bool control,const plan3::InformationState& info,
        const std::vector<plan3::JobForecast>& jobs,const plan3::ControllerContext& context) {
    auto& runtime=*static_cast<Runtime*>(opaque);
    const auto ready=[&] {return cambc::fuel_remaining()>=45000 && cambc::round_number()==unsigned(info.round);};
    const auto status=observe(runtime.inference,runtime.previous,runtime.config,info,ready);
    if(status==Status::cutoff || status==Status::invalid || status==Status::stale)
        return {plan3::IntentStatus::yield,std::nullopt,0,plan3::DecisionReason::no_admitted_job};
    const auto weights=forecast_weights(runtime.inference,runtime.config,control);
    if(!weights || !ready())return {plan3::IntentStatus::yield,std::nullopt,0,plan3::DecisionReason::no_admitted_job};
    if(runtime.campaign && !runtime.reported[unsigned(control)]) {
        runtime.reported[unsigned(control)]=true;plan3_runtime::TelemetryFields fields;
        fields<<",\"family\":\"changepoint\",\"arm\":"<<unsigned(control)
            <<",\"model\":\"assumed-Dirichlet-event3\",\"cap\":"<<runtime.config.cap
            <<",\"hazard\":"<<runtime.config.hazard<<",\"forgetting\":"<<runtime.config.forgetting
            <<",\"snapshot_round\":"<<info.round<<",\"events\":"<<runtime.inference.observations
            <<",\"unknown\":"<<runtime.inference.skipped<<",\"run0\":"<<runtime.inference.posterior[0]
            <<",\"weights\":[";
        for(unsigned w=0;w<4;++w) {if(w)fields<<',';fields<<(*weights)[w];}
        fields<<"],\"calibrated\":false";runtime.campaign->report_policy(fields.str());
    }
    auto current=context;current.belief.weights=*weights;
    auto intent=plan3::select(plan3::Controller::belief,false,info,jobs,current);
    if(!ready())return {plan3::IntentStatus::yield,std::nullopt,0,plan3::DecisionReason::no_admitted_job};
    return intent;
}
inline int run(bool control,const char* profile) {
    plan3_runtime::Campaign campaign(plan3::Controller::ledger,control,profile);
    Runtime runtime;runtime.campaign=&campaign;campaign.supply_selector(select,&runtime);
    while(true) {
        try {if(cambc::is_draft())plan3_runtime::fixed_draft();else campaign.turn();}
        catch(const std::exception& error) {std::fprintf(stdout,"p3 error: %.240s\n",error.what());std::fflush(stdout);}
        catch(...) {std::fputs("p3 error: unknown exception\n",stdout);std::fflush(stdout);}
        cambc::end_turn();
    }
}
} // namespace plan3_changepoint
