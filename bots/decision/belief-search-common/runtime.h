#pragma once
#include "../common/runtime.h"
#include "adapter.h"

namespace plan3_belief_search {
struct Runtime {Policy policy;plan3_runtime::Campaign* campaign=nullptr;std::array<bool,2> reported{};};
inline plan3::JobIntent select(void* opaque,bool control,const plan3::InformationState& info,
        const std::vector<plan3::JobForecast>& jobs,const plan3::ControllerContext& context) {
    auto& runtime=*static_cast<Runtime*>(opaque);
    const auto ready=[&]{return cambc::fuel_remaining()>=45000 && cambc::round_number()==unsigned(info.round);};
    const auto startReady=[&]{return cambc::fuel_remaining()>=65000 && cambc::round_number()==unsigned(info.round);};
    // Current bag is never duplicated as a second projected conversion.
    if(info.carriedGems>0) {
        const plan3::JobForecast* bank=nullptr;
        for(const auto* job:plan3::canonical_menu(jobs).jobs)if(job->id.kind==plan3::JobKind::bank && plan3::admitted(info,*job,false) &&
            (!bank || job->nominal()->tau<bank->nominal()->tau || (job->nominal()->tau==bank->nominal()->tau && job->id<bank->id)))bank=job;
        return bank?plan3::JobIntent{plan3::IntentStatus::selected,bank->id,0,plan3::DecisionReason::objective}:plan3::JobIntent{};
    }
    const auto selected=runtime.policy.choose(control,info,jobs,context,ready,startReady);
    if(runtime.campaign && !runtime.reported[unsigned(control)] && ready()) {
        runtime.reported[unsigned(control)]=true;plan3_runtime::TelemetryFields fields;
        fields<<",\"family\":\"belief-search\",\"arm\":"<<unsigned(control)<<",\"depth\":"<<(control?1:2)
            <<",\"snapshot_round\":"<<info.round<<",\"model\":\"assumed-static-response4\",\"calibrated\":false"
            <<",\"canonical\":"<<runtime.policy.canonicalCount<<",\"mapped\":"<<runtime.policy.mappedCount
            <<",\"unknown\":"<<runtime.policy.unknownCount<<",\"seeded\":"<<bool(runtime.policy.belief())
            <<",\"source_entry_floor\":65000,\"source_stop_floor\":45000,\"native_budget_validated\":false";
        runtime.campaign->report_policy(fields.str());
    }
    return selected;
}
inline void observe(void* opaque,const plan3::PolicyEvent& event) {
    auto& runtime=*static_cast<Runtime*>(opaque);const unsigned before=runtime.policy.updates+runtime.policy.modelGaps+runtime.policy.censored;
    runtime.policy.observe(event);
    if(runtime.campaign && before!=runtime.policy.updates+runtime.policy.modelGaps+runtime.policy.censored && cambc::fuel_remaining()>=45000) {
        plan3_runtime::TelemetryFields fields;
        fields<<",\"family\":\"belief-search\",\"feedback\":true,\"updates\":"<<runtime.policy.updates
            <<",\"model_gaps\":"<<runtime.policy.modelGaps<<",\"censored\":"<<runtime.policy.censored;
        runtime.campaign->report_policy(fields.str());
    }
}
inline int run(bool control,const char* label) {
    plan3_runtime::Campaign campaign(plan3::Controller::ledger,control,label);
    auto runtime=std::make_unique<Runtime>();runtime->campaign=&campaign;
    campaign.supply_selector(select,runtime.get(),true,observe);
    while(true) {
        try {if(cambc::is_draft())plan3_runtime::fixed_draft();else campaign.turn();}
        catch(const std::exception& error){std::fprintf(stdout,"p3 error: %.240s\n",error.what());std::fflush(stdout);}
        catch(...){std::fputs("p3 error: unknown exception\n",stdout);std::fflush(stdout);}
        cambc::end_turn();
    }
}
} // namespace plan3_belief_search
