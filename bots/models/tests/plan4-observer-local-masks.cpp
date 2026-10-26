#define PLAN4_OBSERVER_LOCAL_MASKS 1
#include "../common/observer.h"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace plan4;
namespace cm = cambc::mock;
int distance(Point a, Point b, const void*) { return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y)); }
int main() {
    cm::round=1;cm::fuel=200000;
    cambc::bitboard raw;
    for (auto p : {cambc::vec2{0,0},{7,0},{8,0},{49,0},{0,49},{49,49},{13,23}}) raw.set(p,true);
    const int queries_before=cm::queries;
    ObserverMaskSnapshot cached(raw,50,50);
    const int exported=cm::queries;
    for(int y=0;y<50;++y) for(int x=0;x<50;++x)
        assert(cached.is_set({x,y})==raw.cells[y*50+x]);
    assert(cm::queries==exported&&exported>queries_before);
    raw.set({0,0},false);raw.set({1,0},true);
    assert(cached.is_set({0,0})&&!cached.is_set({1,0}));
    ObserverMaskSnapshot current(raw,50,50);
    assert(!current.is_set({0,0})&&current.is_set({1,0}));
    // An unsupported layout preserves the SDK path instead of inventing bits.
    ObserverMaskSnapshot unsupported(raw,49,50);
    const int fallback_before=cm::queries;
    assert(unsupported.is_set({1,0})&&cm::queries>fallback_before);
    cm::visible.fill(true);cm::actors={{0,{5,5},true,true,cambc::team::ally},
        {2,{6,5},true,true,cambc::team::ally},{7,{8,5},true,true,cambc::team::enemy}};
    cm::actor(7).range=3;cm::allied_spawns={{0,0}};cm::altar_positions={{2,0}};
    cambc::map::mock_groups={{0,{{100,{5,5},0},{101,{6,5},0}}}};
    auto observer=std::make_unique<OnlineObserver>();auto info=std::make_unique<Information>();
    assert(observer->observe_into(*info,{distance,nullptr},40000));
    assert(!info->observation_truncated&&info->groups[0].visible&&info->groups[0].occupancy_known);
    assert(info->groups[0].eligible_allied_occupants==2&&!info->own.recall_safe&&!cm::unauthorized);
    ++cm::round;cm::fuel=200000;cm::actor(7).shown=false;cm::actor(2).shown=false;
    assert(observer->observe_into(*info,{distance,nullptr},40000));
    assert(!info->observation_truncated&&info->groups[0].eligible_allied_occupants==1&&info->own.recall_safe&&!cm::unauthorized);
    ++cm::round;cm::fuel=200000;cm::visible.fill(false);cm::visible[cm::tile({5,5})]=true;
    assert(observer->observe_into(*info,{distance,nullptr},40000));
    assert(info->groups[0].visible&&!info->groups[0].all_spawns_visible&&!info->groups[0].occupancy_known&&!cm::unauthorized);
    ++cm::round;cm::fuel=41000;
    assert(!observer->observe_into(*info,{distance,nullptr},40000));
    assert(info->group_count==0&&info->altar_count==0&&!info->own.alive);
    std::puts("observer local masks: packed parity, snapshot lifetime, fallback, legal visibility and admission pass");
}
