// Production Indexer menu/adapter equivalence under shared nominal acquisition
// materialization. The inherited SDK host models declared fixtures, not fuel,
// native combat, native reachability or actual earned economic production.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#define main unused_indexer_current_main
#include "plan3-indexer-current-sdk.cpp"
#undef main
#pragma clang diagnostic pop
#include <tuple>
#ifndef PLAN3_INDEXER_NOMINAL_CAMPAIGN
#define PLAN3_INDEXER_NOMINAL_CAMPAIGN 0
#endif

namespace {
unsigned nominal_tables=0,nominal_rows=0,nominal_reduced=0,nominal_positive=0;
void same_consumed(const JobForecast& a,const JobForecast& b,const InformationState& state) {
    verify(a.id==b.id && a.complete==b.complete && a.routeKnown==b.routeKnown && a.fullClearAdmitted==b.fullClearAdmitted);
    verify(a.admissionTau==b.admissionTau && a.projectedRenewalAndReturnTime==b.projectedRenewalAndReturnTime &&
        a.campGems==b.campGems && a.campGold==b.campGold && a.rewardOnlyEndpoint==b.rewardOnlyEndpoint &&
        a.mandatorySafety==b.mandatorySafety && a.renewalRequired==b.renewalRequired);
    verify(admitted(state,a,false)==admitted(state,b,false));
    const auto* x=a.nominal();const auto* y=b.nominal();verify(bool(x)==bool(y));
    if(x) {
        verify(x->deltaS==y->deltaS && x->tau==y->tau && x->healthLoss==y->healthLoss &&
            x->goldEarned==y->goldEarned && x->goldSpent==y->goldSpent &&
            x->terminal.position.tile==y->terminal.position.tile &&
            x->terminal.health==y->terminal.health && x->terminal.maximumHealth==y->terminal.maximumHealth);
        verify(x->conversions.size()==y->conversions.size());
        for(std::size_t n=0;n<x->conversions.size();++n)
            verify(x->conversions[n].completedAt==y->conversions[n].completedAt && x->conversions[n].gems==y->conversions[n].gems);
    }
    if(productive(a.id.kind) && a.complete && x) {
        verify(a.outcomes.size()>=4 && b.outcomes.size()==1);
        verify(y->terminal.loops.empty() && y->terminal.loopSupportUnavailable && !y->terminal.preparationTurns);
        ++nominal_reduced;
    }
    ++nominal_rows;
}
auto project_fields(const plan3_indexer::Project& p) {
    return std::tuple{p.key,p.state,p.tau,p.Cfresh,p.Rfresh,p.Rcurrent,p.regenMax,p.G,
        p.currentComplete,p.currentAdmitted,p.freshClearAdmitted,p.routeKnown,p.recoveryKnown};
}
auto row_fields(const plan3_indexer::Row& r) {
    return std::tuple{r.key,r.state,r.reason,r.eligible,r.tau,r.N,r.P,r.D,r.G,
        r.index.numerator,r.index.denominator,r.rate.numerator,r.rate.denominator};
}
auto choice_fields(const plan3_indexer::Choice& c) {
    return std::tuple{c.present,c.row,c.key,c.score.numerator,c.score.denominator,c.N,c.P,c.D};
}
void same_selection(const plan3_indexer::Selection& a,const plan3_indexer::Selection& b) {
    verify(a.priority==b.priority && a.bootstrap==b.bootstrap && a.intent.status==b.intent.status &&
        a.intent.job==b.intent.job && a.intent.objective==b.intent.objective && a.intent.reason==b.intent.reason &&
        a.companion.status==b.companion.status && a.companion.job==b.companion.job &&
        a.companion.objective==b.companion.objective && a.companion.reason==b.companion.reason);
    const auto& x=a.input;const auto& y=b.input;
    verify(x.round==y.round && x.observationRound==y.observationRound && x.roundLimit==y.roundLimit &&
        x.width==y.width && x.height==y.height && x.carriedBag==y.carriedBag &&
        x.physicalCount==y.physicalCount && x.projectCount==y.projectCount && x.physicalKeys==y.physicalKeys &&
        x.physicalCursor==y.physicalCursor && x.cutoff==y.cutoff);
    for(unsigned n=0;n<x.projectCount;++n) {
        verify(project_fields(x.projects[n])==project_fields(y.projects[n]));
        const auto& g=a.geometry[n];const auto& h=b.geometry[n];
        verify(std::tie(g.originTile,g.endpointTile,g.groupField,g.altarField,g.spawnField,g.currentEndpointTile)==
            std::tie(h.originTile,h.endpointTile,h.groupField,h.altarField,h.spawnField,h.currentEndpointTile));
    }
    const auto& t=a.triple;const auto& u=b.triple;
    verify(t.state==u.state && t.reason==u.reason && t.H==u.H && t.rowCount==u.rowCount &&
        t.eligibleCount==u.eligibleCount && t.unknownCount==u.unknownCount && t.rejectedCount==u.rejectedCount &&
        t.projectedCount==u.projectedCount && t.projectedCircuit==u.projectedCircuit);
    verify(choice_fields(t.active)==choice_fields(u.active) && choice_fields(t.rate)==choice_fields(u.rate) &&
        choice_fields(t.circuit)==choice_fields(u.circuit));
    for(unsigned n=0;n<t.rowCount;++n)verify(row_fields(t.rows[n])==row_fields(u.rows[n]));
}
void nominal_table(unsigned mode,unsigned variant) {
    reset_case(variant==4);
    if(variant!=0){visible.fill(~std::uint64_t{0});group_state.fill(2);}
    Campaign c(Controller::ledger,false,"indexer-nominal-campaign");initialize(c,mode);
    if(variant==2 || variant==4 || variant==7)complete_opening(c);
    // Declared fixture cash purchases through the unchanged production shop,
    // not earned income. It supplies a genuinely admitted fresh-service table
    // as well as the low-stock bootstrap and refusal cases above.
    if(variant==7){gold=9000;c.shop();verify(!gold);actor.health=actor.max_health;}
    if(variant==3)bag=20;
    if(variant==5)actor.health=actor.max_health*.25f-1;
    if(variant==6)c.paths_.fields_[unsigned(c.personal_.groups[0].field)].complete=false;
    const auto state=c.information();auto full=c.forecast_context(state);
    verify(full.nominal_acquisitions_only==bool(PLAN3_INDEXER_NOMINAL_CAMPAIGN));
    full.nominal_acquisitions_only=false;auto fast=full.fork_without_loop_cache();fast.nominal_acquisitions_only=true;
    verify(fast.fork_without_loop_cache().nominal_acquisitions_only);
    const auto camps=cambc::vision::camps();const auto fj=c.menu(full,false,camps),nj=c.menu(fast,false,camps);
    verify(full.cutoff==fast.cutoff);
    const auto fm=canonical_menu(fj),nm=canonical_menu(nj);verify(fm.truncated==nm.truncated && fm.jobs.size()==nm.jobs.size());
    for(std::size_t n=0;n<fm.jobs.size();++n)same_consumed(*fm.jobs[n],*nm.jobs[n],state);
    auto ready=[](){return true;};
    const auto f=plan3_indexer::select_menu(full,c.indexer_universe_,fj,mode,c.context_,false,ready,c.indexer_opening_unfinished());
    const auto n=plan3_indexer::select_menu(fast,c.indexer_universe_,nj,mode,c.context_,false,ready,c.indexer_opening_unfinished());
    same_selection(f,n);
    if(variant==7){verify(f.triple.eligibleCount>0 && f.triple.state==plan3_indexer::State::known);++nominal_positive;}
    auto no=[](){return false;};
    same_selection(plan3_indexer::select_menu(full,c.indexer_universe_,fj,mode,c.context_,false,no,true),
        plan3_indexer::select_menu(fast,c.indexer_universe_,nj,mode,c.context_,false,no,true));
    ++nominal_tables;
    std::printf("IX_NOMINAL {\"side\":%u,\"actorType\":%u,\"mode\":%u,\"variant\":%u,\"rows\":%zu,\"projects\":%u,\"eligible\":%u,\"priority\":%s,\"nativeGames\":0}\n",
        bp_side(),current_actor_type(),mode,variant,fm.jobs.size(),f.input.projectCount,f.triple.eligibleCount,f.priority?"true":"false");
}
}
int main() {
    for(unsigned mode=0;mode<3;++mode)for(unsigned variant=0;variant<8;++variant)nominal_table(mode,variant);
    verify(nominal_tables==24 && nominal_rows>0 && nominal_reduced>0 && nominal_positive==3);
    std::printf("IX_NOMINAL_RESULT {\"status\":\"pass\",\"side\":%u,\"actorType\":%u,\"tables\":%u,\"rows\":%u,\"reduced\":%u,\"positiveTriples\":%u,\"checks\":%u,\"nativeGames\":0}\n",
        bp_side(),current_actor_type(),nominal_tables,nominal_rows,nominal_reduced,nominal_positive,checks);
    // The same compiled knob also traverses the existing real Campaign flow,
    // cutoff, banking-priority and conservative conversion-witness fixtures.
    for(unsigned mode=0;mode<3;++mode){
        selection(mode,false);selection(mode,true);selection(mode,false,true);selection(mode,true,true);
        cutoff(mode);faster_double(mode);full_flow(mode);
        for(unsigned which=0;which<5;++which)shared_priorities(mode,which);
        for(unsigned variant=0;variant<4;++variant)conversion(mode,variant);
        prediction_tail_cases(mode);
    }
    std::printf("IX_CURRENT_RESULT {\"status\":\"pass\",\"side\":%u,\"actorType\":%u,\"scenarios\":%u,\"flows\":%u,\"conversions\":%u,\"checks\":%u}\n",
        bp_side(),current_actor_type(),scenarios,flows,conversion_cases,checks);
    return 0;
}
