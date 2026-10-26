// Original matched-control assertions; only includes, helper namespaces, and entry points are adapted.
#include "../triadic-arena/src/c-policy.h"
#include "../realtime-guild/src/c-policy.h"
#include "../progression-interdiction/src/c-policy.h"
#include <cassert>
#include <cstdio>
#include <iostream>
#ifdef NDEBUG
#error "Matched caller assertions must remain enabled"
#endif

// Retained from .cache/plan10-c12-matched-control/callers.cpp
namespace matched_c12 {
using namespace p10;
static Actor body(int id,Champion type,Point at={1,0}){Actor a;a.id=a.pick=id;a.type=type;a.position=at;a.visible=a.alive=a.ally=a.can_move=a.can_attack=a.can_cast=true;a.hp=a.max_hp=1000;a.strength=40;a.focus=100;a.range=1;a.strength_growth=5;a.health_growth=100;a.focus_growth=10;return a;}
static Group source(int id,Point at={2,0},int members=1){Group g;g.id=id;g.position=at;g.approach={at.x-1,at.y};g.observed=g.complete=g.route_known=g.living=true;g.travel=0;g.to_bank=g.to_spawn=2;g.clear_turns=3*members;g.gems=14;g.gold=40;g.respawn=40;
 for(int n=0;n<members;++n){Monster m;m.id=100*id+n;m.group=id;m.position={at.x+n,at.y};m.observed=m.alive=true;m.hp=m.max_hp=120;g.hp+=m.hp;g.monsters.push_back(m);g.spawns.push_back(m.position);}
 for(int x=at.x-1;x<=at.x+members;++x)for(int y=at.y-1;y<=at.y+1;++y){Point p{x,y};if(x<0||y<0||std::find(g.spawns.begin(),g.spawns.end(),p)!=g.spawns.end())continue;g.approach_pads.push_back(p);}return g;
}
static World frame(Champion type){World w;w.self=body(0,type);w.groups={source(10,{2,0},3),source(11,{2,2})};w.groups[1].gold=15;w.groups[1].gems=7;w.groups[1].monsters[0].hp=w.groups[1].hp=40;w.groups[1].clear_turns=1;w.altars.push_back({0,{0,1},{1,1},true,false,false,true,2,0});w.spawn={0,0};w.to_spawn=0;w.geometry_complete=w.observation_complete=w.actor_observation_complete=w.opposition_complete=true;return w;}
static World direct_frame(Champion type){auto w=frame(type);w.groups={source(21)};w.groups[0].gems=1;w.groups[0].gold=0;auto r=body(9,Champion::varg,{2,1});r.ally=false;r.hp=r.max_hp=95;r.strength=0;r.bag=14;w.enemies={r};return w;}
static void ren_cc(Actor&a){a.type=Champion::ren;a.strength=35;auto&ab=a.abilities[0];ab.ready=ab.cc=ab.damage=ab.exclusive=ab.targets_camps=true;ab.targets_self=false;ab.range=4;ab.modifier=.75;ab.duration=1;ab.max_cooldown=8;}
static Directive actual(p10_c12::CPolicy&p,const World&w,Arm arm){auto d=p.step(w,arm);assert(p.traces<=2);return d;}
static void shared_burst_cc_shield(){
 for(Arm arm:{Arm::active,Arm::control}){
  auto w=direct_frame(Champion::ren);ren_cc(w.self);p10_c12::CPolicy p;auto d=actual(p,w,arm);assert(d.task==Task::attack&&d.actor==9&&d.cast_slot==0&&d.template_id==1212);assert(p.direct.selected&&p.direct.damage==99&&p.encounters==0&&!d.safety_verified);
  auto cc=w;cc.self.hp=350;cc.enemies[0].hp=cc.enemies[0].max_hp=1000;cc.enemies[0].strength=160;cc.enemies[0].bag=0;p10_c12::CPolicy c;d=actual(c,cc,arm);assert(d.task==Task::attack&&d.actor==9&&d.cast_slot==0&&c.direct.avoided==160&&c.direct.damage==99&&c.encounters==0);
  auto immune=cc;immune.enemies[0].type=Champion::marcella;p10_c12::CPolicy m;d=actual(m,immune,arm);assert(!m.direct.selected&&d.template_id!=1212);
  auto shield=cc;shield.self.type=Champion::varg;shield.self.abilities={};auto&ab=shield.self.abilities[1];ab.ready=ab.shield=ab.targets_self=true;ab.duration=5;ab.modifier=2.5;p10_c12::CPolicy s;d=actual(s,shield,arm);assert(d.task==Task::attack&&d.actor==9&&d.cast_slot==1&&s.direct.selected&&s.encounters==0);
 }
 std::cout<<"PASS actual CPolicy both arms share native Ren burst/one-round CC and Varg shield/basic\n";
}
static void shared_camp_reset(){for(Arm arm:{Arm::active,Arm::control}){auto w=frame(Champion::vendetta);w.groups={source(61)};w.self.strength=70;w.groups[0].hp=w.groups[0].monsters[0].hp=140;auto&a=w.self.abilities[0];a.ready=a.reset=a.targets_self=true;a.max_cooldown=8;p10_c12::CPolicy p;auto d=actual(p,w,arm);assert(d.task==Task::attack&&d.monster==6100&&d.second_monster==6100&&d.cast_slot==0&&d.safety_verified&&fresh_certificate(w,d));assert(!p.direct.selected&&p.encounters==0);}std::cout<<"PASS actual CPolicy both arms share camp basic/reset/basic; no actor-reset forecast\n";}
static void tactical_guard_negatives(){for(Arm arm:{Arm::active,Arm::control}){
 auto mitigated=direct_frame(Champion::ren);ren_cc(mitigated.self);mitigated.enemies[0].hp=100;mitigated.enemies[0].inventory[int(Item::durability_health)]=5;p10_c12::CPolicy m;auto d=actual(m,mitigated,arm);assert(!m.direct.selected&&d.template_id!=1212);
 auto reset=direct_frame(Champion::ren);ren_cc(reset.self);reset.self.hp=350;auto&r=reset.enemies[0];r.type=Champion::vendetta;r.hp=1000;r.strength=160;r.bag=0;r.abilities[0].ready=r.abilities[0].reset=r.abilities[0].targets_self=true;p10_c12::CPolicy v;d=actual(v,reset,arm);assert(!v.direct.selected&&d.template_id!=1212);
 auto dash=reset;dash.enemies[0].type=Champion::heart;dash.enemies[0].abilities={};auto&a=dash.enemies[0].abilities[1];a.ready=a.dash=a.targets_ground=true;a.range=2;a.radius=1;a.modifier=1.5;p10_c12::CPolicy h;d=actual(h,dash,arm);assert(!h.direct.selected&&d.template_id!=1212);
 auto collateral=direct_frame(Champion::varg);collateral.enemies[0].hp=30;auto unknown=source(22,{3,1});unknown.complete=unknown.observed=false;unknown.monsters[0].observed=false;collateral.groups.push_back(unknown);p10_c12::CPolicy x;d=actual(x,collateral,arm);assert(!x.direct.selected&&d.template_id!=1212);
 auto ren=direct_frame(Champion::ren);ren_cc(ren.self);ren.self.hp=115;ren.enemies[0].type=Champion::ren;ren.enemies[0].hp=70;p10_c12::CPolicy retaliation;d=actual(retaliation,ren,arm);assert(!retaliation.direct.selected&&d.template_id!=1212);
 }std::cout<<"PASS both-arm caller negatives for Durability, rival reset/dash/Heart, Runtime collateral refusal and two Ren retaliation hits\n";}
static World joint_frame(){auto w=frame(Champion::varg);w.self.strength=10;w.groups[0]=source(30,{3,0});auto&camp=w.groups[0];camp.hp=camp.monsters[0].hp=20;camp.monsters[0].awake=true;camp.monsters[0].strength=40;camp.monsters[0].range=1;camp.incoming=40;camp.to_bank=20;camp.clear_turns=2;auto&home=w.groups[1];home.to_bank=1;home.monsters[0].hp=home.hp=40;home.clear_turns=1;w.visible_open_tiles={{1,1},{2,0},{2,1},{3,1},{3,2},{1,0},{1,2}};auto r=body(9,Champion::prism,{2,1});r.ally=false;r.hp=r.max_hp=50;r.strength=0;w.enemies={r};return w;}
static void joint_dependency(){auto w=joint_frame();p10_c12::CPolicy a,c;auto ad=actual(a,w,Arm::active),cd=actual(c,w,Arm::control);assert(!a.direct.selected&&!c.direct.selected);assert(a.encounters==1&&a.source==30&&a.district[1]==30&&a.last.admitted&&a.last.program==arena::Program::burst_rival&&ad.task==Task::attack&&ad.actor==9&&!ad.safety_verified);assert(c.encounters==0&&c.district[1]<0&&cd.task!=Task::attack);bool environment=false;for(const auto&b:a.last.branches)environment|=b.campDamageToRival>0;assert(environment);auto off=w;off.groups[0].monsters[0].awake=false;for(Arm arm:{Arm::active,Arm::control}){p10_c12::CPolicy p;auto d=actual(p,off,arm);assert(p.encounters==0&&d.task!=Task::attack);}w.round=1;w.enemies.clear();ad=actual(a,w,Arm::active);assert(ad.safety_verified&&ad.group==30);std::cout<<"PASS joint camp dependency changes active allocation; sleeping camp removes it; fresh district continuation retained\n";}
static void post_emblems(Actor&a){a.monster_emblem=a.jungle_emblem=true;a.inventory[int(Item::monster_strength)]=5;a.inventory[int(Item::jungle_health)]=5;}
static void acknowledge(World&w,const Directive&d){assert(d.task==Task::buy&&d.buy_count==1&&w.own_gold>=w.shard_cost&&!d.safety_verified);int idx=int(d.item),units=p10_c::power_units(w.self.inventory[idx],1);++w.self.inventory[idx];w.own_gold-=w.shard_cost;if(d.item==Item::strength)w.self.strength+=units*w.self.strength_growth;if(d.item==Item::focus)w.self.focus+=units*w.self.focus_growth;++w.round;w.receipts={{Receipt::Kind::purchase,w.round,-1,w.self.id,idx}};}
static void retained_capital_channels(){for(Arm arm:{Arm::active,Arm::control}){auto w=frame(Champion::varg);post_emblems(w.self);w.self.at_spawn=true;w.own_gold=300;p10_c12::CPolicy p;for(int n=0;n<10;++n){auto d=actual(p,w,arm);assert(p.traces==0);acknowledge(w,d);}assert(w.self.inventory[int(Item::strength)]==10&&p.capital.completed==1&&w.own_gold==0);auto ren=frame(Champion::ren);post_emblems(ren.self);ren.self.at_spawn=true;ren.own_gold=150;p10_c12::CPolicy r;for(int n=0;n<5;++n)acknowledge(ren,actual(r,ren,arm));assert(ren.self.inventory[int(Item::focus)]==5);auto fight=frame(Champion::varg);post_emblems(fight.self);fight.self.at_spawn=true;fight.own_gold=150;fight.groups[0].monsters[0].awake=true;fight.groups[0].monsters[0].range=3;p10_c12::CPolicy x;x.source=x.district[1]=10;assert(actual(x,fight,arm).task!=Task::buy&&!x.capital.active);auto channel=frame(Champion::ren);channel.self.channel=Channel::tribute;channel.round=8;channel.receipts={{Receipt::Kind::payout,3,10,channel.self.id,0,14,40}};p10_c12::CPolicy q;assert(actual(q,channel,arm).task==Task::wait&&q.economy.payouts==1&&q.traces==0);actual(q,channel,arm);assert(q.economy.payouts==1);}std::cout<<"PASS C12 district capital repeats personally funded growth and preserves battle/channel/receipt ownership in both arms\n";}
static int reads=0,fall_at=0;static int falling(){return ++reads<fall_at?100000:1000;}
static void falling_fuel(){int aborts=0;for(Arm arm:{Arm::active,Arm::control})for(int at=1;at<=96;++at){reads=0;fall_at=at;set_work_fuel_reader(falling);auto w=direct_frame(Champion::ren);ren_cc(w.self);auto r=w.enemies[0];r.id=10;r.position={1,1};w.enemies.push_back(r);p10_c12::CPolicy p;auto d=actual(p,w,arm);if(p.direct.fuel_aborted){++aborts;assert(!p.direct.selected&&d.template_id!=1212);}set_work_fuel_reader(nullptr);}assert(aborts>0);std::cout<<"PASS actual CPolicy falling-reader guards discard partial independent menus across both arms; modeled fuel only\n";}
static void run(){shared_burst_cc_shield();shared_camp_reset();tactical_guard_negatives();joint_dependency();retained_capital_channels();falling_fuel();std::cout<<"PASS matched C12 caller suite; no native qualification\n";}

} // namespace matched_c12

// Retained from .cache/plan10-c15-matched-control/fixtures.cpp
namespace matched_c15 {
using namespace p10;
static World frame(Champion type=Champion::varg){World w;w.round=10;w.max_rounds=1000;w.fuel=100000;w.action_reserve=40000;w.spawn={0,0};w.self.id=0;w.self.type=type;w.self.visible=w.self.alive=w.self.ally=true;w.self.can_move=w.self.can_attack=w.self.can_cast=true;w.self.position={1,1};w.self.hp=w.self.max_hp=1000;w.self.strength=70;w.self.focus=100;w.self.range=1;w.self.strength_growth=5;w.self.focus_growth=10;w.shard_cost=30;w.safe_now=true;return w;}
static Group source(int id,std::initializer_list<Monster>mobs){Group g;g.id=id;g.observed=g.complete=g.route_known=g.living=g.program_available=true;g.travel=0;g.to_bank=g.to_spawn=1;g.gems=40;g.monsters=mobs;g.position=g.monsters[0].position;g.approach={1,1};g.approach_pads={{1,1},{1,2},{2,2}};for(auto&m:g.monsters){m.group=id;g.spawns.push_back(m.position);g.hp+=m.hp;}return g;}
static Monster mob(int id,Point p,double hp,double str=0,int range=0){Monster m;m.id=id;m.position=p;m.hp=m.max_hp=hp;m.strength=str;m.range=range;m.observed=m.alive=true;return m;}
static void reset(){p10_mobile::default_cache={};set_work_fuel_reader(nullptr);}
static bool same_prefix(const Directive&a,const Directive&b){return a.task==b.task&&a.group==b.group&&a.actor==b.actor&&a.monster==b.monster&&a.second_monster==b.second_monster&&a.cast_slot==b.cast_slot&&a.destination==b.destination&&a.item==b.item&&a.buy_count==b.buy_count&&a.priority==b.priority&&a.pulse==b.pulse&&a.reserve_reset==b.reserve_reset&&a.strict_tank==b.strict_tank&&a.minimum_start_hp==b.minimum_start_hp&&a.safety_round==b.safety_round&&a.safety_verified==b.safety_verified;}
static void paired_prefix(World w,p10_b15::Kind kind){
 reset();p10_b15::BPolicy active,control;auto a=active.step(w,Arm::active);auto c=control.step(w,Arm::control);assert(same_prefix(a,c)&&a.safety_verified&&fresh_certificate(w,a));assert(active.skills[active.active].kind==kind&&control.skills[control.active].kind==kind);assert(active.guardHits==1&&control.guardHits==1&&active.mobilePlans==0&&control.mobilePlans==0&&active.traces==1&&control.traces==1);
 ++w.round;a=active.step(w,Arm::active);c=control.step(w,Arm::control);assert(same_prefix(a,c)&&fresh_certificate(w,a));assert(active.compiles==1&&active.compiledReuses==1&&active.onlineRebuilds==0);assert(control.compiles==2&&control.onlineRebuilds==2&&control.compiledReuses==0);assert(active.sharedGuardChecks==2&&control.sharedGuardChecks==2);
}
static void exact_menu(){
 auto single=frame();single.groups={source(1,{mob(11,{2,1},60)})};paired_prefix(single,p10_b15::Kind::single_hit);
 auto splash=frame();splash.groups={source(2,{mob(21,{2,1},120),mob(22,{2,2},100)})};auto&r=splash.self.abilities[0];r.ready=r.reset=r.targets_self=true;r.max_cooldown=8;paired_prefix(splash,p10_b15::Kind::splash_reset);
 auto burst=frame(Champion::stellar);burst.self.range=3;burst.groups={source(3,{mob(31,{3,1},150),mob(32,{3,2},140)})};auto&spell=burst.self.abilities[1];spell.ready=spell.damage=spell.aoe=spell.targets_camps=true;spell.modifier=2;spell.radius=1;spell.range=4;spell.max_cooldown=8;paired_prefix(burst,p10_b15::Kind::burst_hold);
 static_assert(p10_b15::BPolicy::primitive_menu.size()==8);std::puts("matched exact primitive menu: single target, positioned splash/reset, ranged burst; literal prefixes and budgets equal PASS");
}
static int ordinary_fuel(){return 100000;}static int tight_fuel(){return 40200;}
static void rejection(const World&w){reset();p10_b15::BPolicy a,c;auto ad=a.step(w,Arm::active);reset();auto cd=c.step(w,Arm::control);assert(a.guardHits==0&&c.guardHits==0&&same_prefix(ad,cd)&&a.traces<=2&&c.traces<=2);}
static void changed_guards(){
 auto w=frame();w.groups={source(5,{mob(51,{2,1},120),mob(52,{2,2},100)})};auto&r=w.self.abilities[0];r.ready=r.reset=r.targets_self=true;r.max_cooldown=8;
 auto cd=w;cd.self.abilities[0].ready=false;cd.self.cooldown[0]=cd.self.abilities[0].cooldown=7;rejection(cd);
 auto low=w;auto g=source(6,{mob(61,{1,2},500,100,3)});g.monsters[0].awake=true;low.groups.push_back(g);low.self.hp=100;rejection(low);
 auto unknown=w;auto h=source(6,{mob(61,{2,3},500,500,3)});h.observed=h.complete=false;h.monsters[0].observed=false;unknown.groups.push_back(h);rejection(unknown);
 auto hidden=w;hidden.groups[0].complete=false;rejection(hidden);
 auto spent=w;spent.self.can_attack=spent.self.can_cast=false;rejection(spent);
 auto foe=w;Actor enemy=foe.self;enemy.id=9;enemy.ally=false;enemy.position={1,2};foe.enemies={enemy};rejection(foe);
 reset();set_work_fuel_reader(tight_fuel);p10_b15::BPolicy a,c;auto ad=a.step(w,Arm::active);auto ct=c.step(w,Arm::control);assert(a.guardHits==0&&c.guardHits==0&&a.traces==0&&c.traces==0&&same_prefix(ad,ct));set_work_fuel_reader(nullptr);
 // The same actual failed-move receipt invalidates either representation.
 auto blocked=frame();blocked.groups={source(7,{mob(71,{2,1},60)}),source(8,{mob(81,{2,2},60)})};p10_b15::BPolicy ba,bc;ba.step(blocked,Arm::active);bc.step(blocked,Arm::control);blocked.receipts={{Receipt::Kind::blocked,blocked.round,ba.source,blocked.self.id}};++blocked.round;auto ab=ba.step(blocked,Arm::active);auto cb=bc.step(blocked,Arm::control);assert(ba.blockedSource==7&&bc.blockedSource==7&&ab.group==8&&cb.group==8&&same_prefix(ab,cb));
 // Group lifecycle, not cached successful damage, invalidates the descriptor.
 auto changed=frame();changed.groups={source(9,{mob(91,{2,1},60)})};p10_b15::BPolicy pa,pc;pa.step(changed,Arm::active);pc.step(changed,Arm::control);++changed.round;changed.groups[0].last_payout=changed.round;pa.step(changed,Arm::active);pc.step(changed,Arm::control);assert(pa.compiles==2&&pc.compiles==2);
 std::puts("matched fresh guards: cooldown/HP/private completeness/allowances/enemy/fuel/block/lifecycle rejection and invalidation PASS");
}
static void mobile_and_service(){
 reset();auto w=frame();w.groups={source(10,{mob(101,{2,1},210)})};p10_b15::BPolicy a,c;auto ad=a.step(w,Arm::active);reset();auto cd=c.step(w,Arm::control);assert(a.mobilePlans==1&&c.mobilePlans==1&&a.guardHits==0&&c.guardHits==0&&same_prefix(ad,cd)&&fresh_certificate(w,ad)&&a.traces==c.traces);
 reset();w=frame(Champion::tavi);w.self.range=3;w.self.position={0,1};w.visible_open_tiles={{0,1},{1,1},{1,2},{2,2}};w.groups={source(11,{mob(111,{4,1},210)})};w.groups[0].approach_pads={{1,1},{1,2},{2,2}};w.groups[0].approach={1,1};p10_b15::BPolicy ranged,rc;ad=ranged.step(w,Arm::active);reset();cd=rc.step(w,Arm::control);assert(ranged.mobilePlans==1&&rc.mobilePlans==1&&same_prefix(ad,cd)&&ad.task==Task::move&&fresh_certificate(w,ad)&&ranged.traces==rc.traces);
 auto service=frame();service.self.bag=30;p10_b15::BPolicy sa,sc;assert(same_prefix(sa.step(service,Arm::active),sc.step(service,Arm::control))&&sa.step(service,Arm::active).task==Task::bank);
 auto recovery=frame();recovery.safe_now=false;recovery.self.hp=10;p10_b15::BPolicy ra,rr;assert(ra.step(recovery,Arm::active).task==Task::recover&&rr.step(recovery,Arm::control).task==Task::recover);
 auto channel=frame();channel.self.channel=Channel::tribute;channel.own_payout=true;channel.last_payout_round=9;p10_b15::BPolicy ca,cc;assert(ca.step(channel,Arm::active).task==Task::wait&&cc.step(channel,Arm::control).task==Task::wait&&ca.economy.payouts==1&&cc.economy.payouts==1);
 auto threshold=frame(Champion::marcella);threshold.self.at_spawn=true;threshold.self.can_attack=threshold.self.can_cast=false;threshold.self.strength_growth=5;threshold.own_gold=30;threshold.groups={source(12,{mob(121,{2,1},75)})};p10_b15::BPolicy ta,tc;auto at=ta.step(threshold,Arm::active);reset();auto ct=tc.step(threshold,Arm::control);assert(at.task==Task::buy&&at.item==Item::monster_strength&&same_prefix(at,ct));
 std::puts("matched remaining primitives: melee/ranged mobile prefix, real loaded service, recovery, receipt-first channel and affordable threshold menu PASS");
}
static void maintenance(){
 reset();auto w=frame();w.self.at_spawn=true;w.own_gold=300;w.self.monster_emblem=w.self.jungle_emblem=true;w.self.inventory[int(Item::monster_strength)]=5;w.self.inventory[int(Item::jungle_health)]=5;w.groups={source(15,{mob(151,{2,1},60)})};p10_c15::CPolicy a,c;set_work_fuel_reader(ordinary_fuel);auto ad=a.step(w,Arm::active);auto cd=c.step(w,Arm::control);assert(ad.task==Task::buy&&ad.item==cd.item&&ad.buy_count==cd.buy_count&&a.capital.active&&c.capital.active&&a.capital.committedCost==c.capital.committedCost&&a.compiles==0&&c.compiles==0);
 w.self.inventory[int(ad.item)]+=5;w.self.strength+=30;w.own_gold-=150;++w.round;w.own_purchase=true;auto next=a.step(w,Arm::active);auto other=c.step(w,Arm::control);assert(next.task==Task::buy&&other.task==Task::buy&&next.item==other.item&&a.capital.completed==1&&c.capital.completed==1&&a.capital.committedCost==150&&c.capital.committedCost==150);
 auto work=frame();work.groups={source(16,{mob(161,{2,1},60)})};p10_c15::CPolicy pa,pc;ad=pa.step(work,Arm::active);cd=pc.step(work,Arm::control);assert(same_prefix(ad,cd)&&pa.guardHits==1&&pc.guardHits==1);++work.round;pa.step(work,Arm::active);pc.step(work,Arm::control);assert(pa.compiles==1&&pc.compiles==2);
 set_work_fuel_reader(nullptr);std::puts("C15 maintenance stays distinct and personally funded; same capital menu in both arms, repeated packet and post-maintenance organization PASS");
}
static void run(){exact_menu();changed_guards();mobile_and_service();maintenance();std::puts("PASS C15/B15 matched-menu actual callers; native fuel/control qualification pending");}

} // namespace matched_c15

// Retained from .cache/plan10-c16-matched-control/fixtures.cpp
namespace matched_c16 {
using namespace p10;
static World frame(Champion type=Champion::varg){World w;w.round=10;w.max_rounds=1000;w.fuel=100000;w.action_reserve=40000;w.spawn={0,0};w.self.id=5;w.self.type=type;w.self.position={1,2};w.self.visible=w.self.alive=w.self.ally=true;w.self.can_move=w.self.can_attack=w.self.can_cast=true;w.self.hp=w.self.max_hp=1000;w.self.strength=40;w.self.focus=100;w.self.range=1;w.self.strength_growth=5;w.self.focus_growth=10;w.safe_now=true;w.shard_cost=30;return w;}
static Monster mob(int id,Point p,double hp,double str=0,int range=0){Monster m;m.id=id;m.position=p;m.hp=m.max_hp=hp;m.strength=str;m.range=range;m.observed=m.alive=true;return m;}
static Group group(int id,std::initializer_list<Monster>mobs,Gems gems,int bank=1,int respawn=100){Group g;g.id=id;g.monsters=mobs;g.gems=gems;g.to_bank=bank;g.to_spawn=1;g.travel=0;g.respawn=respawn;g.observed=g.complete=g.route_known=g.program_available=g.living=true;g.approach={1,2};g.approach_pads={{1,2},{2,2},{1,1},{1,3},{2,4},{3,3},{3,1},{4,2}};g.position=g.monsters[0].position;g.clear_turns=0;for(auto&m:g.monsters){m.group=id;g.spawns.push_back(m.position);g.hp+=m.hp;g.clear_turns+=int(std::ceil(m.hp/40));}return g;}
static Actor actor(int id,Champion type,Point p){Actor a;a.id=id;a.type=type;a.position=p;a.visible=a.alive=a.ally=a.can_move=a.can_attack=a.can_cast=true;a.hp=a.max_hp=1000;a.strength=40;a.focus=100;a.range=3;return a;}
static void capability(World&w,bool crew){auto e=actor(9,Champion::prism,{7,7});e.ally=false;e.hp=100;e.bag=40;e.strength=0;w.enemies={e};if(crew)w.allies.push_back(actor(2,Champion::ren,{6,7}));}
static void reset(){p10_mobile::default_cache={};set_work_fuel_reader(nullptr);}
static void bounded(const p10_c16::CPolicy&p){assert(p.traces>=0&&p.traces<=2);}
static bool same_action(const Directive&a,const Directive&b){return a.task==b.task&&a.group==b.group&&a.actor==b.actor&&a.monster==b.monster&&a.second_monster==b.second_monster&&a.cast_slot==b.cast_slot&&a.destination==b.destination&&a.item==b.item&&a.buy_count==b.buy_count&&a.safety_verified==b.safety_verified;}
static void raw_gems_is_not_acquisition(){
 reset();auto w=frame();w.groups={group(10,{mob(101,{2,1},40)},20),group(11,{mob(111,{2,3},40)},40,20)};capability(w,false);p10_c16::CPolicy a,c;auto ad=a.step(w,Arm::active);reset();auto cd=c.step(w,Arm::control);
 assert(a.schedules[3].ownBank>a.schedules[0].ownBank&&a.schedules[3].ownService<a.schedules[0].ownService);assert(a.schedules[3].bankAt>a.schedules[0].bankAt&&a.schedules[3].renewal>a.schedules[0].renewal);assert(ad.group==10&&cd.group==10&&same_action(ad,cd)&&!a.replacementOwned&&!c.replacementOwned&&a.denialSelections==0);bounded(a);bounded(c);
 std::puts("C16 raw-gems discriminator: larger replacement loses its complete bank/return service comparison in BOTH arms PASS");
}
static World acquisition_frame(){auto w=frame();w.groups={group(20,{mob(201,{2,1},240)},27),group(21,{mob(211,{2,2},120),mob(212,{2,3},120),mob(213,{3,2},120)},23)};capability(w,false);return w;}
static void strong_acquisition_control(){
 reset();auto w=acquisition_frame();p10_c16::CPolicy a,c;auto ad=a.step(w,Arm::active);reset();auto cd=c.step(w,Arm::control);
 assert(a.appointment[0]==20&&a.appointment[1]==21&&a.schedules[3].ownService>a.schedules[0].ownService);assert(ad.group==21&&cd.group==21&&same_action(ad,cd)&&fresh_certificate(w,ad));assert(a.cause==p10_b16::Cause::replacement_acquisition&&c.cause==p10_b16::Cause::replacement_acquisition&&a.replacementOwned&&c.replacementOwned&&!a.interdictionWitness&&!c.interdictionWitness&&!a.denialConditional&&a.denialSelections==0);assert(a.actualAcquisitionPayout<0&&c.actualAcquisitionPayout<0);bounded(a);bounded(c);
 // Interception does not turn an own-yield optimum into a denial witness.
 auto hit=w;capability(hit,true);p10_c16::CPolicy ha,hc;auto h=ha.step(hit,Arm::active);reset();auto k=hc.step(hit,Arm::control);assert(h.group==21&&k.group==21&&ha.cause==p10_b16::Cause::replacement_acquisition&&ha.denialSelections==0&&!ha.interdictionWitness);
 std::puts("C16 strong same-menu control acquires the faster full program despite fewer raw gems; own yield remains distinct from denial PASS");
}
static World denial_frame(){auto w=frame();w.groups={group(30,{mob(301,{2,1},400)},30),group(31,{mob(311,{2,3},40)},14)};capability(w,true);return w;}
static void conditional_geography(){
 reset();auto w=denial_frame();p10_c16::CPolicy a,c;auto ad=a.step(w,Arm::active);reset();auto cd=c.step(w,Arm::control);
 assert(cd.group==30&&ad.group==31&&c.schedules[1].ownService>a.schedules[2].ownService);assert(a.cause==p10_b16::Cause::conditional_denial&&a.denialConditional&&a.denialSelections==1&&a.replacementOwned&&!c.replacementOwned);assert(a.schedules[2].nominalReentry>a.schedules[2].baselineReentry+8&&a.schedules[2].nominalDenied>a.schedules[2].displaced+1);assert(a.schedules[2].unknownSubstitutes&&a.schedules[2].adverseReentry==0&&!a.interdictionWitness);bounded(a);bounded(c);
 auto cheap=w;cheap.groups.push_back(group(32,{mob(321,{3,4},40)},1));p10_c16::CPolicy alternative;auto d=alternative.step(cheap,Arm::active);assert(d.group==30&&!alternative.replacementOwned&&alternative.denialSelections==0);
 auto aligned=w;aligned.groups[1].respawn=1;p10_c16::CPolicy renew;d=renew.step(aligned,Arm::active);assert(d.group==30&&renew.schedules[2].nominalReentry<=renew.schedules[2].baselineReentry&&!renew.replacementOwned);
 auto absent=w;absent.allies.clear();p10_c16::CPolicy noCrew;d=noCrew.step(absent,Arm::active);assert(d.group==30&&!noCrew.replacementOwned&&noCrew.denialSelections==0);
 std::puts("C16 mechanism preserved: conditional priced re-entry geography differs from full-acquisition control; cheap substitute/aligned respawn/absent crew veto, adverse wallet branch retained PASS");
}
static void refused_and_personal(){
 reset();auto w=frame();w.groups={group(40,{mob(401,{2,1},40)},20),group(41,{mob(411,{3,2},2400,5000,1)},100)};capability(w,true);p10_c16::CPolicy a,c;auto ad=a.step(w,Arm::active);reset();auto cd=c.step(w,Arm::control);assert(ad.group==40&&cd.group==40&&a.replacementRefusals>0&&c.replacementRefusals>0&&!a.replacementOwned&&!c.replacementOwned&&!a.schedules[2].feasible&&!c.schedules[2].feasible);bounded(a);bounded(c);
 auto shared=frame();shared.groups={group(42,{mob(421,{2,1},40)},20),group(43,{mob(431,{2,3},40)},1)};capability(shared,true);auto holder=actor(3,Champion::marcella,{2,3});shared.allies.push_back(holder);assert(p10_b16::BPolicy::personal_share(shared,shared.groups[1])==0);p10_c16::CPolicy zero;auto d=zero.step(shared,Arm::active);assert(d.group==42&&!zero.replacementOwned&&zero.replacementRefusals>0);
 // A previously committed source becoming physically unavailable cannot be
 // rescued by a promised kill, missing enemy, unseen helper or stale proof.
 auto gone=acquisition_frame();p10_c16::CPolicy owner;owner.step(gone,Arm::active);assert(owner.replacementOwned);++gone.round;gone.groups[1].program_available=false;gone.enemies.clear();d=owner.step(gone,Arm::active);assert(!owner.replacementOwned&&owner.replacementRefusals>0&&d.group==20);
 std::puts("C16 independent ownership admission: unsafe replacement, zero personal payout and invalidated source refused in both menus PASS");
}
static void receipt_and_capital(){
 reset();auto w=acquisition_frame();p10_c16::CPolicy p;auto d=p.step(w,Arm::active);const int acquired=p.source;assert(p.replacementOwned&&p.actualAcquisitionPayout<0&&!p.capital.active);++w.round;for(auto&m:w.groups[1].monsters){m.hp=0;m.alive=false;}w.groups[1].hp=0;w.groups[1].living=false;w.groups[1].cleared=true;d=p.step(w,Arm::active);assert(d.task==Task::farm&&p.actualAcquisitionPayout<0&&p.deferredReplacement);
 ++w.round;w.self.bag=23;w.own_payout=true;w.last_payout_group=acquired;w.last_payout_round=w.round;w.receipts={{Receipt::Kind::payout,w.round,acquired,w.self.id,0,23,40}};d=p.step(w,Arm::active);assert(d.task==Task::bank&&p.actualAcquisitionSource==acquired&&p.actualAcquisitionPayout==w.round&&p.actualAcquisitionBank<0&&!p.capital.active);
 ++w.round;w.own_payout=false;w.self.bag=0;w.own_bank_completed=true;w.last_bank_round=w.round;w.receipts={{Receipt::Kind::bank,w.round,-1,w.self.id,0,23,0}};w.self.at_spawn=true;w.own_gold=150;d=p.step(w,Arm::active);assert(p.actualAcquisitionBank==w.round&&!p.replacementOwned&&!p.deferredReplacement&&d.task==Task::buy&&p.capital.active&&p.capital.committedCost==150);
 // The old paid-and-banked epoch cannot close a new appointment.
 p.replacementOwned=true;p.source=99;p.acquiredRound=w.round+1;p.observe_acquisition(w);assert(p.replacementOwned);
 auto channel=frame();channel.self.channel=Channel::tribute;channel.own_payout=true;channel.last_payout_round=channel.round;channel.last_payout_group=55;p10_c16::CPolicy pa,pc;pa.replacementOwned=pc.replacementOwned=true;pa.source=pc.source=55;pa.acquiredRound=pc.acquiredRound=9;assert(pa.step(channel,Arm::active).task==Task::wait&&pc.step(channel,Arm::control).task==Task::wait&&pa.actualAcquisitionPayout==channel.round&&pc.actualAcquisitionPayout==channel.round);
 std::puts("C16 actual source ownership: appointment != payout != bank; real epoch receipts precede channel/capital, bank releases recurrent personal maintenance PASS");
}
static int low_fuel(){return 40200;}
static void same_hit_and_budget(){
 reset();auto w=frame(Champion::ren);auto e=actor(9,Champion::prism,{2,2});e.ally=false;e.bag=20;e.hp=30;e.strength=0;w.enemies={e};p10_c16::CPolicy a,c;auto ad=a.step(w,Arm::active);auto cd=c.step(w,Arm::control);assert(same_action(ad,cd)&&ad.actor==9&&a.attempts.used==1&&c.attempts.used==1);w.round+=8;w.enemies.clear();a.step(w,Arm::active);c.step(w,Arm::control);assert(a.attempts.used==1&&c.attempts.used==1&&!a.attempts.active&&!c.attempts.active);
 auto poor=denial_frame();set_work_fuel_reader(low_fuel);p10_c16::CPolicy la,lc;ad=la.step(poor,Arm::active);cd=lc.step(poor,Arm::control);assert(same_action(ad,cd)&&la.traces==0&&lc.traces==0&&!la.replacementOwned&&!lc.replacementOwned);set_work_fuel_reader(nullptr);
 auto capital=frame(Champion::ren);capital.self.at_spawn=true;capital.own_gold=150;p10_c16::CPolicy fa,fc;ad=fa.step(capital,Arm::active);cd=fc.step(capital,Arm::control);assert(same_action(ad,cd)&&ad.task==Task::buy&&fa.capital.committedCost==fc.capital.committedCost);
 std::puts("C16 matching unchanged interception/attempt cap, live two-trace budget and own capital menu PASS");
}
static void run(){raw_gems_is_not_acquisition();strong_acquisition_control();conditional_geography();refused_and_personal();receipt_and_capital();same_hit_and_budget();std::puts("PASS C16 matched full-acquisition actual callers; native strength and robust progression denial unqualified");}

} // namespace matched_c16

static void reset_fixture_vm() {
 p10_mobile::default_cache={};
 p10_mobile::call_diagnostics={};
 p10::set_work_fuel_reader(nullptr);
}
int main() {
 reset_fixture_vm(); matched_c12::run();
 reset_fixture_vm(); matched_c15::run();
 reset_fixture_vm(); matched_c16::run();
 reset_fixture_vm();
 std::puts("PASS assembled C12/C15/C16 matched-control callers; retained assertions, no native qualification");
}
