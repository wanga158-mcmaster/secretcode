#include "../repairman/src/runtime.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
using namespace plan4;
namespace rt=plan4::runtime;
namespace r=plan4::repairman;
namespace p=plan4::repairman::runtime;
int checks=0;
void check(bool ok,const char* why) { ++checks; if (!ok) { std::fprintf(stderr,"FAIL: %s\n",why); std::exit(1); } }
struct Fixture {
    Information info;
    rt::ActorFrame actors;
    rt::CombatKit kit;
    p::AttackFrame frame;
    std::array<rt::PublicGroup,1> groups;
    std::array<rt::PublicMember,2> layout;
    std::array<rt::VisibleMember,2> visible;
    std::array<std::uint8_t,32> sight{},occupied{};
    Fixture() {
        info.round=10; info.own.id=4; info.own.position={1,0}; info.own_spawn={9,0};
        info.own.health=500; info.own.maximum_health=1000;
        actors.clock=kit.clock=frame.clock={10,{4}}; actors.own_observed=true;
        actors.own.id=4; actors.own.champion=11; actors.own.alive=true; actors.own.position=info.own.position;
        actors.own.health=500; actors.own.maximum_health=1000; actors.own.attack_range=3; actors.own.strength=10;
        actors.own.provenance={10,Source::own_state,Status::exact_now};
        kit.exact=true; kit.version=5; kit.stat_epoch=7; kit.champion=11; kit.strength=10; kit.attack_range=3;
        frame.group_id=20; frame.target_member_id=100; frame.lifecycle_version=3; frame.lifecycle_version_supported=true;
        frame.can_attack_now=frame.ordinary_basic_only=true; frame.basic_damage=10; frame.incoming_upper_bound=20;
        frame.kit_version=5; frame.stat_epoch=7;
        frame.action_quote=frame.combat_quote={10,Source::own_state,Status::derived};
        groups[0]={20,0,2,true}; layout[0]={20,100,0,2,0,3,{0,0},200}; layout[1]={20,101,1,2,0,3,{0,1},100};
        visible[0]={20,100,0,2,{0,0},100,{10,Source::personal_vision,Status::exact_now}};
        visible[1]={20,101,1,2,{0,1},0,{10,Source::personal_vision,Status::exact_now}};
        frame.layout={{10,{4}},groups.data(),1,layout.data(),2,false}; frame.members={visible.data(),2,false};
        info.group_count=1; auto& group=info.groups[0]; group.id=20; group.position={0,0};
        group.health=100; group.full_health=300; group.incoming_damage=group.full_incoming_damage=20;
        group.lifecycle=Lifecycle::living; group.visible=group.all_spawns_visible=group.occupancy_known=true;
        group.provenance=group.health_provenance={10,Source::personal_vision,Status::exact_now};
        sight.fill(255); occupied[0]=1;
        auto& safety=frame.safety; safety.observed_round=10; safety.actor_id=4; safety.topology_complete=true;
        safety.walkable=walkable; safety.occupancy={16,16,sight.data(),occupied.data(),sight.size(),true};
        safety.relevant_threats_complete=true; safety.threat_count=1;
        safety.threats[0]={100,3,{0,0},20,{10,Source::personal_vision,Status::exact_now},true,true};
        safety.enemies={{10,Source::personal_vision,Status::exact_now},true,true};
        safety.own_spawn=info.own_spawn; safety.physical_spawn_known=safety.recall_arrival_supported=true;
        safety.spawn_quote={10,Source::own_state,Status::derived};
    }
    static bool walkable(Point point,const void*) { return point.x>=0&&point.x<16&&point.y==0; }
};
template<class Mutation> void refused(Mutation change,p::Unsupported reason,const char* why) {
    auto f=std::make_unique<Fixture>(); change(*f);
    auto active=std::make_unique<p::PreparedAttack>(); auto control=std::make_unique<p::PreparedAttack>();
    const bool a=p::Preparation(true).prepare_into(f->info,f->actors,f->kit,f->frame,*active);
    const bool b=p::Preparation(false).prepare_into(f->info,f->actors,f->kit,f->frame,*control);
    check(!a&&!b&&!active->supported&&!control->supported&&active->unsupported==reason&&control->unsupported==reason,why);
}
bool no_budget(const void*) { return false; }
int main() {
    auto f=std::make_unique<Fixture>(); auto active=std::make_unique<p::PreparedAttack>(); auto control=std::make_unique<p::PreparedAttack>();
    check(p::Preparation(true).prepare_into(f->info,f->actors,f->kit,f->frame,*active)&&
          p::Preparation(false).prepare_into(f->info,f->actors,f->kit,f->frame,*control),
          "active and disabled admit the same fresh one-remaining-target ordinary segment");
    check(active->segment.attacks==4&&control->segment.attacks==4&&active->segment.health_at_recall_arrival==control->segment.health_at_recall_arrival&&
          active->restoration.completion_round==control->restoration.completion_round,
          "control cannot bypass any segment exposure, escape, recall or restoration quote");
    check(active->actor.target_id==100&&active->group.group_id==20&&active->group.health==100&&active->group.lifecycle_version==3&&
          active->basic_damage==10&&active->restoration.destination==f->info.own_spawn,
          "prepared context is joined to actual member/group IDs, current health, actual kit quote and physical spawn");
    r::WorkRecord work; check(r::start_new_work(work,active->group,10),"fresh preparation can start a separately tracked work record");
    r::processed(work,4,active->basic_damage); r::departed(work);
    check(work.predicted_remaining==60&&!r::return_attack_authorized(work,20)&&r::health_provenance(work,20).status==Status::derived,
          "prepared execution does not convert absent predicted remaining work into return attack authority");
    check(!p::Preparation::complete_runtime_available&&p::Preparation::runtime_blocker==p::Unsupported::executor_and_continuation_missing,
          "pure preparation explicitly refuses to claim a complete deployed coupling or private-menu runtime");
    f->frame.can_attack_now=false;
    check(!p::Preparation(true).prepare_into(f->info,f->actors,f->kit,f->frame,*active)&&!active->escape.proof.supported&&
          !active->group.visible&&active->basic_damage==0&&active->segment.attacks==0,
          "reused rejected output cannot retain a prior exact work row or supported nested safety proof");
    refused([](Fixture& x){x.frame.clock.round=9;},p::Unsupported::stale,"stale action-boundary frame is rejected identically");
    refused([](Fixture& x){x.info.own.channel=Channel::recall;},p::Unsupported::actor,"existing channel protection precedes new attacks");
    refused([](Fixture& x){x.actors.own.health=499;},p::Unsupported::actor,"mismatched own state cannot authorize an attack");
    refused([](Fixture& x){x.kit.exact=false;},p::Unsupported::kit,"unknown current kit does not become a nominal safety proof");
    refused([](Fixture& x){x.kit.stat_epoch=8;},p::Unsupported::kit,"stat change invalidates cached actual damage and recall admission");
    refused([](Fixture& x){x.kit.champion=12;},p::Unsupported::kit,"a different champion cannot reuse the quote identity");
    refused([](Fixture& x){x.frame.basic_damage=0;},p::Unsupported::kit,"missing ordinary damage quote cannot create work");
    refused([](Fixture& x){x.frame.combat_quote.observed_round=9;},p::Unsupported::kit,"stale legal combat quote remains unavailable");
    refused([](Fixture& x){x.frame.can_attack_now=false;},p::Unsupported::allowance,"a ranged move or spent attack allowance blocks another attack");
    refused([](Fixture& x){x.frame.ordinary_basic_only=false;},p::Unsupported::allowance,"optional spell timing cannot silently extend the ordinary segment");
    refused([](Fixture& x){x.frame.stationary=false;},p::Unsupported::allowance,"movement requires a fresh segment admission");
    refused([](Fixture& x){x.frame.ability_timing_changed=true;},p::Unsupported::allowance,"ability timing ends a stationary segment");
    refused([](Fixture& x){x.frame.emergency=true;},p::Unsupported::allowance,"common emergency retreat wins over retained-work intent");
    refused([](Fixture& x){x.frame.new_visible_rival=true;},p::Unsupported::allowance,"a newly visible rival invalidates admission");
    refused([](Fixture& x){x.frame.lifecycle_version_supported=false;},p::Unsupported::lifecycle,"uncertain lifecycle epoch cannot support retained work");
    refused([](Fixture& x){x.info.public_layout_truncated=true;},p::Unsupported::layout,"truncated public composition cannot certify all remaining work");
    refused([](Fixture& x){x.layout[1].member_id=100;},p::Unsupported::layout,"duplicate public member identity is rejected");
    refused([](Fixture& x){x.info.groups[0].health_provenance.observed_round=9;},p::Unsupported::health,"stale group health cannot be refreshed by a separate target row");
    refused([](Fixture& x){x.info.groups[0].lifecycle=Lifecycle::captured;},p::Unsupported::health,"stolen or captured stock cannot reuse an old live target");
    refused([](Fixture& x){x.info.groups[0].contested=true;},p::Unsupported::health,"known contest does not become an assured solo work stock");
    refused([](Fixture& x){x.visible[1].health=1;x.info.groups[0].health=101;},p::Unsupported::health,"a second surviving target requires a fresh ordinary target change");
    refused([](Fixture& x){x.frame.members.count=1;},p::Unsupported::health,"missing dead member observation prevents complete remaining-health join");
    refused([](Fixture& x){x.visible[0].health=99;},p::Unsupported::health,"provenance labels cannot hide contradictory aggregate health");
    refused([](Fixture& x){x.layout[0].maximum_health=201;},p::Unsupported::health,"public full-health mismatch is rejected");
    refused([](Fixture& x){x.frame.safety.own_spawn={8,0};},p::Unsupported::escape,"restoration cannot teleport to a privately substituted safe endpoint");
    refused([](Fixture& x){x.frame.safety.enemies.visible_threats_excluded=false;},p::Unsupported::escape,"unresolved current enemy threats cannot authorize a recall endpoint");
    refused([](Fixture& x){x.frame.safety.threats[0].alive=false;},p::Unsupported::escape,"a live attack target cannot be quoted as a dead stationary threat");
    refused([](Fixture& x){x.frame.safety.threats[0].attack_range=1;},p::Unsupported::escape,"a private shortened target range cannot create a safe endpoint");
    refused([](Fixture& x){x.frame.safety.threat_count=0;},p::Unsupported::escape,"complete target evidence cannot be omitted from a positive current damage proof");
    refused([](Fixture& x){x.info.own.health=370;x.actors.own.health=370;},p::Unsupported::safety,"no strict k-plus-one safety margin means no attack in either arm");
    refused([](Fixture& x){x.info.work_budget={no_budget,nullptr};},p::Unsupported::budget,"fuel denial prevents any partial preparation becoming supported");
    std::printf("plan4-repairman-runtime: %d checks passed (preparation only; SDK executor/continuation unavailable)\n",checks);
}
