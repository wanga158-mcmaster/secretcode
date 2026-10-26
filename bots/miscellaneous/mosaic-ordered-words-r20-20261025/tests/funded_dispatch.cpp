#include "funded_dispatch.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <initializer_list>

using namespace mosaic::funded_dispatch;
unsigned checks=0;
void check(bool ok,int line){++checks;if(!ok){std::fprintf(stderr,"funded-dispatch failed %d\n",line);std::abort();}}
#define CHECK(x) check(bool(x),__LINE__)

Input walking(Fuel fuel=80000){
    Input in;in.frame=20;in.observation=3;in.fuel=fuel;
    in.owner=Owner::approach;in.work=Knowledge::unknown;in.work_probe_cost=5000;
    in.physical={Action::move,true,true,true,20,3,36000};return in;
}
void arbitration(){
    auto in=walking();auto d=decide(in);
    CHECK(d.kind==Kind::probe_work&&d.protected_tail==41000);
    // Independent physical sequence: a bounded negative probe spends exactly
    // its declared work; empty menus then cannot steal the 36K actual owner.
    in.fuel-=in.work_probe_cost;in.work=Knowledge::absent;
    d=decide(in);CHECK(d.kind==Kind::physical&&d.action==Action::move&&d.protected_tail==36000);
    unsigned menus=0;
    while(optional_fits(in.fuel,10000,d.protected_tail)){in.fuel-=10000;++menus;}
    CHECK(menus==3&&in.fuel==45000);
    CHECK(decide(in).kind==Kind::physical&&in.fuel>=in.physical.cost);
    // Reproduces the OLD late-entry state as underfunded, rather than claiming
    // that a new lower floor makes it executable. The caller must fund earlier.
    in.fuel=35000;CHECK(decide(in).kind==Kind::blocked);
    in.fuel=36000;CHECK(decide(in).kind==Kind::blocked);
    in.fuel=36001;CHECK(decide(in).kind==Kind::physical);
    in=walking(40000);CHECK(decide(in).kind==Kind::physical); // decline unfunded probe

    // A fresh actual Basic remains the incumbent even when its ranged engine
    // effect will consume M. Complete continuation is part of the 20K quote.
    in=walking();in.work=Knowledge::executable;
    in.work_action={Action::basic,true,true,true,20,3,20000};
    d=decide(in);CHECK(d.kind==Kind::work&&d.action==Action::basic&&d.protected_tail==20000);
    for(auto owner:{Owner::work,Owner::approach,Owner::returning,Owner::bank,Owner::recover,Owner::capture,Owner::observe}){
        in.owner=owner;CHECK(decide(in).kind==Kind::work);
    }
    in.work_action.owner_compatible=false;CHECK(decide(in).kind==Kind::probe_work);
    in.work_action.owner_compatible=true;in.work_action.continuation_admitted=false;
    CHECK(decide(in).kind==Kind::probe_work);
    in.work_action.continuation_admitted=true;in.work_action.observation=2;
    CHECK(decide(in).kind==Kind::probe_work); // stale proof is nomination only
    in.work_action.observation=3;in.work_action.frame=19;CHECK(decide(in).kind==Kind::probe_work);
    in.work_action.frame=20;in.work_action.cost=85000;CHECK(decide(in).kind==Kind::physical);

    in=walking();in.refresh_required=true;in.refresh_cost=4000;
    CHECK(decide(in).kind==Kind::refresh&&decide(in).protected_tail==40000);
    in.fuel=39000;CHECK(decide(in).kind==Kind::blocked);
    in=walking();in.physical.ready=false;in.work=Knowledge::absent;
    in.preparation={true,true,3,12000};
    CHECK(decide(in).kind==Kind::prepare&&decide(in).protected_tail==48000);
    in.preparation.hold_admitted=false;CHECK(decide(in).reason==Reason::exposed_preparation);
    in=walking();in.physical.action=Action::none;in.work=Knowledge::absent;in.nominal_goal=true;
    CHECK(decide(in).kind==Kind::blocked&&decide(in).reason==Reason::unresolved_nominal_goal);
    in=walking();in.physical.ready=false;
    CHECK(decide(in).kind==Kind::blocked&&decide(in).reason==Reason::missing_owner);

    // Independently check every admission against actual sequential subtraction
    // across the full requested fuel range, including equality boundaries.
    for(Fuel fuel=0;fuel<=100000;fuel+=137){
        in=walking(fuel);d=decide(in);
        if(d.kind==Kind::probe_work){
            CHECK(fuel>5000);const auto after=fuel-5000;CHECK(after>36000);
            in.fuel=after;in.work=Knowledge::absent;CHECK(decide(in).kind==Kind::physical);
        }else if(d.kind==Kind::physical)CHECK(fuel>36000);
        else CHECK(fuel<=36000);
    }
    in=walking();in.fuel=std::numeric_limits<Fuel>::max();CHECK(decide(in).reason==Reason::invalid_cost);
    CHECK(!optional_fits(100000,0,36000));CHECK(!optional_fits(100000,5000,0));
    CHECK(!optional_fits(46000,10000,36000));CHECK(optional_fits(46001,10000,36000));
    CHECK(!fits(100000,std::numeric_limits<Fuel>::max(),36000));
}
void nominal_work_acquisition(){
    // Independent perch model: Basic range 3 misses the enemy at distance 4,
    // a ready damage AoE reaches it, and no current source can hit this tile.
    // Reaching the navigation root is therefore not evidence of absent work.
    constexpr int distance=4,basic_range=3,cast_range=4,hp=100,next_damage=0;
    constexpr bool cast_ready=true;
    CHECK(distance>basic_range&&cast_ready&&distance<=cast_range);
    auto perch=[](){
        auto in=walking(40000);in.nominal_goal=true;
        in.physical.ready=false;in.physical.continuation_admitted=false;
        in.physical.cost=32000;in.preparation={false,true,0,12000};
        in.work_probe_cost=8000;
        in.work_action={Action::cast,false,next_damage<hp,true,20,3,8000};
        return in;
    };
    auto in=perch();auto d=decide(in);
    CHECK(d.kind==Kind::probe_work&&d.action==Action::none&&d.protected_tail==16000);
    // The bounded probe observes actual work, then the real action consumes
    // only its remaining operation/continuation quote, without needing Move.
    in.fuel-=in.work_probe_cost;in.work_probe_cost=0;
    in.work=(cast_ready&&distance<=cast_range)?Knowledge::executable:Knowledge::absent;
    in.work_action.ready=in.work==Knowledge::executable;
    d=decide(in);unsigned actions=0;
    if(d.kind==Kind::work&&d.action==Action::cast){in.fuel-=in.work_action.cost;++actions;}
    CHECK(actions==1&&in.fuel==24000&&d.protected_tail==8000);

    // HOLD admission, freshness, compatibility and a real possible command
    // are independent requirements; a nominal root supplies none of them.
    in=perch();in.work_action.continuation_admitted=false;CHECK(decide(in).kind==Kind::blocked);
    in=perch();in.work_action.frame=19;CHECK(decide(in).kind==Kind::blocked);
    in=perch();in.work_action.observation=2;CHECK(decide(in).kind==Kind::blocked);
    in=perch();in.work_action.owner_compatible=false;CHECK(decide(in).kind==Kind::blocked);
    in=perch();in.work_action.action=Action::none;CHECK(decide(in).kind==Kind::blocked);
    in=perch();in.work_action.cost=0;CHECK(decide(in).kind==Kind::blocked);
    in=perch();in.fuel=16000;CHECK(decide(in).kind==Kind::blocked);
    in.fuel=16001;CHECK(decide(in).kind==Kind::probe_work);
    // A negative or unfinished probe consumes its one opportunity. The caller
    // must now refine its owner/observation; neither wait nor fake preparation
    // is reported by this decision component.
    for(auto result:{Knowledge::absent,Knowledge::unknown}){
        in=perch();CHECK(decide(in).kind==Kind::probe_work);
        in.fuel-=in.work_probe_cost;in.work_probe_cost=0;in.work=result;
        d=decide(in);CHECK(d.kind==Kind::blocked&&d.action==Action::none);
        CHECK(d.reason==Reason::unresolved_nominal_goal);
    }
    // A stale executable claim is not the fresh UNKNOWN/HOLD acquisition case.
    in=perch();in.work=Knowledge::executable;in.work_action.observation=2;
    CHECK(decide(in).kind==Kind::blocked);
}
void residual_dispatch_tail(){
    // The nomination's fresh mask/projection/quotes have already been paid.
    // Preserve the remaining dispatch+failure closure, not that completed prefix.
    constexpr Fuel entry=48000,paid_nomination=20000,remaining_dispatch=16000;
    auto in=walking(entry-paid_nomination);in.work=Knowledge::absent;
    in.physical.cost=remaining_dispatch;
    auto d=decide(in);
    CHECK(d.kind==Kind::physical&&d.protected_tail==16000&&d.optional_allowance==12000);
    CHECK(optional_fits(in.fuel,8000,d.protected_tail));in.fuel-=8000;
    CHECK(decide(in).kind==Kind::physical&&in.fuel>remaining_dispatch);
    in.fuel-=remaining_dispatch;CHECK(in.fuel==4000); // actual finite tail still fits
    // Independently show the double charge rejects this executable continuation.
    CHECK(!fits(entry-paid_nomination,paid_nomination+remaining_dispatch));
    in=walking(entry-paid_nomination);in.work=Knowledge::absent;
    in.physical.cost=32000;CHECK(decide(in).kind==Kind::blocked);
    // The residual quote does not renew an expired nomination or spent owner.
    in.physical.cost=remaining_dispatch;in.physical.observation=2;
    CHECK(decide(in).kind==Kind::blocked);
    in.physical.observation=3;in.physical.ready=false;CHECK(decide(in).kind==Kind::blocked);
}
void finite_preparation(){
    Cursor cursor;CHECK(cursor.request(77,3));CHECK(cursor.enter(10));
    CHECK(!cursor.advance(77,10,0,false)&&cursor.remaining()==3);
    CHECK(cursor.advance(77,10,0,true)&&cursor.remaining()==2);
    CHECK(!cursor.advance(77,10,0,true)&&cursor.remaining()==2); // no repeated credit
    CHECK(cursor.enter(11));CHECK(cursor.request(77,3)&&cursor.remaining()==2);
    CHECK(!cursor.advance(77,10,1,true));CHECK(cursor.advance(77,11,1,true));
    CHECK(cursor.enter(12));CHECK(cursor.advance(77,12,2,true)&&cursor.remaining()==0);
    CHECK(!cursor.validate(77,12,5,false));CHECK(cursor.validate(77,12,5,true));
    CHECK(cursor.authority(12,5)&&!cursor.authority(12,6));
    CHECK(!cursor.validate(77,12,6,false)&&!cursor.authority(12,5));
    CHECK(cursor.validate(77,12,6,true)&&cursor.authority(12,6));
    CHECK(cursor.authority(77,12,6)&&!cursor.authority(78,12,6));
    CHECK(cursor.enter(13)&&!cursor.authority(13,5));CHECK(cursor.remaining()==0);
    CHECK(!cursor.enter(12));CHECK(!cursor.request(77,2));
    CHECK(cursor.request(78,2)&&cursor.remaining()==2); // actual new dependency problem
    CHECK(!cursor.request(78,0)&&!cursor.request(78,33));
}
void failure_closure(){
    FailureFence fence;CHECK(fence.enter(10,0,1));CHECK(fence.permits(10,0,1,2));
    CHECK(fence.refused(2));CHECK(fence.refresh_required());
    CHECK(!fence.permits(10,0,1,3)); // old whole menu is revoked
    CHECK(!fence.refreshed(10,0,1));CHECK(fence.refreshed(10,0,2));
    CHECK(!fence.permits(10,0,2,2)&&fence.permits(10,0,2,3));
    CHECK(!fence.refused(2));CHECK(fence.refused(3));
    CHECK(fence.refreshed(10,0,3));CHECK(!fence.permits(10,0,3,3));
    // Actual own effect (e.g. killing the newly revealed blocker) permits fresh
    // validation of the freed action; an observation nonce alone did not.
    CHECK(fence.enter(10,1,4));CHECK(fence.permits(10,1,4,2));
    CHECK(!fence.enter(10,0,5));CHECK(!fence.permits(10,1,3,2));
    CHECK(fence.enter(11,0,1));CHECK(fence.permits(11,0,1,2));
    CHECK(!fence.permits(11,0,1,32));CHECK(!fence.refused(32));
}
int main(){arbitration();nominal_work_acquisition();residual_dispatch_tail();finite_preparation();failure_closure();std::printf("%u funded-dispatch checks passed\n",checks);}
