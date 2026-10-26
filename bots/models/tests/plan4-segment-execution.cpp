#include "../common/safe-segment-execution.h"
#include <cassert>
#include <iostream>
#include <memory>
using namespace plan4;
namespace s=plan4::safe_segment;
struct Fixture {
    std::array<std::uint8_t,32> visible{},occupied{};
    s::AttackContext actor{};s::EscapeScene scene{};Settings settings{};s::ExecutionBody body{};
    Fixture() {
        visible.fill(255);occupied[0]=1;
        body.round=10;body.actor_id=4;body.group_id=7;body.target_id=30;body.position={1,0};
        body.health=500;body.maximum_health=1000;body.kit_version=12;body.stat_epoch=2;body.lifecycle_version=3;
        body.alive=body.can_attack=body.can_move=true;
        actor.predicted_incoming=20;actor.already_attackable=true;
        scene.topology_complete=scene.relevant_threats_complete=true;scene.walkable=walkable;
        scene.occupancy={16,16,visible.data(),occupied.data(),visible.size(),true};
        scene.threat_count=1;scene.threats[0]={30,3,{0,0},20,{},true,true};
        scene.own_spawn={9,0};scene.physical_spawn_known=scene.recall_arrival_supported=true;
    }
    static bool walkable(Point p,const void*) {return p.x>=0&&p.x<16&&p.y==0;}
    s::ExecutionProposal proposal() {
        actor.predicted_incoming=body.position.x<=3?20:0;
        actor.observed_round=scene.observed_round=body.round;actor.actor_id=scene.actor_id=body.actor_id;
        actor.target_id=body.target_id;actor.position=body.position;actor.health=body.health;actor.maximum_health=body.maximum_health;actor.alive=body.alive;
        scene.enemies={{body.round,Source::personal_vision,Status::exact_now},true,true};
        scene.spawn_quote={body.round,Source::own_state,Status::derived};
        scene.threats[0].provenance={body.round,Source::personal_vision,Status::exact_now};
        return {&actor,&scene,&settings,body.group_id,body.kit_version,body.stat_epoch,body.lifecycle_version,true};
    }
};
int main() {
    int cases=0;auto f=std::make_unique<Fixture>();auto e=std::make_unique<s::SegmentExecutor>();
    for (int n=0;n<6;++n) {
        const auto command=e->next(f->proposal(),f->body,64);assert(command.kind==s::ExecutionKind::attack);++cases;
        auto wrong=f->body;++wrong.actor_id;assert(!e->acknowledge(command,wrong,true));++cases;
        assert(e->acknowledge(command,f->body,true));++cases;
        assert(e->next(f->proposal(),f->body,64).kind==s::ExecutionKind::wait);++cases;
        ++f->body.round;f->body.health-=20;
    }
    assert(e->stage()==s::ExecutionStage::reevaluating);++cases;
    for (int x=2;x<=4;++x) {
        const auto command=e->next(f->proposal(),f->body,64);
        assert(command.kind==s::ExecutionKind::escape_move && command.destination==Point({x,0}));++cases;
        assert(e->acknowledge(command,f->body,true));f->body.position=command.destination;
        ++f->body.round;f->body.health-=20;
    }
    auto command=e->next(f->proposal(),f->body,64);assert(command.kind==s::ExecutionKind::recall);++cases;
    assert(e->acknowledge(command,f->body,true));
    f->body.lifecycle_version=0; // Channel preservation needs no hidden group-generation query.
    for (int n=0;n<5;++n) {++f->body.round;f->body.recalling=true;
        assert(e->next({},f->body,0).kind==s::ExecutionKind::wait);++cases;}
    ++f->body.round;f->body.recalling=false;f->body.on_spawn=true;f->body.position={9,0};
    assert(e->next({},f->body,0).kind==s::ExecutionKind::wait && e->active());++cases;
    ++f->body.round;f->body.health=1000;
    assert(e->next({},f->body,0).kind==s::ExecutionKind::complete && !e->active());++cases;
    for (int fault=0;fault<8;++fault) {
        f=std::make_unique<Fixture>();e=std::make_unique<s::SegmentExecutor>();
        command=e->next(f->proposal(),f->body,64);assert(e->acknowledge(command,f->body,true));
        ++f->body.round;auto proposal=f->proposal();
        if(fault==0)++f->body.kit_version;
        if(fault==1)++f->body.lifecycle_version;
        if(fault==2)++f->body.round;
        if(fault==3)f->body.emergency=true;
        if(fault==4)f->body.new_visible_rival=true;
        if(fault==5)f->occupied[0]|=4;
        if(fault==6)f->scene.relevant_threats_complete=false;
        if(fault==7)f->body.health=350;
        assert(e->next(proposal,f->body,64).kind==s::ExecutionKind::abort && !e->active());++cases;
    }
    for (int fault=0;fault<4;++fault) {
        f=std::make_unique<Fixture>();e=std::make_unique<s::SegmentExecutor>();
        for (int n=0;n<4;++n) {command=e->next(f->proposal(),f->body,64);assert(e->acknowledge(command,f->body,true));
            ++f->body.round;f->body.health-=20;}
        auto p=f->proposal();
        if(fault==0)p.scene=nullptr;
        if(fault==1)--f->scene.threats[0].provenance.observed_round;
        if(fault==3){f->body.can_attack=false;f->actor.escape_premise_lost=true;}
        assert(e->next(p,f->body,fault==2?0:64).kind==s::ExecutionKind::abort);++cases;
    }
    f=std::make_unique<Fixture>();e=std::make_unique<s::SegmentExecutor>();
    assert(e->next(f->proposal(),f->body,0).kind==s::ExecutionKind::abort);++cases;
    ++f->body.round;command=e->next(f->proposal(),f->body,64);
    assert(command.kind==s::ExecutionKind::attack && !e->acknowledge(command,f->body,false) && !e->active());++cases;
    f->body.round+=20;assert(e->next(f->proposal(),f->body,64).kind==s::ExecutionKind::attack);++cases;
    struct RecoveryDouble {int recalls=0;bool can_cast=false;
        void recall(){assert(can_cast);++recalls;}} sdk;
    for (int fault=0;fault<5;++fault) {
        f=std::make_unique<Fixture>();e=std::make_unique<s::SegmentExecutor>();
        command=e->next({},f->body,64);assert(command.kind==s::ExecutionKind::abort);
        s::RecoveryPermission permit{10,10,40000,true,false,true};
        if(fault==0)permit.can_start_recall=false; // stunned: all actions unavailable
        if(fault==1)permit.channel_active=true;
        if(fault==2)permit.current_round=11;
        if(fault==3)permit.fuel=34000;
        if(fault==4)permit.alive=false;
        assert(!s::checked_recovery(permit,[&]{sdk.recall();}) && sdk.recalls==0);++cases;
    }
    sdk.can_cast=true;
    assert(s::checked_recovery({10,10,40000,true,false,true},[&]{sdk.recall();}) && sdk.recalls==1);++cases;
    std::cout<<"plan4 segment execution "<<cases<<" checks passed\n";
}
