#pragma once
#include "../common/src/competitive.h"
#include "../common/src/doctrines_a.h"
#include "../common/src/doctrines_b.h"
#include <cstdio>
#include <cstdlib>

namespace {
using namespace p7::live;
int checks=0;
#define CHECK_PARTIAL(condition) do {++checks;if(!(condition)){std::fprintf(stderr,"partial check line %d: %s\n",__LINE__,#condition);std::abort();}}while(false)

Frame contact_card() {
    Frame f;f.design=Design::harvest_and_exit;f.round=171;
    f.tile=4754;f.health=f.max_health=900;f.damage=55;f.focus=100;
    f.kit_level=0;f.team_size=5;f.ordinal=1;f.source_count=2;
    // HP, incoming, stock and owner health come from the actual first post-heal
    // contact. Remaining frame/quote inputs are supplied to isolate selection;
    // this fixture is not a reconstruction of private native Frame memory.
    auto& monolith=f.sources[0];monolith.id=17;monolith.tile=5151;
    monolith.distance=4;monolith.spawn_distance=70;monolith.cycle_turns=600;
    monolith.gems=228;monolith.gold=300;monolith.health=20360;monolith.incoming=200;
    monolith.seen=monolith.reachable=monolith.home_connected=true;
    monolith.feasible=monolith.joint_feasible=monolith.reward_available=false;
    auto& inspect=f.sources[1];inspect.id=10;inspect.tile=0;inspect.distance=30;
    inspect.spawn_distance=57;inspect.cycle_turns=80;inspect.gold=40;
    inspect.health=240;inspect.incoming=0;inspect.reachable=inspect.home_connected=true;
    inspect.seen=inspect.feasible=inspect.joint_feasible=false;
    return f;
}

void predicate_boundaries() {
    Frame f=contact_card();auto x=f.sources[0];
    CHECK_PARTIAL(f.health>std::max(f.max_health*.55f,self_camp_incoming(f,x)*3+f.max_health*.2f));
    CHECK_PARTIAL(competitive_detail::productive(f,x)==!bool(P7_NO_PARTIAL_ADMISSION));
    f.health=780;CHECK_PARTIAL(!competitive_detail::productive(f,x));
    f.health=779;CHECK_PARTIAL(!competitive_detail::productive(f,x));
    f.health=900;
    x.seen=false;CHECK_PARTIAL(competitive_detail::productive(f,x)); // inspect only
    x.seen=true;x.feasible=true;CHECK_PARTIAL(competitive_detail::productive(f,x));
    x.feasible=false;x.reward_available=true;CHECK_PARTIAL(competitive_detail::productive(f,x));
    x.reward_available=false;x.joint_feasible=true;
    CHECK_PARTIAL(competitive_detail::productive(f,x)); // preserve existing joint farm admission
    f.health=780;CHECK_PARTIAL(!competitive_detail::productive(f,x)); // add no new permission
    f.health=900;x.joint_feasible=false;
    CHECK_PARTIAL(!competitive_detail::productive(f,x,true)); // existing affordable veto
    x.seen=false;x.respawning=true;CHECK_PARTIAL(!competitive_detail::productive(f,x));
    x.respawning=false;x.distance=-1;CHECK_PARTIAL(!competitive_detail::productive(f,x));
    x.distance=1;x.gems=0;x.gold=40;f.kit_level=10;
    CHECK_PARTIAL(!competitive_detail::productive(f,x)); // old mature capital limit
}

void actual_planner_can_change_discovery() {
    for(bool retained:{false,true}) {
        Frame f=contact_card();CompetitiveState state;
        if(retained){state.source=0;state.best_distance=4;state.last_progress=f.round;state.last_tile=f.tile;}
        Task proposal;proposal.goal=Goal::recover;
        const auto task=competitive_plan(f,proposal,state);
#if P7_NO_PARTIAL_ADMISSION
        CHECK_PARTIAL(task.goal==Goal::inspect&&task.source==1&&state.source==1);
#else
        CHECK_PARTIAL(task.goal==Goal::farm&&task.source==0&&state.source==0);
#endif
    }
    Frame f=contact_card();CompetitiveState stale;
    stale.source=0;stale.best_distance=4;stale.last_progress=f.round-8;stale.last_tile=f.tile;
    Task proposal;proposal.goal=Goal::recover;
    const auto after_timeout=competitive_plan(f,proposal,stale);
    CHECK_PARTIAL(after_timeout.goal==Goal::inspect&&after_timeout.source==1);
}

void bypass_and_certificate_limits() {
    Frame f=contact_card();CompetitiveState state;
    // With no candidate, the controller's farm proposal is still returned.
    // This ablation is not a universal ban on all partial programmes.
    f.source_count=1;Task proposal=farm(0);
    CHECK_PARTIAL(competitive_plan(f,proposal,state)==proposal);
    f=contact_card();f.sources[0].joint_feasible=true;
    state={};CHECK_PARTIAL(competitive_plan(f,farm(0),state).source==0);
    f.ally_count=1;f.allies[0].id=42;f.allies[0].group=0;
    f.allies[0].visible=f.allies[0].can_attack=true;
    Task rally=farm(0);rally.goal=Goal::rally;state={};
    CHECK_PARTIAL(competitive_detail::supported_intent(f,rally,state,false));
    f.sources[0].joint_feasible=false;f.healing_ready=true;
    f.allies[0].health=50;f.allies[0].max_health=100;f.allies[0].tile=17;
    Task heal=farm(0);heal.heal=true;heal.peer=42;heal.waypoint=17;
    CHECK_PARTIAL(competitive_detail::supported_intent(f,heal,state,false));
    f=contact_card();f.channel_active=true;state={};
    CHECK_PARTIAL(competitive_plan(f,proposal,state)==proposal);
    f.channel_active=false;f.safe_capture=true;state={};
    CHECK_PARTIAL(competitive_plan(f,proposal,state)==proposal);
    // Existing complete solo work remains available in each geographic frame.
    for(int design=1;design<=16;++design){
        f=contact_card();f.design=static_cast<Design>(design);f.source_count=1;
        f.sources[0].feasible=true;state={};
        CHECK_PARTIAL(competitive_detail::productive(f,f.sources[0]));
        const auto task=competitive_plan(f,farm(0),state);
        CHECK_PARTIAL(task.goal==Goal::farm&&task.source==0&&state.source==0);
    }
}
}
int main(){predicate_boundaries();actual_planner_can_change_discovery();bypass_and_certificate_limits();
    std::printf("partial admission arm=%d checks=%d\n",P7_NO_PARTIAL_ADMISSION,checks);}
