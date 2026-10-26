// Actual collector, opt-in packet decode. No synthetic native-cost claim.
#define PLAN4_ACTOR_PACKET_BLOCK 1
#include "../common/actor-evidence.h"
#include <cassert>
#include <iostream>
#include <memory>
namespace cm=cambc::mock;
using plan4::Point;
using plan4::runtime::ActorEvidence;
void setup(){
    cm::reset();cm::query_cost=25;
    cm::actors.push_back({7,{3,4},true,true,cambc::team::ally,23,80,120,17,3,9,4,true});
    cm::actors.push_back({9,{4,4},true,false,cambc::team::enemy,777});
    cm::actor(0).position={2,5};cm::actor(0).bag=51;cm::actor(0).hp=64;
}
void complete(const ActorEvidence& collector){
    const auto& f=collector.frame();
    assert(f.roster_complete&&f.own_observed&&!f.truncated&&f.count==1);
    assert(f.clock.round==10&&f.clock.own.id==0&&f.allied_count==2&&f.enemy_count==1);
    const auto& a=f.samples[0];
    assert(a.id==7&&a.champion==7&&a.position==(Point{3,4})&&a.health==80&&a.maximum_health==120);
    assert(a.strength==17&&a.attack_range==3&&a.bag==23&&a.current_priority==9&&a.next_priority==4);
    assert(a.completed_turn&&a.alive&&a.ally&&a.draft_ordinal==-1&&!f.draft_order_known);
    assert(a.provenance.observed_round==10&&a.provenance.source==plan4::Source::personal_vision&&
        a.provenance.status==plan4::Status::exact_now);
    assert(f.own.position==(Point{2,5})&&f.own.bag==51&&f.own.health==64);
    assert(collector.minimum_fuel()==95200&&collector.stop_reason()==0&&collector.stop_fuel()==-1);
    assert(!cm::unauthorized);
}
int main(){
    setup();auto out=std::make_unique<ActorEvidence>();out->start();complete(*out);
    // Same packet, alive, gem and priority imports; immutable identity is
    // already in the packet. The double does not charge guard imports.
    assert(cm::queries==13&&cm::raw_queries==3);
    int cases=1;const int calls=cm::queries;
    for(int cut=1;cut<=calls;++cut){
        setup();cm::stop_after=cut;out=std::make_unique<ActorEvidence>();out->start();
        assert(out->frame().truncated&&out->stop_reason()==1&&out->stop_fuel()==60000&&cm::queries==cut&&!cm::unauthorized);++cases;
        setup();cm::shift_after=cut;out=std::make_unique<ActorEvidence>();out->start();
        assert(out->frame().truncated&&out->stop_reason()==2&&cm::queries==cut&&!cm::unauthorized);
        assert(!out->frame().count&&!out->frame().own_observed);++cases;
    }
    setup();out=std::make_unique<ActorEvidence>();out->start();
    ++cm::round;cm::fuel=100000;cm::actor(7).shown=false;cm::actor(9).shown=true;
    cm::actor(9).position={8,8};cm::actor(9).bag=92;out->start();
    assert(!out->frame().truncated&&out->frame().count==1&&out->frame().samples[0].id==9);
    assert(out->frame().samples[0].position==(Point{8,8})&&out->frame().samples[0].bag==92);
    assert(out->frame().samples[0].provenance.observed_round==11&&!cm::unauthorized);++cases;
    for(int handle:{0,7,9}){
        setup();cm::raw_error_actor=handle;cm::raw_error=12;cm::raw_error_payload=true;
        out=std::make_unique<ActorEvidence>();bool threw=false;
        try{out->start();}catch(const std::runtime_error&){threw=true;}
        assert(threw&&!cm::unauthorized);++cases;
    }
    setup();cm::fuel=70000;out=std::make_unique<ActorEvidence>();out->start();
    assert(out->frame().truncated&&!out->frame().own_observed&&!cm::queries&&out->minimum_fuel()==71200);++cases;
    setup();cm::negative_actor=7;out=std::make_unique<ActorEvidence>();out->start();
    assert(!out->frame().truncated&&out->frame().count==0&&!cm::unauthorized);++cases;
    setup();cm::actor(7).alive=false;out=std::make_unique<ActorEvidence>();out->start();
    assert(!out->frame().truncated&&out->frame().count==0&&!cm::unauthorized);++cases;
    std::cout<<"actor packet block "<<cases<<" functional cases passed; guard costs require native evidence\n";
}
