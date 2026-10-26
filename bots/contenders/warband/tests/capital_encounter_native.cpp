#define WB_ENCOUNTER_NATIVE_HOST_FREE
#include "capital_encounter_native.h"
#include <cstdio>
namespace n=wb::capital::encounter_native;
namespace e=wb::capital::encounter;
unsigned checks{},failures{};
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL %d: %s\n",__LINE__,#x);}}while(false)
void set(std::vector<std::uint8_t>& bits,unsigned width,int x,int y){const auto at=static_cast<unsigned>(y)*width+static_cast<unsigned>(x);bits[at/8]|=static_cast<std::uint8_t>(1u<<(at%8));}
e::Body make(int id,int team,e::Point p,float hp,float attack) {
    e::Body b;b.id=id;b.team=team;b.position=p;b.hp=b.max_hp=hp;b.basic=attack;b.move_range=b.attack_range=1;
    b.alive=b.m=b.a=b.c=b.provenance_known=b.effects_closed=true;return b;
}
n::Snapshot scene() {
    n::Snapshot s;s.source.owner=1;s.source.round=2;s.source.width=s.source.height=6;s.root_team=1;
    s.source.economic_step={3,3};s.source.recovery_step={1,1};s.own={1};s.foe={0};
    s.world.owner=1;s.world.round=2;s.world.count=2;s.world.revision=1;s.world.respawn_ticks=60;
    s.world.spawn_healing_known=true;s.world.spawn_healing_fraction=1;
    s.world.bodies[0]=make(1,1,{2,2},100,10);s.world.bodies[0].ranged=true;s.world.bodies[0].attack_range=3;
    s.world.bodies[0].hp=90;
    s.world.bodies[1]=make(0,0,{3,2},1000,60);
    auto& reset=s.world.bodies[1].abilities[0];reset.kind=e::Kind::Reset;reset.max_cooldown=8;reset.target_mask=e::self_target;
    s.walk.resize(5);s.spawn.resize(5);s.enemy_spawn.resize(5);s.camps.resize(5);set(s.spawn,6,0,0);
    s.stationary_vision[1].assign(5,255);s.recovery_distances.resize(36);
    for(int y=0;y<6;++y)for(int x=0;x<6;++x)s.recovery_distances[static_cast<unsigned>(y*6+x)]=static_cast<std::uint16_t>(std::max(x,y));
    s.npc_mitigation.fill(1);return s;
}
e::Observation observation(const n::Snapshot& s,int observer) {
    e::Observation o;o.observer=observer;o.round=s.world.round;o.count=s.world.count;
    for(unsigned k=0;k<o.count;++k){o.bodies[k].id=s.world.bodies[k].id;o.bodies[k].team=s.world.bodies[k].team;o.bodies[k].visible=o.bodies[k].visibility_known=true;o.bodies[k].body=&s.world.bodies[k];}
    return o;
}
int main() {
    auto s=scene();
    for(int y=-1;y<=6;++y)for(int x=-1;x<=6;++x)CHECK(n::inside(s,{x,y})==(x>=0&&y>=0&&x<6&&y<6));
    CHECK(n::tile(s,s.spawn,{0,0}));CHECK(!n::tile(s,s.spawn,{0,1}));CHECK(!n::tile(s,s.spawn,{-1,0}));
    auto own=observation(s,1);CHECK(n::recovery_step(s,own)==e::Point({1,1}));
    auto word=n::recovery_word(s,own);CHECK(word.complete&&word.count==1&&word.steps[0].kind==e::PrimitiveKind::Move);
    CHECK(n::geometry(&s,s.world,1,word.steps[0])==e::Verdict::Allowed);
    set(s.walk,6,1,1);CHECK(n::geometry(&s,s.world,1,word.steps[0])==e::Verdict::Blocked);s.walk.assign(5,0);
    CHECK(n::visibility(&s,s.world,0,1)==e::Verdict::Allowed);
    auto moved=s.world;moved.bodies[1].position={4,2};CHECK(n::visibility(&s,moved,0,1)==e::Verdict::Unknown);
    auto reply=n::policy(&s,observation(s,0));CHECK(reply.complete&&reply.count==3);
    CHECK(reply.steps[0].kind==e::PrimitiveKind::Basic&&reply.steps[1].kind==e::PrimitiveKind::Cast&&reply.steps[2].kind==e::PrimitiveKind::Basic);
    auto hidden=observation(s,0);hidden.bodies[0].visible=false;hidden.bodies[0].body=nullptr;CHECK(n::policy(&s,hidden).count==0);
    hidden.bodies[0].visibility_known=false;CHECK(!n::policy(&s,hidden).complete);
    // A ground AoE is never silently changed into the SDK's open unit-aim form.
    auto area=s.world.bodies[1].abilities[0];area.kind=e::Kind::Area;area.range=4;area.radius=1;area.stun_turns=1;area.target_mask=19;area.amount=100;
    s.world.bodies[1].abilities[0]=area;reply=n::policy(&s,observation(s,0));CHECK(reply.count==2&&reply.steps[1].target==-1);
    CHECK(n::geometry(&s,s.world,0,reply.steps[1])==e::Verdict::Allowed);
    auto entity_area=reply.steps[1];entity_area.target=1;CHECK(n::geometry(&s,s.world,0,entity_area)==e::Verdict::Unknown);
    set(s.camps,6,2,1);CHECK(n::geometry(&s,s.world,0,reply.steps[1])==e::Verdict::Unknown);s.camps.assign(5,0);
    s=scene();std::array<e::Slot,7> slots{{{2,0},{3,0},{3,1},{4,1},{4,0},{5,0},{5,1}}};
    e::Scenario scenario{1,1,1,5,0,1,slots,true,true,false};
    e::Callbacks callbacks{&s,&n::geometry,&n::visibility,&n::policy,&n::phase,&n::spawn};
    e::Requirement requirement{1,5,0,true,true,true,&n::ready,&s};
    e::Word economic;economic.complete=true;n::append(economic,{e::PrimitiveKind::Move,-1,-1,{3,3}});
    const auto failed=e::run(s.world,economic,scenario,callbacks,requirement);
    CHECK(failed.service==e::Service::Refuted&&failed.refutation_proved&&failed.refutation==e::Refutation::OwnerDied);
    CHECK(failed.usable_inputs==0&&!failed.horizon_reached);
    e::Word recovery;recovery.complete=true;n::append(recovery,{e::PrimitiveKind::Move,-1,-1,{1,1}});
    const auto restored=e::run(s.world,recovery,scenario,callbacks,requirement);
    CHECK(restored.status==e::Status::Complete&&restored.service==e::Service::Ready&&restored.horizon_reached);
    CHECK(restored.service_ready_round==4&&restored.usable_inputs==3);
    const auto* after=n::body(restored.terminal,1);CHECK(after&&after->hp==100&&after->position==e::Point({0,0}));
    // Known awake source selection accounts for death before a later source.
    s=scene();s.world.bodies[0].position={2,2};s.world.bodies[0].hp=40;s.world.bodies[1].position={4,2};
    std::array<n::Source,2> sources{{{10,{2,3},3,50,true},{11,{2,3},3,50,true}}};s.source.sources=sources;s.empty_awake_phases.resize(2);
    const auto batch=n::phase(&s,s.world,3);CHECK(batch.complete&&batch.count==2);
    CHECK(batch.effects[0].target==1&&batch.effects[1].target==0);
    // No counted source can be revived from an unknown sleep countdown.
    s.world.bodies[0].position={0,0};s.world.bodies[1].position={5,5};sources[0].range=sources[1].range=1;
    CHECK(n::phase(&s,s.world,4).count==0);s.world.bodies[0].position={2,2};
    CHECK(!n::phase(&s,s.world,5).complete);
    // Restoration preserves current ability debt rather than minting readiness.
    s=scene();s.world.bodies[0].position={0,0};s.world.bodies[0].hp=100;s.world.bodies[0].cooldown[0]=6;auto ready=observation(s,1);
    CHECK(n::ready(&s,ready)==e::Verdict::Allowed);CHECK(ready.bodies[0].body->cooldown[0]==6);
    // A coverage failure records the actual first rejected actor and ALL
    // observed unsupported effects; it cannot become an executable offer.
    auto before=n::counters();n::Coverage coverage;coverage.faults=n::UnknownPassive|n::Consumable;
    auto restricted=make(4,0,{2,2},100,10);restricted.channel_present=true;restricted.non_timed_restrictions=4;
    n::classify_effects(coverage,restricted);
    CHECK(coverage.faults==(n::UnknownPassive|n::Consumable|n::Channel|n::Restrictions));
    auto rejected=n::reject_coverage({},4,coverage);
    CHECK(rejected.status==n::Status::Unsupported&&rejected.rejection==n::Rejection::UnsupportedKit);
    CHECK(rejected.rejected_actor==4&&rejected.coverage.faults==coverage.faults);
    CHECK(n::counters().rejected_actor[4]==before.rejected_actor[4]+1);
    for(unsigned bit=0;bit<8;++bit)CHECK(n::counters().coverage_faults[bit]==before.coverage_faults[bit]+((coverage.faults&(1u<<bit))?1:0));
    CHECK(n::counters().offers==before.offers&&n::counters().selected==before.selected);
    // Ryth's queried Strength is a fixed sampled stat in the no-purchase,
    // no-max-health-mutation family. Losing CURRENT health is not losing
    // bonus MAX health, and the already sampled passive is never added twice.
    s=scene();s.world.bodies[1].basic=70;s.world.bodies[1].max_hp=2000;s.world.bodies[1].hp=1000;
    s.world.bodies[1].abilities={};s.world.bodies[0].hp=s.world.bodies[0].max_hp=1000;
    auto sample=observation(s,0);const auto ryth=n::policy(&s,sample);
    CHECK(ryth.complete&&ryth.count==1&&ryth.steps[0].kind==e::PrimitiveKind::Basic);
    const auto healthy=e::run(s.world,economic,scenario,callbacks,requirement);
    s.world.bodies[1].hp=1;
    const auto wounded=e::run(s.world,economic,scenario,callbacks,requirement);
    CHECK(healthy.terminal.bodies[1].basic==70&&wounded.terminal.bodies[1].basic==70);
    CHECK(healthy.terminal.bodies[1].max_hp==2000&&wounded.terminal.bodies[1].max_hp==2000);
    CHECK(healthy.terminal.bodies[0].hp==wounded.terminal.bodies[0].hp);
    std::printf("capital_encounter_native: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
