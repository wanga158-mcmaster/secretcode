// Current Backpressure promoted from the v1 SDK fixture; unchanged SDK and captured
// static records plus explicitly
// modeled live state. No engine or bot module is launched.
#if defined(__wasm__)
#undef __wasm__
#include <cambc.h>
#define __wasm__ 1
#else
#include <cambc.h>
#endif
#include "../backpressure-common/runtime.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <type_traits>
#include "backpressure-config-v1.h"
std::int64_t (*prediction_fuel_probe)()=nullptr;
#include "backpressure-host-v1.h"
#include "plan3-backpressure-extra-host-v1.h"
#include "backpressure-reference-v1.h"

namespace {
using namespace plan3_runtime;
using namespace plan3;
using Campaign=plan3_runtime::Campaign;
unsigned sdk_cases=0,paired_cases=0,forecast_cases=0,overflow_cases=0,shared_cases=0;
unsigned maximum_format_bytes=0;
unsigned diagnostic_cases=0,planning_trace_cases=0;
unsigned queue_cases=0,queue_cutoffs=0;
using Domain=plan3_backpressure::ForecastDomain;
Campaign* prediction_campaign=nullptr;
std::int64_t prediction_tail=1000000;
std::int64_t committed_fuel() {return prediction_campaign && prediction_campaign->active_?prediction_tail:-1;}
void captured_actor() {
    cambc::detail::champion_design design{};std::memcpy(&design,bp_config::champions[10].data(),sizeof(design));
    const auto& stats=design.base_stats;
    actor={int(bp_side()),10,bp_axis(2),bp_axis(2),stats.base_health,stats.base_health,stats.base_strength,stats.base_focus,stats.base_move_speed,stats.base_attack_range,1|(1<<3)|(1<<4)};
    enemy.team=1-int(bp_side());enemy.x=bp_axis(3);enemy.y=bp_axis(2);
}
void reset_case() {
    prediction_fuel_probe=nullptr;prediction_campaign=nullptr;
    reset();captured_actor();current_round=32;own_score=100;bag=10;gold=0;group_state={2,1,1};
    raw_gems={66,66,0};raw_gold={270,270,105};all_gold_map=false;bp_hidden_collision=false;cost_zero_item=-1;buys=submissions=recalls=actions=0;
    query_trace.clear();fuel_trace.clear();++sdk_cases;
}
template<class C> void initialize(C& c) {
    unsigned callbacks=0;
    while(!c.initialized_) {verify(++callbacks<128);c.initialize_campaign();}
    while(c.field_cursor_<c.field_order_.size())c.register_fields();
    for(unsigned i=0;i<64;++i){c.paths_.begin_turn();c.paths_.advance();}
    c.personal_.observe(cambc::vision::camps(),{});
    c.planning_audits_=2;c.candidate_audit_=true;c.last_progress_=current_round;c.last_review_=current_round;
    verify(c.personal_.initialization_complete && c.personal_.groups.size()==3);
    for(const auto& g:c.personal_.groups)verify(g.field>=0 && c.paths_.get(g.field,{actor.x,actor.y})>=0);
    while(!c.capital_.advance_catalogue(4)){}
}
void same_job(const JobForecast& a,const JobForecast& b) {
    verify(a.id==b.id && a.complete==b.complete && a.fullClearAdmitted==b.fullClearAdmitted && a.routeKnown==b.routeKnown);
    verify(a.campGems==b.campGems && a.campGold==b.campGold && a.projectedRenewalAndReturnTime==b.projectedRenewalAndReturnTime && a.outcomes.size()==b.outcomes.size());
    for(unsigned i=0;i<a.outcomes.size();++i) {
        const auto& x=a.outcomes[i];const auto& y=b.outcomes[i];
        verify(x.deltaS==y.deltaS && x.tau==y.tau && x.healthLoss==y.healthLoss && x.response==y.response && x.lifecycle==y.lifecycle);
        verify(x.terminal.bag==y.terminal.bag && x.terminal.health==y.terminal.health && x.terminal.position.tile==y.terminal.position.tile && x.terminal.loops.size()==y.terminal.loops.size());
    }
}
template<class C> bool preflight(C& c) {
    if constexpr(requires {c.backpressure_domain_;})
        return c.backpressure_domain_.raw_supported() && c.backpressure_domain_.forecast_supported(c.personal_,cambc::vision::camps(),{});
    return true;
}
template<class C> void arm(C& c,bool rate) {if constexpr(requires {c.set_backpressure_arm(rate);})c.set_backpressure_arm(rate);else verify(!rate);}
struct Run {
    std::optional<JobForecast> active;
    std::vector<unsigned> queries;
    std::vector<std::int64_t> fuels;
    unsigned decisions=0,activations=0;
};
void reset_mode(unsigned mode) {
    reset_case();if(mode==7)bag=900;if(mode==9 || mode==11 || mode==12 || mode==14)bag=0;
    if(mode==1 || mode==9 || mode==11 || mode==12)group_state[0]=1;if(mode==2)group_state[0]=3;
    if(mode==13){all_gold_map=true;raw_gems={0,0,0};raw_gold={105,105,105};bag=0;group_state={1,1,1};}
    if(mode==14){items[0]=32;actor.strength+=32;}
}
template<class C> InformationState prepare_mode(C& c,unsigned mode,bool rate) {
    arm(c,rate);initialize(c);
    if(mode==3)c.personal_.observations[c.group_index(bp_key(0))].round=current_round-1;
    if(mode==4) {c.personal_.observation_round=current_round-1;for(auto& o:c.personal_.observations)o.round=current_round-1;}
    if(mode==5)c.personal_.observations[c.group_index(bp_key(0))].lifecycle.reset();
    if(mode==14)for(auto& observation:c.personal_.observations)observation.lifecycle.reset();
    if(mode==11 || mode==12) {
        for(unsigned i=0;i<6;++i)set(visible.data(),bp_axis(bp_x[i]),bp_axis(bp_y[i]),false);
        for(unsigned i=0;i<2;++i){auto& o=c.personal_.observations[c.group_index(bp_key(i))];if(mode==11)o.lifecycle.reset();else o.round=current_round-1;}
    }
    verify(preflight(c));auto state=c.information();if(mode==10)state.roundLimit=state.round+5;
    if(mode==6)for(unsigned i=0;i<31;++i)c.telemetry_.emit("candidate",",\"candidate_prefix\":true");
    return state;
}
template<class C> Run choice(unsigned mode,bool rate,unsigned stop=0) {
    reset_mode(mode);C c(Controller::ledger,false,"backpressure-sdk");auto state=prepare_mode(c,mode,rate);
    fuel_reads=0;cutoff_fuel_read=stop;query_trace.clear();fuel_trace.clear();
    std::printf("BP_CAPTURE %u %u %u\n",mode,unsigned(rate),stop);
    c.choose(state,mode==8,cambc::vision::camps());
    if(mode==6){verify(c.telemetry_.context_emitted_ && c.telemetry_.details_==32);}
    Run out;out.active=c.active_;out.queries=query_trace;out.fuels=fuel_trace;out.decisions=c.telemetry_.decisions;out.activations=c.telemetry_.activations;
    verify(actions==0);c.telemetry_.summary(true);verify(c.telemetry_.lines_<=96);return out;
}
__attribute__((noinline)) plan3_backpressure::PairResult inspect_pair(unsigned mode) {
    reset_mode(mode);Campaign c(Controller::ledger,false,"backpressure-sdk");const auto state=prepare_mode(c,mode,false);
    auto context=c.forecast_context(state);
    const auto prefix=two_trip::Menu::single_prefix(c.personal_,c.paths_,cambc::self().position());
    const auto jobs=c.menu(context,mode==8,cambc::vision::camps(),prefix);
    c.context_.newlyFeasibleWinningBank=state.carriedGems>0 && state.carriedGems>=state.winningGems-state.ownPermanentScore;
    const auto selected=plan3_backpressure::select_menu(c.personal_,state,jobs,false,cambc::self().is_submitting_gems(),true,c.context_,
        context.cutoff || c.backpressure_domain_.unknown_gem_opportunity(c.personal_,unsigned(state.round)),[] {return cambc::fuel_remaining()>=45000;});
    return selected.pair; // Fixture-owned copy only; production retains no snapshot.
}
void paired(unsigned mode,unsigned stop=0) {
    const auto pressure=choice<Campaign>(mode,false,stop),rate=choice<Campaign>(mode,true,stop);++paired_cases;
    verify(pressure.queries==rate.queries && pressure.fuels==rate.fuels && pressure.decisions==rate.decisions);
    const auto pair=stop?plan3_backpressure::PairResult{}:inspect_pair(mode);
    if(!stop && mode==0) {
        verify(pressure.active && rate.active && productive(pressure.active->id.kind) && rate.active->id.kind==JobKind::bank);
        verify(pair.queues[0]==0 && pair.queues[1]==66 && pair.queues[2]==10 && pair.queues[3]==0);
        const auto& a=pair.rows[pair.active.row];const auto& b=pair.rows[pair.control.row];
        std::printf("BP_REAL_FORK {\"queues\":[0,66,10,0],\"ids\":[[%d,%d,%d,%d,%d],[%d,%d,%d,%d,%d]],\"pressureD\":%u,\"rateD\":%u,\"pressureConverted\":%llu,\"rateConverted\":%llu,\"pressureNumerator\":\"%s\",\"rateNumerator\":\"%s\"}\n",a.key[0],a.key[1],a.key[2],a.key[3],a.key[4],b.key[0],b.key[1],b.key[2],b.key[3],b.key[4],a.duration,b.duration,static_cast<unsigned long long>(a.converted),static_cast<unsigned long long>(b.converted),plan3_backpressure::decimal(a.pressure.numerator).c_str(),plan3_backpressure::decimal(b.rate.numerator).c_str());
    }
    if(!stop && (mode==1 || mode==2 || mode==5))verify(pair.queues[0]==0 && pair.queues[1]==0);
    if(!stop && mode==4)verify(!pressure.active && !rate.active && !pair.active.present && !pair.control.present);
    if(!stop && mode==7)verify(pressure.active && rate.active && pressure.active->id.kind==JobKind::bank && rate.active->id.kind==JobKind::bank);
    if(!stop && mode==8)verify(pressure.active && rate.active && pressure.active->id.kind==JobKind::renew && rate.active->id.kind==JobKind::renew);
    if(!stop && (mode==9 || mode==11 || mode==12 || mode==13 || mode==14)) {
        const auto old=choice<BaselineCampaign>(mode,false);
        verify(old.active && old.active->rewardOnlyEndpoint);
        verify(pressure.active && rate.active && pressure.active->rewardOnlyEndpoint && rate.active->rewardOnlyEndpoint);
        verify(pressure.active->id==old.active->id && rate.active->id==old.active->id);
    }
}
__attribute__((noinline)) void forecast_parity() {
    for(unsigned mode=0;mode<3;++mode) {
        reset_case();group_state[0]=mode==0?2:mode==1?1:3;
        Campaign c(Controller::ledger,false,"backpressure-sdk");initialize(c);verify(preflight(c));
        BaselineCampaign old(Controller::ledger,false,"backpressure-sdk");initialize(old);
        const auto state=c.information();auto context=c.forecast_context(state),baseline=old.forecast_context(state);
        same_job(context.bank(0),baseline.bank(0));same_job(context.renewal(false),baseline.renewal(false));
        for(unsigned g=0;g<c.personal_.groups.size();++g)same_job(context.acquisition(int(g)),baseline.acquisition(int(g)));
        ++forecast_cases;
    }
}
__attribute__((noinline)) void channels_and_purchases() {
    for(unsigned phase:{1u,2u,12u})for(bool rate:{false,true}) {
        reset_case();Campaign c(Controller::ledger,false,"backpressure-sdk");arm(c,rate);initialize(c);
        actor.state|=1u<<phase;query_trace.clear();actions=0;c.turn();
        verify(actions==0 && !c.active_);++shared_cases;
    }
    std::vector<unsigned> old_queries;std::array<unsigned,static_cast<unsigned>(cambc::item_type::count)> old_items{};unsigned old_gold=0;
    for(unsigned which=0;which<3;++which) {
        reset_case();gold=30;items.fill(0);query_trace.clear();
        if(which==0){BaselineCampaign c(Controller::ledger,false,"backpressure-sdk");c.shop();old_queries=query_trace;old_items=items;old_gold=gold;}
        else {Campaign c(Controller::ledger,false,"backpressure-sdk");arm(c,which==2);c.shop();verify(query_trace==old_queries && items==old_items && gold==old_gold);}
        ++shared_cases;
    }
}
// One live Campaign object and its actual turn->choose->execute path on the
// default WASI stack. No comparison Campaign or saved PairResult is nested.
__attribute__((noinline)) void single_turn(bool rate) {
    reset_case();Campaign c(Controller::ledger,false,"backpressure-sdk");arm(c,rate);initialize(c);
    c.turn();verify(c.telemetry_.decisions==1 && c.active_);
    verify(c.active_->id.kind==(rate?JobKind::bank:JobKind::cycle));
    std::printf("BP_SINGLE_TURN {\"arm\":%u,\"decisions\":%u,\"kind\":%d}\n",unsigned(rate),c.telemetry_.decisions,int(c.active_->id.kind));
    ++shared_cases;
}
// Trigger by actual committed state, not a fragile SDK-read ordinal. This host
// response proves the guard boundary, not native formatting/destructor cost.
__attribute__((noinline)) void prediction_tail_cases(bool rate) {
    std::optional<JobForecast> healthy;
    for(std::int64_t tail:{1000000,44999,45000,29000}) {
        reset_case();Campaign c(Controller::ledger,false,"backpressure-sdk");arm(c,rate);initialize(c);
        prediction_campaign=&c;prediction_tail=tail;prediction_fuel_probe=committed_fuel;
        const bool turn=tail==29000;
        const unsigned omitted=c.telemetry_.details_omitted,cutoffs=c.telemetry_.cutoffs;
        std::printf("BP_TAIL_BEGIN {\"arm\":%u,\"fuel\":%lld,\"turn\":%s}\n",unsigned(rate),static_cast<long long>(tail),turn?"true":"false");
        if(turn)c.turn();else c.choose(c.information(),false,cambc::vision::camps());
        prediction_fuel_probe=nullptr;prediction_campaign=nullptr;
        verify(c.active_ && c.telemetry_.activations==1 && actions==0 && moves==0);
        if(!healthy)healthy=c.active_;else same_job(*healthy,*c.active_);
        verify(c.telemetry_.details_omitted==omitted+unsigned(tail<45000));
        const bool fresh=c.conversion_snapshot_.valid && c.conversion_snapshot_.round==unsigned(current_round) &&
            c.conversion_snapshot_.scoreRound==unsigned(current_round) && c.conversion_snapshot_.bag==bag;
        if(turn)verify(fresh && c.telemetry_.cutoffs==cutoffs+1);
        std::printf("BP_TAIL_RESULT {\"arm\":%u,\"fuel\":%lld,\"turn\":%s,\"omitted\":%u,\"freshSnapshot\":%s,\"actions\":%u,\"moves\":%u}\n",
            unsigned(rate),static_cast<long long>(tail),turn?"true":"false",c.telemetry_.details_omitted-omitted,fresh?"true":"false",actions,moves);
        ++shared_cases;
    }
}
__attribute__((noinline)) void raw_overflow() {
    for(unsigned mode=0;mode<10;++mode)for(bool rate:{false,true}) {
        if(mode>=5 && mode<=7)continue; // Immutable SDK costs require fresh instances.
        reset_case();Campaign c(Controller::ledger,false,"backpressure-sdk");arm(c,rate);
        if(mode==0)raw_gems[0]=0x80000000u;if(mode==1)raw_gold[0]=0x80000000u;
        if(mode==4)raw_gold={0x7fffffffu,0x7fffffffu,105};
        if(mode>=5 && mode<=7)cost_zero_item=mode==5?0:mode==6?28:18;
        if(mode==8)raw_gems={0x7fffffffu,5,0};
        if(mode<2 || (mode>=4 && mode<=8)) {
            for(unsigned i=0;i<32 && !c.initialized_;++i)c.initialize_campaign();
            verify(!c.initialized_ && c.personal_.groups.empty());
            Domain::Report report;verify(!c.backpressure_domain_.forecast_supported(c.personal_,{}, {},false,&report));
            verify(report.reason==Domain::Reason::availability && report.index==-1);++diagnostic_cases;
        } else {
            if(mode==2)raw_gems={5,0,0};if(mode==9)raw_gems={5,5,0};
            initialize(c);bag=mode==2?std::uint32_t(std::numeric_limits<int>::max()-4):mode==9?std::uint32_t(std::numeric_limits<int>::max()-9):10;
            own_score=mode==3?std::uint32_t(std::numeric_limits<int>::max()-4):0;
            Domain::Report report;verify(c.backpressure_domain_.raw_supported(&report) && report.reason==Domain::Reason::none);
            verify(!c.backpressure_domain_.forecast_supported(c.personal_,cambc::vision::camps(),{},false,&report));
            verify(report.reason==Domain::Reason::numeric && report.index==-1);++diagnostic_cases;
            query_trace.clear();c.turn();verify(!c.active_ && actions==0);
        }
        ++overflow_cases;
    }
}
__attribute__((noinline)) void initial_bad_cost(unsigned item) {
    verify(item==0 || item==28 || item==6);
    for(bool rate:{false,true}) {
        reset_case();Campaign c(Controller::ledger,false,"backpressure-sdk");arm(c,rate);
        for(unsigned i=0;i<32 && !c.initialized_;++i)c.initialize_campaign();
        verify(!c.initialized_ && c.personal_.groups.empty());
        Domain::Report report;verify(!c.backpressure_domain_.forecast_supported(c.personal_,{}, {},false,&report));
        verify(report.reason==Domain::Reason::availability && report.index==-1);
    }
    std::printf("BP_STATIC_DOMAIN_RESULT {\"status\":\"passed\",\"side\":%u,\"item\":%u,\"reason\":\"availability\",\"checks\":%u}\n",bp_side(),item,checks);
}
__attribute__((noinline)) void negative_health() {
    for(bool alive:{false,true})for(bool rate:{false,true}) {
        reset_case();Campaign c(Controller::ledger,false,"backpressure-sdk");arm(c,rate);initialize(c);
        actor.health=-5;actor.state=alive?1|(1<<3)|(1<<4):0;query_trace.clear();c.turn();
        verify(!c.active_ || c.active_->id.kind==JobKind::renew);verify(buys==0 && submissions==0);++shared_cases;
    }
}
template<class Call>
void diagnostic(const char* label,Call&& call,Domain::Reason expected,int index=-1,unsigned stop=0,std::int64_t remaining=1000000) {
    std::vector<unsigned> queries;std::vector<std::int64_t> fuels;
    for(unsigned reported=0;reported<2;++reported) {
        query_trace.clear();fuel_trace.clear();fuel_reads=0;fuel=remaining;cutoff_fuel_read=stop;
        Domain::Report report{Domain::Reason::views,999};
        const bool value=call(reported?&report:nullptr);
        verify(value==(expected==Domain::Reason::none));
        if(!reported){queries=query_trace;fuels=fuel_trace;}
        else {verify(queries==query_trace && fuels==fuel_trace);verify(report.reason==expected && report.index==index);}
    }
    fuel=1000000;fuel_reads=cutoff_fuel_read=0;
    std::printf("BP_DOMAIN_REASON {\"label\":\"%s\",\"reason\":\"%s\",\"index\":%d,\"queryParity\":true,\"boolParity\":true}\n",label,Domain::reason_name(expected),index);++diagnostic_cases;
}
__attribute__((noinline)) void domain_diagnostics() {
    reset_case();Campaign c(Controller::ledger,false,"planning-gate-sdk");initialize(c);
    const auto camps=cambc::vision::camps();
    const auto forecast=[&](Domain::Report* r){return c.backpressure_domain_.forecast_supported(c.personal_,camps,{},true,r);};
    diagnostic("healthy",forecast,Domain::Reason::none);
    actor.health=-1;diagnostic("negative_owned_health",forecast,Domain::Reason::numeric);actor.health=actor.max_health;
    const auto maximum=actor.max_health;actor.max_health=std::numeric_limits<float>::quiet_NaN();
    diagnostic("raw_maximum_nan",[&](Domain::Report* r){return c.backpressure_domain_.raw_supported(r);},Domain::Reason::raw);actor.max_health=maximum;
    diagnostic("item_guard",forecast,Domain::Reason::fuel_items,0,0,64999);
    diagnostic("group_guard",forecast,Domain::Reason::fuel_groups,0,unsigned(cambc::item_type::count)+1);
    const double hp=c.personal_.groups[0].full_health;c.personal_.groups[0].full_health=1e12;
    diagnostic("clear_turn_bound",forecast,Domain::Reason::clearbounds,0);c.personal_.groups[0].full_health=hp;
    const auto observation_round=c.personal_.observations[0].round;c.personal_.observations[0].round=current_round+1;
    diagnostic("future_observation",forecast,Domain::Reason::numeric,0);c.personal_.observations[0].round=observation_round;
    const unsigned old_bag=bag;bag=400000000;
    diagnostic("continuation_int_bound",forecast,Domain::Reason::continuation);bag=old_bag;
    diagnostic("view_guard",[&](Domain::Report* r){return Domain::views_supported(camps,{},r);},Domain::Reason::fuel_views,0,0,44999);
    show_enemy=true;const auto enemies=cambc::vision::champions(cambc::team::enemy);verify(enemies.size()==1);
    enemy.health=std::numeric_limits<float>::quiet_NaN();
    diagnostic("visible_enemy_nan",[&](Domain::Report* r){return Domain::views_supported(camps,enemies,r);},Domain::Reason::views,int(camps.size()));
    enemy.health=enemy.max_health;show_enemy=false;
    diagnostic("fresh_report_reset",forecast,Domain::Reason::none);
}
Campaign* gate_campaign=nullptr;bool gate_low=false;unsigned gate_lines=0;
std::int64_t gate_after_shop() {
    using P=PlanningGateTrace::Point;
    if(gate_campaign && gate_campaign->planning_trace_.current_.round==current_round &&
       gate_campaign->planning_trace_.current_.fuel[unsigned(P::shop)]>=0 &&
       gate_campaign->planning_trace_.current_.fuel[unsigned(P::vision)]<0){if(!gate_low)gate_lines=gate_campaign->telemetry_.lines_;gate_low=true;}
    return gate_low?28999:-1;
}
__attribute__((noinline)) void planning_trace() {
    using P=PlanningGateTrace::Point;using G=PlanningGateTrace::Gate;
    reset_case();PlanningGateTrace trace;Telemetry log;
    fuel=28999;trace.begin(3);trace.mark(P::vision);trace.defer(G::vision_fuel,"below_30000");
    trace.defer(G::views,"must_not_replace_pending");trace.flush(log);verify(log.lines_==0 && trace.pending_);
    ++current_round;fuel=65000;
    std::printf("BP_GATE_BEGIN {\"case\":\"bounded_trace\",\"oldRound\":%u}\n",current_round-1);
    trace.flush(log);verify(log.lines_==1 && !trace.pending_);trace.flush(log);verify(log.lines_==1);
    trace.begin(3);trace.defer(G::vision_fuel,"duplicate");trace.flush(log);verify(log.lines_==1);
    ++current_round;trace.defer(G::views,"mixed_round");trace.flush(log);verify(log.lines_==2);
    for(unsigned gate=2;gate<unsigned(G::count);++gate){trace.begin(3);trace.defer(static_cast<G>(gate),"bounded");trace.flush(log);}
    const unsigned count=log.lines_;verify(count==unsigned(G::count));
    for(unsigned gate=0;gate<unsigned(G::count);++gate){trace.defer(static_cast<G>(gate),"duplicate");trace.flush(log);}
    verify(log.lines_==count && !trace.pending_);
    std::printf("BP_GATE_RESULT {\"case\":\"bounded_trace\",\"events\":%u}\n",count);++planning_trace_cases;
    reset_case();Campaign c(Controller::ledger,false,"planning-gate-cold");gate_campaign=&c;gate_low=false;prediction_fuel_probe=gate_after_shop;
    for(unsigned i=0;i<128 && !gate_low;++i){c.turn();if(!gate_low)++current_round;}
    verify(gate_low && c.initialized_ && !c.active_ && actions==0 && moves==0 && c.telemetry_.lines_==gate_lines && c.planning_trace_.pending_);
    verify(c.conversion_snapshot_.valid && c.conversion_snapshot_.round==current_round && c.conversion_snapshot_.scoreRound==current_round);
    const unsigned observed=current_round;++current_round;prediction_fuel_probe=nullptr;gate_campaign=nullptr;gate_low=false;fuel=1000000;
    std::printf("BP_GATE_BEGIN {\"case\":\"cold_turn\",\"oldRound\":%u}\n",observed);
    c.turn();
    verify(c.planning_trace_.current_.round==current_round && c.conversion_snapshot_.round==current_round);
    std::printf("BP_GATE_RESULT {\"case\":\"cold_turn\",\"events\":1,\"freshRound\":%u,\"observedRound\":%u}\n",current_round,observed);++planning_trace_cases;
}
__attribute__((noinline)) void collision_counter() {
    reset_case();Campaign c(Controller::ledger,false,"backpressure-sdk");initialize(c);
    bp_hidden_collision=true;verify(!c.move_one({bp_axis(3),bp_axis(2)}));
    verify(c.last_move_status()==Executor::MoveStatus::hidden_collision);
    verify(c.telemetry_.collisions==1 && static_cast<Telemetry*>(&c.telemetry_)->collisions==1);
    const auto fields=c.telemetry_.summary_fields(true);
    verify(fields.str().find("\"hidden_collisions\":1")!=std::string_view::npos);
    c.telemetry_.summary(true);++shared_cases;
}
namespace queue_now=plan3_backpressure;
template<class A,class B> void same_queue_score(const A& a,const B& b) {
    verify(a.numerator==b.numerator && a.denominator==b.denominator);
}
template<class A,class B> void same_queue_choice(const A& a,const B& b) {
    verify(a.present==b.present && a.row==b.row && a.key==b.key);same_queue_score(a.score,b.score);
}
void empty_queue_result(const queue_now::PairResult& value) {
    verify(value.rows.empty() && value.rowCount==0 && value.eligibleCount==0 && value.rejectedCount==0 && value.omittedDependencyCount==0);
    verify(value.queues==std::array<std::uint64_t,4>{});
    const queue_now::Choice empty{};
    same_queue_choice(value.active,empty);same_queue_choice(value.control,empty);same_queue_choice(value.requiredRenew,empty);
}
__attribute__((noinline)) std::unique_ptr<queue_now::Input> actual_queue_input() {
    reset_case();Campaign c(Controller::ledger,false,"queue-self-check");initialize(c);verify(preflight(c));
    const auto state=c.information();auto context=c.forecast_context(state);
    const auto prefix=two_trip::Menu::single_prefix(c.personal_,c.paths_,cambc::self().position());
    const auto jobs=c.menu(context,false,cambc::vision::camps(),prefix);
    return std::make_unique<queue_now::Input>(queue_now::map_menu(c.personal_,state,jobs,false,true,[] {return true;}));
}
__attribute__((noinline)) void queue_self_checks() {
    std::vector<queue_now::Input> inputs;
    queue_now::Input input;input.snapshotRound=input.observationRound=100;input.roundLimit=300;
    input.width=input.height=100;input.winningGems=1000;input.candidatePrefix=true;
    inputs.push_back(input); // Known empty table.
    const auto add=[&](int kind,int first,int second,int tau,int converted,int altar=9999) {
        auto& job=input.jobs[input.jobCount++];job.key={kind,first,second,kind==3?-1:altar,-1};
        job.nominalTau=tau;job.converted=converted;job.sourceDomainValidated=true;job.renewalRequired=kind==3;
    };
    input.carriedGems=10;input.groupCount=2;
    input.groups[0]={1,100,queue_now::Lifecycle::alive,100,queue_now::State::known};
    input.groups[1]={2,5,queue_now::Lifecycle::reward,100,queue_now::State::known};
    add(0,1,-1,20,110);add(2,-1,-1,1,10);inputs.push_back(input); // Exact12-guard fork.
    input.jobs[0].fullClearAdmitted=false;add(3,-1,-1,1,0);inputs.push_back(input); // Rejected slot0, Bank1, Renew2.
    input=inputs[0];input.winningGems=std::numeric_limits<int>::max();input.groupCount=32;
    for(unsigned g=0;g<32;++g)input.groups[g]={int(g+1),1000000000,queue_now::Lifecycle::alive,100,queue_now::State::known};
    for(unsigned g=0;g<16;++g)add(0,int(g+1),-1,64,1000000000);
    for(unsigned g=16;g<32;g+=2)add(1,int(g+1),int(g+2),64,2000000000);
    add(3,-1,-1,1,0);for(int altar=9000;altar<9003;++altar)add(2,-1,-1,1,0,altar);
    verify(input.jobCount==28);inputs.push_back(input); // Full table; dot products exceed64bits.
    input=inputs[1];input.groups[0].observedRound=99;inputs.push_back(input);
    input=inputs[1];input.state=queue_now::State::cutoff;inputs.push_back(input);
    input=inputs[1];input.state=queue_now::State::unavailable;inputs.push_back(input);
    input=inputs[1];--input.observationRound;inputs.push_back(input);
    input=inputs[1];input.jobs[1]=input.jobs[0];inputs.push_back(input);
    input=inputs[1];input.groups[1]=input.groups[0];inputs.push_back(input);
    input=inputs[1];input.jobs[1].state=queue_now::State::cutoff;inputs.push_back(input);
    input=inputs[1];input.groups[1].state=queue_now::State::cutoff;inputs.push_back(input);
    input=inputs[1];input.submitting=true;inputs.push_back(input);
    input=inputs[1];input.carriedGems=std::numeric_limits<int>::max();inputs.push_back(input);
    const auto actual=actual_queue_input();verify(actual->state==queue_now::State::known && actual->jobCount>1);inputs.push_back(*actual);
    unsigned focused=0;
    for(unsigned n=0;n<inputs.size();++n) {
        const auto& current=inputs[n];unsigned calls=0;
        auto full=std::make_unique<queue_now::PairResult>(queue_now::evaluate(current,[&] {++calls;return true;}));
        if(full->state==queue_now::State::known)verify(full->rows.size()==current.jobCount);
        else empty_queue_result(*full);
        if(n==1)verify(calls==12 && full->active.row==0 && full->control.row==1);
        if(n==2)verify(full->rows[0].reason==queue_now::Reason::full_clear && full->active.row==1 && full->requiredRenew.row==2);
        if(n==3)verify(full->rows[16].pressure.numerator>std::numeric_limits<std::uint64_t>::max());
        for(unsigned limit=0;limit<calls;++limit) {
            unsigned seen=0;
            auto stopped=std::make_unique<queue_now::PairResult>(queue_now::evaluate(current,[&] {return seen++<limit;}));
            verify(seen>limit && stopped->state==queue_now::State::cutoff);empty_queue_result(*stopped);++queue_cutoffs;
            if(n==1 && (limit==3 || limit==10 || limit==11))++focused;
        }
        ++queue_cases;
    }
    verify(focused==3);
    std::printf("BP_QUEUE_SELF_RESULT {\"cases\":%u,\"cutoffs\":%u,\"focusedCuts\":%u}\n",queue_cases,queue_cutoffs,focused);
}
__attribute__((noinline)) void guarded_mapping() {
    reset_case();Campaign c(Controller::ledger,false,"backpressure-sdk");initialize(c);verify(preflight(c));
    const auto state=c.information();auto context=c.forecast_context(state);
    const std::vector<JobForecast> jobs{context.acquisition(c.group_index(bp_key(0)))};
    struct Ready {
        unsigned calls=0,limit=0;
        explicit Ready(unsigned value):limit(value){}
        Ready(const Ready&)=delete;
        bool operator()(){return ++calls<limit;}
    } cut(2),full(1000000);
    const auto partial=plan3_backpressure::map_menu(c.personal_,state,jobs,false,true,cut);
    verify(cut.calls==2 && partial.state==plan3_backpressure::State::cutoff);
    const auto cleared=plan3_backpressure::evaluate(partial,[] {return true;});
    verify(cleared.state==plan3_backpressure::State::cutoff);empty_queue_result(cleared);
    const auto input=plan3_backpressure::map_menu(c.personal_,state,jobs,false,true,full);
    verify(input.state==plan3_backpressure::State::known && full.calls>=2);
    Ready boundary(full.calls+1);
    const auto selected=plan3_backpressure::select_menu(c.personal_,state,jobs,false,false,true,c.context_,false,boundary);
    verify(boundary.calls==full.calls+1 && selected.pair.state==plan3_backpressure::State::cutoff);empty_queue_result(selected.pair);
    verify(selected.intent.status!=IntentStatus::selected && selected.companion.status!=IntentStatus::selected);
    ++shared_cases;
}
__attribute__((noinline)) void formatter_caps() {
    BackpressureTelemetry over;
    verify(over.format_line(1,"varg","candidate",std::string(600,'x')).empty());
    verify(over.lines_==0 && over.oversize_omitted==1 && !over.format_line(1,"varg","candidate","").empty());
    BackpressureTelemetry t;t.reserve_context_line();
    auto line=t.format_line(1000000,"vesper_vale","start",",\"profile\":\"backpressure-sdk\"");verify(!line.empty());
    for(unsigned i=0;i<31;++i)verify(!t.format_line(i,"vesper_vale","candidate",",\"candidate_prefix\":true").empty());
    verify(t.format_line(1,"vesper_vale","candidate","").empty());
    const char* fields=",\"bp\":1,\"arm\":1,\"Q\":[68719476704,68719476704,2147483647,0],\"s\":[0,0,2147483647,0],\"D\":128,\"a\":[\"295147905179352825855\",128],\"r\":[\"2147483647\",128],\"ids\":[[1,9998,9999,9997,-1],[2,-1,-1,9997,-1]],\"omitted\":32,\"prefix\":true,\"assumed\":true";
    line=t.format_line(1000000,"vesper_vale","backpressure-context",fields,false,true);
    verify(!line.empty() && line.size()<=512);maximum_format_bytes=unsigned(line.size());
    std::fputs("BP_FORMAT ",stdout);std::fwrite(line.view().data(),1,line.size(),stdout);
    verify(t.format_line(1,"vesper_vale","backpressure-context",fields,false,true).empty());
    for(unsigned i=0;i<62;++i)verify(!t.format_line(i,"vesper_vale","summary",",\"final\":false").empty());
    verify(t.lines_==95 && t.format_line(1,"vesper_vale","summary","").empty());
    const auto final=t.format_line(1000000,"vesper_vale","summary",",\"final\":true",true);
    verify(!final.empty() && t.lines_==96 && t.format_line(1,"vesper_vale","summary","",true).empty());
    std::fputs("BP_FORMAT ",stdout);std::fwrite(final.view().data(),1,final.size(),stdout);
}
// Actual current Campaign selection, followed by explicitly modeled own channel
// snapshots. These state transitions are host fixtures, never native evidence.
__attribute__((noinline)) void current_conversion_cases() {
    for(bool rate:{false,true})for(unsigned mode=0;mode<9;++mode) {
        reset_case();Campaign c(Controller::ledger,false,"backpressure-current-sdk");
        arm(c,rate);initialize(c);
        c.choose(c.information(),false,cambc::vision::camps());
        verify(c.active_ && c.active_->id.kind==(rate?JobKind::bank:JobKind::cycle));
        const unsigned predicted=unsigned(c.active_->nominal()->deltaS);
        const unsigned actual=predicted+(mode==8?2:0);
        bag=actual;c.stage_=2;actor.state=1|(1<<2);
        c.last_score_round_=current_round;c.last_team_score_=own_score;
        if(mode==6)--c.last_score_round_;
        c.finish_observation();
        verify(c.conversion_snapshot_.bankingJob==c.active_->id);
        current_round+=mode==2?2:1;bag=0;actor.state=1|(1<<3)|(1<<4);
        if(mode==3)actor.state|=1<<9;
        if(mode==4)++c.start_round_;
        if(mode==5)actor.state&=~1;
        own_score+=mode==2?1:actual+(mode==1?5:0);
        c.score_observations();
        const bool physical=mode==0 || mode==1 || mode==4 || mode==7 || mode==8;
        const bool completed=physical && mode!=4;
        verify(c.telemetry_.converted==(physical?actual:0));
        verify(c.telemetry_.completed==unsigned(completed));
        verify(bool(c.active_)!=completed);
        verify(c.telemetry_.conversion_error==(mode==8?2:0));
        const unsigned count=c.telemetry_.converted;
        c.score_observations();verify(c.telemetry_.converted==count);
        std::printf("BP_CURRENT_CONVERSION {\"arm\":%u,\"mode\":%u,\"converted\":%u,\"completed\":%u,\"conversionError\":%.0f}\n",
            unsigned(rate),mode,c.telemetry_.converted,c.telemetry_.completed,c.telemetry_.conversion_error);
    }
}

}
int main() {
    std::printf("BP_OBJECT_BYTES {\"campaign\":%zu,\"pair\":%zu,\"input\":%zu}\n",sizeof(Campaign),sizeof(plan3_backpressure::PairResult),sizeof(plan3_backpressure::Input));std::fflush(stdout);
    verify(cambc::detail::absolute_team(cambc::team::ally)==int(bp_side()) && cambc::detail::absolute_team(cambc::team::enemy)==1-int(bp_side()));
    if(const char* bad=std::getenv("P3_BP_BAD_COST")){initial_bad_cost(unsigned(std::atoi(bad)));return 0;}
    queue_self_checks();current_conversion_cases();
    forecast_parity();for(unsigned mode=0;mode<=14;++mode)paired(mode);
    for(unsigned stop=1;stop<=64;++stop)paired(0,stop);
    single_turn(false);single_turn(true);
    prediction_tail_cases(false);prediction_tail_cases(true);
    channels_and_purchases();raw_overflow();negative_health();domain_diagnostics();planning_trace();collision_counter();guarded_mapping();formatter_caps();
    std::printf("BACKPRESSURE_SDK_RESULT {\"status\":\"passed\",\"side\":%u,\"checks\":%u,\"sdkCases\":%u,\"pairedCases\":%u,\"forecastCases\":%u,\"overflowCases\":%u,\"sharedCases\":%u,\"diagnosticCases\":%u,\"planningTraceCases\":%u,\"maximumFormatBytes\":%u,\"engineCalls\":0}\n",bp_side(),checks,sdk_cases,paired_cases,forecast_cases,overflow_cases,shared_cases,diagnostic_cases,planning_trace_cases,maximum_format_bytes);
}
