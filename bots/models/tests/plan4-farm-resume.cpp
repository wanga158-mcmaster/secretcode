#include "../common/pure.h"
#include <cassert>
#include <iostream>
#include <memory>
using namespace plan4;
int distance_quote(Point a,Point b,const void*) { return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y)); }
int main() {
    auto information=std::make_unique<Information>();auto& i=*information;
    i.own.id=7;i.own.health=i.own.maximum_health=1000;i.own.bag=11;i.own.gold=9;
    i.own.position=i.own_spawn={0,0};i.own.on_spawn=true;i.travel={distance_quote,nullptr};
    i.group_count=2;i.altar_count=1;
    for (int n=0;n<2;++n) {
        auto& g=i.groups[n];g.id=10+n;g.position={2+n*2,0};g.gems=24;g.gold=7;
        g.visible=g.occupancy_known=g.all_spawns_visible=true;g.lifecycle=Lifecycle::living;
        g.clear_rounds=g.full_clear_rounds=3;g.health=100;g.respawn_rounds=40;
        g.provenance=g.health_provenance={i.round,Source::personal_vision,Status::exact_now};
    }
    i.altars[0].position={6,0};i.altars[0].altar_position={6,1};
    i.altars[0].accessible=i.altars[0].lock_known=true;
    Menu menu;menu.count=2;menu.single_count=1;
    menu.jobs[0]={JobKind::farm,0,-1,0};menu.jobs[0].menu_rank=0;
    menu.jobs[1]={JobKind::farm,0,1,0};menu.jobs[1].menu_rank=1;
    auto baseline=std::make_unique<Forecast>(),full=std::make_unique<Forecast>(),prefix=std::make_unique<Forecast>();
    auto out=std::make_unique<Forecast>();auto state=std::make_unique<SimulationState>(),next=std::make_unique<SimulationState>();
    int cases=0;
    for (int pair=0;pair<2;++pair) {
        const Job job=menu.jobs[pair];
        simulate_first_into(i,job,menu,*full,*next);
        assert(initialize_state_into(i,menu,*baseline,*state));
        *prefix=*full;int first_capture=-1;
        for (int n=0;n<full->event_count;++n) if (full->events[n].kind==EventKind::capture) {first_capture=n;break;}
        assert(first_capture>=0);const auto& capture=full->events[first_capture];
        prefix->event_count=first_capture+1;prefix->our_secured=prefix->enemy_secured=0;
        prefix->completed_conversion=false;prefix->first_conversion_round=-1;prefix->terminal_z=0;
        prefix->final_round=state->now=capture.end.round;prefix->final_bag=state->bag=capture.bag_after;
        prefix->final_gold=state->gold=capture.gold_after;prefix->final_health=state->health=capture.health_after;
        state->position=i.groups[0].position;
        state->groups[0]={Lifecycle::captured,state->now+i.groups[0].respawn_rounds,true,true};
        assert(complete_farm_after_first_into(i,job,menu,*prefix,*state,*out,*next));
        assert(out->macros_started==1 && out->our_secured==unsigned(pair?59:35) && out->event_count==full->event_count &&
            out->final_round==full->final_round && out->final_bag==full->final_bag && out->final_gold==full->final_gold);++cases;
        for (int n=0;n<out->event_count;++n) assert(out->events[n].kind==full->events[n].kind &&
            out->events[n].end.round==full->events[n].end.round && out->events[n].bag_after==full->events[n].bag_after);
        const auto reject=[&] {assert(!complete_farm_after_first_into(i,job,menu,*prefix,*state,*out,*next));++cases;};
        ++prefix->macros_started;reject();--prefix->macros_started;
        ++state->bag;reject();--state->bag;
        ++state->gold;reject();--state->gold;
        state->groups[0].lifecycle=Lifecycle::living;reject();state->groups[0].lifecycle=Lifecycle::captured;
        ++state->groups[1].ready_round;reject();--state->groups[1].ready_round;
        ++prefix->events[first_capture].bag_before;reject();--prefix->events[first_capture].bag_before;
        ++prefix->events[first_capture].group_id;reject();--prefix->events[first_capture].group_id;
        ++state->source_actor;reject();--state->source_actor;
        prefix->our_secured=1;reject();prefix->our_secured=0;
        Job absent=job;absent.menu_rank=9;
        assert(!complete_farm_after_first_into(i,absent,menu,*prefix,*state,*out,*next));++cases;
        assert(!complete_farm_after_first_into(i,job,menu,*prefix,*state,*prefix,*next));++cases;
    }
    std::cout<<"plan4 farm resume "<<cases<<" cases passed\n";
}
