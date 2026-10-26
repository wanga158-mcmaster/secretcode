// Synthetic legal DTO/core fixtures only; no native contract witness is claimed.
#include "../covenant/src/runtime.h"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace plan4;
namespace rt=plan4::runtime;
namespace cv=plan4::covenant;
namespace {
int metric(Point a,Point b,const void*) {return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
struct Fixture {
    std::unique_ptr<Information> info=std::make_unique<Information>();Menu menu{};
    Fixture() {
        auto& i=*info;i.round=10;i.own.id=3;i.own.position={0,0};i.own.health=i.own.maximum_health=100;i.own.bag=24;i.own.recall_safe=true;
        i.group_count=2;i.altar_count=2;i.travel={metric,nullptr};i.own_spawn={0,0};
        for (int g=0;g<2;++g) {auto& group=i.groups[g];group.id=10+g;group.position={2+g*2,0};
            group.health=group.full_health=100;group.clear_rounds=group.full_clear_rounds=2;group.gems=24;group.respawn_rounds=30;
            group.visible=group.occupancy_known=group.all_spawns_visible=true;group.lifecycle=Lifecycle::living;
            group.provenance=group.health_provenance={i.round,Source::personal_vision,Status::exact_now};
            menu.jobs[g].kind=JobKind::farm;menu.jobs[g].group=g;menu.jobs[g].altar=0;menu.jobs[g].menu_rank=g;}
        for(int a=0;a<2;++a) {i.altars[a].id=30+a;i.altars[a].position={a,2};i.altars[a].altar_position={a,3};i.altars[a].lock_known=true;
            menu.jobs[3+a].kind=JobKind::bank;menu.jobs[3+a].altar=a;menu.jobs[3+a].menu_rank=3+a;}
        menu.jobs[2]=menu.jobs[0];menu.jobs[2].second_group=1;menu.jobs[2].menu_rank=2;
        menu.jobs[5].kind=JobKind::renew;menu.jobs[6].kind=JobKind::hold;menu.count=7;menu.single_count=2;
    }
    void round(int value) {info->round=value;for(int g=0;g<info->group_count;++g) info->groups[g].provenance.observed_round=info->groups[g].health_provenance.observed_round=value;}
    rt::ExecutionRepairFrame frame(const Job& job,std::uint64_t epoch=1,bool phase=false) const {
        return {{info->round,{info->own.id}},info.get(),&menu,job,epoch,job.group,true,false,rt::ProductionPhase::travel,phase};
    }
    cv::Observation observation() const {cv::Observation out;out.round=info->round;out.own=info->own;out.physical_provenance={info->round,Source::own_state,Status::exact_now};out.revision=1;return out;}
};
bool same(const Forecast& a,const Forecast& b) {
    if(a.event_count!=b.event_count || a.macros_started!=b.macros_started || a.final_round!=b.final_round || a.first_conversion_round!=b.first_conversion_round ||
        a.our_secured!=b.our_secured || a.enemy_secured!=b.enemy_secured || a.final_bag!=b.final_bag || a.final_gold!=b.final_gold ||
        a.final_health!=b.final_health || a.terminal_z!=b.terminal_z || a.abort_reason!=b.abort_reason || a.truncated!=b.truncated ||
        a.budget_exhausted!=b.budget_exhausted || a.stock_unknown!=b.stock_unknown || a.travel_unknown!=b.travel_unknown ||
        a.completed_conversion!=b.completed_conversion || a.terminal_unknown!=b.terminal_unknown) return false;
    for(int e=0;e<a.event_count;++e) {const auto& x=a.events[e];const auto& y=b.events[e];
        if(x.kind!=y.kind || x.start.round!=y.start.round || x.start.phase!=y.start.phase || x.end.round!=y.end.round || x.end.phase!=y.end.phase ||
            x.group_id!=y.group_id || x.bag_before!=y.bag_before || x.bag_after!=y.bag_after || x.secured!=y.secured ||
            x.gold_after!=y.gold_after || x.health_after!=y.health_after || x.status!=y.status) return false;}
    return true;
}
std::unique_ptr<Evaluation> evaluate(rt::Covenant& model,Fixture& f) {model.observe_snapshot(*f.info);auto out=std::make_unique<Evaluation>();rt::evaluate_model(*f.info,f.menu,model,*out);return out;}
void unchanged_without_actual_binding_and_unknown_phases() {
    auto f=std::make_unique<Fixture>();auto model=std::make_unique<rt::Covenant>();auto out=evaluate(*model,*f);
    auto expected=std::make_unique<Evaluation>();plan4::evaluate_into(*f->info,f->menu,*expected);
    assert(model->model_evidence().reported && !model->model_evidence().supported && !model->model_evidence().coupling_enabled);
    for(int n=0;n<f->menu.count;++n) assert(same(out->forecasts[n],expected->forecasts[n]));
    model->selected(*f->info,f->menu.jobs[0],1,true);const int deadline=model->monitor().contract().deadline;
    f->round(100);f->info->groups[0].visible=false;f->info->groups[0].provenance={};
    const auto repair=rt::execution_repair(*model,f->frame(f->menu.jobs[0]));
    assert(!repair.supported && !model->monitor().contract().failed && model->monitor().contract().deadline==deadline);
    out=evaluate(*model,*f);assert(!model->model_evidence().supported && !model->model_evidence().coupling_enabled);
}
void same_actual_repairs_and_full_fixed_product_paths() {
    auto f=std::make_unique<Fixture>();auto active=std::make_unique<rt::Covenant>(false),control=std::make_unique<rt::Covenant>();
    // Ordinary-start fixtures construct a flat warmup Bot, then restore the
    // declared arm before admitting its first real contract.
    active->set_enabled(true); control->set_enabled(false);
    for(auto* model:{active.get(),control.get()}) {model->observe_snapshot(*f->info);model->selected(*f->info,f->menu.jobs[0],1,true);}
    const auto accepted=active->monitor().contract(); active->set_enabled(false); active->set_enabled(true);
    assert(active->monitor().contract().epoch==accepted.epoch && active->monitor().contract().deadline==accepted.deadline &&
        active->monitor().contract().phase_sequence==accepted.phase_sequence);
    f->round(11);auto& group=f->info->groups[0];group.lifecycle=Lifecycle::respawning;group.respawn_remaining=30;group.health=0;
    const auto a=rt::execution_repair(*active,f->frame(f->menu.jobs[0])),c=rt::execution_repair(*control,f->frame(f->menu.jobs[0]));
    assert(a.supported && a.action==rt::ExecutionRepairAction::bank && a.bank_menu_index==3 && a.action==c.action && a.bank_menu_index==c.bank_menu_index);
    auto predicted=evaluate(*active,*f),flat=evaluate(*control,*f);
    assert(predicted->menu.count==f->menu.count && predicted->evaluated_count==f->menu.count && flat->evaluated_count==f->menu.count);
    assert(active->model_evidence().supported && active->model_evidence().coupling_enabled && !control->model_evidence().coupling_enabled);
    auto expected=std::make_unique<Forecast>();plan4::predict_into(*f->info,f->menu.jobs[0],f->menu,*expected);assert(same(flat->forecasts[0],*expected));
    assert(predicted->forecasts[0].macros_started>1 && predicted->forecasts[0].first_conversion_round<flat->forecasts[0].first_conversion_round);
    assert(predicted->forecasts[0].final_bag<=48 && predicted->chosen==choose(*f->info,f->menu,predicted->forecasts));
    unsigned captured=0,banked=0;
    for(int e=0;e<predicted->forecasts[0].event_count;++e) {const auto& event=predicted->forecasts[0].events[e];
        if(event.kind==EventKind::capture) captured+=event.bag_after-event.bag_before;
        if(event.kind==EventKind::bank) banked+=event.secured;}
    assert(banked==predicted->forecasts[0].our_secured && captured+24==banked+predicted->forecasts[0].final_bag);
    const int deadline=active->monitor().contract().deadline;f->round(12);active->selected(*f->info,f->menu.jobs[0],2,true);
    assert(active->monitor().contract().deadline==deadline);const auto again=rt::execution_repair(*active,f->frame(f->menu.jobs[0],2));assert(again.supported && again.action==a.action);
}
void blocked_conversion_second_common_bank_and_protection() {
    auto f=std::make_unique<Fixture>();f->info->own.position=f->info->altars[0].position;
    auto active=std::make_unique<rt::Covenant>(),control=std::make_unique<rt::Covenant>(false);
    for(auto* model:{active.get(),control.get()}) {model->observe_snapshot(*f->info);model->selected(*f->info,f->menu.jobs[3],1,true);}
    f->round(11);f->info->altars[0].hostile_lock=true;
    auto frame=f->frame(f->menu.jobs[3]);frame.phase=rt::ProductionPhase::bank;frame.phase_known=true;
    const auto a=rt::execution_repair(*active,frame),c=rt::execution_repair(*control,frame);
    assert(a.supported && a.action==rt::ExecutionRepairAction::bank && a.bank_menu_index==4 && c.bank_menu_index==4);
    frame.selected_job=f->menu.jobs[4];frame.protected_progress=true;f->info->own.channel=Channel::tribute;f->info->own.channel_ticks=3;
    const auto protected_reply=rt::execution_repair(*active,frame);assert(protected_reply.supported && protected_reply.action==rt::ExecutionRepairAction::preserve);
    frame.protected_progress=false;f->info->own.channel=Channel::none;f->info->in_progress_group_id=10;
    assert(rt::execution_repair(*active,frame).action==rt::ExecutionRepairAction::preserve);
}
void unknown_endpoint_and_wrong_epoch_cannot_advance() {
    auto f=std::make_unique<Fixture>();auto model=std::make_unique<rt::Covenant>();model->observe_snapshot(*f->info);model->selected(*f->info,f->menu.jobs[0],1,true);
    const auto sequence=model->monitor().contract().phase_sequence;
    assert(!model->phase_started({rt::ProductionPhase::capture,3,1,1,10,6,0,0,false}));
    model->phase_completed({rt::ProductionPhase::capture,3,1,1,10,11,6,0,0,0,true,true,false,rt::PhaseCensor::none,0,0,true});
    f->round(11);model->observe_snapshot(*f->info);assert(model->monitor().contract().phase_sequence==sequence && model->monitor().contract().completed_mask==0);
    auto bad=f->frame(f->menu.jobs[0],999);assert(!rt::execution_repair(*model,bad).supported);
    bad=f->frame(f->menu.jobs[0]);--bad.clock.round;assert(!rt::execution_repair(*model,bad).supported);
}
void bank_repair_completion_keeps_failed_suffix_memory() {
    auto f=std::make_unique<Fixture>();auto model=std::make_unique<rt::Covenant>();model->observe_snapshot(*f->info);model->selected(*f->info,f->menu.jobs[0],1,true);
    f->round(11);f->info->groups[0].lifecycle=Lifecycle::respawning;f->info->groups[0].respawn_remaining=30;
    assert(rt::execution_repair(*model,f->frame(f->menu.jobs[0])).action==rt::ExecutionRepairAction::bank);
    assert(model->phase_started({rt::ProductionPhase::bank,3,1,2,11,3,1,0,false}));
    model->phase_completed({rt::ProductionPhase::bank,3,1,2,11,14,3,1,1,0,true,true,false,rt::PhaseCensor::none,0,0,true});
    model->selection_canceled();f->round(14);f->info->own.bag=0;model->observe_snapshot(*f->info);
    assert(model->monitor().contract().stage==cv::Stage::done && model->monitor().contract().converted && model->monitor().contract().completed_mask==0);
    const auto epoch=model->monitor().contract().epoch;model->selected(*f->info,f->menu.jobs[0],2,true);
    assert(model->monitor().contract().epoch==epoch); // The original unavailable suffix remains latched until round19.
}
void future_guard_requires_real_physical_checkpoint() {
    auto f=std::make_unique<Fixture>();auto monitor=std::make_unique<cv::Covenant>();auto bridge=std::make_unique<cv::FixedProduct>();auto obs=f->observation();
    const auto job=f->menu.jobs[3];assert(monitor->accept(cv::physical_key(*f->info,job),cv::Stage::approach,0,-1,obs)==cv::Admit::accepted);
    cv::RepairQuotes quotes;quotes.bank_count=1;quotes.banks[0]={30,5,true};auto out=std::make_unique<Forecast>(),flat=std::make_unique<Forecast>();
    plan4::predict_into(*f->info,job,f->menu,*flat);
    // Stored deadline12; now a longer known public route predicts arrival30.
    f->info->altars[0].position={20,0};obs=f->observation();plan4::predict_into(*f->info,job,f->menu,*flat);
    const auto result=bridge->predict_into(*f->info,job,f->menu,*monitor,obs,quotes,true,true,true,*out);
    assert(result.guard_known && !result.composed && result.gap==cv::BindingGap::future_checkpoint && same(*out,*flat));
}
void retained_acquisitions_are_not_replayed_and_cutoffs_are_pathwise() {
    auto f=std::make_unique<Fixture>();auto monitor=std::make_unique<cv::Covenant>();auto bridge=std::make_unique<cv::FixedProduct>();auto obs=f->observation();
    const auto pair=f->menu.jobs[2];const auto key=cv::physical_key(*f->info,pair);
    assert(monitor->accept(key,cv::Stage::approach,2,-1,obs)==cv::Admit::accepted);
    auto receipt=[&](cv::Milestone kind) {return cv::Receipt{key,monitor->contract().epoch,monitor->contract().phase_sequence,3,obs.round,kind,{obs.round,Source::own_state,Status::exact_now},true,false,-1,0};};
    assert(monitor->complete(receipt(cv::Milestone::arrival),2,-1,obs)==cv::ReceiptResult::advanced);
    assert(monitor->complete(receipt(cv::Milestone::clear),6,-1,obs)==cv::ReceiptResult::advanced);
    auto captured=receipt(cv::Milestone::capture_receipt);captured.group_id=10;captured.stock_epoch=77;captured.own_attribution=true;
    assert(monitor->complete(captured,4,-1,obs)==cv::ReceiptResult::advanced);
    auto out=std::make_unique<Forecast>(),expected=std::make_unique<Forecast>();cv::RepairQuotes quotes;
    const auto result=bridge->predict_into(*f->info,pair,f->menu,*monitor,obs,quotes,false,false,false,*out);
    Job remaining=pair;remaining.group=1;remaining.second_group=-1;plan4::predict_into(*f->info,remaining,f->menu,*expected);
    assert(!result.composed && same(*out,*expected));
    f->info->settings.max_rounds=11;obs=f->observation();
    const auto cutoff=bridge->predict_into(*f->info,pair,f->menu,*monitor,obs,quotes,true,false,false,*out);
    assert(!cutoff.composed && out->our_secured==0 && out->final_round<=11 && out->final_bag==24);
}
void stale_source_and_fuel_denial_cannot_claim_support() {
    auto f=std::make_unique<Fixture>();auto model=std::make_unique<rt::Covenant>();model->observe_snapshot(*f->info);f->round(11);
    auto out=std::make_unique<Evaluation>();rt::evaluate_model(*f->info,f->menu,*model,*out);assert(!model->model_evidence().supported && !model->model_evidence().coupling_enabled);
    model->observe_snapshot(*f->info);f->info->work_budget={[](const void*){return false;},nullptr};rt::evaluate_model(*f->info,f->menu,*model,*out);
    assert(out->budget_exhausted && out->evaluated_count==0 && !model->model_evidence().coupling_enabled && model->model_evidence().paths==0);
}
void missing_start_wrong_remaining_target_and_unquoted_retreat_are_unsupported() {
    auto f=std::make_unique<Fixture>();auto model=std::make_unique<rt::Covenant>();model->observe_snapshot(*f->info);model->selected(*f->info,f->menu.jobs[0],1,true);
    model->phase_completed({rt::ProductionPhase::bank,3,1,2,10,11,1,1,1,0,true,true,false,rt::PhaseCensor::none,0,0,true});
    f->round(11);f->info->own.bag=0;model->observe_snapshot(*f->info);assert(!model->monitor().contract().converted && model->monitor().contract().completed_mask==0);
    auto frame=f->frame(f->menu.jobs[0]);frame.remaining_group=1;f->info->groups[0].lifecycle=Lifecycle::respawning;
    assert(!rt::execution_repair(*model,frame).supported && !model->monitor().contract().failed);
    f=std::make_unique<Fixture>();model=std::make_unique<rt::Covenant>();f->info->own.position=f->info->altars[0].position;
    model->observe_snapshot(*f->info);model->selected(*f->info,f->menu.jobs[3],1,true);f->round(11);
    f->info->altars[0].hostile_lock=f->info->altars[1].hostile_lock=true;frame=f->frame(f->menu.jobs[3]);frame.phase=rt::ProductionPhase::bank;frame.phase_known=true;
    assert(!rt::execution_repair(*model,frame).supported); // Healthy RENEW's one-round wait is not a retreat-route quote.
}
void a_new_physical_key_needs_its_own_execution_binding() {
    auto f=std::make_unique<Fixture>();auto model=std::make_unique<rt::Covenant>();
    model->observe_snapshot(*f->info);model->selected(*f->info,f->menu.jobs[0],1,true);
    f->round(11);f->info->groups[0].lifecycle=Lifecycle::respawning;
    assert(rt::execution_repair(*model,f->frame(f->menu.jobs[0])).supported);
    model->selected(*f->info,f->menu.jobs[1],2,true);
    f->info->groups[1].lifecycle=Lifecycle::respawning;
    const auto actual=evaluate(*model,*f);auto expected=std::make_unique<Evaluation>();plan4::evaluate_into(*f->info,f->menu,*expected);
    assert(!model->model_evidence().supported && !model->model_evidence().coupling_enabled);
    for(int n=0;n<f->menu.count;++n) assert(same(actual->forecasts[n],expected->forecasts[n]));
}
rt::PhaseStart start_phase(rt::ProductionPhase phase,int round,std::uint64_t sequence,int group=-1) {
    const int mode=phase==rt::ProductionPhase::clear?2:phase==rt::ProductionPhase::restore?3:phase==rt::ProductionPhase::bank?1:0;
    return {phase,3,1,sequence,round,2,mode,7,false,group};
}
rt::PhaseReceipt finish_phase(const rt::PhaseStart& source,int round) {
    return {source.phase,source.actor,source.job_epoch,source.phase_sequence,source.start_round,round,source.nominal_duration,
        source.mode,source.mode,source.stat_epoch,true,true,false,rt::PhaseCensor::none,0,0,true,source.group_id};
}
void farm_milestones_require_matching_actual_endpoints_in_both_arms() {
    for(bool enabled:{false,true}) {
        auto f=std::make_unique<Fixture>();auto model=std::make_unique<rt::Covenant>(enabled);
        model->observe_snapshot(*f->info);model->selected(*f->info,f->menu.jobs[2],1,true);
        const auto travel=start_phase(rt::ProductionPhase::travel,10,1,10);
        assert(model->phase_started(travel));model->phase_completed(finish_phase(travel,11));
        f->round(11);f->info->own.position=f->info->groups[0].position;model->observe_snapshot(*f->info);
        assert(model->monitor().contract().stage==cv::Stage::clear && model->monitor().contract().completed_mask==0);
        const auto clear=start_phase(rt::ProductionPhase::clear,11,2,10);
        assert(model->phase_started(clear));model->phase_completed(finish_phase(clear,12));
        // An exact END clear is confirmed at the next fresh START. Preserve
        // the receipt's endpoint time; the next promise starts at recognition.
        f->round(13);auto& group=f->info->groups[0];group.health=0;group.lifecycle=Lifecycle::cleared;group.reward_available=true;
        model->observe_snapshot(*f->info);
        assert(model->monitor().contract().stage==cv::Stage::capture && model->monitor().contract().phase_start==13);
        assert(model->monitor().contract().completed_mask==0 && model->monitor().contract().remaining_group==0);
        assert(!model->phase_started(start_phase(rt::ProductionPhase::capture,13,3,10)));
        f->round(14);group.lifecycle=Lifecycle::respawning;f->info->own.bag+=24;
        model->observe_snapshot(*f->info);
        assert(model->monitor().contract().stage==cv::Stage::capture && model->monitor().contract().completed_mask==0);
        assert(!model->phase_started(start_phase(rt::ProductionPhase::bank,14,4)));
        assert(!model->monitor().contract().converted); // Bag/lifecycle never becomes own capture attribution.
    }
}
void group_and_phase_identity_cannot_be_substituted_or_replayed() {
    auto f=std::make_unique<Fixture>();auto model=std::make_unique<rt::Covenant>();
    model->observe_snapshot(*f->info);model->selected(*f->info,f->menu.jobs[0],1,true);
    auto start=start_phase(rt::ProductionPhase::travel,10,1,11);assert(!model->phase_started(start));
    start.group_id=10;start.mode=2;assert(!model->phase_started(start));
    start.mode=0;++start.actor;assert(!model->phase_started(start));
    --start.actor;++start.job_epoch;assert(!model->phase_started(start));
    --start.job_epoch;assert(model->phase_started(start));
    auto receipt=finish_phase(start,11);receipt.group_id=11;model->phase_completed(receipt);
    f->round(11);model->observe_snapshot(*f->info);assert(model->monitor().contract().stage==cv::Stage::approach);
    receipt.group_id=10;model->phase_completed(receipt);model->observe_snapshot(*f->info);
    assert(model->monitor().contract().stage==cv::Stage::clear);
    const auto sequence=model->monitor().contract().phase_sequence;
    model->phase_completed(receipt);model->observe_snapshot(*f->info);
    assert(model->monitor().contract().phase_sequence==sequence);
    start=start_phase(rt::ProductionPhase::clear,11,1,10);assert(!model->phase_started(start));
    start.phase_sequence=2;assert(model->phase_started(start));
    receipt=finish_phase(start,12);++receipt.stat_epoch;model->phase_completed(receipt);
    f->round(12);f->info->groups[0].health=0;f->info->groups[0].lifecycle=Lifecycle::cleared;f->info->groups[0].reward_available=true;
    model->observe_snapshot(*f->info);assert(model->monitor().contract().stage==cv::Stage::clear);
}
void stale_censored_or_changed_contract_receipts_cannot_advance() {
    for(int kind=0;kind<6;++kind) {
        auto f=std::make_unique<Fixture>();auto model=std::make_unique<rt::Covenant>();
        model->observe_snapshot(*f->info);model->selected(*f->info,f->menu.jobs[0],1,true);
        const auto start=start_phase(rt::ProductionPhase::travel,10,1,10);assert(model->phase_started(start));
        auto receipt=finish_phase(start,11);
        if(kind==0)receipt.censor=rt::PhaseCensor::lost_sight;
        if(kind==1)receipt.completed=false;
        if(kind==2)receipt.endpoint_observed=false;
        if(kind==5)receipt.emergency=true;
        model->phase_completed(receipt);
        f->round(kind==3?13:11);
        if(kind==4)model->selected(*f->info,f->menu.jobs[1],2,true);
        model->observe_snapshot(*f->info);assert(model->monitor().contract().stage==cv::Stage::approach);
        assert(model->monitor().contract().completed_mask==0);
        if(kind==5)assert(model->phase_started(start_phase(rt::ProductionPhase::travel,11,2,10)));
    }
}
void renew_restoration_uses_actual_receipt_and_current_spawn_endpoint() {
    for(bool enabled:{false,true}) {
        auto f=std::make_unique<Fixture>();f->info->own.health=60;f->info->own.on_spawn=true;
        auto model=std::make_unique<rt::Covenant>(enabled);model->observe_snapshot(*f->info);model->selected(*f->info,f->menu.jobs[5],1,true);
        assert(model->monitor().accepted() && model->monitor().contract().stage==cv::Stage::restore);
        const auto start=start_phase(rt::ProductionPhase::restore,10,1);assert(model->phase_started(start));
        model->phase_completed(finish_phase(start,12));f->round(12);f->info->own.health=100;f->info->own.on_spawn=false;
        model->observe_snapshot(*f->info);assert(!model->monitor().contract().restored);
        // A rejected endpoint cannot be replayed after moving back onto spawn.
        f->info->own.on_spawn=true;model->phase_completed(finish_phase(start,12));model->observe_snapshot(*f->info);
        assert(!model->monitor().contract().restored);
        f->info->own.health=60;model->observe_snapshot(*f->info);
        const auto retry=start_phase(rt::ProductionPhase::restore,12,2);assert(model->phase_started(retry));
        model->phase_completed(finish_phase(retry,14));f->round(14);f->info->own.health=100;model->observe_snapshot(*f->info);
        assert(model->monitor().contract().restored && model->monitor().contract().stage==cv::Stage::done);
    }
}
void same_turn_travel_and_clear_keep_real_tracker_order() {
    for(bool enabled:{false,true}) for(int condition=0;condition<3;++condition) {
        auto f=std::make_unique<Fixture>();f->info->groups[0].position={1,0};
        auto model=std::make_unique<rt::Covenant>(enabled);model->observe_snapshot(*f->info);model->selected(*f->info,f->menu.jobs[0],1,true);
        rt::PhaseTracker tracker;auto clock=rt::PhaseClock{3,10,0,7,true,true,false,true};
        const auto travel=start_phase(rt::ProductionPhase::travel,10,1,10);
        tracker.begin_phase(travel,clock).forward_to(*model);
        if(condition==1)tracker.censor_pending(rt::PhaseCensor::lost_sight,clock).forward_to(*model);
        else tracker.finish({travel,10,rt::EndpointKind::travel_arrival,{10,Source::own_state,Status::exact_now},false},clock).forward_to(*model);
        const auto clear=start_phase(rt::ProductionPhase::clear,10,2,10);clock.mode=2;
        tracker.begin_phase(clear,clock).forward_to(*model);
        tracker.finish({clear,10,rt::EndpointKind::group_clear,{10,Source::personal_vision,Status::exact_now},false},clock).forward_to(*model);
        assert(model->monitor().contract().stage==cv::Stage::approach); // Await fresh physical confirmation.
        if(condition==2)model->selected(*f->info,f->menu.jobs[0],2,true);
        f->round(11);f->info->own.position={1,0};auto& group=f->info->groups[0];group.health=0;
        group.lifecycle=Lifecycle::cleared;group.reward_available=true;model->observe_snapshot(*f->info);
        if(condition==0)assert(model->monitor().contract().stage==cv::Stage::capture && model->monitor().contract().phase_sequence==3);
        else assert(model->monitor().contract().stage==cv::Stage::approach && model->monitor().contract().phase_sequence==1);
        assert(model->monitor().contract().completed_mask==0 && !model->monitor().contract().converted);
    }
}
detour::Route route(const Fixture& f,bool terminal=false) {
    detour::Route out;out.actor=f.info->own.id;out.round=f.info->round;out.origin=f.info->own.position;
    out.obstruction={1,0};out.steps[0]={0,1};out.count=terminal?1:3;
    if(!terminal) {out.steps[1]={1,1};out.steps[2]={2,0};}
    out.goal=out.steps[out.count-1];out.visits=out.count;out.proven=true;assert(detour::valid(out));return out;
}
rt::ExecutionRepairFrame route_frame(const Fixture& f,const Job& job,const detour::Route& proof) {
    auto frame=f.frame(job,1,true);frame.alternate_route=&proof;frame.ordinary_goal_bound=true;return frame;
}
void alternate_guards_and_actual_progress_match_in_both_arms() {
    for(bool enabled:{false,true}) {
        auto f=std::make_unique<Fixture>();auto model=std::make_unique<rt::Covenant>(enabled);
        model->observe_snapshot(*f->info);model->selected(*f->info,f->menu.jobs[0],1,true);
        const auto initial=model->monitor().contract();auto proof=route(*f);auto frame=route_frame(*f,f->menu.jobs[0],proof);
        const auto travel=start_phase(rt::ProductionPhase::travel,10,1,10);assert(model->phase_started(travel));
        const auto repair=rt::execution_repair(*model,frame);
        assert(repair.supported && repair.action==rt::ExecutionRepairAction::alternate_path);
        assert(model->monitor().contract().failed && model->monitor().contract().failed_cause==cv::Cause::approach_obstruction);
        assert(model->monitor().contract().epoch==initial.epoch && model->monitor().contract().deadline==initial.deadline);
        auto canceled=finish_phase(travel,10);canceled.completed=false;canceled.censor=rt::PhaseCensor::interrupted;
        model->phase_completed(canceled);
        assert(!model->alternate_route_result(frame,false,proof.origin));
        assert(model->monitor().contract().failed && model->monitor().contract().deadline==initial.deadline);
        assert(rt::execution_repair(*model,frame).action==rt::ExecutionRepairAction::alternate_path);
        assert(model->alternate_route_result(frame,true,proof.steps[0]));
        const auto resumed=model->monitor().contract();
        assert(!resumed.failed && resumed.stage==cv::Stage::approach && resumed.key==initial.key && resumed.epoch==initial.epoch);
        assert(resumed.nominal_duration==2 && resumed.phase_sequence==initial.phase_sequence+1 && resumed.deadline==cv::promise_deadline(10,2));
        assert(resumed.completed_mask==0 && !resumed.converted && f->info->own.bag==24);
        assert(!model->alternate_route_result(frame,true,proof.steps[0]));
        auto remaining=start_phase(rt::ProductionPhase::travel,10,2,10);remaining.nominal_duration=2;
        assert(model->phase_started(remaining));model->phase_completed(finish_phase(remaining,12));
        f->round(12);f->info->own.position=proof.goal;model->observe_snapshot(*f->info);
        assert(model->monitor().contract().stage==cv::Stage::clear && model->monitor().contract().completed_mask==0);
        model->phase_completed(canceled);model->observe_snapshot(*f->info);assert(model->monitor().contract().stage==cv::Stage::clear);
    }
}
void alternate_proofs_need_current_geometry_job_and_ordinary_goal_binding() {
    for(int kind=0;kind<10;++kind) {
        auto f=std::make_unique<Fixture>();auto model=std::make_unique<rt::Covenant>();
        model->observe_snapshot(*f->info);model->selected(*f->info,f->menu.jobs[0],1,true);
        const auto initial=model->monitor().contract();auto proof=route(*f);auto frame=route_frame(*f,f->menu.jobs[0],proof);
        if(kind==0)proof.proven=false;
        if(kind==1)--proof.round;
        if(kind==2)++proof.actor;
        if(kind==3)proof.origin={1,0};
        if(kind==4)proof.steps[1]=proof.steps[0];
        if(kind==5)frame.phase_known=false;
        if(kind==6)frame.ordinary_goal_bound=false;
        if(kind==7)++frame.job_epoch;
        if(kind==8)frame.remaining_group=1;
        if(kind==9)frame.alternate_route=nullptr;
        assert(rt::execution_repair(*model,frame).action!=rt::ExecutionRepairAction::alternate_path);
        assert(!model->monitor().contract().failed && model->monitor().contract().deadline==initial.deadline && model->monitor().contract().epoch==initial.epoch);
    }
    for(int kind=0;kind<7;++kind) {
        auto f=std::make_unique<Fixture>();auto model=std::make_unique<rt::Covenant>();
        model->observe_snapshot(*f->info);model->selected(*f->info,f->menu.jobs[0],1,true);
        auto proof=route(*f);auto frame=route_frame(*f,f->menu.jobs[0],proof);
        assert(rt::execution_repair(*model,frame).action==rt::ExecutionRepairAction::alternate_path);
        const auto failed=model->monitor().contract();auto actual=proof.steps[0];
        if(kind==0)actual=proof.origin;
        if(kind==1)++frame.job_epoch;
        if(kind==2)--frame.clock.round;
        if(kind==3)proof.steps[1]={1,2}; // Still a full route, but not the accepted proposal.
        if(kind==4)frame.ordinary_goal_bound=false;
        if(kind==5)f->info->own.position={0,1}; // Original snapshot drifted.
        if(kind==6)f->round(11);
        assert(!model->alternate_route_result(frame,true,actual));
        assert(model->monitor().contract().failed && model->monitor().contract().deadline==failed.deadline &&
            model->monitor().contract().epoch==failed.epoch && model->monitor().contract().phase_sequence==failed.phase_sequence);
    }
}
void terminal_alternate_arrival_uses_actual_goal_without_capture_or_conversion() {
    for(bool enabled:{false,true}) for(bool bank:{false,true}) {
        auto f=std::make_unique<Fixture>();auto model=std::make_unique<rt::Covenant>(enabled);
        const auto job=f->menu.jobs[bank?3:0];model->observe_snapshot(*f->info);model->selected(*f->info,job,1,true);
        const auto initial=model->monitor().contract();auto proof=route(*f,true);auto frame=route_frame(*f,job,proof);
        // The common goal is an attack/altar-range tile rather than a center.
        assert(proof.goal!=f->info->groups[0].position && proof.goal!=f->info->altars[0].position);
        assert(rt::execution_repair(*model,frame).action==rt::ExecutionRepairAction::alternate_path);
        assert(model->alternate_route_result(frame,true,proof.goal));
        const auto& contract=model->monitor().contract();
        assert(contract.stage==(bank?cv::Stage::convert:cv::Stage::clear) && !contract.failed && contract.epoch==initial.epoch && contract.key==initial.key);
        assert(contract.phase_sequence==initial.phase_sequence+2 && contract.nominal_duration==(bank?tribute_ticks(24,f->info->settings):2));
        assert(contract.completed_mask==0 && !contract.converted && f->info->own.bag==24);
    }
    auto f=std::make_unique<Fixture>();auto model=std::make_unique<rt::Covenant>();
    model->observe_snapshot(*f->info);model->selected(*f->info,f->menu.jobs[0],1,true);
    auto proof=route(*f,true);auto frame=route_frame(*f,f->menu.jobs[0],proof);f->info->groups[0].visible=false;
    assert(rt::execution_repair(*model,frame).action==rt::ExecutionRepairAction::alternate_path);
    assert(model->alternate_route_result(frame,true,proof.goal));
    assert(model->monitor().contract().stage==cv::Stage::approach && model->monitor().contract().nominal_duration==0);
}
void alternate_routes_preserve_channels_and_live_capture() {
    for(bool enabled:{false,true}) for(bool capture:{false,true}) {
        auto f=std::make_unique<Fixture>();auto model=std::make_unique<rt::Covenant>(enabled);
        model->observe_snapshot(*f->info);model->selected(*f->info,f->menu.jobs[0],1,true);
        const auto initial=model->monitor().contract();auto proof=route(*f);auto frame=route_frame(*f,f->menu.jobs[0],proof);
        if(capture)f->info->in_progress_group_id=10;else f->info->own.channel=Channel::tribute;
        assert(rt::execution_repair(*model,frame).action==rt::ExecutionRepairAction::preserve);
        assert(!model->alternate_route_result(frame,true,proof.steps[0]));
        assert(!model->monitor().contract().failed && model->monitor().contract().epoch==initial.epoch && model->monitor().contract().deadline==initial.deadline);
        assert(f->info->own.bag==24 && model->monitor().contract().completed_mask==0);
    }
}
rt::StockAcquisition stock_row(const Fixture& f,int group,int capture_round,unsigned gems,std::uint64_t epoch=9) {
    return {f.info->groups[group].id,capture_round,epoch,gems,{f.info->round,Source::own_state,Status::exact_now}};
}
rt::OwnStockFrame stock_frame(const Fixture& f,rt::StockAcquisition first,std::uint64_t epoch=9) {
    rt::OwnStockFrame out;out.clock={f.info->round,{f.info->own.id}};out.stock_epoch=epoch;out.bag=f.info->own.bag;
    out.acquisitions[0]=first;out.count=1;out.known=true;return out;
}
rt::CapturedGroup own_capture(const Fixture& f,int group,unsigned before,unsigned amount,std::uint64_t epoch=9) {
    rt::CapturedGroup out;out.group_id=f.info->groups[group].id;out.round=f.info->round;out.respawn_rounds=30;
    out.provenance={f.info->round,Source::personal_vision,Status::exact_now};out.personally_witnessed=true;
    out.origin=rt::CaptureOrigin::isolated_own_stock;out.actor_attribution_known=true;out.actor_id=f.info->own.id;
    out.stock_epoch=epoch;out.bag_before=before;out.bag_after=before+amount;out.received_gems=amount;
    out.own_provenance={f.info->round,Source::own_state,Status::exact_now};return out;
}
void first_capture_stage(rt::Covenant& model,Fixture& f) {
    model.observe_snapshot(*f.info);model.selected(*f.info,f.menu.jobs[2],1,true);
    const auto travel=start_phase(rt::ProductionPhase::travel,10,1,10);
    assert(model.phase_started(travel));model.phase_completed(finish_phase(travel,11));
    f.round(11);f.info->own.position=f.info->groups[0].position;model.observe_snapshot(*f.info);
    assert(model.monitor().contract().stage==cv::Stage::clear);
    const auto clear=start_phase(rt::ProductionPhase::clear,11,2,10);
    assert(model.phase_started(clear));model.phase_completed(finish_phase(clear,12));
    f.round(12);f.info->groups[0].health=0;f.info->groups[0].lifecycle=Lifecycle::cleared;f.info->groups[0].reward_available=true;
    model.observe_snapshot(*f.info);assert(model.monitor().contract().stage==cv::Stage::capture);
}
void captured_group(Fixture& f,int group) {
    f.info->groups[group].lifecycle=Lifecycle::respawning;f.info->groups[group].reward_available=false;
    f.info->groups[group].respawn_remaining=30;
}
void measured_own_shares_advance_full_two_camp_jobs_in_both_arms() {
    for(bool enabled:{false,true}) {
        auto f=std::make_unique<Fixture>();auto model=std::make_unique<rt::Covenant>(enabled);first_capture_stage(*model,*f);
        f->round(13);captured_group(*f,0);f->info->own.bag=36;
        auto source=own_capture(*f,0,24,12); // An observed shared reward, not the full public24 gems.
        model->observe_own_stock(stock_frame(*f,stock_row(*f,0,13,12)));model->observe_snapshot(*f->info);
        assert(source.event_status==Status::derived && model->capture_receipt(*f->info,source));
        auto contract=model->monitor().contract();assert(contract.completed_mask==1 && contract.completed[0].stock_epoch==9);
        assert(contract.remaining_group==1 && contract.stage==cv::Stage::approach && contract.phase_start==13 && contract.nominal_duration==2);
        assert(contract.completed[0].provenance.source==Source::own_state && contract.completed[0].provenance.observed_round==13);
        const int deadline=contract.deadline;const auto sequence=contract.phase_sequence;
        assert(!model->capture_receipt(*f->info,source));assert(model->monitor().contract().phase_sequence==sequence);
        model->selected(*f->info,f->menu.jobs[2],1,true);assert(model->monitor().contract().deadline==deadline);
        const auto travel=start_phase(rt::ProductionPhase::travel,13,3,11);
        assert(model->phase_started(travel));model->phase_completed(finish_phase(travel,14));
        f->round(14);f->info->own.position=f->info->groups[1].position;
        model->observe_own_stock(stock_frame(*f,stock_row(*f,0,13,12)));model->observe_snapshot(*f->info);
        assert(model->monitor().contract().stage==cv::Stage::clear);
        const auto clear=start_phase(rt::ProductionPhase::clear,14,4,11);
        assert(model->phase_started(clear));model->phase_completed(finish_phase(clear,15));
        f->round(15);f->info->groups[1].health=0;f->info->groups[1].lifecycle=Lifecycle::cleared;f->info->groups[1].reward_available=true;
        model->observe_snapshot(*f->info);assert(model->monitor().contract().stage==cv::Stage::capture);
        f->round(16);captured_group(*f,1);f->info->own.bag=60;
        auto frame=stock_frame(*f,stock_row(*f,0,13,12));frame.acquisitions[1]=stock_row(*f,1,16,24);frame.count=2;
        model->observe_own_stock(frame);model->observe_snapshot(*f->info);
        assert(model->capture_receipt(*f->info,own_capture(*f,1,36,24)));
        contract=model->monitor().contract();assert(contract.completed_mask==3 && contract.stage==cv::Stage::convert);
        assert(contract.completed[0].provenance.observed_round==13 && contract.completed[1].provenance.observed_round==16);
        assert(contract.nominal_duration==distance(*f->info,f->info->own.position,f->info->altars[0].position)+tribute_ticks(60,f->info->settings));
        f->round(17);f->info->own.position=f->info->altars[0].position;model->observe_snapshot(*f->info);
        const auto bank=start_phase(rt::ProductionPhase::bank,17,5);assert(model->phase_started(bank));
        model->phase_completed(finish_phase(bank,18));f->round(18);f->info->own.bag=0;model->observe_snapshot(*f->info);
        assert(model->monitor().contract().stage==cv::Stage::done && model->monitor().contract().converted && model->monitor().contract().completed_mask==3);
    }
}
void capture_receipts_require_current_attribution_stock_and_physical_endpoint() {
    for(bool enabled:{false,true}) for(int invalid=0;invalid<28;++invalid) {
        auto f=std::make_unique<Fixture>();auto model=std::make_unique<rt::Covenant>(enabled);first_capture_stage(*model,*f);
        f->round(13);captured_group(*f,0);f->info->own.bag=36;
        auto source=own_capture(*f,0,24,12);auto stock=stock_frame(*f,stock_row(*f,0,13,12));
        switch(invalid) {
            case 0:source.origin=rt::CaptureOrigin::lifecycle_boundaries;break;
            case 1:source.actor_attribution_known=false;break;
            case 2:++source.actor_id;break;
            case 3:--source.round;break;
            case 4:source.own_provenance.status=Status::derived;break;
            case 5:source.own_provenance.source=Source::personal_vision;break;
            case 6:++source.stock_epoch;break;
            case 7:++source.bag_after;break;
            case 8:--source.received_gems;break;
            case 9:stock.known=false;break;
            case 10:++stock.stock_epoch;break;
            case 11:++stock.acquisitions[0].group_id;break;
            case 12:--stock.acquisitions[0].capture_round;break;
            case 13:--stock.acquisitions[0].gems;break;
            case 14:--stock.acquisitions[0].provenance.observed_round;break;
            case 15:f->info->groups[0].lifecycle=Lifecycle::living;break;
            case 16:--f->info->groups[0].provenance.observed_round;break;
            case 17:f->info->own.channel=Channel::tribute;break;
            case 18:model->selection_canceled();break;
            case 19:++source.group_id;break;
            case 20:--stock.clock.round;break;
            case 21:++stock.bag;break;
            case 22:source.received_gems=0;break;
            case 23:f->info->in_progress_group_id=10;break;
            case 24:stock.acquisitions[0].stock_epoch=0;break;
            case 25:stock.count=3;break;
            case 26:++stock.clock.own.id;break;
            case 27:stock.acquisitions[0].provenance.source=Source::own_occupancy;break;
        }
        const auto before=model->monitor().contract();model->observe_own_stock(stock);model->observe_snapshot(*f->info);
        assert(!model->capture_receipt(*f->info,source));const auto& after=model->monitor().contract();
        assert(after.stage==before.stage && after.phase_sequence==before.phase_sequence && after.deadline==before.deadline && after.completed_mask==0);
        if(invalid==17 || invalid==23) {
            auto frame=f->frame(f->menu.jobs[2]);frame.protected_progress=true;
            const auto repair=rt::execution_repair(*model,frame);assert(repair.supported && repair.action==rt::ExecutionRepairAction::preserve);
        }
    }
}
void own_receipt_can_complete_a_failed_capture_but_never_another_suffix() {
    for(bool enabled:{false,true}) for(bool hidden:{false,true}) {
        auto f=std::make_unique<Fixture>();f->info->own.bag=0;auto model=std::make_unique<rt::Covenant>(enabled);first_capture_stage(*model,*f);
        f->round(30);captured_group(*f,0);auto repair=f->frame(f->menu.jobs[2]);assert(rt::execution_repair(*model,repair).supported);
        assert(model->monitor().contract().failed && model->monitor().remaining().stage==cv::Stage::capture);
        captured_group(*f,0);f->info->groups[0].visible=!hidden;f->info->own.bag=12;
        model->observe_own_stock(stock_frame(*f,stock_row(*f,0,30,12)));model->observe_snapshot(*f->info);
        assert(model->capture_receipt(*f->info,own_capture(*f,0,0,12)));
        assert(!model->monitor().contract().failed && model->monitor().contract().completed_mask==1 && model->monitor().contract().stage==cv::Stage::approach);
    }
    auto f=std::make_unique<Fixture>();auto model=std::make_unique<rt::Covenant>();model->observe_snapshot(*f->info);
    model->selected(*f->info,f->menu.jobs[2],1,true);captured_group(*f,0);f->info->own.bag=36;
    model->observe_own_stock(stock_frame(*f,stock_row(*f,0,10,12)));model->observe_snapshot(*f->info);
    assert(!model->capture_receipt(*f->info,own_capture(*f,0,24,12)) && model->monitor().contract().stage==cv::Stage::approach);
}
void retained_stock_transfers_require_current_stable_epoch_associations() {
    for(bool enabled:{false,true}) for(int condition=0;condition<9;++condition) {
        auto f=std::make_unique<Fixture>();auto model=std::make_unique<rt::Covenant>(enabled);
        auto stock=stock_frame(*f,stock_row(*f,0,9,12));model->observe_own_stock(stock);model->observe_snapshot(*f->info);
        model->selected(*f->info,f->menu.jobs[2],1,true);assert(model->monitor().contract().completed_mask==1);
        f->round(11);stock=stock_frame(*f,stock_row(*f,0,9,12));
        if(condition==1) {f->info->own.bag+=7;stock.bag=f->info->own.bag;}
        if(condition==2) {++stock.stock_epoch;stock.count=0;}
        if(condition==3) stock.known=false;
        if(condition==4) --stock.clock.round;
        if(condition==5) --stock.acquisitions[0].provenance.observed_round;
        if(condition==6) {stock.acquisitions[0].capture_round=10;}
        if(condition==7) ++stock.acquisitions[0].gems;
        if(condition==8) {++stock.stock_epoch;}
        model->observe_own_stock(stock);model->observe_snapshot(*f->info);
        auto changed=f->menu.jobs[2];changed.altar=1;model->selected(*f->info,changed,2,true);
        const auto& contract=model->monitor().contract();assert(contract.completed_mask==(condition<=1?1u:0u));
        assert(contract.stage==cv::Stage::approach && contract.remaining_group==(condition<=1?1:0));
        if(condition<=1) assert(contract.completed[0].stock_epoch==9 && contract.completed[0].provenance.observed_round==11);
    }
}
// Only these supported fixtures provide the common core with a cached route.
// The original unsupported-route cases intentionally retain a null callback.
int cached_route(Point from,Point to,Point* output,int capacity,const void*) {
    const int count=std::min(metric(from,to,nullptr),capacity);
    for(int step=0;step<count;++step) {
        from.x+=(to.x>from.x)-(to.x<from.x);from.y+=(to.y>from.y)-(to.y<from.y);output[step]=from;
    }
    return count;
}
int event_count(const Forecast& forecast,EventKind kind,int group=-1) {
    int count=0;for(int e=0;e<forecast.event_count;++e)
        if(forecast.events[e].kind==kind && (group<0 || forecast.events[e].group_id==group)) ++count;
    return count;
}
void conserved_future_path(const Information& source,const Forecast& forecast) {
    unsigned acquired=0,banked=0,gold=source.own.gold;
    for(int e=0;e<forecast.event_count;++e) {
        const auto& event=forecast.events[e];
        assert(event.status==Status::predicted && event.start.round>=source.round && event.end.round>=event.start.round);
        if(event.kind==EventKind::capture) acquired+=event.bag_after-event.bag_before;
        else assert(event.gold_after==gold);
        if(event.kind==EventKind::bank) banked+=event.secured;
        gold=event.gold_after;
    }
    assert(source.own.bag+acquired==banked+forecast.final_bag && banked==forecast.our_secured && gold==forecast.final_gold);
}
void original_promise(const cv::Contract& actual,const cv::Contract& accepted) {
    assert(actual.key==accepted.key && actual.epoch==accepted.epoch && actual.phase_sequence==accepted.phase_sequence &&
        actual.phase_start==accepted.phase_start && actual.nominal_duration==accepted.nominal_duration && actual.deadline==accepted.deadline &&
        actual.completed_mask==accepted.completed_mask && actual.stage==accepted.stage && !actual.failed);
}
void exact_control_m0(Fixture& f,const Evaluation& control) {
    auto expected=std::make_unique<Evaluation>();plan4::evaluate_into(*f.info,f.menu,*expected);
    assert(control.menu.count==expected->menu.count && control.evaluated_count==expected->evaluated_count && control.chosen==expected->chosen);
    for(int n=0;n<f.menu.count;++n) {
        const auto& actual=control.forecasts[n];const auto& flat=expected->forecasts[n];
        assert(same(actual,flat) && actual.lock_unknown==flat.lock_unknown && actual.no_conversion==flat.no_conversion &&
            actual.objective.paths==flat.objective.paths && actual.objective.terminal_z==flat.objective.terminal_z &&
            actual.objective.our_secured==flat.objective.our_secured && actual.objective.enemy_secured==flat.objective.enemy_secured &&
            actual.objective.first_conversion_round==flat.objective.first_conversion_round);
    }
}
void future_approach_uses_cached_prefix_and_same_actual_deadline_repair() {
    auto f=std::make_unique<Fixture>();f->info->travel.route_prefix=cached_route;f->menu.single_count=0;
    auto active=std::make_unique<rt::Covenant>(),control=std::make_unique<rt::Covenant>(false);const auto job=f->menu.jobs[0];
    for(auto* model:{active.get(),control.get()}) {model->observe_snapshot(*f->info);model->selected(*f->info,job,1,true);}
    const auto accepted=active->monitor().contract();assert(accepted.deadline==15);
    f->round(11);f->info->groups[0].position={20,0};
    for(auto* model:{active.get(),control.get()}) {
        auto frame=f->frame(job,1,true);assert(rt::execution_repair(*model,frame).supported);
    }
    const auto predicted=evaluate(*active,*f),flat=evaluate(*control,*f);exact_control_m0(*f,*flat);
    const auto& path=predicted->forecasts[0];
    assert(active->model_evidence().supported && active->model_evidence().coupling_enabled && control->model_evidence().supported && !control->model_evidence().coupling_enabled);
    assert(event_count(path,EventKind::partial_travel)==1 && event_count(path,EventKind::capture)==0 && path.macros_started==2);
    assert(path.first_conversion_round==24 && path.our_secured==24 && path.final_bag==0 && !same(path,flat->forecasts[0]));
    const auto& prefix=path.events[0];assert(prefix.kind==EventKind::partial_travel && prefix.start.round==11 && prefix.end.round==16);
    original_promise(active->monitor().contract(),accepted);original_promise(control->monitor().contract(),accepted);
    assert(f->info->own.position==Point({0,0}) && f->info->own.bag==24 && f->info->groups[0].health==100);
    conserved_future_path(*f->info,path);
    f->round(16);f->info->own.position={5,0};
    for(auto* model:{active.get(),control.get()}) {
        const auto repair=rt::execution_repair(*model,f->frame(job,1,true));
        assert(repair.supported && repair.action==rt::ExecutionRepairAction::bank && repair.bank_menu_index==3);
        const auto& failed=model->monitor().contract();
        assert(failed.failed && failed.failed_cause==cv::Cause::deadline && failed.failed_suffix.stage==cv::Stage::approach &&
            failed.failed_suffix.group==10 && failed.deadline==accepted.deadline && failed.completed_mask==0);
        model->selected(*f->info,job,2,true);
        assert(model->monitor().contract().epoch==accepted.epoch && model->monitor().contract().deadline==accepted.deadline && model->monitor().contract().failed);
    }
}
void future_clear_retains_residual_damage_and_latches_the_same_actual_suffix() {
    auto f=std::make_unique<Fixture>();auto& i=*f->info;i.travel.route_prefix=cached_route;i.own.bag=0;i.own.on_spawn=true;
    for(int g=0;g<i.group_count;++g) i.groups[g].respawn_rounds=200;
    i.groups[0].position={0,0};i.groups[0].clear_damage_per_round=50;i.groups[0].incoming_damage=1;
    i.groups[1].position={1,0};i.groups[1].health=i.groups[1].full_health=10;
    i.groups[1].clear_rounds=i.groups[1].full_clear_rounds=1;i.groups[1].clear_damage_per_round=10;i.groups[1].gems=7;i.groups[1].gold=2;
    auto active=std::make_unique<rt::Covenant>(),control=std::make_unique<rt::Covenant>(false);const auto job=f->menu.jobs[0];
    for(auto* model:{active.get(),control.get()}) {
        model->observe_snapshot(i);model->selected(i,job,1,true);
        const auto arrival=start_phase(rt::ProductionPhase::travel,10,1,10);assert(model->phase_started(arrival));model->phase_completed(finish_phase(arrival,10));
    }
    f->round(11);for(auto* model:{active.get(),control.get()}) model->observe_snapshot(i);
    const auto accepted=active->monitor().contract();assert(accepted.stage==cv::Stage::clear && accepted.deadline==16);
    i.groups[0].clear_rounds=i.groups[0].full_clear_rounds=10;i.groups[0].clear_damage_per_round=10;
    for(auto* model:{active.get(),control.get()}) {
        auto frame=f->frame(job,1,true);frame.phase=rt::ProductionPhase::clear;assert(rt::execution_repair(*model,frame).supported);
    }
    const auto predicted=evaluate(*active,*f),flat=evaluate(*control,*f);exact_control_m0(*f,*flat);
    assert(active->model_evidence().supported && active->model_evidence().coupling_enabled && control->model_evidence().supported && !control->model_evidence().coupling_enabled);
    const auto& path=predicted->forecasts[0];assert(event_count(path,EventKind::partial_clear,10)==1 && !same(path,flat->forecasts[0]));
    int residual=0;for(int e=0;e<path.event_count;++e) {
        const auto& event=path.events[e];
        if(event.kind==EventKind::partial_clear && event.group_id==10)
            assert(event.start.round==11 && event.end.round==17 && event.health_after==92 && event.bag_after==0 && event.gold_after==0);
        if(event.kind==EventKind::clear && event.group_id==10) {
            assert(event.start.round>=25 && event.end.round-event.start.round==4 && event.health_after==96);++residual;
        }
        if((event.kind==EventKind::travel || event.kind==EventKind::clear || event.kind==EventKind::capture) && event.group_id==10)
            assert(event.start.round>=25);
    }
    assert(residual==1 && event_count(path,EventKind::capture,10)==1 && event_count(path,EventKind::capture,11)==1);
    assert(i.own.health==100 && i.own.bag==0 && i.groups[0].health==100 && i.groups[0].clear_rounds==10);
    original_promise(active->monitor().contract(),accepted);original_promise(control->monitor().contract(),accepted);conserved_future_path(i,path);
    f->round(17);i.own.health=92;i.groups[0].health=40;i.groups[0].clear_rounds=4;
    for(auto* model:{active.get(),control.get()}) {
        auto frame=f->frame(job,1,true);frame.phase=rt::ProductionPhase::clear;const auto repair=rt::execution_repair(*model,frame);
        const auto& failed=model->monitor().contract();
        assert(repair.supported && repair.action==rt::ExecutionRepairAction::renew && failed.failed &&
            failed.failed_cause==cv::Cause::deadline && failed.failed_suffix.stage==cv::Stage::clear && failed.failed_suffix.group==10 &&
            failed.deadline==accepted.deadline && failed.completed_mask==0);
    }
}
void future_conversion_preserves_predicted_and_actual_channels_in_both_arms() {
    auto f=std::make_unique<Fixture>();f->info->travel.route_prefix=cached_route;f->menu.single_count=0;
    f->info->own.position=f->info->altars[0].position;
    auto active=std::make_unique<rt::Covenant>(),control=std::make_unique<rt::Covenant>(false);const auto job=f->menu.jobs[3];
    for(auto* model:{active.get(),control.get()}) {model->observe_snapshot(*f->info);model->selected(*f->info,job,1,true);}
    const auto accepted=active->monitor().contract();assert(accepted.stage==cv::Stage::convert && accepted.deadline==17);
    f->round(11);f->info->own.bag=100;
    for(auto* model:{active.get(),control.get()}) {
        auto frame=f->frame(job,1,true);frame.phase=rt::ProductionPhase::bank;assert(rt::execution_repair(*model,frame).supported);
    }
    const auto predicted=evaluate(*active,*f),flat=evaluate(*control,*f);exact_control_m0(*f,*flat);
    assert(active->model_evidence().supported && !active->model_evidence().coupling_enabled && control->model_evidence().supported && !control->model_evidence().coupling_enabled);
    assert(same(predicted->forecasts[3],flat->forecasts[3]) && predicted->forecasts[3].first_conversion_round==21 && predicted->forecasts[3].our_secured==100);
    assert(f->info->own.channel==Channel::none && f->info->own.bag==100);
    original_promise(active->monitor().contract(),accepted);original_promise(control->monitor().contract(),accepted);
    conserved_future_path(*f->info,predicted->forecasts[3]);
    f->round(18);f->info->own.channel=Channel::tribute;f->info->own.channel_ticks=3;
    for(auto* model:{active.get(),control.get()}) {
        auto frame=f->frame(job,1,true);frame.phase=rt::ProductionPhase::bank;const auto preserve=rt::execution_repair(*model,frame);
        assert(preserve.supported && preserve.action==rt::ExecutionRepairAction::preserve && !model->monitor().contract().failed && model->monitor().contract().deadline==accepted.deadline);
    }
    // A fresh interrupted-channel state is unprotected and uses the same repair.
    f->round(19);f->info->own.channel=Channel::none;f->info->own.channel_ticks=0;
    for(auto* model:{active.get(),control.get()}) {
        auto frame=f->frame(job,1,true);frame.phase=rt::ProductionPhase::bank;const auto repair=rt::execution_repair(*model,frame);
        assert(repair.supported && repair.action==rt::ExecutionRepairAction::bank && repair.bank_menu_index==3 && model->monitor().contract().failed &&
            model->monitor().contract().failed_suffix.stage==cv::Stage::convert && model->monitor().contract().deadline==accepted.deadline);
    }
}
void future_restoration_keeps_partial_healing_and_actual_repair_latch() {
    auto f=std::make_unique<Fixture>();auto& i=*f->info;i.travel.route_prefix=cached_route;f->menu.single_count=0;
    i.own.bag=0;i.own.health=80;i.own.on_spawn=true;
    auto active=std::make_unique<rt::Covenant>(),control=std::make_unique<rt::Covenant>(false);const auto job=f->menu.jobs[5];
    for(auto* model:{active.get(),control.get()}) {model->observe_snapshot(i);model->selected(i,job,1,true);}
    const auto accepted=active->monitor().contract();assert(accepted.stage==cv::Stage::restore && accepted.nominal_duration==1 && accepted.deadline==14);
    f->round(11);i.own.health=1;
    for(auto* model:{active.get(),control.get()}) {
        auto frame=f->frame(job,1,true);frame.phase=rt::ProductionPhase::restore;assert(rt::execution_repair(*model,frame).supported);
    }
    const auto predicted=evaluate(*active,*f),flat=evaluate(*control,*f);exact_control_m0(*f,*flat);
    assert(active->model_evidence().supported && active->model_evidence().coupling_enabled && control->model_evidence().supported && !control->model_evidence().coupling_enabled);
    const auto& path=predicted->forecasts[5];
    assert(path.macros_started==2 && path.final_round==16 && path.final_health==100 && event_count(path,EventKind::heal)==5 && !same(path,flat->forecasts[5]));
    for(int e=0;e<4;++e) assert(path.events[e].kind==EventKind::heal && path.events[e].health_after==1+20*(e+1) && path.events[e].end.round==11+e);
    assert(path.events[4].kind==EventKind::heal && path.events[4].start.round==15 && path.events[4].health_after==100);
    assert(i.own.health==1 && i.own.bag==0 && i.groups[0].health==100);
    original_promise(active->monitor().contract(),accepted);original_promise(control->monitor().contract(),accepted);conserved_future_path(i,path);
    f->round(15);i.own.health=81;
    for(auto* model:{active.get(),control.get()}) {
        auto frame=f->frame(job,1,true);frame.phase=rt::ProductionPhase::restore;const auto repair=rt::execution_repair(*model,frame);
        assert(repair.supported && repair.action==rt::ExecutionRepairAction::renew && model->monitor().contract().failed &&
            model->monitor().contract().failed_suffix.stage==cv::Stage::restore && model->monitor().contract().deadline==accepted.deadline);
        const auto restore=start_phase(rt::ProductionPhase::restore,15,1);assert(model->phase_started(restore));model->phase_completed(finish_phase(restore,16));
    }
    f->round(16);i.own.health=100;
    for(auto* model:{active.get(),control.get()}) {
        model->observe_snapshot(i);assert(model->monitor().contract().restored && model->monitor().contract().stage==cv::Stage::done);
        model->selected(i,job,2,true);
        assert(model->monitor().contract().epoch==accepted.epoch && model->monitor().contract().deadline==accepted.deadline && model->monitor().contract().restored);
    }
}
void farm_deadline_renew_receipt_keeps_original_failed_suffix_memory() {
    auto f=std::make_unique<Fixture>();auto& i=*f->info;i.own.bag=0;i.own.health=60;i.own.on_spawn=true;
    auto active=std::make_unique<rt::Covenant>(),control=std::make_unique<rt::Covenant>(false);const auto job=f->menu.jobs[0];
    for(auto* model:{active.get(),control.get()}) {model->observe_snapshot(i);model->selected(i,job,1,true);}
    const auto accepted=active->monitor().contract();assert(accepted.deadline==15);
    f->round(16);
    for(auto* model:{active.get(),control.get()}) {
        const auto repair=rt::execution_repair(*model,f->frame(job,1,true));
        assert(repair.supported && repair.action==rt::ExecutionRepairAction::renew && model->monitor().contract().failed &&
            model->monitor().contract().failed_suffix.stage==cv::Stage::approach && model->monitor().contract().failed_suffix.group==10);
        const auto restore=start_phase(rt::ProductionPhase::restore,16,1);
        assert(model->phase_started(restore));model->phase_completed(finish_phase(restore,18));
    }
    f->round(18);i.own.health=100;
    for(auto* model:{active.get(),control.get()}) {
        model->observe_snapshot(i);const auto& done=model->monitor().contract();
        assert(done.stage==cv::Stage::done && done.restored && !done.failed && done.deadline==accepted.deadline &&
            done.epoch==accepted.epoch && done.completed_mask==0 && !done.converted && i.own.bag==0 && i.groups[0].health==100);
        model->selected(i,job,2,true);
        assert(model->monitor().contract().epoch==accepted.epoch && model->monitor().contract().deadline==accepted.deadline && model->monitor().contract().restored);
    }
}
}
int main() {
    unchanged_without_actual_binding_and_unknown_phases();same_actual_repairs_and_full_fixed_product_paths();
    blocked_conversion_second_common_bank_and_protection();unknown_endpoint_and_wrong_epoch_cannot_advance();
    bank_repair_completion_keeps_failed_suffix_memory();future_guard_requires_real_physical_checkpoint();
    retained_acquisitions_are_not_replayed_and_cutoffs_are_pathwise();stale_source_and_fuel_denial_cannot_claim_support();
    missing_start_wrong_remaining_target_and_unquoted_retreat_are_unsupported();
    a_new_physical_key_needs_its_own_execution_binding();
    farm_milestones_require_matching_actual_endpoints_in_both_arms();group_and_phase_identity_cannot_be_substituted_or_replayed();
    stale_censored_or_changed_contract_receipts_cannot_advance();renew_restoration_uses_actual_receipt_and_current_spawn_endpoint();
    same_turn_travel_and_clear_keep_real_tracker_order();
    alternate_guards_and_actual_progress_match_in_both_arms();alternate_proofs_need_current_geometry_job_and_ordinary_goal_binding();
    terminal_alternate_arrival_uses_actual_goal_without_capture_or_conversion();alternate_routes_preserve_channels_and_live_capture();
    measured_own_shares_advance_full_two_camp_jobs_in_both_arms();capture_receipts_require_current_attribution_stock_and_physical_endpoint();
    own_receipt_can_complete_a_failed_capture_but_never_another_suffix();retained_stock_transfers_require_current_stable_epoch_associations();
    future_approach_uses_cached_prefix_and_same_actual_deadline_repair();future_clear_retains_residual_damage_and_latches_the_same_actual_suffix();
    future_conversion_preserves_predicted_and_actual_channels_in_both_arms();future_restoration_keeps_partial_healing_and_actual_repair_latch();
    farm_deadline_renew_receipt_keeps_original_failed_suffix_memory();
    std::puts("plan4 covenant runtime: 28 synthetic scenarios passed; native capture/fuel witnesses pending");
}
