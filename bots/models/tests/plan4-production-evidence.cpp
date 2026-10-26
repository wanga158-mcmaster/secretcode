#include "../common/production-evidence.h"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace plan4;
namespace rt=plan4::runtime;
namespace {
rt::ProductionObservation observation(int round=10) {
    rt::ProductionObservation out;out.clock={round,{3}};out.own_provenance={round,Source::own_state,Status::exact_now};
    out.alive=true;out.health=50;out.maximum_health=100;out.group_id=20;out.group_visible=true;
    out.group_lifecycle=Lifecycle::living;out.group_provenance={round,Source::personal_vision,Status::exact_now};return out;
}
rt::PhaseStart start(rt::ProductionPhase phase,int nominal=4) {return {phase,3,1,1,10,nominal,0,2,false,20};}
rt::PhaseClock clock(int round=10) {return {3,round,0,2,true,true,false,true};}
void frozen_mask_arrival_needs_exact_current_own_source() {
    rt::ProductionEndpointEvidence evidence;auto request=start(rt::ProductionPhase::travel);auto own=observation();
    assert(evidence.begin(request,own));assert(evidence.inspect(request,own,false).verdict==rt::ProductionVerdict::pending);
    own.at_requested_goal=true;auto end=evidence.inspect(request,own,true);
    assert(end.verdict==rt::ProductionVerdict::complete && end.kind==rt::EndpointKind::travel_arrival);
    own.own_provenance.observed_round=9;assert(evidence.inspect(request,own,true).verdict==rt::ProductionVerdict::source);
    own=observation();own.at_requested_goal=true;auto wrong=request;++wrong.group_id;
    assert(evidence.inspect(wrong,own,true).verdict==rt::ProductionVerdict::source);
    wrong=request;++wrong.nominal_duration;assert(evidence.inspect(wrong,own,true).verdict==rt::ProductionVerdict::source);
    own.channel_active=true;assert(evidence.inspect(request,own,true).verdict==rt::ProductionVerdict::ambiguous);
}
void authoritative_world_clear_is_not_a_capture_or_self_work_receipt() {
    rt::ProductionEndpointEvidence evidence;auto request=start(rt::ProductionPhase::clear);auto own=observation();
    assert(evidence.begin(request,own));own.group_lifecycle=Lifecycle::cleared;
    assert(evidence.inspect(request,own,true).verdict==rt::ProductionVerdict::ambiguous);
    evidence.targeted_action(false,10);assert(!evidence.has_targeted_action());
    evidence.targeted_action(true,10);auto end=evidence.inspect(request,own,true);
    assert(end.verdict==rt::ProductionVerdict::complete && end.kind==rt::EndpointKind::group_clear && end.provenance.source==Source::personal_vision);
    own.group_visible=false;assert(evidence.inspect(request,own,true).verdict==rt::ProductionVerdict::lost_sight);
    own=observation();own.group_lifecycle=Lifecycle::respawning;
    assert(evidence.inspect(request,own,false).verdict==rt::ProductionVerdict::ambiguous);
    own.group_lifecycle=Lifecycle::captured;assert(evidence.inspect(request,own,false).verdict==rt::ProductionVerdict::ambiguous);
    own=observation(11);own.group_lifecycle=Lifecycle::cleared;
    assert(evidence.inspect(request,own,false).verdict==rt::ProductionVerdict::ambiguous);
    own=observation();own.group_lifecycle=Lifecycle::cleared;own.channel_active=true;
    assert(evidence.inspect(request,own,false).verdict==rt::ProductionVerdict::ambiguous);
    own=observation();own.group_provenance.status=Status::derived;assert(!evidence.begin(request,own));
    assert(!evidence.begin(start(rt::ProductionPhase::capture),observation()));
}
void restore_requires_owner_start_spawn_full_health_and_fixed_maximum() {
    rt::ProductionEndpointEvidence evidence;auto request=start(rt::ProductionPhase::restore);auto own=observation();
    assert(evidence.begin(request,own));own.health=100;
    assert(evidence.inspect(request,own,false).verdict==rt::ProductionVerdict::pending);
    own.on_own_spawn=true;
    assert(evidence.inspect(request,own,true).verdict==rt::ProductionVerdict::pending);
    auto end=evidence.inspect(request,own,false);assert(end.verdict==rt::ProductionVerdict::complete && end.kind==rt::EndpointKind::own_restored);
    own.channel_active=true;assert(evidence.inspect(request,own,false).verdict==rt::ProductionVerdict::pending);
    own.channel_active=false;own.maximum_health=120;own.health=120;
    assert(evidence.inspect(request,own,false).verdict==rt::ProductionVerdict::ambiguous);
    own=observation();own.health=100;assert(!evidence.begin(request,own));
    own=observation();request.emergency=true;assert(!evidence.begin(request,own));
}
void tracker_preserves_groups_and_censors_interruptions() {
    for(int kind=0;kind<5;++kind){rt::PhaseTracker tracker;auto request=start(rt::ProductionPhase::clear);auto now=clock();
        auto begun=tracker.begin_phase(request,now);assert(begun.has_start && begun.start.group_id==20);
        now.round=11;
        switch(kind){case 0:now.alive=false;break;case 1:now.emergency=true;break;case 2:++now.stat_epoch;break;case 3:++now.mode;break;case 4:now.premises_visible=false;break;}
        auto ended=tracker.observe_turn(now);assert(ended.has_receipt && !ended.receipt.completed && ended.receipt.group_id==20);
    }
    rt::PhaseTracker tracker;auto request=start(rt::ProductionPhase::travel);tracker.begin_phase(request,clock(),4);
    auto other=request;other.group_id=21;
    assert(tracker.finish({other,10,rt::EndpointKind::travel_arrival,{10,Source::own_state,Status::exact_now},false},clock()).result==rt::TrackerResult::mismatch);
    auto ended=tracker.finish({request,10,rt::EndpointKind::travel_arrival,{10,Source::own_state,Status::exact_now},false},clock());
    assert(ended.receipt.completed && ended.receipt.end_round==ended.receipt.start_round && ended.receipt.group_id==20);
    assert(!tracker.finish({request,10,rt::EndpointKind::travel_arrival,{10,Source::own_state,Status::exact_now},false},clock()).has_receipt);
}
void restore_nominal_matches_uncorrected_common_first_macro() {
    auto info=std::make_unique<Information>();auto forecast=std::make_unique<Forecast>();auto state=std::make_unique<SimulationState>();Menu menu;
    Job renew;renew.kind=JobKind::renew;info->round=10;info->own.id=3;info->own.maximum_health=100;info->own_spawn={0,0};
    for(bool spawn:{false,true})for(int hp:{5,20,39,60,99})for(int recall:{0,3,5})for(int escape:{0,2}){
        info->own.position=spawn?Point{0,0}:Point{5,5};info->own.on_spawn=spawn;info->own.health=float(hp);info->own.recall_safe=true;
        info->own.escape_rounds=escape;info->own.escape_incoming=0;info->settings.recall_rounds=recall;
        simulate_first_into(*info,renew,menu,*forecast,*state);
        assert(forecast->abort_reason==AbortReason::none && rt::nominal_restore(*info)==forecast->final_round-info->round);
    }
    info->own.on_spawn=false;info->own.position={5,5};info->own.health=70;info->own.escape_incoming=5;info->own.escape_rounds=2;
    simulate_first_into(*info,renew,menu,*forecast,*state);assert(rt::nominal_restore(*info)==forecast->final_round-info->round);
    info->own.recall_safe=false;assert(rt::nominal_restore(*info)==-1);info->own.recall_safe=true;
    info->own.health=10;assert(rt::nominal_restore(*info)==-1);
    info->own.health=100;assert(rt::nominal_restore(*info)==0);
    info->own.health=50;info->own.on_spawn=true;info->settings.spawn_heal_fraction=0;assert(rt::nominal_restore(*info)==-1);
}
}
int main(){frozen_mask_arrival_needs_exact_current_own_source();authoritative_world_clear_is_not_a_capture_or_self_work_receipt();
    restore_requires_owner_start_spawn_full_health_and_fixed_maximum();tracker_preserves_groups_and_censors_interruptions();
    restore_nominal_matches_uncorrected_common_first_macro();
    std::puts("production evidence: 5 endpoint/censor scenarios plus61 nominal-M0 comparisons passed; capture attribution unsupported");}
