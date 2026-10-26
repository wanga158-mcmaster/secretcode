#pragma once
#include "../common/runtime.h"
#include "adapter.h"

namespace plan3_shadow_prices {
struct Runtime {Policy policy;plan3_runtime::Campaign* campaign=nullptr;std::array<bool,2> reported{};};
inline plan3::JobIntent select(void* opaque,bool control,const plan3::InformationState& info,
        const std::vector<plan3::JobForecast>& jobs,const plan3::ControllerContext& context) {
    auto& runtime=*static_cast<Runtime*>(opaque);
    const auto ready=[&]{return cambc::fuel_remaining()>=45000 && cambc::round_number()==unsigned(info.round);};
    if(!ready())return {};
    if(info.carriedGems>0)return bank_prefix(info,jobs); // Old bag is settled once outside the replicated LP.
    const auto mapped=map_menu(info,jobs,context.menuComplete,ready);
    const auto& result=runtime.policy.solve(mapped,ready);
    if(runtime.campaign && !runtime.reported[unsigned(control)] && ready()) {
        runtime.reported[unsigned(control)]=true;
        plan3_runtime::TelemetryFields fields;
        fields<<",\"family\":\"shadow-prices\",\"arm\":"<<unsigned(control)<<",\"snapshot_round\":"<<info.round
            <<",\"surrogate\":\"4-resource-replicated-rows\",\"control\":\"first-derived-frozen\",\"units\":1"
            <<",\"state\":"<<int(result.state)<<",\"certified\":"<<result.certified<<",\"canonical\":"<<mapped.canonicalCount
            <<",\"unknown\":"<<mapped.unknownCount<<",\"rejected\":"<<mapped.rejectedCount<<",\"over_budget\":"<<mapped.overBudget
            <<",\"frozen\":"<<bool(runtime.policy.reference())<<",\"recovery_constraint_redundant\":true,\"assumed\":true";
        runtime.campaign->report_policy(fields.str());
    }
    if(!ready())return {plan3::IntentStatus::yield,std::nullopt,0,plan3::DecisionReason::no_admitted_job,false,mapped.candidatePrefix};
    return policy_intent(mapped,result,control);
}
inline int run(bool control,const char* label) {
    plan3_runtime::Campaign campaign(plan3::Controller::treasury,control,label);
    auto runtime=std::make_unique<Runtime>();runtime->campaign=&campaign;campaign.supply_selector(select,runtime.get());
    while(true) {
        try {if(cambc::is_draft())plan3_runtime::fixed_draft();else campaign.turn();}
        catch(const std::exception& error){std::fprintf(stdout,"p3 error: %.240s\n",error.what());std::fflush(stdout);}
        catch(...){std::fputs("p3 error: unknown exception\n",stdout);std::fflush(stdout);}
        cambc::end_turn();
    }
}
} // namespace plan3_shadow_prices
