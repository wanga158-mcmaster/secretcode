#pragma once
#include "../common/runtime.h"
#include "regret.h"

namespace plan3_hindsight {
struct Runtime {
    plan3_runtime::Campaign* campaign=nullptr;std::array<bool,2> reported{};std::uint64_t nextToken=0;
    struct Pending {std::uint64_t token=0;plan3::JobId job;int started=-1;bool committed=false;};
    Pending pending;unsigned commits=0,exactEndpoints=0,censored=0,aborts=0;std::uint64_t exactGems=0;
};
inline plan3::JobIntent select(void* opaque,bool control,const plan3::InformationState& info,
        const std::vector<plan3::JobForecast>& jobs,const plan3::ControllerContext&) {
    auto& state=*static_cast<Runtime*>(opaque);
    const auto ready=[&]{return cambc::fuel_remaining()>=45000 && cambc::round_number()==unsigned(info.round);};
    const auto table=map_menu(info,jobs,ready);const auto result=evaluate(table,ready);
    if(state.campaign && !state.reported[unsigned(control)] && ready()) {
        state.reported[unsigned(control)]=true;plan3_runtime::TelemetryFields fields;
        fields<<",\"family\":\"hindsight\",\"arm\":"<<unsigned(control)
            <<",\"model\":\"assumed-online-response-regret\",\"calibrated\":false,\"snapshot_round\":"<<info.round
            <<",\"covered\":"<<table.covered<<",\"single_world_collapse\":"<<bool(table.covered && !(table.covered&(table.covered-1)))
            <<",\"represented\":"<<table.represented<<",\"rows\":"<<table.rows.size()
            <<",\"canonical\":"<<table.canonicalCount<<",\"truncated\":"<<table.truncated
            <<",\"state\":"<<unsigned(table.state)<<",\"reason\":"<<unsigned(table.reason)
            <<",\"control_objective\":\"covered-world-security\"";
        state.campaign->report_policy(fields.str());
    }
    if(result.state!=State::known || !ready())return {plan3::IntentStatus::yield,std::nullopt,0,plan3::DecisionReason::no_admitted_job,false,table.truncated};
    const auto& choice=control?result.security:result.regret;
    // Proposals for companion arms are not observations. Only Campaign's
    // actual committed event installs an endpoint-matching token.
    const auto token=++state.nextToken;
    return {plan3::IntentStatus::selected,choice.job,control?choice.value:-choice.value,
        plan3::DecisionReason::objective,false,table.truncated,token};
}
inline void observe(void* opaque,const plan3::PolicyEvent& event) {
    auto& state=*static_cast<Runtime*>(opaque);if(!event.token)return;
    if(event.kind==plan3::PolicyEvent::Kind::committed) {
        if(state.pending.committed && state.pending.token==event.token)return;
        state.pending={event.token,event.job,event.startedRound,true};++state.commits;return;
    }
    const auto& pending=state.pending;
    if(!pending.committed || event.token!=pending.token || event.job!=pending.job ||
       event.startedRound!=pending.started || event.round<event.startedRound)return;
    if(event.kind==plan3::PolicyEvent::Kind::aborted){++state.aborts;++state.censored;}
    else if(event.conversion && event.exactConverted){++state.exactEndpoints;state.exactGems+=*event.exactConverted;}
    else ++state.censored;
    state.pending={}; // Unknown and aborted endpoints never become zero reward.
}
inline int run(bool control,const char* label) {
    plan3_runtime::Campaign campaign(plan3::Controller::ledger,control,label);
    Runtime state;state.campaign=&campaign;campaign.supply_selector(select,&state,false,observe);
    while(true) {
        try {if(cambc::is_draft())plan3_runtime::fixed_draft();else campaign.turn();}
        catch(const std::exception& error){std::fprintf(stdout,"p3 error: %.240s\n",error.what());std::fflush(stdout);}
        catch(...){std::fputs("p3 error: unknown exception\n",stdout);std::fflush(stdout);}
        cambc::end_turn();
    }
}
} // namespace plan3_hindsight
