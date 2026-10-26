#include "../counterpunch/src/counter.h"
#include "../counterpunch/src/policy.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <string>

namespace cp=h12_counterpunch;

// This adapter drives the production arm/cleanup caller. SDK masks, occupancy
// and damage remain engine responsibilities; the fixture makes successful and
// refused receipts explicit instead of treating calls as successful actions.
struct World {
    cp::Opportunity o;
    int turn=10,basic_calls=0,stun_calls=0,withdrawals=0;
    bool shown=true,life=true,allowed=true,basic_result=true,stun_result=true;
    bool refresh_capacity=true;
    float actual_basic=-1;
    std::string order;
    World(){
        o.health=160;o.basic=35;o.stun=75;o.retaliation=20;
        o.hp=o.maximum=900;o.incoming=20;
        o.alive=o.awake=o.adjacent=o.basic_ready=o.damaging=true;
        o.stun_now=o.stun_next=o.escape=true;
    }
    int round()const{return turn;}
    cp::Opportunity opportunity()const{return o;}
    bool visible()const{return shown;}
    bool same_life()const{return life;}
    bool alive()const{return o.alive&&o.health>0;}
    bool basic(){
        ++basic_calls;order+='B';
        if(!basic_result||!shown||!alive()||!o.basic_ready||!allowed)return false;
        o.health=std::max(0.f,o.health-(actual_basic<0?o.basic:actual_basic));
        o.basic_ready=false;
        if(!refresh_capacity)o.stun_next=false;
        return true;
    }
    bool stun(){
        ++stun_calls;order+='S';
        if(!stun_result||!shown||!alive()||!o.stun_now||!allowed)return false;
        o.health=std::max(0.f,o.health-o.stun);o.stun_now=false;return true;
    }
    bool cleanup_available()const{
        return shown&&life&&allowed&&cp::cleanup_admitted(o);
    }
    void withdraw(){++withdrawals;order+='W';}
    void next(float observed_damage=20){
        ++turn;o.health=std::max(0.f,o.health-observed_damage);
        o.basic_ready=true;o.stun_now=o.stun_next;
    }
};

// Production's receipt predicate is what keeps a successful basic from being
// misclassified as no work after fresh post-action state declines exposure.
struct WorkCaller {
    cp::Counter counter;
    int work=0,moves=0;
    cp::CounterResult fight(World&w,int id){
        const auto result=counter.arm(w,id);
        if(cp::counter_acted(result))++work;else ++moves;
        return result;
    }
};

template<class Change> void refused(Change change){
    World w;change(w.o);cp::Counter c;
    assert(c.arm(w,7)==cp::CounterResult::None);
    assert(!c.pending()&&w.basic_calls==0&&w.stun_calls==0&&w.withdrawals==0);
}

void arm_cleanup_cases(){
    {
        World w;cp::Counter c;
        assert(cp::admit(w.o));
        assert(c.arm(w,7)==cp::CounterResult::Armed);
        assert(c.pending()&&c.target()==7&&c.after()==125);
        assert(w.order=="B"&&w.stun_calls==0);
        // Endpoint work is merely observed here, without attributing its cause.
        w.next();assert(w.o.health==105);
        assert(c.cleanup(w)==cp::CounterResult::Cleanup);
        assert(w.order=="BBS"&&w.o.health==0&&!c.pending());
        assert(c.cleanup(w)==cp::CounterResult::None);
    }
    {
        // Fresh actual HP below one basic skips the reserved stun entirely.
        World w;cp::Counter c;assert(c.arm(w,7)==cp::CounterResult::Armed);
        w.next(95);assert(c.cleanup(w)==cp::CounterResult::Cleanup);
        assert(w.order=="BB"&&w.stun_calls==0);
    }
    {
        // Cleanup can consist of only one basic if cooldown denies the stun.
        World w;w.o.stun_now=w.o.stun_next=false;w.o.health=85;
        cp::Counter c;assert(c.arm(w,7)==cp::CounterResult::Armed);
        w.next();assert(c.cleanup(w)==cp::CounterResult::Cleanup);
        assert(w.order=="BB");
    }
    refused([](auto&o){o.health=110;}); // Already affordable current legal kill.
    refused([](auto&o){o.health=145;}); // No retaliation needed for next cleanup.
    refused([](auto&o){o.health=166;}); // One expected phase is insufficient.
    refused([](auto&o){o.shielded=true;});
    refused([](auto&o){o.awake=false;});
    refused([](auto&o){o.stunned=true;});
    refused([](auto&o){o.alive=false;});
    refused([](auto&o){o.incoming=0;});
    refused([](auto&o){o.damaging=false;}); // Other camps cannot supply its hit.
    refused([](auto&o){o.retaliation=0;});
    refused([](auto&o){o.competitor=true;});
    refused([](auto&o){o.enemy=true;});
    refused([](auto&o){o.escape=false;});
    refused([](auto&o){o.adjacent=false;});
    refused([](auto&o){o.basic_ready=false;});
    refused([](auto&o){o.hp=400;}); // Reserve + two incoming phases, strict.
    refused([](auto&o){o.bag=1;o.hp=535;});
    {
        World w;w.o.bag=1;w.o.hp=536;cp::Counter c;
        assert(c.arm(w,7)==cp::CounterResult::Armed);
    }
    {
        World w;w.basic_result=false;cp::Counter c;
        assert(c.arm(w,7)==cp::CounterResult::None);
        assert(w.basic_calls==1&&!c.pending()&&w.o.health==160);
    }
    for(int failure=0;failure<3;++failure){
        World w;WorkCaller caller;
        if(failure==0)w.refresh_capacity=false;
        if(failure==1)w.actual_basic=55;
        if(failure==2)w.actual_basic=10;
        assert(caller.fight(w,7)==cp::CounterResult::Basic);
        assert(w.basic_calls==1&&!caller.counter.pending());
        assert(caller.work==1&&caller.moves==0);
    }
    {
        World w;WorkCaller caller;
        assert(caller.fight(w,7)==cp::CounterResult::Armed);
        assert(caller.work==1&&caller.moves==0&&caller.counter.pending());
    }
    {
        World w;WorkCaller caller;w.basic_result=false;
        assert(caller.fight(w,7)==cp::CounterResult::None);
        assert(caller.work==0&&caller.moves==1&&!caller.counter.pending());
    }
    {
        World w;WorkCaller caller;w.actual_basic=160;
        assert(caller.fight(w,7)==cp::CounterResult::Basic);
        assert(caller.work==1&&caller.moves==0&&!caller.counter.pending());
    }
}

void freshness_cases(){
    for(int failure=0;failure<10;++failure){
        World w;cp::Counter c;assert(c.arm(w,7)==cp::CounterResult::Armed);
        w.next();
        if(failure==0)w.shown=false;
        if(failure==1)w.life=false; // Camp has renewed, not the purchased target.
        if(failure==2)++w.turn; // A missed owner entry cannot carry a phase forward.
        if(failure==3)w.allowed=false;
        if(failure==4)w.o.basic_ready=false;
        if(failure==5)w.o.stun_now=false;
        if(failure==6)w.o.enemy=true; // New enemy arrived during the camp phase.
        if(failure==7)w.o.shielded=true;
        if(failure==8)w.o.hp=380;
        if(failure==9){w.o.bag=1;w.o.hp=515;}
        assert(c.cleanup(w)==cp::CounterResult::Withdraw);
        assert(!c.pending()&&w.order=="BW"&&w.withdrawals==1);
        // Refusal cannot re-arm this same camp just because masks refresh.
        w=World{};w.turn=12;
        assert(c.arm(w,7)==cp::CounterResult::None&&w.basic_calls==0);
    }
    {
        World w;cp::Counter c;assert(c.arm(w,7)==cp::CounterResult::Armed);
        w.next();w.o.alive=false;
        assert(c.cleanup(w)==cp::CounterResult::Dead);
        assert(w.order=="B"&&w.withdrawals==0&&!c.pending());
    }
    {
        World w;cp::Counter c;assert(c.arm(w,7)==cp::CounterResult::Armed);
        w.next();w.stun_result=false;
        assert(c.cleanup(w)==cp::CounterResult::Withdraw);
        assert(w.order=="BBSW"&&w.o.health==70&&!c.pending());
    }
    {
        World w;cp::Counter c;assert(c.arm(w,7)==cp::CounterResult::Armed);
        w.next();assert(c.cleanup(w)==cp::CounterResult::Cleanup);
        w=World{};w.turn=12;
        assert(c.arm(w,7)==cp::CounterResult::None);
        c.renewed(8);assert(c.arm(w,7)==cp::CounterResult::None);
        c.renewed(7);assert(c.arm(w,7)==cp::CounterResult::Armed);
    }
    {
        // Spending another target or resetting owner-life state does not clear
        // this camp's per-incarnation refusal record.
        World w;cp::Counter c;assert(c.arm(w,7)==cp::CounterResult::Armed);
        w.next();assert(c.cleanup(w)==cp::CounterResult::Cleanup);
        w=World{};w.turn=12;assert(c.arm(w,8)==cp::CounterResult::Armed);
        w.next();assert(c.cleanup(w)==cp::CounterResult::Cleanup);
        c.reset();w=World{};w.turn=14;
        assert(c.arm(w,7)==cp::CounterResult::None);
        assert(c.arm(w,8)==cp::CounterResult::None);
        c.renewed(7);assert(c.arm(w,7)==cp::CounterResult::Armed);
    }
}

void cleanup_guard_cases(){
    World w;w.o.health=105;
    assert(cp::cleanup_admitted(w.o));
    for(int failure=0;failure<8;++failure){
        auto o=w.o;
        if(failure==0)o.enemy=true;
        if(failure==1)o.shielded=true;
        if(failure==2)o.basic_ready=false;
        if(failure==3)o.stun_now=false;
        if(failure==4)o.hp=380;
        if(failure==5){o.bag=1;o.hp=515;}
        if(failure==6)o.adjacent=false;
        if(failure==7)o.alive=false;
        assert(!cp::cleanup_admitted(o));
    }
    w.o.hp=381;assert(cp::cleanup_admitted(w.o));
    w.o.bag=1;w.o.hp=516;assert(cp::cleanup_admitted(w.o));
    w.o.stun_now=false;w.o.health=35;assert(cp::cleanup_admitted(w.o));
    w.o.health=36;assert(!cp::cleanup_admitted(w.o));
}

void policy_cases(){
    cp::Policy p;using P=heuristics::Purchase;
    static_assert(cp::Policy::roster==std::array{10,7,0,3,4});
    for(unsigned jungle=0;jungle<5;++jungle)
        assert(p.purchase(9,0,jungle)==P::jungle_focus);
    for(unsigned monster=0;monster<5;++monster)
        assert(p.purchase(9,monster,5)==P::monster_health);
    assert(p.purchase(9,5,5)==P::power_focus);
    assert(p.purchase(11,0,0)==P::monster_focus);
    assert(p.purchase(1,0,0)==P::monster_strength);
    assert(p.purchase(7,5,0)==P::jungle_health);
    assert(p.purchase(7,5,5)==P::power_health);
    heuristics::State s;s.hp=s.maximum=900;s.altar=10;
    heuristics::Job healthy;healthy.id=1;healthy.gems=10;healthy.gold=30;
    healthy.work=6;healthy.travel=3;healthy.altar=10;healthy.affordable=true;
    healthy.known=true;healthy.original_hp=1000;healthy.remaining_hp=1000;
    auto wounded=healthy;wounded.id=2;wounded.remaining_hp=500;
    std::array jobs{healthy,wounded};
    assert(heuristics::choose(p,s,jobs)==2);
    jobs[0].ready=true;jobs[0].work=0;
    assert(heuristics::choose(p,s,jobs)==1);
    jobs[0].affordable=false;
    assert(heuristics::choose(p,s,jobs)==2);
    s.bag=11;assert(!p.bank(s));s.bag=12;assert(p.bank(s));
    s.bag=1;s.hp=557;assert(p.bank(s));
    s.hp=900;s.round=1990;assert(p.bank(s));
    s.bag=0;assert(!p.bank(s));
    assert(!p.retreat(500,900,20,0));
    assert(p.retreat(500,900,40,1));
    assert(!p.retreat(900,900,20,1));
}

void spawn_arrival_cases(){
    // Drive production lifecycle bookkeeping directly. A new chip at owner
    // entry is not a new recall/respawn arrival.
    cp::SpawnRest a;
    assert(!cp::rest_after_arrival(false,true,true));
    assert(!cp::rest_after_arrival(true,false,true));
    assert(!cp::rest_after_arrival(true,true,false));
    assert(cp::rest_after_arrival(true,true,true));
    // A wounded recall arrival rests through its healing owner end exactly once.
    assert(a.entry(100,true,true));
    for(int round=101;round<=110;++round)assert(!a.entry(round,true,true));
    // Leaving and genuinely returning creates a new rest opportunity.
    assert(!a.entry(111,false,true));
    assert(a.entry(112,true,true));
    assert(!a.entry(113,true,true));
    // Death clears occupancy history; a wounded respawn gets one arrival rest.
    a.death();
    assert(a.entry(174,true,true));
    assert(!a.entry(175,true,true));
    // Full-health arrival proceeds immediately. Later repeated damage cannot
    // turn that same residence into an endless spawn recovery gate.
    cp::SpawnRest full;
    assert(!full.entry(200,true,false));
    for(int round=201;round<=210;++round)assert(!full.entry(round,true,true));
}

void paid_capture_cases(){
    {
        cp::PaidCapture paid;paid.expect(7);paid.hold(7,20,10,3);
        assert(paid.source()==7&&paid.possible(21,30,10));
        assert(paid.receipt(21,true,30,10));
        assert(paid.source()==-1&&!paid.possible(21,30,10));
        assert(!paid.receipt(21,true,30,10)); // No duplicate personal receipt.
    }
    {
        cp::PaidCapture paid;paid.expect(7);paid.hold(8,20,10,3);
        assert(!paid.possible(21,30,10)&&!paid.receipt(21,true,30,10));
        // A foreign pad cannot overwrite an already valid own-pad baseline.
        paid.hold(7,20,10,3);paid.hold(8,20,0,0);
        assert(!paid.possible(21,10,3));
    }
    {
        cp::PaidCapture paid;paid.expect(7);paid.hold(7,20,10,3);
        // Shared income without any own currency increase, and a source's
        // observed death/reward lifecycle alone, supply no personal payment.
        assert(!paid.possible(21,10,3));
        assert(!paid.receipt(21,true,10,3));
        assert(!paid.receipt(21,false,10,3));
        assert(!paid.receipt(21,true,9,2));
    }
    {
        cp::PaidCapture paid;paid.expect(7);
        assert(!paid.possible(21,30,10)&&!paid.receipt(21,true,30,10));
        paid.hold(7,20,10,3);
        assert(!paid.possible(20,30,10)&&!paid.receipt(20,true,30,10));
        assert(!paid.possible(22,30,10)&&!paid.receipt(22,true,30,10));
    }
    {
        cp::PaidCapture paid;paid.expect(7);paid.hold(7,20,10,3);
        assert(paid.possible(21,30,10));
        // Failure to confirm fresh selected-source respawn consumes nothing.
        assert(!paid.receipt(21,false,30,10));
        assert(paid.source()==7&&paid.possible(21,30,10));
        assert(paid.receipt(21,true,30,10));
    }
    {
        cp::PaidCapture paid;paid.expect(7);paid.hold(7,20,10,3);
        paid.reset(); // Runtime also uses this reset on death.
        assert(paid.source()==-1&&!paid.receipt(21,true,30,10));
        paid.expect(7);paid.hold(7,20,10,3);paid.expect(8);
        assert(paid.source()==8&&!paid.receipt(21,true,30,10));
        paid.hold(8,21,30,10);assert(paid.receipt(22,true,40,17));
    }
    {
        cp::PaidCapture paid;paid.expect(7);paid.hold(7,20,10,0);
        assert(paid.possible(21,30,0)&&paid.receipt(21,true,30,0));
        // Personal gold is valid capture income. It supplies no carried gems
        // and therefore cannot prove a gem bank or actual conversion.
        heuristics::State s;s.gold=30;s.bag=0;s.altar=0;
        assert(!cp::Policy{}.bank(s));
    }
    {
        cp::PaidCapture paid;paid.expect(7);paid.hold(7,20,10,3);
        assert(!paid.receipt(21,true,10,3)); // Selected group completed unpaid.
        paid.completed(7);
        assert(paid.source()==-1&&!paid.possible(21,30,10));
        // Its later incarnation cannot inherit the retired Counterpunch claim.
        paid.hold(7,100,30,10);
        assert(!paid.possible(101,50,17)&&!paid.receipt(101,true,50,17));
        paid.expect(7);paid.hold(7,110,50,17);
        assert(paid.receipt(111,true,70,24));
    }
    {
        cp::PaidCapture paid;paid.expect(7);paid.hold(7,20,10,3);
        paid.completed(8); // Another source completing is not our lifecycle.
        assert(paid.source()==7&&paid.possible(21,30,10));
        // A source switch alone also leaves a valid prior selected-pad hold
        // receiptable; holding that foreign source does not reset expectation.
        paid.hold(8,20,0,0);
        assert(paid.receipt(21,true,30,10));
    }
}

void progress_and_guard_cases(){
    {
        cp::EscapeFacts f;f.move=f.dash=f.visible=f.walk_free=true;
        f.interior_free=f.landing_free=f.outside=true;
        assert(cp::escape_admitted(f,false)&&cp::escape_admitted(f,true));
        f.cooldown=1;
        assert(cp::escape_admitted(f,false)&&!cp::escape_admitted(f,true));
        f.cooldown=2;assert(!cp::escape_admitted(f,false));f.cooldown=0;
        f.range=1;assert(!cp::escape_admitted(f,false));f.range=2;
        for(int field=0;field<7;++field){
            auto denied=f;
            if(field==0)denied.move=false;
            if(field==1)denied.dash=false;
            if(field==2)denied.visible=false;
            if(field==3)denied.walk_free=false;
            if(field==4)denied.interior_free=false;
            if(field==5)denied.landing_free=false;
            if(field==6)denied.outside=false;
            assert(!cp::escape_admitted(denied,false)&&!cp::escape_admitted(denied,true));
        }
    }
    cp::RouteWatch route;const cp::Point goal{20,20};route.adopt(3,10);
    // Measured walking distance wins while straight-line distance could grow
    // from three to nineteen on a wall detour. No Chebyshev value enters it.
    for(int step=0;step<=50;++step){
        route.measured(goal,50-step,10+step);
        assert(!route.stale(10+step,180));
    }
    assert(!route.stale(67,180));assert(route.stale(68,180));
    route.adopt(4,69);route.measured({1,1},20,69);
    assert(route.stale(77,180)); // New source does not erase global no-work age.
    cp::Renewal renewal;
    assert(!renewal.due(179,179,false,false));
    assert(!renewal.due(180,180,true,false));
    assert(!renewal.due(180,180,false,true));
    assert(renewal.due(180,180,false,false));renewal.begin(180);
    assert(renewal.attempts()==1&&!renewal.due(359,359,false,false));
    assert(renewal.due(360,360,false,false));renewal.begin(360);
    assert(renewal.attempts()==2&&!renewal.due(2000,2000,false,false));
    renewal.useful_work(400);assert(renewal.attempts()==0);
    assert(!renewal.due(579,180,false,false));assert(renewal.due(580,180,false,false));
    assert(cp::has_action(false,false,true)&&!cp::has_action(false,false,false));
    assert(!cp::cast_compatible(true,true,true));assert(cp::cast_compatible(true,false,true));
    assert(!cp::cast_compatible(false,false,false));
    assert(cp::capture_receipt(true,true,20,0,20,7));
    assert(!cp::capture_receipt(false,true,20,0,20,7));
    assert(!cp::capture_receipt(true,false,20,0,20,7));
    assert(!cp::capture_receipt(true,true,20,0,20,0));
    assert(!cp::cancel_for_exposure(true,0)&&cp::cancel_for_exposure(true,1));
    assert(cp::component_needs_refresh(false,false));
    assert(cp::component_needs_refresh(true,false));
    assert(!cp::component_needs_refresh(true,true));
    assert(cp::component_admitted(80000)&&!cp::component_admitted(79999));
    assert(!cp::defend_allowed(true,20,0)&&!cp::defend_allowed(false,20,21));
}

int main(){
    arm_cleanup_cases();freshness_cases();cleanup_guard_cases();policy_cases();spawn_arrival_cases();paid_capture_cases();progress_and_guard_cases();
    std::puts("counterpunch: production arm/cleanup, refusals, finance and liveness guards passed");
}
