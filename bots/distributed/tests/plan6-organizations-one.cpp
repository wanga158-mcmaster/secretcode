// Pure personal-view fixtures: no SDK, native engine or game reservations.
#include "../caravan/src/policy.h"
#include "../trailhead/src/policy.h"
#include "../guilds/src/policy.h"
#include "../mesh/src/policy.h"
#include "../launchpad/src/policy.h"
#include "../cadence/src/policy.h"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>

using namespace p6;
using namespace p6::organization;

static int checks=0;
static void check_at(bool condition,int line,const char* expression) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr,"FAIL organizations 01-06 line %d: %s\n",line,expression);
        std::exit(1);
    }
}
// Remains active in the optimized -DNDEBUG runner.
#define check(condition) check_at(bool(condition),__LINE__,#condition)

static void refresh(Input& in,int round) {
    in.frame.clock.round=round;
    for (int i=0; i<in.frame.actor_count; ++i) in.frame.actors[i].clock=in.frame.clock;
    in.frame.group.clock=in.frame.clock;
    for (int i=0; i<in.site_count; ++i)
        for (int a=0; a<in.sites[i].actor_count; ++a) in.sites[i].cycles[a].clock=in.frame.clock;
}
static std::unique_ptr<Input> fixture(int ordinal=1,int picks=3) {
    auto heap=std::make_unique<Input>();
    auto& in=*heap;
    in.ordinal=ordinal; in.picks=picks;
    in.settings={2000,1000,5};
    in.frame.clock={10,10+ordinal,0};
    in.frame.own.actor.id=10+ordinal;
    in.frame.own.actor.pick_ordinal=ordinal;
    in.frame.own.actor.team=Team::ally;
    in.frame.own.actor.alive=true;
    in.frame.own.actor.health=in.frame.own.actor.max_health=100;
    in.frame.own.actor.strength=20;
    in.frame.own.actor.ready_attack_group=7;
    in.frame.own.actor.can_attack=true;
    in.frame.own.actor.position={2+ordinal,2};
    for (int a=0; a<picks; ++a) if (a!=ordinal) {
        auto actor=in.frame.own.actor;
        actor.id=10+a; actor.pick_ordinal=a; actor.position={2+a,2};
        auto& observation=in.frame.actors[in.frame.actor_count++];
        observation={actor,in.frame.clock,actor.id,Knowledge::exact};
    }
    in.site_count=2;
    for (int i=0; i<in.site_count; ++i) {
        auto& site=in.sites[i];
        site.group.id=7+i; site.group.approach={7+i,0}; site.group.retreat={7+i,2};
        site.group.spawn={7+i,1}; site.group.lifecycle=Lifecycle::living;
        site.known=true; site.joint_better=true; site.holder=12;
        site.actor_count=picks;
        for (int a=0; a<picks; ++a) {
            site.actors[a]=10+a;
            auto& c=site.cycles[a];
            c.clock=in.frame.clock; c.actor=10+a; c.group=site.group.id;
            c.solo=c.route_known=c.bank_executable=c.renewal_reachable=true;
            Exposure e{100,100,0,0,0,0,true};
            c.clear=c.hold=c.bank=c.recovery=e;
        }
    }
    in.proposed_group=7; in.independent_group=8; in.safe_now=true;
    in.rally={1,1}; in.bank_approach={0,0};
    return heap;
}
static Actor& ally(Input& in,int ordinal) {
    if (in.ordinal==ordinal) return in.frame.own.actor;
    for (int a=0; a<in.frame.actor_count; ++a)
        if (in.frame.actors[a].value.pick_ordinal==ordinal) return in.frame.actors[a].value;
    check(false); return in.frame.own.actor;
}
static void hide(Input& in,int ordinal) {
    for (int a=0; a<in.frame.actor_count; ++a)
        if (in.frame.actors[a].value.pick_ordinal==ordinal)
            in.frame.actors[a].knowledge=Knowledge::unknown;
}
static void no_solo(Input& in) {
    for (auto& site:in.sites) for (auto& c:site.cycles) c.solo=false;
}
static void rosters() {
    const std::array profiles{caravan::profile_value,trailhead::profile_value,guilds::profile_value,
                              mesh::profile_value,launchpad::profile_value,cadence::profile_value};
    for (const auto& p:profiles) for (int i=0; i<5; ++i) {
        check(p.roster[i]!=Champion::cassi && p.roster[i]!=Champion::issac);
        for (int j=0; j<i; ++j) check(p.roster[i]!=p.roster[j]);
    }
}
static void caravan_cases() {
    auto heap=fixture(); auto& in=*heap;
    caravan::State active,control;
    in.actual_payout=true; in.phase=Phase::bank; in.frame.own.actor.bag=12;
    check(caravan::choose(active,in,false).intent==Intent::bank);
    check(caravan::choose(control,in,true).intent==Intent::independent);
    in=*fixture(); active={}; hide(in,0);
    for (int turn=0; turn<8; ++turn) {
        refresh(in,10+turn);
        check(caravan::choose(active,in,false).intent==Intent::rally);
    }
    refresh(in,18);
    check(caravan::choose(active,in,false).intent==Intent::independent);
    check(active.project.retired_group==7);
    in=*fixture(); refresh(in,19);
    check(caravan::choose(active,in,false).intent==Intent::independent);
    refresh(in,64);
    check(caravan::choose(active,in,false).intent==Intent::work);
    in=*fixture(); active={}; no_solo(in); ally(in,2).ready_attack_group=-1;
    check(caravan::choose(active,in,false).intent!=Intent::work);
    // Both policies first choose among the same real production alternatives.
    // A zero-gem own payout then leaves the active follower in crew logistics.
    in=*fixture(); caravan::Policy convoy, breakup;
    check(convoy.choose(in,false).group==7 && breakup.choose(in,true).group==7);
    check(in.site(in.independent_group)->solo(in.frame,in.frame.clock.self));
    refresh(in,11); in.actual_payout=true; in.phase=Phase::allocate;
    in.frame.own.gold=20;
    const auto escorted=convoy.choose(in,false);
    check(escorted.intent==Intent::escort && escorted.participant==ally(in,0).id);
    check(escorted.explicit_waypoint && escorted.waypoint==ally(in,0).position);
    const auto released=breakup.choose(in,true);
    check(released.intent==Intent::independent && released.group==in.independent_group);
    in.actual_payout=false; hide(in,0); refresh(in,12);
    check(convoy.choose(in,false).intent==Intent::independent);
}
static void trailhead_cases() {
    auto heap=fixture(); auto& in=*heap;
    ally(in,0).position=in.sites[1].group.approach;
    trailhead::State active,control;
    check(trailhead::choose(active,in,false).group==8);
    check(trailhead::choose(control,in,true).group==7);
    hide(in,0);
    for (int turn=0; turn<4; ++turn) {
        refresh(in,11+turn);
        check(trailhead::choose(active,in,false).intent==Intent::rally);
    }
    refresh(in,15);
    check(trailhead::choose(active,in,false).intent==Intent::independent);
    in=*fixture(); hide(in,0); active={};
    check(trailhead::choose(active,in,false).intent==Intent::independent);
    in=*fixture(0); active={}; in.frame.own.actor.position=in.sites[0].group.approach;
    check(trailhead::choose(active,in,false).intent==Intent::approach);
    refresh(in,11);
    check(trailhead::choose(active,in,false).intent==Intent::approach);
    refresh(in,12);
    check(trailhead::choose(active,in,false).intent==Intent::work);
    // A wall, absent arrival or clock change cannot serve as pad attendance.
    in=*fixture(0); active={};
    for (int turn=0; turn<8; ++turn) {
        refresh(in,10+turn);
        check(trailhead::choose(active,in,false).intent==Intent::approach);
    }
    refresh(in,18);
    check(trailhead::choose(active,in,false).intent==Intent::independent);
}
static void guild_cases() {
    auto heap=fixture(); auto& in=*heap; guilds::State active,control;
    in.actual_payout=true; in.frame.own.actor.bag=9;
    check(guilds::choose(active,in,false).intent==Intent::bank);
    check(guilds::choose(control,in,true).intent==Intent::independent);
    in=*fixture(2); active={};
    check(guilds::choose(active,in,false).intent==Intent::independent);
    in=*fixture(3,4); active={}; ally(in,2).position=in.sites[0].group.approach;
    check(guilds::choose(active,in,false).intent==Intent::work);
    hide(in,2); refresh(in,11);
    check(guilds::choose(active,in,false).intent==Intent::rally);
    // A failure of the second cell does not stop the first cell's own work.
    auto other_heap=fixture(0,4); auto& other=*other_heap; hide(other,2); guilds::State first_cell;
    check(guilds::choose(first_cell,other,false).intent==Intent::work);
    for (int turn=1; turn<8; ++turn) {
        refresh(in,11+turn);
        check(guilds::choose(active,in,false).intent==Intent::rally);
    }
    refresh(in,19);
    check(guilds::choose(active,in,false).intent==Intent::independent);
    for(int ordinal=0; ordinal<2; ++ordinal) {
        in=*fixture(ordinal); guilds::Policy pair, detached;
        check(pair.choose(in,false).group==7 && detached.choose(in,true).group==7);
        check(in.site(in.independent_group)->solo(in.frame,in.frame.clock.self));
        refresh(in,11); in.actual_payout=true; in.phase=Phase::allocate;
        in.frame.own.gold=20;
        const int partner=ordinal==0?1:0;
        const auto escorted=pair.choose(in,false);
        check(escorted.intent==Intent::escort && escorted.participant==ally(in,partner).id);
        check(escorted.explicit_waypoint && escorted.waypoint==ally(in,partner).position);
        const auto released=detached.choose(in,true);
        check(released.intent==Intent::independent && released.group==in.independent_group);
        in.actual_payout=false; hide(in,partner); refresh(in,12);
        check(pair.choose(in,false).intent==Intent::independent);
    }
}
static void mesh_cases() {
    auto heap=fixture(0); auto& in=*heap; hide(in,1); refresh(in,64);
    mesh::State active,control; active.produced=control.produced=true;
    check(mesh::choose(active,in,false).intent==Intent::approach);
    check(mesh::choose(control,in,true).intent==Intent::work);
    refresh(in,65); check(mesh::choose(active,in,false).intent==Intent::approach);
    refresh(in,66); check(mesh::choose(active,in,false).intent==Intent::work);
    check(active.edges[0].misses==1);
    refresh(in,96); check(mesh::choose(active,in,false).intent==Intent::approach);
    refresh(in,97); check(mesh::choose(active,in,false).intent==Intent::approach);
    refresh(in,98); check(mesh::choose(active,in,false).intent==Intent::work);
    check(active.edges[0].retired);
    in=*fixture(0); active={}; refresh(in,64);
    check(mesh::choose(active,in,false).intent==Intent::work); // Bootstrap produces.
    active.produced=true;
    check(mesh::choose(active,in,false).reason==std::string_view("mesh-current-contact-aid"));
}
static void launchpad_cases() {
    auto heap=fixture(); auto& in=*heap; hide(in,2);
    launchpad::State active,control;
    check(launchpad::choose(active,in,false).intent==Intent::approach);
    check(launchpad::choose(control,in,true).intent==Intent::approach);
    refresh(in,12);
    check(launchpad::choose(active,in,false).intent==Intent::approach);
    check(launchpad::choose(control,in,true).intent==Intent::work);
    // Timer expiry only releases to independent work; it never passes the barrier.
    active={};
    for (int turn=0; turn<8; ++turn) {
        refresh(in,20+turn);
        check(launchpad::choose(active,in,false).intent==Intent::approach);
    }
    refresh(in,28);
    check(launchpad::choose(active,in,false).intent==Intent::independent);
    in=*fixture(); active={};
    check(launchpad::choose(active,in,false).intent==Intent::work);
    in.sites[0].joint_better=false; active={};
    check(launchpad::choose(active,in,false).intent!=Intent::work);
    in=*fixture(); active={};
    in.frame.group={in.sites[0].group,in.frame.clock,7,Knowledge::exact};
    in.frame.group.value.lifecycle=Lifecycle::reward_available;
    in.frame.group.value.original_spawn_visible=true;
    in.frame.group.value.uncontested_now=true;
    ally(in,2).spawn_group=7;
    check(launchpad::choose(active,in,false).intent==Intent::independent);
}
static void cadence_cases() {
    auto heap=fixture(0); auto& in=*heap; refresh(in,3);
    cadence::State active,control;
    check(cadence::choose(active,in,false).intent==Intent::independent);
    const auto retained=cadence::choose(control,in,true);
    check(retained.intent==Intent::control_watch && retained.participant==12);
    refresh(in,9);
    check(cadence::choose(active,in,false).intent==Intent::control_watch);
    in.sites[0].joint_better=false;
    check(cadence::choose(active,in,false).intent==Intent::independent);
    in=*fixture(2); hide(in,0); hide(in,1); active={};
    check(cadence::choose(active,in,false).intent==Intent::work);
    in.sites[0].cycles[2].solo=false;
    check(cadence::choose(active,in,false).intent==Intent::independent);
}
int main() {
    rosters(); caravan_cases(); trailhead_cases(); guild_cases(); mesh_cases();
    launchpad_cases(); cadence_cases();
    std::cout << "Plan 6 organizations 01-06: " << checks << " checks passed\n";
}
