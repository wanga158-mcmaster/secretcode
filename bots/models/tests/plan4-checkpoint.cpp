#include "../common/pure.h"
#include <cassert>
#include <iostream>
#include <memory>
#include <tuple>
using namespace plan4;
int metric(Point a,Point b,const void*){return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
auto outcome(const Forecast& f){return std::make_tuple(f.event_count,f.macros_started,f.final_round,f.first_conversion_round,
    f.our_secured,f.enemy_secured,f.final_bag,f.final_gold,f.final_health,f.terminal_z,f.terminal_unknown,f.truncated,
    f.stock_unknown,f.travel_unknown,f.no_conversion,f.completed_conversion,int(f.abort_reason));}
void same(const Forecast& a,const Forecast& b){
    assert(outcome(a)==outcome(b));
    for(int n=0;n<a.event_count;++n){const auto& x=a.events[n];const auto& y=b.events[n];
        assert(x.kind==y.kind&&x.start.round==y.start.round&&x.start.phase==y.start.phase&&
            x.end.round==y.end.round&&x.end.phase==y.end.phase&&x.group_id==y.group_id&&
            x.bag_before==y.bag_before&&x.bag_after==y.bag_after&&x.secured==y.secured&&
            x.health_after==y.health_after&&x.gold_after==y.gold_after&&x.status==y.status);}
}
void regressions(int& cases){
    auto source=std::make_unique<Information>();auto& i=*source;
    i.own.id=7;i.own.health=1;i.own.maximum_health=1000;i.own.on_spawn=true;
    i.own.position=i.own_spawn={0,0};i.travel={metric,nullptr};i.settings.max_rounds=2000;
    i.settings.capture_rounds=0;i.settings.spawn_heal_fraction=.008125f;
    i.group_count=i.altar_count=1;auto& g=i.groups[0];g.id=10;g.position={1,0};g.gems=24;g.gold=7;
    g.respawn_rounds=40;g.lifecycle=Lifecycle::cleared;g.visible=g.reward_available=g.occupancy_known=true;
    g.provenance={0,Source::personal_vision,Status::exact_now};
    i.altars[0].position={1,0};i.altars[0].altar_position={1,1};i.altars[0].accessible=i.altars[0].lock_known=true;
    Menu menu;menu.count=menu.single_count=1;menu.jobs[0]={JobKind::farm,0,-1,0};
    auto prior=std::make_unique<Forecast>(),out=std::make_unique<Forecast>();
    auto state=std::make_unique<SimulationState>(),next=std::make_unique<SimulationState>();
    assert(initialize_state_into(i,menu,*prior,*state));
    assert(prior->event_count==0&&prior->macros_started==0&&prior->final_round==i.round&&
        prior->final_bag==i.own.bag&&state->health==i.own.health&&state->position==i.own.position);++cases;
    i.own.health=0;const auto unchanged=state->layout_fingerprint;
    assert(!initialize_state_into(i,menu,*out,*state)&&state->layout_fingerprint==unchanged);i.own.health=1;++cases;
    state->health=prior->final_health=0;int cursor=0;
    assert(!continue_from_into(i,menu,*prior,*state,*out));
    assert(!advance_fixed_into(i,menu,*prior,*state,*out,*next,cursor).valid);++cases;
    state->stopped=true;assert(continue_from_into(i,menu,*prior,*state,*out)&&out->our_secured==0&&out->event_count==0);++cases;
    assert(initialize_state_into(i,menu,*prior,*state));
    ++i.our_score;assert(!continue_from_into(i,menu,*prior,*state,*out));--i.our_score;++cases;
    ++i.enemy_score;assert(!continue_from_into(i,menu,*prior,*state,*out));--i.enemy_score;++cases;
    ++i.own.position.x;assert(!continue_from_into(i,menu,*prior,*state,*out));--i.own.position.x;++cases;
    i.own.on_spawn=false;assert(!continue_from_into(i,menu,*prior,*state,*out));i.own.on_spawn=true;++cases;
    ++i.own_spawn.x;assert(!continue_from_into(i,menu,*prior,*state,*out));--i.own_spawn.x;++cases;
    // Quoted kit/cash/HP changes are explicitly permitted at a macro boundary.
    i.own.maximum_health=1200;state->health=prior->final_health=1100;state->gold=prior->final_gold=3;
    g.clear_rounds=g.full_clear_rounds=2;g.incoming_damage=g.full_incoming_damage=1;
    cursor=0;assert(advance_fixed_into(i,menu,*prior,*state,*out,*next,cursor).valid&&out->our_secured==24&&out->final_gold==10);++cases;
    i.own.maximum_health=1000;g.clear_rounds=g.full_clear_rounds=0;g.incoming_damage=g.full_incoming_damage=0;
    // A long, legitimately represented heal creates a near-full event prefix.
    // Its following capture fits; the bank transaction does not. It must retain
    // the captured bag and gold rather than manufacture a winning conversion.
    i.our_score=990;Job renew;renew.kind=JobKind::renew;
    simulate_first_into(i,renew,menu,*prior,*state);assert(prior->event_count==123&&state->now==123);
    cursor=0;auto result=advance_fixed_into(i,menu,*prior,*state,*out,*next,cursor);
    assert(result.valid&&out->abort_reason==AbortReason::event_capacity&&out->truncated&&next->stopped&&
        out->our_secured==0&&out->terminal_z==0&&out->final_bag==24&&out->final_gold==7&&out->event_count==127&&
        next->groups[0].lifecycle==Lifecycle::captured);++cases;
    // Only one slot remains after travel. No unlogged capture may mutate stock,
    // inventory, gold, or the model's capture callback history.
    i.settings.spawn_heal_fraction=.0079375f;int captures=0;
    ForecastHooks hooks;hooks.path=&captures;hooks.captured=[](int,int,const Information&,const void*,void* path){++*static_cast<int*>(path);};
    simulate_first_into(i,renew,menu,*prior,*state);assert(prior->event_count==126&&state->now==126);
    cursor=0;result=advance_fixed_into(i,menu,*prior,*state,*out,*next,cursor,hooks);
    assert(result.valid&&out->abort_reason==AbortReason::event_capacity&&out->event_count==127&&out->final_bag==0&&
        out->final_gold==0&&captures==0&&next->groups[0].lifecycle==Lifecycle::cleared&&next->now==127);++cases;
    // Boundary-only prefixes isolate lower-level mutations after a full buffer.
    // Their inert padded rows are not claimed to be an engine observation.
    auto pad=[&](int count){Event event{};event.kind=EventKind::wait;event.start=event.end={state->now,Phase::owner_action};
        event.health_after=state->health;event.bag_before=event.bag_after=state->bag;event.gold_after=state->gold;event.status=Status::predicted;
        for(int n=prior->event_count;n<count;++n)prior->events[n]=event;
        prior->event_count=count;};
    i.our_score=0;i.own.health=500;i.settings.spawn_heal_fraction=.2f;
    assert(initialize_state_into(i,menu,*prior,*state));pad(max_events);
    cursor=0;result=advance_fixed_into(i,menu,*prior,*state,*out,*next,cursor);
    assert(result.valid&&out->abort_reason==AbortReason::event_capacity&&out->macros_started==0&&next->now==0&&
        next->position==state->position&&next->health==500&&next->groups[0].lifecycle==Lifecycle::cleared);++cases;
    assert(initialize_state_into(i,menu,*prior,*state));pad(max_events-1);
    {
        detail::Simulation simulation(i,menu,*out);assert(simulation.restore(*state));*out=*prior;
        simulation.run_first(renew);simulation.checkpoint(*next);
    }
    assert(out->abort_reason==AbortReason::event_capacity&&out->event_count==max_events&&next->health==700&&next->now==1);++cases;
    // Travel cannot vacate owned stock when there is no room for its event.
    i.altars[0].position={3,0};
    assert(initialize_state_into(i,menu,*prior,*state));state->groups[0]={Lifecycle::captured,40,true,true};
    state->position=g.position;pad(max_events);Job bank;bank.kind=JobKind::bank;bank.altar=0;
    {
        detail::Simulation simulation(i,menu,*out);assert(simulation.restore(*state));*out=*prior;
        simulation.run_first(bank);simulation.checkpoint(*next);
    }
    assert(next->groups[0].occupied_by_us&&next->groups[0].ready_round==40&&next->position==g.position);++cases;
}
int main(){
    auto data=std::make_unique<Information>();auto& i=*data;
    i.own.health=i.own.maximum_health=1000;i.own.bag=10;i.own.recall_safe=true;i.travel={metric,nullptr};
    i.group_count=2;i.altar_count=1;i.altars[0].position={4,0};i.altars[0].altar_position={4,1};i.altars[0].lock_known=true;
    for(int n=0;n<2;++n){auto& g=i.groups[n];g.id=10+n;g.position={2+n*4,0};g.gems=24;g.gold=10;
        g.full_health=g.health=100;g.clear_rounds=g.full_clear_rounds=2;g.incoming_damage=g.full_incoming_damage=1;
        g.respawn_rounds=20;g.lifecycle=Lifecycle::living;g.visible=g.all_spawns_visible=g.occupancy_known=true;
        g.provenance=g.health_provenance={0,Source::personal_vision,Status::exact_now};}
    auto direct=std::make_unique<Forecast>(),first=std::make_unique<Forecast>(),resumed=std::make_unique<Forecast>();
    auto state=std::make_unique<SimulationState>();int cases=0;
    auto next=std::make_unique<SimulationState>();auto stepped=std::make_unique<Forecast>();
    for(int scenario=0;scenario<6;++scenario){
        i.round=0;i.settings.max_rounds=scenario==1?14:scenario==2?200:2000;
        i.our_score=scenario==3?990:0;i.own.channel=scenario==4?Channel::tribute:scenario==5?Channel::recall:Channel::none;
        i.own.channel_ticks=3;const auto menu=make_menu(i);
        for(int n=0;n<menu.count;++n){predict_into(i,menu.jobs[n],menu,*direct);
            simulate_first_into(i,menu.jobs[n],menu,*first,*state);
            assert(continue_from_into(i,menu,*first,*state,*resumed));same(*direct,*resumed);
            int cursor=0,steps=0;
            for(;;){const auto result=advance_fixed_into(i,menu,*first,*state,*stepped,*next,cursor);
                assert(result.valid);if(!result.advanced)break;
                assert(++steps<=max_macros);std::swap(first,stepped);std::swap(state,next);}
            same(*direct,*stepped);++cases;}
    }
    i.own.channel=Channel::none;i.our_score=0;i.settings.max_rounds=2000;
    const auto menu=make_menu(i);simulate_first_into(i,menu.jobs[0],menu,*first,*state);
    assert(!continue_from_into(i,menu,*first,*state,*first));++cases;
    const int now=state->now;state->now=-1;assert(!continue_from_into(i,menu,*first,*state,*resumed));state->now=now;++cases;
    const float hp=state->health;state->health=i.own.maximum_health+1;assert(!continue_from_into(i,menu,*first,*state,*resumed));state->health=hp;++cases;
    ++i.groups[0].id;assert(!continue_from_into(i,menu,*first,*state,*resumed));--i.groups[0].id;++cases;
    ++i.own.id;assert(!continue_from_into(i,menu,*first,*state,*resumed));--i.own.id;++cases;
    ++i.settings.capture_rounds;assert(!continue_from_into(i,menu,*first,*state,*resumed));--i.settings.capture_rounds;++cases;
    ++i.altars[0].position.x;assert(!continue_from_into(i,menu,*first,*state,*resumed));--i.altars[0].position.x;++cases;
    Menu fixed;fixed.count=fixed.single_count=3;
    for(int n=0;n<3;++n)fixed.jobs[n]={JobKind::farm,n,-1,0};
    const auto allowed=[](const Job& job,const void*){return job.group!=1;};int cursor=1;
    assert(next_fixed_job(fixed,1,128,1,cursor,allowed,nullptr)==2&&cursor==0);++cases;
    assert(next_fixed_job(fixed,1,128,8,cursor,allowed,nullptr)==-1&&cursor==0);++cases;
    assert(next_fixed_job(fixed,128,128,1,cursor,allowed,nullptr)==-1);++cases;
    fixed.single_count=17;assert(next_fixed_job(fixed,1,128,1,cursor,allowed,nullptr)==-1);++cases;
    regressions(cases);
    std::cout<<"{\"schema\":\"plan4-checkpoint-tests/1\",\"passed\":true,\"cases\":"<<cases<<",\"engineWitnesses\":0}\n";
}
