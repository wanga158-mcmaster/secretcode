#pragma once
#include "../common/runtime.h"
#include "policy.h"

namespace plan3_sequence {
struct Runtime {
    Planner planner;plan3_runtime::Campaign* campaign=nullptr;
    std::array<bool,2> reported{};
    explicit Runtime(bool control):planner(control) {}
};
inline plan3::JobIntent select(void* opaque,bool control,const plan3::InformationState& info,
                              const std::vector<plan3::JobForecast>& jobs,const plan3::ControllerContext& context) {
    auto& runtime=*static_cast<Runtime*>(opaque);
    const auto ready=[&] {return cambc::fuel_remaining()>=45000 && cambc::round_number()==unsigned(info.round);};
    const auto selected=runtime.planner.preview(control,info,jobs,context,ready);
    if(!runtime.reported[unsigned(control)] && ready()) {
        runtime.reported[unsigned(control)]=true;plan3_runtime::TelemetryFields fields;
        fields<<",\"family\":\"sequence\",\"arm\":"<<unsigned(control)<<",\"stage\":"<<unsigned(selected.second)
            <<",\"state\":"<<int(selected.state)<<",\"availability\":"<<selected.availability
            <<",\"signal\":"<<unsigned(selected.signal)<<",\"action\":"<<selected.action
            <<",\"tickets0\":"<<selected.tickets0<<",\"slot\":"<<selected.slot<<",\"omitted\":"<<selected.omitted
            <<",\"model\":\""<<policy_model_sha256<<"\",\"assumed_payoffs\":true"
            <<",\"ticket_stream\":\"prescribed64\",\"history\":\"governing\",\"online_CFR\":false";
        runtime.campaign->report_policy(fields.str());
    }
    return selected.state==State::known?selected.intent:plan3::JobIntent{};
}
inline void observer(void* opaque,const plan3::PolicyEvent& event) {
    if(!static_cast<Runtime*>(opaque)->planner.observe(event))throw std::logic_error("Sequence job history mismatch");
}
inline int run(bool control,const char* label) {
    plan3_runtime::Campaign campaign(plan3::Controller::ledger,control,label);
    Runtime runtime(control);runtime.campaign=&campaign;campaign.supply_selector(select,&runtime,false,observer);
    while(true) {
        try {if(cambc::is_draft())plan3_runtime::fixed_draft();else campaign.turn();}
        catch(const std::exception& error) {std::fprintf(stdout,"p3 error: %.240s\n",error.what());std::fflush(stdout);}
        catch(...) {std::fputs("p3 error: unknown exception\n",stdout);std::fflush(stdout);}
        cambc::end_turn();
    }
}
} // namespace plan3_sequence
