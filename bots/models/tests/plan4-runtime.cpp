#include "../habitat/src/runtime.h"
#include "../chronicle/src/runtime.h"
#include "../governor/src/runtime.h"
#include <cassert>
#include <iostream>
#include <memory>
using namespace plan4;
int distance_metric(Point a,Point b,const void*) { return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y)); }
struct Batch {
    int calls = 0, fault = 0;
    void evaluate_into(const Information& info, const Menu& menu, Evaluation& out) {
        ++calls; plan4::evaluate_into(info, menu, out);
        out.menu.count = 0; out.chosen = 999; // The model never owns selection/menu.
        if (fault == 1) out.evaluated_count = menu.count + 1;
        if (fault == 2 && out.evaluated_count) out.forecasts[0].budget_exhausted = true;
    }
};
bool same_forecast(const Forecast& a,const Forecast& b) {
    if (a.event_count!=b.event_count || a.macros_started!=b.macros_started || a.final_round!=b.final_round ||
        a.first_conversion_round!=b.first_conversion_round || a.our_secured!=b.our_secured || a.enemy_secured!=b.enemy_secured ||
        a.final_bag!=b.final_bag || a.final_gold!=b.final_gold || a.final_health!=b.final_health || a.terminal_z!=b.terminal_z ||
        a.terminal_unknown!=b.terminal_unknown || a.stock_unknown!=b.stock_unknown || a.travel_unknown!=b.travel_unknown ||
        a.lock_unknown!=b.lock_unknown || a.truncated!=b.truncated || a.completed_conversion!=b.completed_conversion ||
        a.no_conversion!=b.no_conversion || a.budget_exhausted!=b.budget_exhausted || a.abort_reason!=b.abort_reason) return false;
    for (int n=0;n<a.event_count;++n) {
        const auto& x=a.events[n]; const auto& y=b.events[n];
        if (x.kind!=y.kind || x.start.round!=y.start.round || x.start.phase!=y.start.phase || x.end.round!=y.end.round ||
            x.end.phase!=y.end.phase || x.group_id!=y.group_id || x.bag_before!=y.bag_before || x.bag_after!=y.bag_after ||
            x.secured!=y.secured || x.gold_after!=y.gold_after || x.health_after!=y.health_after || x.status!=y.status) return false;
    }
    return true;
}
std::unique_ptr<Information> diagnostic_fixture() {
    auto out=std::make_unique<Information>(); auto& i=*out;
    i.round=2; i.own.id=0; i.own.health=i.own.maximum_health=1000; i.travel={distance_metric,nullptr};
    i.group_count=i.altar_count=1; i.altars[0].id=90; i.altars[0].position={4,0}; i.altars[0].lock_known=true;
    auto& g=i.groups[0]; g.id=10; g.position={2,0}; g.gems=24; g.respawn_rounds=30;
    g.full_clear_rounds=g.clear_rounds=2; g.full_health=g.health=100; g.incoming_damage=g.full_incoming_damage=1;
    g.visible=g.all_spawns_visible=g.occupancy_known=true; g.lifecycle=Lifecycle::living;
    g.provenance=g.health_provenance={2,Source::personal_vision,Status::exact_now}; return out;
}
Menu diagnostic_menu() {
    Menu menu; menu.count=menu.single_count=1; menu.jobs[0].kind=JobKind::farm; menu.jobs[0].group=menu.jobs[0].altar=0; return menu;
}
void governor_diagnostics() {
    auto input=diagnostic_fixture(); auto& i=*input; i.round=100;
    i.groups[0].provenance.observed_round=i.groups[0].health_provenance.observed_round=i.round;
    runtime::Governor active,control(false); auto menu=diagnostic_menu();
    runtime::CombatKit kit{{i.round,{i.own.id}},1,1,10,2,0,0,42,0,true};
    for (auto* model:{&active,&control}) { model->observe_kit(42); model->observe_combat(kit); model->observe_snapshot(i); }
    auto result=std::make_unique<Evaluation>(),other=std::make_unique<Evaluation>(); auto old=std::make_unique<Forecast>();
    runtime::evaluate_model(i,menu,active,*result);
    assert(active.model_evidence().reported && !active.model_evidence().supported && !active.model_evidence().coupling_enabled && active.model_evidence().samples==0);
    for (unsigned n=1;n<=3;++n) {
        runtime::PhaseStart start{runtime::ProductionPhase::travel,0,n,1,int(20*n),10,0,0,false};
        runtime::PhaseReceipt finish{runtime::ProductionPhase::travel,0,n,1,int(20*n),int(20*n+14),10,0,0,0,true,true,false,runtime::PhaseCensor::none,4,4,true};
        for (auto* model:{&active,&control}) { assert(model->phase_started(start)); model->phase_completed(finish); }
        runtime::evaluate_model(i,menu,active,*result);
        assert(active.model_evidence().samples==int(n));
        assert(active.model_evidence().supported==(n==3));
    }
    runtime::evaluate_model(i,menu,control,*other);
    const auto evidence=active.model_evidence();
    assert(evidence.supported && evidence.coupling_enabled && evidence.paths==1 && evidence.unknown>0 && evidence.work>0);
    assert(control.model_evidence().supported && !control.model_evidence().coupling_enabled && control.model_evidence().samples==3);
    plan4::predict_into(i,menu.jobs[0],menu,*old,active.state().hooks()); assert(same_forecast(result->forecasts[0],*old));
    plan4::predict_into(i,menu.jobs[0],menu,*old,control.state().hooks()); assert(same_forecast(other->forecasts[0],*old));
    runtime::evaluate_model(i,menu,active,*result); assert(active.model_evidence().work==evidence.work && active.model_evidence().paths==1);
    runtime::PhaseStart safety{runtime::ProductionPhase::clear,0,4,2,80,5,0,0,false};
    runtime::PhaseReceipt censored{runtime::ProductionPhase::clear,0,4,2,80,88,5,0,0,0,true,true,true,runtime::PhaseCensor::safety,0,0,true};
    assert(active.phase_started(safety)); active.phase_completed(censored); runtime::evaluate_model(i,menu,active,*result);
    assert(active.model_evidence().samples==3 && active.state().entry(governor::Phase5::clear).completions==0);
    auto stale=kit; --stale.clock.round; active.observe_combat(stale); runtime::evaluate_model(i,menu,active,*result);
    assert(!active.model_evidence().supported && !active.model_evidence().coupling_enabled);
    plan4::predict_into(i,menu.jobs[0],menu,*old); assert(same_forecast(result->forecasts[0],*old));
    control.observe_combat(stale); runtime::evaluate_model(i,menu,control,*other); assert(same_forecast(other->forecasts[0],*old));
    active.observe_combat(kit); active.observe_snapshot(i); i.work_budget={[](const void*){return false;},nullptr};
    runtime::evaluate_model(i,menu,active,*result);
    assert(result->budget_exhausted && !active.model_evidence().coupling_enabled && active.model_evidence().paths==0);
    i.work_budget={}; active.observe_kit(43,1); kit.version=43;kit.stat_epoch=1; active.observe_combat(kit);active.observe_snapshot(i);
    runtime::evaluate_model(i,menu,active,*result);
    assert(active.model_evidence().samples==0 && !active.model_evidence().supported && active.model_evidence().revision>evidence.revision);
}
void habitat_diagnostics() {
    auto input=diagnostic_fixture(); auto& i=*input; auto menu=diagnostic_menu(); runtime::Habitat active,control(false);
    for (auto* model:{&active,&control}) { model->observe_kit(42); model->observe_snapshot(i); }
    runtime::ProducerFrame producers; producers.count=1;
    for (int round=1;round<=2;++round) {
        producers.clock={round,{0}}; producers.samples[0]={2,10,{2,0},{round,Source::personal_vision,Status::exact_now},true,true,false,runtime::ProductionEvidence::visible_cleared_spawn};
        active.observe_producers(producers); control.observe_producers(producers);
    }
    auto result=std::make_unique<Evaluation>(),other=std::make_unique<Evaluation>(); auto old=std::make_unique<Forecast>();
    runtime::evaluate_model(i,menu,active,*result); runtime::evaluate_model(i,menu,control,*other);
    const auto evidence=active.model_evidence();
    assert(evidence.reported && evidence.supported && evidence.coupling_enabled && evidence.samples==1 && evidence.unknown>0 && evidence.paths==1);
    assert(control.model_evidence().supported && !control.model_evidence().coupling_enabled && control.model_evidence().samples==1);
    habitat::predict_into(i,menu.jobs[0],menu,active.state(),true,*old); assert(same_forecast(result->forecasts[0],*old));
    habitat::predict_into(i,menu.jobs[0],menu,control.state(),false,*old); assert(same_forecast(other->forecasts[0],*old));
    runtime::evaluate_model(i,menu,active,*result); assert(active.model_evidence().work==evidence.work && active.model_evidence().paths==1);
    menu.single_count=0; runtime::evaluate_model(i,menu,active,*result);
    assert(active.model_evidence().supported && !active.model_evidence().coupling_enabled && result->forecasts[0].our_secured==24);
    menu.single_count=1; ++i.round; runtime::evaluate_model(i,menu,active,*result);
    assert(!active.model_evidence().supported && !active.model_evidence().coupling_enabled);
    plan4::predict_into(i,menu.jobs[0],menu,*old); assert(same_forecast(result->forecasts[0],*old));
    runtime::evaluate_model(i,menu,control,*other); assert(same_forecast(other->forecasts[0],*old));
    i.public_layout_truncated=true; active.observe_snapshot(i); runtime::evaluate_model(i,menu,active,*result);
    assert(active.model_evidence().reported && !active.model_evidence().supported && active.model_evidence().reason==2);
    i.public_layout_truncated=false; active.observe_snapshot(i); i.work_budget={[](const void*){return false;},nullptr};
    runtime::evaluate_model(i,menu,active,*result);
    assert(result->budget_exhausted && !active.model_evidence().supported && active.model_evidence().paths==0 && active.model_evidence().work==0);
}
void chronicle_diagnostics() {
    auto input=diagnostic_fixture(); auto& i=*input; auto& group=i.groups[0]; auto menu=diagnostic_menu();
    runtime::Chronicle active,control(false); i.round=10; group.lifecycle=Lifecycle::respawning; group.respawn_remaining=30;
    group.provenance.observed_round=10; active.observe_snapshot(i);control.observe_snapshot(i);
    runtime::CapturedGroup receipt{10,10,30,{10,Source::personal_vision,Status::exact_now},true};
    assert(active.capture_receipt(i,receipt)); assert(!control.capture_receipt(i,receipt));
    i.round=12; group.visible=false;group.lifecycle=Lifecycle::unknown;group.respawn_remaining=-1;group.provenance={};
    active.observe_snapshot(i);control.observe_snapshot(i);
    auto result=std::make_unique<Evaluation>(),other=std::make_unique<Evaluation>(); auto old=std::make_unique<Forecast>();
    runtime::evaluate_model(i,menu,active,*result); runtime::evaluate_model(i,menu,control,*other);
    const auto evidence=active.model_evidence();
    assert(evidence.reported && evidence.supported && evidence.coupling_enabled && evidence.samples==1 && evidence.paths==1 && evidence.work>0);
    assert(control.model_evidence().reported && !control.model_evidence().supported && !control.model_evidence().coupling_enabled && control.model_evidence().samples==0);
    active.state().predict_into(i,menu.jobs[0],menu,*old);assert(same_forecast(result->forecasts[0],*old));
    control.state().predict_into(i,menu.jobs[0],menu,*old);assert(same_forecast(other->forecasts[0],*old));
    runtime::evaluate_model(i,menu,active,*result); assert(active.model_evidence().work==evidence.work && active.model_evidence().paths==1);
    ++i.round; runtime::evaluate_model(i,menu,active,*result); assert(!active.model_evidence().supported && !active.model_evidence().coupling_enabled);
    plan4::predict_into(i,menu.jobs[0],menu,*old); assert(same_forecast(result->forecasts[0],*old));
    runtime::evaluate_model(i,menu,control,*other); assert(same_forecast(other->forecasts[0],*old));
    group.visible=true;group.lifecycle=Lifecycle::living;group.provenance={i.round,Source::personal_vision,Status::exact_now};
    active.observe_snapshot(i);runtime::evaluate_model(i,menu,active,*result);
    assert(!active.model_evidence().supported && active.model_evidence().samples==0 && !active.model_evidence().coupling_enabled);
    active.observe_snapshot(i);i.work_budget={[](const void*){return false;},nullptr};runtime::evaluate_model(i,menu,active,*result);
    assert(result->budget_exhausted && !active.model_evidence().coupling_enabled && active.model_evidence().paths==0 && active.model_evidence().work==0);
}
int main() {
    auto snapshot=std::make_unique<Information>();auto& i=*snapshot;
    i.round=2;i.own.id=0;i.own.health=i.own.maximum_health=1000;i.travel={distance_metric,nullptr};
    i.group_count=i.altar_count=1;i.altars[0].position={4,0};
    auto& g=i.groups[0];g.id=10;g.position={2,0};g.gems=24;g.respawn_rounds=30;
    g.full_clear_rounds=g.clear_rounds=2;g.full_health=g.health=100;g.incoming_damage=g.full_incoming_damage=1;
    g.visible=g.all_spawns_visible=g.occupancy_known=true;g.lifecycle=Lifecycle::living;
    g.provenance=g.health_provenance={2,Source::personal_vision,Status::exact_now};
    runtime::Habitat habitat(true),control(false);
    habitat.observe_kit(42);control.observe_kit(42);habitat.observe_snapshot(i);control.observe_snapshot(i);
    runtime::ProducerFrame producers;producers.count=1;producers.clock={1,{0}};
    producers.samples[0]={2,10,{2,0},{1,Source::personal_vision,Status::exact_now},true,true,false,runtime::ProductionEvidence::visible_cleared_spawn};
    habitat.observe_producers(producers);control.observe_producers(producers);
    producers.clock.round=2;producers.samples[0].provenance.observed_round=2;
    habitat.observe_producers(producers);control.observe_producers(producers);
    assert(habitat::pressure(habitat.state(),0,2).observed_consumers==1);
    assert(habitat::pressure(control.state(),0,2).observed_consumers==1);
    assert(habitat::pressure(habitat.state(),0,2).unknown);
    const auto revision=habitat.state().capacity_revision;
    habitat.observe_kit(43,1);habitat.observe_snapshot(i);
    assert(habitat.state().combat_version==43&&habitat.state().capacity_revision>revision);
    i.public_layout_truncated=true;habitat.observe_snapshot(i);
    assert(!habitat.state().supported&&habitat.state().unknown_geometry);
    i.public_layout_truncated=false;habitat.observe_snapshot(i);assert(habitat.state().supported);
    auto ordinary=std::make_unique<Evaluation>(),adapted=std::make_unique<Evaluation>();
    const auto menu=make_menu(i);runtime::M0 baseline;
    evaluate_into(i,menu,*ordinary);runtime::evaluate_model(i,menu,baseline,*adapted);
    assert(ordinary->chosen==adapted->chosen&&ordinary->evaluated_count==adapted->evaluated_count);
    Batch batch; runtime::evaluate_model(i,menu,batch,*adapted);
    assert(batch.calls==1 && adapted->menu.count==menu.count && adapted->chosen==ordinary->chosen);
    batch.fault=1;runtime::evaluate_model(i,menu,batch,*adapted);
    assert(adapted->budget_exhausted && adapted->evaluated_count==0 && adapted->chosen==-1);
    batch.fault=2;runtime::evaluate_model(i,menu,batch,*adapted);
    assert(adapted->budget_exhausted && adapted->evaluated_count==0 && adapted->chosen==-1);
    i.work_budget={[](const void*){return false;},nullptr};
    const int before_calls=batch.calls;runtime::evaluate_model(i,menu,batch,*adapted);
    assert(batch.calls==before_calls && adapted->budget_exhausted && adapted->evaluated_count==0);
    i.work_budget={};runtime::evaluate_model(i,menu,baseline,*adapted);
    for(int n=0;n<ordinary->evaluated_count;++n) {
        assert(ordinary->forecasts[n].our_secured==adapted->forecasts[n].our_secured);
        assert(ordinary->forecasts[n].first_conversion_round==adapted->forecasts[n].first_conversion_round);
        assert(ordinary->forecasts[n].event_count==adapted->forecasts[n].event_count);
    }

    runtime::Chronicle chronicle(true),discard(false);
    runtime::VisibleMember member{10,99,0,2,{2,0},25,{2,Source::personal_vision,Status::exact_now}};
    chronicle.observe_snapshot(i,{&member,1,false});
    assert(!chronicle.state().history(10).exact_health_vector);
    chronicle::MemberObservation copied;assert(chronicle.state().member_health(10,0,copied));
    assert(copied.member_id==99&&copied.health==25);
    i.round=10;g.lifecycle=Lifecycle::respawning;g.respawn_remaining=30;
    g.provenance.observed_round=10;
    chronicle.observe_snapshot(i);discard.observe_snapshot(i);
    runtime::CapturedGroup receipt{10,10,30,{10,Source::personal_vision,Status::exact_now},true};
    assert(chronicle.capture_receipt(i,receipt));assert(!discard.capture_receipt(i,receipt));
    i.round=11;g.provenance.observed_round=11;g.respawn_remaining=29;
    chronicle.observe_snapshot(i);
    i.round=12;g.visible=false;g.lifecycle=Lifecycle::unknown;
    chronicle.observe_snapshot(i);discard.observe_snapshot(i);
    assert(chronicle.state().history(10).earliest_ready_round==40);
    assert(discard.state().history(10).earliest_ready_round<0);

    runtime::Governor governor(true),unbiased(false);governor.observe_kit(42);unbiased.observe_kit(42);
    for(unsigned n=1;n<=3;++n) {
        runtime::PhaseStart start{runtime::ProductionPhase::travel,0,n,1,int(20*n),10,0,0,false};
        runtime::PhaseReceipt end{runtime::ProductionPhase::travel,0,n,1,int(20*n),int(20*n+14),10,0,0,0,true,true,false,
            runtime::PhaseCensor::none,4,4,true};
        assert(governor.phase_started(start));assert(unbiased.phase_started(start));
        governor.phase_completed(end);unbiased.phase_completed(end);
    }
    assert(governor.state().quote_phase(governor::Phase5::travel,10,10)>10);
    assert(unbiased.state().quote_phase(governor::Phase5::travel,10,10)==10);
    governor.observe_kit(43,1);
    assert(governor.state().stat_epoch()==1&&governor.state().entry(governor::Phase5::travel).completions==0);
    governor_diagnostics();habitat_diagnostics();chronicle_diagnostics();
    std::cout<<"{\"schema\":\"plan4-runtime-adapter-tests/1\",\"passed\":true,\"scenarios\":10,\"engineWitnesses\":0}\n";
}
