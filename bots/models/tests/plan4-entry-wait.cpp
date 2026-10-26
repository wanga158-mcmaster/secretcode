#include "../common/pure.h"
#include <cassert>
#include <iostream>
#include <memory>
using namespace plan4;
int metric(Point a,Point b,const void*){return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
struct Context {
    int calls=0, release=4, mode=0;
    bool budget_ok=true;
    std::array<PhaseEntry,8> entries{};
};
EntryWait quote(const PhaseEntry& entry,const Information&,const void*,void* value) {
    auto& state=*static_cast<Context*>(value);
    assert(state.calls<8);state.entries[state.calls++]=entry;
    if(state.mode==1)return {4,false,true};
    if(state.mode==2)return {4,true,false};
    if(state.mode==3)return {-1,true,true};
    if(state.mode==4){state.budget_ok=false;return {4,true,true};}
    if(state.mode==5)return {0,true,false};
    return {state.mode==6?state.release:std::max(0,state.release-entry.now),true,true};
}
int main() {
    auto i=std::make_unique<Information>();i->own.id=17;i->own.health=i->own.maximum_health=1000;
    i->own.bag=10;i->own.position=i->own_spawn={0,0};i->travel={metric,nullptr};
    i->group_count=i->altar_count=1;i->altars[0].position={4,0};i->altars[0].lock_known=true;
    auto& group=i->groups[0];group.id=25;group.position={2,0};group.visible=group.occupancy_known=true;
    group.health=group.full_health=100;group.clear_rounds=group.full_clear_rounds=2;
    group.incoming_damage=group.full_incoming_damage=1;group.lifecycle=Lifecycle::living;
    group.provenance=group.health_provenance={0,Source::personal_vision,Status::exact_now};
    group.gems=24;group.gold=10;group.respawn_rounds=20;
    Menu menu;menu.count=menu.single_count=1;menu.jobs[0]={JobKind::farm,0,-1,0};
    auto base=std::make_unique<Forecast>(),active=std::make_unique<Forecast>(),control=std::make_unique<Forecast>();
    auto checkpoint=std::make_unique<SimulationState>();
    simulate_first_into(*i,menu.jobs[0],menu,*base,*checkpoint);
    Context context;ForecastHooks hooks;hooks.path=&context;hooks.entry_wait=quote;
    simulate_first_into(*i,menu.jobs[0],menu,*active,*checkpoint,hooks);
    int cases=0;
    assert(context.calls==2&&active->first_conversion_round==base->first_conversion_round+4);
    assert(active->final_health==base->final_health&&active->our_secured==base->our_secured&&active->final_gold==base->final_gold);++cases;
    const auto& first=context.entries[0];const auto& bank=context.entries[1];
    assert(first.kind==EventKind::travel&&first.group==0&&first.altar==-1&&first.now==0&&first.nominal==2);
    assert(first.from==(Point{0,0})&&first.to==(Point{2,0})&&first.health==1000&&first.bag==10);
    assert(bank.group==-1&&bank.altar==0&&bank.from==(Point{2,0})&&bank.to==(Point{4,0})&&bank.bag==34);++cases;
    int waits=0;for(int n=0;n<active->event_count;++n)if(active->events[n].kind==EventKind::wait){
        ++waits;assert(active->events[n].health_after==1000);assert(active->events[n].bag_after==10);}
    assert(waits==1);++cases;
    context={};context.mode=6;
    simulate_first_into(*i,menu.jobs[0],menu,*control,*checkpoint,hooks);
    assert(control->first_conversion_round==active->first_conversion_round+4&&control->final_health==active->final_health);++cases;
    for(int mode=1;mode<=3;++mode){context={};context.mode=mode;
        simulate_first_into(*i,menu.jobs[0],menu,*active,*checkpoint,hooks);
        assert(active->abort_reason==AbortReason::unsupported_wait&&!active->completed_conversion&&active->our_secured==0);
        assert(active->final_bag==10&&active->final_gold==0&&active->final_health==1000);++cases;
    }
    context={};context.mode=4;
    i->work_budget={[](const void* value){return static_cast<const Context*>(value)->budget_ok;},&context};
    simulate_first_into(*i,menu.jobs[0],menu,*active,*checkpoint,hooks);
    assert(active->budget_exhausted&&!active->event_count&&!active->completed_conversion&&active->final_bag==10);++cases;
    i->work_budget={};context={};context.mode=5;
    simulate_first_into(*i,menu.jobs[0],menu,*active,*checkpoint,hooks);
    assert(active->first_conversion_round==base->first_conversion_round&&active->event_count==base->event_count);
    assert(active->our_secured==base->our_secured&&active->final_health==base->final_health);++cases;
    context={};context.release=129;
    simulate_first_into(*i,menu.jobs[0],menu,*active,*checkpoint,hooks);
    assert(active->abort_reason==AbortReason::horizon&&!active->completed_conversion&&!active->our_secured);++cases;
    std::cout<<"entry wait cases="<<cases<<"\n";
}
