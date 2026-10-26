#define WB_SERVICE_JOINT_NATIVE_HOST_FREE
#define WB_SERVICE_FORECAST_NATIVE_HOST_FREE
#define WB_ENCOUNTER_NATIVE_HOST_FREE
#define WB_SERVICE_NATIVE_HOST_FREE
#include "../src/capital_service_joint_native.h"
#include <cstdio>
#include <limits>

namespace j=wb::capital::service_joint_native;namespace n=wb::capital::service_forecast_native;namespace f=wb::capital::service_forecast;namespace en=wb::capital::encounter_native;
static unsigned checks=0,failures=0;
static void check(bool okay,const char* why){++checks;if(!okay){++failures;std::printf("FAIL %s\n",why);}}
static n::Epoch source(){return {4,10,55,3};}
static j::Request request(){return {source(),{{{3},{4},{5}}}};}
static j::PublicRoster roster(){return {source(),{{3,4,5}},true};}
static n::Capture base(){n::Capture c;c.source=source();c.status=n::Status::NeedModel;c.reason=n::Reason::ExternalUnknown;c.local_facts_complete=true;
    auto& w=c.world;w.round=10;w.horizon=100;w.team=1;w.phase=f::Phase::ActorAction;w.slot=3;w.revision=1009;w.actors=1;w.npcs=2;w.externals=3;w.slots=6;w.team_score=42;
    auto& b=w.bodies[0];b.id=4;b.kit=f::Kit::Willow;b.pose={11,2};b.hp=600;b.max_hp=700;b.strength=55;b.focus=100;b.actual=b.bag_known=b.gold_known=true;b.bag=9;b.gold=b.capital_floor=33;
    b.cooldown={6,0};b.reset_slot=1;b.heal_slot=0;b.heal_modifier=3;b.heal_range=4;b.items[41]=2;b.effective_recall_ticks=5;
    w.rules={true,5,2,50,8,0,8,5,0.1,1,false,false,2000,1000,0};w.group={7,f::Life::Alive,true,true,0,0,120,8,17,false};
    w.camps[0]={0,{8,2},642.f,1350.f,20.f,129.8f,3,1,true,true,true,true,false,false,1};
    w.camps[1]={2,{8,3},500.f,1350.f,20.f,0.f,3,0,true,false,false,false,true,false,0};
    w.others[0]={3,1,{9,2},1100,20,.9f,true,true,false};w.others[1]={0,0,{20,20},1300,50,1,true,true,false};w.others[2]={5,1,{9,3},900,0,.7f,true,true,false};
    c.external_coverage[0].faults=16;c.external_coverage[1].faults=4;c.external_coverage[2].faults=8;c.unknown_actor[0]=1;c.unknown_actor[1]=2;c.unknown_count=2;
    w.even={f::Slot{false,0,0},f::Slot{false,0,3},f::Slot{false,0,1},f::Slot{true,0,-1},f::Slot{false,0,2},f::Slot{false,0,5}};
    w.odd={f::Slot{false,0,3},f::Slot{false,0,0},f::Slot{true,0,-1},f::Slot{false,0,1},f::Slot{false,0,5},f::Slot{false,0,2}};return c;
}
static j::PeerFacts peer(int id,f::Kit kit){j::PeerFacts p;p.source=source();p.actor.id=id;p.info_actual=p.equipment_actual=p.bag_actual=true;p.focus=70;p.npc_taken_multiplier=1;p.bag=14;
    p.priority=13;p.priority_known=true;auto& b=p.body;b.id=id;b.team=1;b.position={9,id==3?2:3};b.hp=id==3?1100:900;b.max_hp=id==3?1400:1000;b.basic=id==3?40:70;
    b.alive=b.modeled=b.effects_closed=true;b.move_range=b.attack_range=1;b.cooldown={3,5};b.completed_turn=id==3;b.m=id!=3;b.a=b.c=false;b.ranged=kit==f::Kit::Willow;
    auto& d=p.design;d.actual=d.has_reset=true;d.kit=kit;d.reset_slot=kit==f::Kit::Willow?1:0;d.reset_cooldown=8;d.recall_base=5;
    if(kit==f::Kit::Varg){d.has_shield=true;d.shield_slot=1;d.shield_cooldown=8;d.shield_duration=5;d.shield_modifier=2.5;d.varg_splash=1;}
    if(kit==f::Kit::Willow){d.has_heal=true;d.heal_slot=0;d.heal_cooldown=8;d.heal_range=4;d.heal_modifier=3;}
    p.modifiers.actual=true;return p;
}
static std::array<j::PeerFacts,2> peers(){auto tank=peer(3,f::Kit::Varg);tank.body.mitigation=tank.npc_taken_multiplier=.9f;tank.body.shield_present=true;tank.body.shield=20;tank.body.shield_turns=1;
    tank.modifiers.durability=tank.modifiers.home=true;tank.modifiers.home_magnitude=.4f;tank.items[0]=1;tank.items[31]=1;
    auto damage=peer(5,f::Kit::Vendetta);damage.modifiers.jungle=true;damage.npc_taken_multiplier=.7f;damage.items[41]=4;return {tank,damage};}
static bool same_slot(f::Slot a,f::Slot b){return a.controlled==b.controlled&&a.actor==b.actor&&a.external_id==b.external_id;}
static void conversion(){auto b=base();auto r=request();auto p=peers();j::JointCapture out;
    check(j::convert(b,r,roster(),p,out)==j::Status::Ready&&out.world_ready,"actual three-body fact conversion succeeds");
    check(out.owner_index==1&&out.world.actors==3&&out.world.bodies[0].id==3&&out.world.bodies[1].id==4&&out.world.bodies[2].id==5,"named actual body handles preserve explicit requested control mapping");
    check(out.world.bodies[1].gold_known&&out.world.bodies[1].gold==33&&out.world.bodies[1].capital_floor==33&&out.world.bodies[1].items[41]==2,"actual owner wallet and all equipment retained");
    for(unsigned a:{0u,2u})check(!out.world.bodies[a].gold_known&&out.world.bodies[a].gold==0&&out.world.bodies[a].capital_floor==0&&out.world.bodies[a].bag_known&&out.world.bodies[a].bag==14,"visible peer bag never invents peer wallet or guaranteed capital");
    check(out.world.bodies[0].taken_multiplier==.9f&&out.world.bodies[0].npc_taken_multiplier==.9f&&out.world.bodies[2].taken_multiplier==1&&out.world.bodies[2].npc_taken_multiplier==.7f,"full NPC mitigation stays distinct from champion damage mitigation");
    check(out.world.bodies[0].effective_recall_ticks==3&&out.world.bodies[1].effective_recall_ticks==5&&out.world.bodies[2].effective_recall_ticks==5,"effective Recall duration follows each actual Home inventory");
    check(out.world.bodies[0].splash_radius_known&&out.world.bodies[0].splash_radius==1&&out.world.bodies[2].splash_radius==0,"queried Varg splash is not assigned to another kit");
    check(out.completed[0]&&!out.world.bodies[0].m&&!out.world.bodies[0].a&&!out.world.bodies[0].c&&out.world.bodies[0].completed,"ended peer retains actual MAC and completed flag");
    check(out.world.bodies[2].m&&!out.world.bodies[2].a&&!out.world.bodies[2].c&&!out.completed[2],"unfinished peer also retains exact observed remaining MAC");
    check(out.requires_start[0]&&!out.requires_start[1]&&out.requires_start[2]&&!out.prestart_provenance_known[0],"nonowner words require future dated START, without fabricated prior walk/cast authority");
    check(out.priority_known[0]&&out.priority[0]==13&&!out.priority_known[1],"only queried priority metadata is labeled known");
    check(out.world.rules.reset_cooldown==8&&out.world.rules.shield_cooldown==8&&out.world.rules.heal_cooldown==8,"queried participating abilities fill consistent shared cooldown fields");
    check(out.world.externals==1&&out.world.others[0].id==0&&out.world.others[0].hp==1300&&out.world.others[0].shield==50&&out.external_coverage[0].faults==4,"only actual named allies are promoted, enemy facts and coverage retained");
    check(out.unknown_count==2&&out.unknown_actor[0]==1&&out.unknown_actor[1]==2&&!out.external_facts_complete,"unseen enemy boundary remains explicit Unknown");
    check(out.world.group.id==7&&out.world.group.generation==17&&out.world.group.gold==120&&out.world.group.life==f::Life::Alive&&out.world.camps[0].hp==642.f&&out.world.camps[0].shield==129.8f&&out.world.camps[0].awake&&out.world.camps[0].empty_batches==1&&out.world.camps[0].damaged_since_batch,"joint capture preserves actual group lifetime and current NPC phase state");
    bool schedule=out.world.slots==b.world.slots&&out.world.slot==b.world.slot;for(unsigned a=0;a<b.world.slots;++a)schedule=schedule&&same_slot(out.world.even[a],b.world.even[a])&&same_slot(out.world.odd[a],b.world.odd[a]);
    check(schedule&&out.requires_schedule_rebind,"schedule bytes are untouched and explicitly require new control mapping");
    check(!out.command_authority&&!out.scenario_closed&&!out.utility_complete&&!out.commitment,"observed joint capacity grants neither commitment nor model or command authority");
    p[0].items[0]=99;b.world.camps[0].hp=0;check(out.world.bodies[0].items[0]==1&&out.world.camps[0].hp==642.f,"joint output owns immutable copies of all used actual facts");
}
static void rejection_paths(){auto b=base();auto r=request();auto p=peers();auto public_roster=roster();j::JointCapture out;
    auto bad=r;bad.actors[0].entity=j::Entity::Npc;check(j::convert(b,bad,public_roster,p,out)==j::Status::Unsupported&&out.reason==j::Reason::Namespace,"numeric NPC ID cannot impersonate named champion");
    bad=r;bad.actors[2].id=3;check(j::convert(b,bad,public_roster,p,out)==j::Status::Unsupported&&out.reason==j::Reason::Duplicate,"duplicate named body rejected");
    bad=r;++bad.source.effect;check(j::convert(b,bad,public_roster,p,out)==j::Status::Stale,"different actual effect generation cannot reuse base group or self facts");
    public_roster.actual_complete=false;check(j::convert(b,r,public_roster,p,out)==j::Status::NeedObservation,"three selected IDs do not prove complete actual allied roster");public_roster=roster();
    public_roster.allies[2]=8;check(j::convert(b,r,public_roster,p,out)==j::Status::Unsupported&&out.reason==j::Reason::Roster,"omitted fourth or different allied handle fails exact roster binding");public_roster=roster();
    p[0].body.team=0;check(j::convert(b,r,public_roster,p,out)==j::Status::Unsupported&&out.reason==j::Reason::Team,"observed enemy never promoted to controlled ally");p=peers();
    p[0].source.input--;check(j::convert(b,r,public_roster,p,out)==j::Status::Stale,"stale peer observation is not fresh support");p=peers();
    p[0].body.modeled=false;check(j::convert(b,r,public_roster,p,out)==j::Status::NeedObservation&&out.reason==j::Reason::UnknownPeer,"unseen peer stays missing rather than being synthesized");p=peers();
    p[0].body.alive=false;check(j::convert(b,r,public_roster,p,out)==j::Status::NeedObservation&&out.reason==j::Reason::Life,"dead peer has no fabricated respawn or available service");p=peers();
    p[0].bag_actual=false;check(j::convert(b,r,public_roster,p,out)==j::Status::NeedObservation,"unknown carried custody cannot become a known peer bag");p=peers();
    p[0].coverage.faults=en::Consumable;check(j::convert(b,r,public_roster,p,out)==j::Status::Unsupported&&out.reason==j::Reason::Effects,"existing consumable coverage gate remains in force");p=peers();
    p[0].body.channel_present=true;check(j::convert(b,r,public_roster,p,out)==j::Status::Unsupported,"active peer channel is not erased by future Hold or START");p=peers();
    p[0].body.enhanced_present=true;check(j::convert(b,r,public_roster,p,out)==j::Status::Unsupported,"unrepresented enhancement rejected explicitly");p=peers();
    p[0].modifiers.jungle=true;check(j::convert(b,r,public_roster,p,out)==j::Status::Unsupported&&out.reason==j::Reason::Equipment,"combined durability and Jungle native rounding order stays unsupported");p=peers();
    p[0].modifiers.treasure=true;check(j::convert(b,r,public_roster,p,out)==j::Status::Unsupported,"hidden fractional Treasure cash carry is not zeroed");p=peers();
    p[0].design.reset_cooldown=7;check(j::convert(b,r,public_roster,p,out)==j::Status::Unsupported&&out.reason==j::Reason::Rules,"global cooldown layout cannot represent conflicting actual reset durations");p=peers();
    p[0].focus=std::numeric_limits<float>::quiet_NaN();check(j::convert(b,r,public_roster,p,out)==j::Status::Unsupported&&out.reason==j::Reason::Numbers,"nonfinite actual combat stat rejected");p=peers();
    p[0].body.cooldown[1]=-1;check(j::convert(b,r,public_roster,p,out)==j::Status::Unsupported,"negative cooldown cannot become huge unsigned debt");p=peers();
    p[0].design.kit=f::Kit::Vendetta;p[0].design.has_shield=false;check(j::convert(b,r,public_roster,p,out)==j::Status::Unsupported&&out.reason==j::Reason::Kit,"exact class requires one actual Varg, Willow and Vendetta");p=peers();
    p[0].body.position=p[1].body.position;check(j::convert(b,r,public_roster,p,out)==j::Status::Unsupported,"distinct named living bodies require distinct actual positions");p=peers();
    b.status=n::Status::NeedFuel;check(j::convert(b,r,public_roster,p,out)==j::Status::NeedFuel&&!out.world_ready,"unfinished funded observation cannot publish an owned joint World");b=base();
    b.world.bodies[0].a=false;check(j::convert(b,r,public_roster,p,out)==j::Status::NeedObservation,"current owner initial class requires actual full MAC");b=base();
    b.world.others[0].team=0;check(j::convert(b,r,public_roster,p,out)==j::Status::Unsupported&&out.reason==j::Reason::Team,"base external identity cannot conflict with fresh peer allegiance");
    check(!out.world_ready&&!out.command_authority&&!out.commitment,"failed conversion leaves no stale world or physical commitment authority");
}
static void exact_modifiers_and_namespace(){auto b=base();auto p=peers();auto r=request();j::JointCapture out;p[1].modifiers.monster=true;p[1].modifiers.monster_magnitude=.2f;
    check(j::convert(b,r,roster(),p,out)==j::Status::Ready&&out.world.bodies[2].camp_multiplier==f::fp_add(1.f,.2f),"queried Monster multiplier uses supported single native factor");
    p[1].design.camp_multiplier=1.1f;check(j::convert(b,r,roster(),p,out)==j::Status::Unsupported,"unproved compounded camp-factor order remains unsupported");p=peers();
    p[0].modifiers.home_magnitude=1;check(j::convert(b,r,roster(),p,out)==j::Status::Unsupported,"invalid effective zero Recall time cannot be inferred");p=peers();
    b.world.camps[0].id=3;check(j::convert(b,r,roster(),p,out)==j::Status::Ready&&out.world.camps[0].id==out.world.bodies[0].id,"actual NPC and champion numeric IDs may coincide in distinct namespaces");
    std::swap(p[0],p[1]);check(j::convert(b,r,roster(),p,out)==j::Status::Ready&&out.world.bodies[0].kit==f::Kit::Varg,"peer fact order cannot silently change named control mapping");
}
int main(){conversion();rejection_paths();exact_modifiers_and_namespace();std::printf("capital_service_joint_native checks %u failures %u\n",checks,failures);return failures?1:0;}
