#define PLAN3_TEMPO_NOMINAL 1
#define P3_NOMINAL_HELPERS_ONLY
#include "plan3-ledger-nominal-current.cpp"

static void tempo_case(bool control) {
    auto c=campaign(control);c->controller_=Controller::tempo;
    const auto state=c->information();auto full=c->forecast_context(state);
    NC(full.nominal_acquisitions_only && !full.terminal_renewal_loops);
    full.nominal_acquisitions_only=false;full.terminal_renewal_loops=true;
    auto fast=full.fork_without_loop_cache();fast.nominal_acquisitions_only=true;fast.terminal_renewal_loops=false;
    const auto camps=cambc::vision::camps();const auto prefix=tt::Menu::single_prefix(c->personal_,c->paths_,cambc::self().position());
    const auto fj=c->menu(full,false,camps,prefix),nj=c->menu(fast,false,camps,prefix);
    const auto fm=canonical_menu(fj),nm=canonical_menu(nj);NC(fm.jobs.size()==nm.jobs.size());
    for(std::size_t i=0;i<fm.jobs.size();++i) {
        equal_consumed(*fm.jobs[i],*nm.jobs[i]);
        NC(tempo_value(*fm.jobs[i],control)==tempo_value(*nm.jobs[i],control));
        NC(admitted(state,*fm.jobs[i],false)==admitted(state,*nm.jobs[i],false));
    }
    const auto expected=select(Controller::tempo,control,state,fj,c->context_);
    const auto result=select(Controller::tempo,control,state,nj,c->context_);
    NC(expected.job && expected.job==result.job && expected.objective==result.objective);
    NC(full.loop_evaluations>0 && fast.loop_evaluations==0);
    // The common executor must finish the actual selected complete job.
    c->turn();NC(c->active_ && c->active_->id==*expected.job);
    for(unsigned i=0;i<180 && !c->telemetry_.completed;++i){advance_round();c->turn();}
    NC(c->telemetry_.completed && own_score && submissions);
    std::printf("TEMPO_NOMINAL {\"side\":%u,\"control\":%s,\"gems\":%u,\"captures\":%u,\"submissions\":%u,\"fullLoops\":%u,\"slimLoops\":%u}\n",bp_side(),control?"true":"false",own_score,captures,submissions,full.loop_evaluations,fast.loop_evaluations);
}
int main(){actual_caller(false);actual_caller(true);tempo_case(false);tempo_case(true);negatives();std::printf("TEMPO_NOMINAL_RESULT {\"status\":\"pass\",\"checks\":%u}\n",nchecks);}
