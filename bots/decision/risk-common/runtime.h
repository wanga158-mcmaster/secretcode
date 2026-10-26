#pragma once
#include "../common/runtime.h"
#include "scenarios.h"

namespace plan3_risk {
struct Runtime {
    Model model;
    plan3_runtime::Campaign* campaign=nullptr;
    std::array<bool,2> reportAttempted{};
};
inline plan3::JobIntent select(void* opaque,bool control,const plan3::InformationState& info,
                              const std::vector<plan3::JobForecast>& jobs,const plan3::ControllerContext& context) {
    auto& runtime=*static_cast<Runtime*>(opaque);
    const auto ready=[] {return cambc::fuel_remaining()>=45000;};
    const auto mapped=map_scenarios(info,jobs,context,runtime.model,ready);
    Result result;
    if(mapped.state==State::known && ready())result=evaluate(mapped.input);
    if(runtime.campaign && !runtime.reportAttempted[unsigned(control)] && ready()) {
        runtime.reportAttempted[unsigned(control)]=true;
        plan3_runtime::TelemetryFields fields;
        fields<<",\"family\":\""<<(runtime.model.family==Family::ambiguity?"ambiguity":"ruin")
            <<"\",\"arm\":"<<unsigned(control)<<",\"model\":\"assumed-tv4\",\"calibrated\":false"
            <<",\"control_model\":\""<<(runtime.model.control==Control::minimax?"full-minimax":"belief-mean")<<'"'
            <<",\"units\":"<<runtime.model.valueUnits<<",\"probability_units\":"<<runtime.model.probabilityTotal
            <<",\"snapshot_round\":"<<info.round
            <<",\"radius\":["<<runtime.model.radius.numerator<<','<<runtime.model.radius.denominator
            <<"],\"budget\":["<<runtime.model.budget.numerator<<','<<runtime.model.budget.denominator
            <<"],\"canonical\":"<<mapped.canonicalCount<<",\"mapped\":"<<mapped.input.jobCount
            <<",\"omitted\":"<<mapped.omittedUnknown<<",\"state\":"<<int(mapped.state)<<",\"reason\":"<<int(mapped.reason);
        runtime.campaign->report_policy(fields.str());
    }
    if(mapped.state!=State::known || result.state!=State::known || !ready())
        return {plan3::IntentStatus::yield,std::nullopt,0,plan3::DecisionReason::no_admitted_job,false,mapped.candidatePrefix};
    const auto minimax=full_minimax(mapped);
    const Choice* choice=&result.belief;
    if(runtime.model.family==Family::ambiguity)
        choice=control?(runtime.model.control==Control::minimax?&minimax:&result.belief):&result.ambiguity;
    else if(!control)choice=&result.ruin;
    if(!choice->present)return {plan3::IntentStatus::yield,std::nullopt,0,plan3::DecisionReason::no_admitted_job,false,mapped.candidatePrefix};
    const double value=double(choice->score.numerator)/choice->score.denominator/runtime.model.valueUnits;
    return {plan3::IntentStatus::selected,job_id(choice->key),value,plan3::DecisionReason::objective,false,mapped.candidatePrefix};
}
inline int run(Family family,Control control_rule,bool control,const char* profile) {
    plan3_runtime::Campaign campaign(plan3::Controller::ledger,control,profile);
    Runtime runtime;runtime.model.family=family;runtime.model.control=control_rule;runtime.campaign=&campaign;
    campaign.supply_selector(select,&runtime,true);
    while(true) {
        try {if(cambc::is_draft())plan3_runtime::fixed_draft();else campaign.turn();}
        catch(const std::exception& error) {std::fprintf(stdout,"p3 error: %.240s\n",error.what());std::fflush(stdout);}
        catch(...) {std::fputs("p3 error: unknown exception\n",stdout);std::fflush(stdout);}
        cambc::end_turn();
    }
}
} // namespace plan3_risk
