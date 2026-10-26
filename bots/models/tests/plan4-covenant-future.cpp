// Synthetic legal DTO/common-core fixtures. No native match witness is claimed.
#include "../covenant/src/product.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <tuple>
using namespace plan4;
namespace cv=plan4::covenant;
namespace {
int metric(Point a,Point b,const void*) {return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
int route(Point from,Point to,Point* out,int capacity,const void*) {
    const int count=std::min(metric(from,to,nullptr),capacity);
    for (int n=0;n<count;++n) {
        from.x+=(to.x>from.x)-(to.x<from.x);from.y+=(to.y>from.y)-(to.y<from.y);out[n]=from;
    }
    return count;
}
struct Fixture {
    std::unique_ptr<Information> info=std::make_unique<Information>();Menu menu{};
    Fixture() {
        auto& i=*info;i.round=10;i.own.id=3;i.own.health=i.own.maximum_health=100;
        i.own.position=i.own_spawn={0,0};i.own.bag=24;i.own.recall_safe=true;
        i.travel={metric,nullptr,route};i.group_count=2;i.altar_count=2;
        for (int g=0;g<2;++g) {
            auto& group=i.groups[g];group.id=10+g;group.position={20+g,0};
            group.health=group.full_health=100;group.clear_rounds=group.full_clear_rounds=10;
            group.clear_damage_per_round=10;group.gems=24;group.respawn_rounds=200;
            group.visible=group.all_spawns_visible=group.occupancy_known=true;group.lifecycle=Lifecycle::living;
            group.provenance=group.health_provenance={i.round,Source::personal_vision,Status::exact_now};
            menu.jobs[g]={JobKind::farm,g,-1,0,g};
        }
        menu.jobs[2]={JobKind::farm,0,1,0,2};
        for (int a=0;a<2;++a) {
            i.altars[a].id=30+a;i.altars[a].position={a*2,0};i.altars[a].altar_position={a*2,1};i.altars[a].lock_known=true;
            menu.jobs[3+a]={JobKind::bank,-1,-1,a,3+a};
        }
        menu.jobs[5].kind=JobKind::renew;menu.jobs[6].kind=JobKind::hold;menu.count=7;menu.single_count=1;
    }
    cv::Observation observation() const {
        cv::Observation out;out.round=info->round;out.own=info->own;out.revision=1;
        out.physical_provenance={info->round,Source::own_state,Status::exact_now};return out;
    }
    void accept(cv::Covenant& monitor,const Job& job,cv::Stage stage=cv::Stage::approach,int nominal=0) const {
        assert(monitor.accept(cv::physical_key(*info,job),stage,nominal,-1,observation())==cv::Admit::accepted);
    }
};
auto values(const Forecast& f) {
    return std::make_tuple(f.event_count,f.macros_started,f.final_round,f.first_conversion_round,f.our_secured,f.enemy_secured,
        f.final_bag,f.final_gold,f.final_health,f.terminal_z,f.terminal_unknown,f.stock_unknown,f.travel_unknown,f.lock_unknown,
        f.truncated,f.completed_conversion,f.no_conversion,f.budget_exhausted,f.abort_reason,f.objective.paths,f.objective.terminal_z,
        f.objective.our_secured,f.objective.enemy_secured,f.objective.first_conversion_round);
}
bool same(const Forecast& a,const Forecast& b) {
    if (values(a)!=values(b)) return false;
    for (int n=0;n<a.event_count;++n) {
        const auto& x=a.events[n];const auto& y=b.events[n];
        if (std::tie(x.kind,x.start.round,x.start.phase,x.end.round,x.end.phase,x.group_id,x.bag_before,x.bag_after,x.secured,
            x.gold_after,x.health_after,x.status)!=std::tie(y.kind,y.start.round,y.start.phase,y.end.round,y.end.phase,y.group_id,
            y.bag_before,y.bag_after,y.secured,y.gold_after,y.health_after,y.status)) return false;
    }
    return true;
}
int events(const Forecast& forecast,EventKind kind,int group=-1) {
    int count=0;for (int n=0;n<forecast.event_count;++n)
        if (forecast.events[n].kind==kind && (group<0 || forecast.events[n].group_id==group)) ++count;
    return count;
}
void conserved(const Information& source,const Forecast& forecast) {
    unsigned acquired=0,banked=0,gold=source.own.gold;
    for (int n=0;n<forecast.event_count;++n) {
        const auto& event=forecast.events[n];
        assert(event.status==Status::predicted && event.start.round>=source.round && event.end.round>=event.start.round);
        if (event.kind==EventKind::capture) acquired+=event.bag_after-event.bag_before;
        if (event.kind==EventKind::bank) banked+=event.secured;
        if (event.kind!=EventKind::capture) assert(event.gold_after==gold);
        gold=event.gold_after;
    }
    assert(banked==forecast.our_secured && source.own.bag+acquired==banked+forecast.final_bag && gold==forecast.final_gold);
}
cv::FixedResult predict(cv::FixedProduct& product,Fixture& f,const Job& job,const cv::Covenant& monitor,Forecast& out,
    bool enabled=true,bool phase=true,const cv::RepairQuotes& quotes={}) {
    return product.predict_into(*f.info,job,f.menu,monitor,f.observation(),quotes,enabled,true,phase,out);
}
void partial_travel_requotes_retained_bag_and_latches_from_future(int& cases) {
    auto f=std::make_unique<Fixture>();auto product=std::make_unique<cv::FixedProduct>();cv::Covenant monitor;
    const auto job=f->menu.jobs[0];f->accept(monitor,job);const auto promise=monitor.contract();
    auto active=std::make_unique<Forecast>(),control=std::make_unique<Forecast>(),flat=std::make_unique<Forecast>();
    plan4::predict_into(*f->info,job,f->menu,*flat);
    cv::RepairQuotes stale;stale.bank_count=2;stale.banks[0]={30,3,true};stale.banks[1]={31,5,true};
    const auto result=predict(*product,*f,job,monitor,*active,true,true,stale);
    assert(result.supported && result.composed && result.gap==cv::BindingGap::none && result.guard_known);
    assert(result.guard.at.round==12 && result.guard.at.phase==Phase::owner_end && result.guard.repair_start_round==13);
    assert(result.guard.repair.kind==cv::RepairKind::convert_retained && result.guard.repair.quoted_duration==6);
    assert(result.guard.repair.retained_bag==24 && result.repair_menu_index==3 && result.continuation_steps==0);
    assert(active->macros_started==2 && active->final_round==19 && active->first_conversion_round==19 && active->our_secured==24);
    assert(events(*active,EventKind::partial_travel)==1 && events(*active,EventKind::capture)==0);
    const auto disabled=predict(*product,*f,job,monitor,*control,false,true,stale);
    assert(disabled.supported && !disabled.composed && same(*control,*flat));
    assert(monitor.contract().deadline==promise.deadline && monitor.contract().phase_sequence==promise.phase_sequence &&
        monitor.contract().completed_mask==promise.completed_mask && !monitor.contract().failed);
    assert(f->info->own.position==Point({0,0}) && f->info->own.bag==24 && f->info->groups[0].health==100);
    conserved(*f->info,*active);cases+=2;
}
void bank_order_and_healthy_replan_remain_fixed(int& cases) {
    auto f=std::make_unique<Fixture>();f->menu.single_count=0;f->info->altars[0].position={20,0};
    auto product=std::make_unique<cv::FixedProduct>();auto out=std::make_unique<Forecast>();cv::Covenant monitor;
    const auto bank=f->menu.jobs[3];f->accept(monitor,bank);
    const auto result=predict(*product,*f,bank,monitor,*out);
    assert(result.supported && result.composed && result.repair_menu_index==3 && result.guard.repair.quoted_duration==20);
    assert(out->first_conversion_round==33 && f->menu.jobs[3].altar==0 && f->menu.jobs[4].altar==1);
    conserved(*f->info,*out);++cases;
    f=std::make_unique<Fixture>();f->info->own.bag=0;cv::Covenant healthy;const auto farm=f->menu.jobs[0];f->accept(healthy,farm);
    const auto hold=predict(*product,*f,farm,healthy,*out);
    assert(hold.supported && hold.composed && hold.guard.repair.kind==cv::RepairKind::replan && hold.repair_menu_index==6);
    assert(out->final_round==14 && out->our_secured==0 && out->final_bag==0 && out->final_gold==0 && events(*out,EventKind::capture)==0);
    conserved(*f->info,*out);++cases;
}
void partial_clear_is_physical_and_renew_uses_checkpoint_damage(int& cases) {
    auto f=std::make_unique<Fixture>();f->info->groups[0].position={0,0};f->info->groups[0].incoming_damage=1;
    f->info->own.bag=0;const auto farm=f->menu.jobs[0];
    auto prefix=std::make_unique<Forecast>();auto state=std::make_unique<SimulationState>();PhaseCheckpoint cut;
    assert(simulate_first_until_into(*f->info,farm,f->menu,13,*prefix,*state,cut));
    assert(state->now==13 && state->position==Point({0,0}) && state->health<100 && state->bag==0 && state->gold==0);
    assert(state->partial_health[0]==70 && state->clear_reserve_paid[0] && state->groups[0].lifecycle==Lifecycle::living);
    assert(events(*prefix,EventKind::partial_clear)==1 && events(*prefix,EventKind::clear)==0 && events(*prefix,EventKind::capture)==0);
    cv::Covenant monitor;f->accept(monitor,farm,cv::Stage::clear);auto product=std::make_unique<cv::FixedProduct>();auto out=std::make_unique<Forecast>();
    const auto result=predict(*product,*f,farm,monitor,*out);
    assert(result.supported && result.composed && result.guard.repair.kind==cv::RepairKind::renew && result.repair_menu_index==5);
    assert(result.guard.repair.quoted_duration==1 && out->final_round==14 && out->final_health==100 && out->final_bag==0);
    assert(f->info->own.health==100 && f->info->groups[0].health==100 && f->info->groups[0].clear_rounds==10);
    conserved(*f->info,*out);cases+=2;
}
void later_fixed_tail_consumes_residual_clear_after_latch(int& cases) {
    auto f=std::make_unique<Fixture>();auto& i=*f->info;i.groups[0].position={0,0};i.groups[0].incoming_damage=1;
    i.groups[1].position={1,0};i.groups[1].health=i.groups[1].full_health=10;
    i.groups[1].clear_rounds=i.groups[1].full_clear_rounds=1;i.groups[1].gems=7;i.groups[1].gold=2;f->menu.single_count=2;
    const auto job=f->menu.jobs[0];cv::Covenant monitor;f->accept(monitor,job,cv::Stage::clear);
    auto product=std::make_unique<cv::FixedProduct>();auto out=std::make_unique<Forecast>();
    const auto result=predict(*product,*f,job,monitor,*out);
    assert(result.supported && result.composed && result.continuation_steps>=2 && out->first_conversion_round==16);
    int cleared=0;for (int n=0;n<out->event_count;++n) {
        const auto& event=out->events[n];
        if (event.kind==EventKind::clear && event.group_id==10) {
            assert(event.start.round>=21 && event.end.round-event.start.round==7);++cleared;
        }
        if ((event.kind==EventKind::travel || event.kind==EventKind::clear || event.kind==EventKind::capture) && event.group_id==10)
            assert(event.start.round>=21);
    }
    assert(cleared==1 && events(*out,EventKind::capture,10)==1 && events(*out,EventKind::capture,11)==1);
    assert(i.groups[0].health==100 && i.groups[0].clear_rounds==10 && out->our_secured==55 && out->final_gold==2);
    conserved(i,*out);++cases;
}
void predicted_capture_and_channels_are_protected(int& cases) {
    for (const unsigned bag:{24u,100u}) {
        auto f=std::make_unique<Fixture>();f->info->own.bag=bag;f->menu.single_count=0;
        cv::Covenant monitor;const auto bank=f->menu.jobs[3];f->accept(monitor,bank,cv::Stage::convert);
        auto product=std::make_unique<cv::FixedProduct>();auto out=std::make_unique<Forecast>(),flat=std::make_unique<Forecast>();
        plan4::predict_into(*f->info,bank,f->menu,*flat);const auto result=predict(*product,*f,bank,monitor,*out);
        assert(result.supported && result.protected_progress && result.guard.protected_progress && !result.composed && same(*out,*flat));
        assert(f->info->own.channel==Channel::none && !monitor.contract().failed);++cases;
    }
    auto f=std::make_unique<Fixture>();f->menu.single_count=0;auto& group=f->info->groups[0];
    group.position={0,0};group.health=0;group.lifecycle=Lifecycle::cleared;group.reward_available=true;
    cv::Covenant monitor;const auto farm=f->menu.jobs[0];f->accept(monitor,farm,cv::Stage::capture);
    auto product=std::make_unique<cv::FixedProduct>();auto out=std::make_unique<Forecast>(),flat=std::make_unique<Forecast>();
    plan4::predict_into(*f->info,farm,f->menu,*flat);const auto result=predict(*product,*f,farm,monitor,*out);
    assert(result.supported && result.protected_progress && !result.composed && same(*out,*flat));
    assert(f->info->own.bag==24 && group.lifecycle==Lifecycle::cleared);++cases;
}
void prior_acquisition_and_original_promise_are_retained(int& cases) {
    auto f=std::make_unique<Fixture>();f->menu.single_count=0;f->info->groups[0].position={0,0};
    f->info->groups[0].lifecycle=Lifecycle::captured;f->info->groups[0].respawn_remaining=200;
    const auto pair=f->menu.jobs[2];cv::Covenant monitor;f->accept(monitor,pair);
    const auto key=cv::physical_key(*f->info,pair);const auto obs=f->observation();
    auto receipt=[&](cv::Milestone kind) {return cv::Receipt{key,monitor.contract().epoch,monitor.contract().phase_sequence,
        3,10,kind,{10,Source::own_state,Status::exact_now},true,false,-1,0};};
    assert(monitor.complete(receipt(cv::Milestone::arrival),10,-1,obs)==cv::ReceiptResult::advanced);
    assert(monitor.complete(receipt(cv::Milestone::clear),6,-1,obs)==cv::ReceiptResult::advanced);
    auto acquired=receipt(cv::Milestone::capture_receipt);acquired.own_attribution=true;acquired.group_id=10;acquired.stock_epoch=77;
    assert(monitor.complete(acquired,0,-1,obs)==cv::ReceiptResult::advanced);const auto original=monitor.contract();
    auto product=std::make_unique<cv::FixedProduct>();auto out=std::make_unique<Forecast>();const auto result=predict(*product,*f,pair,monitor,*out);
    assert(result.supported && result.composed && events(*out,EventKind::capture)==0 && out->our_secured==24);
    assert(monitor.contract().completed_mask==1 && monitor.contract().completed[0].stock_epoch==77 &&
        monitor.contract().phase_sequence==original.phase_sequence && monitor.contract().deadline==original.deadline && !monitor.contract().failed);
    conserved(*f->info,*out);++cases;
}
void unsupported_future_geometry_combat_and_recall_keep_m0(int& cases) {
    for (int mode=0;mode<5;++mode) {
        auto f=std::make_unique<Fixture>();cv::Stage stage=cv::Stage::approach;
        if (mode==0) f->info->travel.route_prefix=nullptr;
        if (mode==1) f->info->travel.route_prefix=[](Point,Point,Point* out,int capacity,const void*) {
            if (capacity>0) out[0]={9,9};return std::min(1,capacity);
        };
        if (mode==2 || mode==3) {
            stage=cv::Stage::clear;f->info->groups[0].position={0,0};
            f->info->groups[0].clear_damage_per_round=mode==2?0.0f:3.0f;
        }
        if (mode==4) {f->info->own.bag=0;f->info->own.health=60;}
        const auto job=f->menu.jobs[0];cv::Covenant monitor;f->accept(monitor,job,stage);
        auto product=std::make_unique<cv::FixedProduct>();auto out=std::make_unique<Forecast>(),flat=std::make_unique<Forecast>();
        plan4::predict_into(*f->info,job,f->menu,*flat);const auto result=predict(*product,*f,job,monitor,*out);
        assert(result.guard_known && !result.supported && !result.composed && same(*out,*flat));
        assert(result.gap==(mode==4?cv::BindingGap::repair:cv::BindingGap::future_checkpoint));++cases;
    }
    auto f=std::make_unique<Fixture>();f->menu.single_count=2;f->info->altars[0].lock_known=false;
    const auto job=f->menu.jobs[0];cv::Covenant monitor;f->accept(monitor,job);
    auto product=std::make_unique<cv::FixedProduct>();auto out=std::make_unique<Forecast>(),flat=std::make_unique<Forecast>();
    plan4::predict_into(*f->info,job,f->menu,*flat);const auto result=predict(*product,*f,job,monitor,*out);
    // The retained tail's unknown altar lock cannot become a future event.
    assert(!result.supported && !result.composed && same(*out,*flat));++cases;
}
void horizon_suppressed_milestones_keep_supported_repairs(int& cases) {
    for (bool clearing:{false,true}) {
        auto f=std::make_unique<Fixture>();auto& i=*f->info;f->menu.single_count=0;
        i.settings.max_rounds=clearing?18:25;
        if (clearing) {i.groups[0].position={0,0};i.groups[0].incoming_damage=1;}
        const auto job=f->menu.jobs[0];cv::Covenant monitor;
        f->accept(monitor,job,clearing?cv::Stage::clear:cv::Stage::approach);
        auto product=std::make_unique<cv::FixedProduct>();
        auto out=std::make_unique<Forecast>(),flat=std::make_unique<Forecast>(),control=std::make_unique<Forecast>();
        plan4::predict_into(i,job,f->menu,*flat);
        assert(flat->abort_reason==AbortReason::horizon && flat->final_round==i.settings.max_rounds &&
            events(*flat,clearing?EventKind::clear:EventKind::travel)==0);
        const auto result=predict(*product,*f,job,monitor,*out);
        assert(result.guard_known && result.supported && result.composed && result.gap==cv::BindingGap::none &&
            result.guard.at.round==12 && result.guard.at.phase==Phase::owner_end && result.guard.repair_start_round==13 &&
            result.guard.repair.kind==cv::RepairKind::convert_retained && result.repair_menu_index==3);
        assert(out->final_round==(clearing?16:19) && out->first_conversion_round==out->final_round &&
            out->our_secured==24 && out->final_bag==0 && out->final_health==(clearing?95:100) &&
            out->macros_started==2 && events(*out,EventKind::capture)==0 &&
            events(*out,clearing?EventKind::partial_clear:EventKind::partial_travel)==1);
        const auto disabled=predict(*product,*f,job,monitor,*control,false);
        assert(disabled.guard_known && disabled.supported && !disabled.composed && same(*control,*flat));
        conserved(i,*out);cases+=2;
    }
    // Earlier owner-end heals do not prove that restoration finished. Here the
    // deadline guard is known, but its remaining repair cannot finish before H.
    auto f=std::make_unique<Fixture>();auto& i=*f->info;f->menu.single_count=0;
    i.settings.max_rounds=13;i.own.health=20;i.own.bag=0;
    const auto job=f->menu.jobs[5];cv::Covenant monitor;f->accept(monitor,job,cv::Stage::restore);
    auto product=std::make_unique<cv::FixedProduct>();auto out=std::make_unique<Forecast>(),flat=std::make_unique<Forecast>();
    plan4::predict_into(i,job,f->menu,*flat);
    assert(flat->abort_reason==AbortReason::horizon && events(*flat,EventKind::heal)==3 && flat->final_health==80);
    const auto result=predict(*product,*f,job,monitor,*out);
    assert(result.guard_known && result.guard.cause==cv::Cause::deadline && result.guard.repair_start_round==13 &&
        !result.supported && !result.composed && result.gap==cv::BindingGap::repair && same(*out,*flat));++cases;
}
void horizon_evidence_requires_known_completion_and_supported_cut(int& cases) {
    for (bool unknown_distance:{false,true}) {
        auto f=std::make_unique<Fixture>();f->info->settings.max_rounds=25;f->menu.single_count=0;
        if (unknown_distance) f->info->travel.distance=[](Point,Point,const void*) {return -1;};
        else f->info->travel.route_prefix=nullptr;
        const auto job=f->menu.jobs[0];cv::Covenant monitor;f->accept(monitor,job);
        auto product=std::make_unique<cv::FixedProduct>();auto out=std::make_unique<Forecast>(),flat=std::make_unique<Forecast>();
        plan4::predict_into(*f->info,job,f->menu,*flat);
        const auto result=predict(*product,*f,job,monitor,*out);
        assert(!result.supported && !result.composed && same(*out,*flat));
        assert(result.guard_known==!unknown_distance &&
            result.gap==(unknown_distance?cv::BindingGap::unknown_guard:cv::BindingGap::future_checkpoint));++cases;
    }
}
void restoration_deadline_uses_final_owner_end(int& cases) {
    for (const float health:{20.0f,40.0f}) {
        auto f=std::make_unique<Fixture>();f->menu.single_count=0;f->info->own.health=health;f->info->own.bag=0;
        const auto job=f->menu.jobs[5];cv::Covenant monitor;f->accept(monitor,job,cv::Stage::restore);
        auto product=std::make_unique<cv::FixedProduct>();auto out=std::make_unique<Forecast>(),flat=std::make_unique<Forecast>();
        plan4::predict_into(*f->info,job,f->menu,*flat);
        const auto result=predict(*product,*f,job,monitor,*out);
        if (health==20) {
            assert(result.guard_known && result.supported && result.composed &&
                result.guard.repair_start_round==13 && result.guard.repair.kind==cv::RepairKind::renew &&
                result.guard.repair.quoted_duration==1 && out->final_round==14 && out->final_health==100 &&
                events(*out,EventKind::heal)==4 && out->macros_started==2);
        } else {
            // The third and final heal ends at owner_end(12), meeting deadline
            // 12 even though the next owner-action boundary is round 13.
            assert(!result.guard_known && !result.composed && same(*out,*flat) &&
                flat->final_round==13 && flat->final_health==100 && events(*flat,EventKind::heal)==3);
        }
        conserved(*f->info,*out);++cases;
    }
}
struct Budget {int calls=0,limit=std::numeric_limits<int>::max();};
bool spend(const void* context) {auto& budget=*static_cast<Budget*>(const_cast<void*>(context));return budget.calls++<budget.limit;}
void checkpoint_budget_denial_is_flat_and_reusable(int& cases) {
    for (const int max_rounds:{2000,25}) {
        auto f=std::make_unique<Fixture>();Budget budget;f->info->work_budget={spend,&budget};
        f->info->settings.max_rounds=max_rounds;
        const auto job=f->menu.jobs[0];auto first=std::make_unique<Forecast>(),flat=std::make_unique<Forecast>();auto state=std::make_unique<SimulationState>();
        simulate_first_into(*f->info,job,f->menu,*first,*state);assert(continue_from_into(*f->info,f->menu,*first,*state,*flat));
        const int baseline_calls=budget.calls;budget.calls=0;budget.limit=baseline_calls;
        cv::Covenant monitor;f->accept(monitor,job);auto product=std::make_unique<cv::FixedProduct>();auto out=std::make_unique<Forecast>();
        const auto denied=predict(*product,*f,job,monitor,*out);
        assert(!denied.supported && !denied.composed && denied.gap==cv::BindingGap::budget && same(*out,*flat));
        f->info->work_budget={};f->info->travel.route_prefix=nullptr;const auto retry=predict(*product,*f,job,monitor,*out);
        assert(!retry.composed && retry.gap==cv::BindingGap::future_checkpoint && same(*out,*flat));cases+=2;
    }
}
} // namespace
int main() {
    int cases=0;partial_travel_requotes_retained_bag_and_latches_from_future(cases);
    bank_order_and_healthy_replan_remain_fixed(cases);partial_clear_is_physical_and_renew_uses_checkpoint_damage(cases);
    later_fixed_tail_consumes_residual_clear_after_latch(cases);predicted_capture_and_channels_are_protected(cases);
    prior_acquisition_and_original_promise_are_retained(cases);unsupported_future_geometry_combat_and_recall_keep_m0(cases);
    horizon_suppressed_milestones_keep_supported_repairs(cases);horizon_evidence_requires_known_completion_and_supported_cut(cases);
    restoration_deadline_uses_final_owner_end(cases);
    checkpoint_budget_denial_is_flat_and_reusable(cases);
    std::printf("{\"schema\":\"plan4-covenant-future-tests/1\",\"passed\":true,\"cases\":%d,\"engineWitnesses\":0}\n",cases);
}
