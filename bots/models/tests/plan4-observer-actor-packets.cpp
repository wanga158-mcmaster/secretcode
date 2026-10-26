#ifndef PLAN4_OBSERVER_ACTOR_PACKETS
#define PLAN4_OBSERVER_ACTOR_PACKETS 1
#endif
#include "../common/observer.h"
#include <cassert>
#include <cstdio>
#include <memory>
namespace cm=cambc::mock;
using namespace plan4;
int distance(Point a,Point b,const void*){return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
int main(){
    cm::reset();cm::query_cost=1;cm::fuel=200000;cm::visible.fill(true);
    cm::actors={{0,{5,5},true,true,cambc::team::ally},{2,{6,5},true,true,cambc::team::ally},
                {7,{8,5},true,true,cambc::team::enemy}};
    cm::actor(7).range=3;
    cm::allied_spawns={{0,0}};cm::altar_positions={{2,0}};
    cambc::map::mock_groups={{0,{{100,{5,5},0},{101,{6,5},0}}}};
    auto observer=std::make_unique<OnlineObserver>();auto info=std::make_unique<Information>();
    assert(observer->observe_into(*info,{distance,nullptr},40000));
    assert(!info->observation_truncated&&info->group_count==1&&info->own.position==(Point{5,5}));
    assert(!info->own.recall_safe&&info->groups[0].eligible_allied_occupants==2&&info->groups[0].own_is_lowest_occupant);
    assert(info->groups[0].occupied_spawn&&info->groups[0].reward_available&&!info->groups[0].contested&&!cm::unauthorized);
    ++cm::round;cm::fuel=200000;cm::actor(7).shown=false;
    assert(observer->observe_into(*info,{distance,nullptr},40000));
    assert(!info->observation_truncated&&info->own.recall_safe&&!cm::unauthorized);
    ++cm::round;cm::fuel=200000;cm::actor(2).shown=false;
    assert(observer->observe_into(*info,{distance,nullptr},40000));
    assert(!info->observation_truncated&&info->own.recall_safe&&info->groups[0].eligible_allied_occupants==1&&!cm::unauthorized);
    ++cm::round;cm::fuel=200000;cm::actor(2).shown=true;cm::actor(2).position={7,7};
    assert(observer->observe_into(*info,{distance,nullptr},40000));
    assert(!info->observation_truncated&&info->groups[0].eligible_allied_occupants==1&&!cm::unauthorized);
    // Positive/hidden/current-position facts above are identical in both arms.
#if PLAN4_OBSERVER_ACTOR_PACKETS
    ++cm::round;cm::fuel=200000;cm::raw_error_actor=2;cm::raw_error=12;bool threw=false;
    try{observer->observe_into(*info,{distance,nullptr},40000);}catch(const std::runtime_error&){threw=true;}
    assert(threw&&!cm::unauthorized);
#endif
    std::puts("observer actor facts PASS: current visibility, allocation, recall and position refresh; no native-cost inference");
}
