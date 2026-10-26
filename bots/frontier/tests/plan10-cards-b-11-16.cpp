#include "../investment-frontier/src/b-policy.h"
#include "../triadic-arena/src/b-policy.h"
#include "../conversion-race/src/b-policy.h"
#include "../paired-lifeline/src/b-policy.h"
#include "../realtime-guild/src/b-policy.h"
#include "../progression-interdiction/src/b-policy.h"
#include <cassert>
#include <iostream>
using namespace p10;
static Actor body(int id,Champion type,Point at={1,0}){Actor a;a.id=id;a.pick=id;a.type=type;a.position=at;a.visible=a.alive=a.ally=a.can_move=a.can_attack=a.can_cast=true;a.hp=a.max_hp=1000;a.strength=40;a.focus=100;a.range=1;a.strength_growth=10;a.health_growth=100;a.focus_growth=10;return a;}
static Group source(int id,Point at={2,0},int members=1){Group g;g.id=id;g.position=at;g.approach={at.x-1,at.y};g.observed=g.complete=g.route_known=g.living=true;g.travel=0;g.to_bank=2;g.to_spawn=2;g.clear_turns=3*members;g.gems=14;g.gold=40;g.respawn=40;
 for(int n=0;n<members;++n){Monster m;m.id=100*id+n;m.group=id;m.position={at.x+n,at.y};m.observed=m.alive=true;m.hp=m.max_hp=120;m.range=0;g.hp+=m.hp;g.monsters.push_back(m);g.spawns.push_back(m.position);}
 for(int x=at.x-1;x<=at.x+members;++x)for(int y=at.y-1;y<=at.y+1;++y){Point p{x,y};if(x<0||y<0||std::find(g.spawns.begin(),g.spawns.end(),p)!=g.spawns.end())continue;g.approach_pads.push_back(p);}return g;
}
static World frame(Champion type){World w;w.self=body(0,type);w.groups={source(10,{2,0},3),source(11,{2,2})};w.groups[1].gold=15;w.groups[1].gems=7;w.groups[1].monsters[0].hp=w.groups[1].hp=40;w.groups[1].clear_turns=1;w.altars.push_back({0,{0,1},{1,1},true,false,false,true,2,0});w.spawn={0,0};w.to_spawn=0;w.geometry_complete=w.observation_complete=w.opposition_complete=true;return w;}
static void clear_source(Group&g){g.cleared=true;g.living=false;g.hp=0;for(auto&m:g.monsters){m.alive=false;m.hp=0;}}
template<class P>static void receipt_before_channel(Champion type){P p;auto w=frame(type);w.self.channel=Channel::tribute;w.own_payout=true;w.last_payout_group=10;w.last_payout_round=3;w.receipts={{Receipt::Kind::payout,3,10,w.self.id,0,14,40}};w.round=8;auto d=p.step(w,Arm::active);assert(d.task==Task::wait&&p.economy.payouts==1&&p.traces==0);p.step(w,Arm::active);assert(p.economy.payouts==1);}
static void mobile_first_income(){
 {auto w=frame(Champion::ryth);p10_b11::BPolicy p;auto d=p.step(w,Arm::active);assert(p.traces<=2&&d.safety_verified&&d.valid_round==w.round);}
 {auto w=frame(Champion::marcella);p10_b13::BPolicy p;auto d=p.step(w,Arm::active);assert(p.traces<=2&&d.safety_verified);clear_source(*const_cast<Group*>(w.group(p.source)));w.round=1;d=p.step(w,Arm::active);assert(d.task==Task::farm);}
 {auto w=frame(Champion::varg);p10_b15::BPolicy p;auto d=p.step(w,Arm::active);assert(p.traces<=2&&d.safety_verified&&p.source>=0);}
 {auto w=frame(Champion::marcella);p10_b16::BPolicy p;auto d=p.step(w,Arm::active);assert(p.traces<=2&&d.safety_verified);clear_source(*const_cast<Group*>(w.group(p.source)));w.round=1;d=p.step(w,Arm::active);assert(d.task==Task::farm);}
}
static void owned_frontier(){
 auto w=frame(Champion::ryth);w.self.hp=w.self.max_hp=500;w.own_gold=150;w.allies={body(2,Champion::ren,{1,1})};w.groups[0]=source(20);auto&g=w.groups[0];g.gold=250;g.hp=g.monsters[0].hp=400;g.monsters[0].strength=g.incoming=80;g.monsters[0].range=3;g.clear_turns=10;
 p10_b11::BPolicy p;auto d=p.step(w,Arm::active);assert(p.traces==2&&p.order.feasible&&!investment::common_bundle(p.order.itemCounts));assert(d.task==Task::recover&&!d.safety_verified);
 w.round=1;w.self.at_spawn=true;d=p.step(w,Arm::active);assert(d.task==Task::buy&&w.self.strength==40&&p.traces==0);
 auto poor=w;poor.self.at_spawn=false;poor.own_gold=0;p10_b11::BPolicy unfunded;d=unfunded.step(poor,Arm::active);assert(d.task!=Task::buy&&!unfunded.order.feasible&&unfunded.traces<=2);
}
static void late_arena(){
 auto w=frame(Champion::varg);w.self.strength=10;w.groups[0]=source(30,{3,0});auto&camp=w.groups[0];camp.hp=camp.monsters[0].hp=20;camp.monsters[0].awake=true;camp.monsters[0].strength=40;camp.monsters[0].range=1;camp.incoming=40;camp.to_bank=20;camp.clear_turns=2;
 auto&home=w.groups[1];home.to_bank=1;home.monsters[0].hp=home.hp=40;home.clear_turns=1;w.visible_open_tiles={{1,1},{2,0},{2,1},{3,1},{3,2},{1,0},{1,2}};auto r=body(9,Champion::prism,{2,1});r.ally=false;r.hp=r.max_hp=50;r.strength=0;w.enemies={r};
 p10_b12::BPolicy p;auto d=p.step(w,Arm::active);assert(p.traces<=2);assert(p.last.program==arena::Program::burst_rival);assert(p.last.admitted&&d.task==Task::attack&&d.actor==r.id&&!d.safety_verified);
 auto off=w;off.groups[0].monsters[0].awake=false;p10_b12::BPolicy ordinary;d=ordinary.step(off,Arm::active);assert(ordinary.encounters==0&&d.task!=Task::attack);
 w.round=1;w.enemies.clear();d=p.step(w,Arm::active);assert(p.traces<=2&&d.safety_verified&&d.group==camp.id);
}
static void distinct_race(){
 auto w=frame(Champion::marcella);w.score=990;auto&a=w.groups[0];a.gems=60;a.respawn=20;a.to_bank=5;auto&b=w.groups[1];b.gems=10;b.respawn=100;b.to_bank=1;
 p10_b13::BPolicy p;auto d=p.step(w,Arm::active);assert(p.traces<=2&&p.route[0]==a.id&&p.route[3]==b.id&&p.selected==3&&d.group==b.id&&d.safety_verified);
 w.self.bag=10;w.round=1;d=p.step(w,Arm::active);assert(d.task==Task::bank);
 assert(p.campaigns[0].count==2&&p.campaigns[0].repeatFrom==1&&p.campaigns[0].events[1].bag==a.gems);
 auto funded=frame(Champion::tavi);funded.own_gold=30;funded.self.strength_growth=200;funded.groups[0].monsters[0].hp=funded.groups[0].hp=600;funded.groups[0].monsters.resize(1);funded.groups[0].spawns.resize(1);funded.groups[0].gems=80;funded.groups[0].clear_turns=15;p10_b13::BPolicy dev;d=dev.step(funded,Arm::active);assert(dev.traces<=2);if(dev.selected==1)assert(d.task==Task::buy&&d.item==Item::monster_strength&&!d.safety_verified);
}
static void linked_bound_and_custody(){
 auto w=frame(Champion::issac);w.allies={body(1,Champion::cassi,{1,1})};w.allies[0].range=3;w.allies[0].hp=w.allies[0].max_hp=600;p10_b14::BPolicy p;auto d=p.step(w,Arm::active);assert(p.traces<=2&&p.ownWitness&&p.partnerWitness&&d.safety_verified);
 auto lethal=w;lethal.groups[0].monsters[0].strength=100;lethal.groups[0].monsters[0].range=3;lethal.groups[0].incoming=100;lethal.allies[0].hp=30;p10_b14::BPolicy no;d=no.step(lethal,Arm::active);assert(!d.safety_verified&&no.traces<=2);
 auto custody=w;custody.groups[0].monsters[0].max_hp=2400;custody.groups[0].monsters[0].strength=60;custody.groups[0].monsters[0].range=4;clear_source(custody.groups[0]);custody.self.type=Champion::cassi;custody.self.position={2,0};custody.allies[0]=body(1,Champion::issac,{3,0});custody.groups[0].spawns.push_back({3,0});custody.visible_open_tiles.clear();p10_b14::BPolicy hold;d=hold.step(custody,Arm::active);assert(hold.recipient==pair::Recipient::issac&&d.task==Task::release);
 auto service=frame(Champion::issac);service.self.bag=40;auto c=body(1,Champion::cassi,{1,1});c.hp=100;c.max_hp=600;c.bag=2;service.allies={c};p10_b14::BPolicy ordered;ordered.committedService=true;ordered.serviceStart=0;ordered.order=pair::ServiceOrder::issac_first;d=ordered.step(service,Arm::active);assert(d.task==Task::bank&&ordered.order==pair::ServiceOrder::issac_first);
}
static void compiled_vocabulary(){
 auto w=frame(Champion::varg);w.groups={source(60,{2,0},2)};for(auto&m:w.groups[0].monsters)m.hp=60;w.groups[0].hp=120;w.groups[0].monsters[1].position={2,1};w.groups[0].spawns[1]={2,1};w.self.abilities[0].ready=w.self.abilities[0].reset=true;w.self.abilities[0].targets_self=true;w.self.abilities[0].max_cooldown=8;
 p10_b15::BPolicy p;auto d=p.step(w,Arm::active);assert(p.guardHits==1&&p.traces==1&&d.safety_verified&&d.task==Task::attack&&d.cast_slot==0&&d.second_monster>=0);
 const auto active=d;auto control=w;p10_b15::BPolicy c;d=c.step(control,Arm::control);
 assert(c.guardHits==1&&c.traces==p.traces&&d.task==active.task&&d.monster==active.monster&&d.second_monster==active.second_monster&&d.cast_slot==active.cast_slot&&fresh_certificate(control,d));
 ++w.round;++control.round;auto reused=p.step(w,Arm::active);auto rebuilt=c.step(control,Arm::control);
 assert(p.compiledReuses==1&&c.onlineRebuilds==2&&p.compiles==1&&c.compiles==2&&reused.task==rebuilt.task&&reused.cast_slot==rebuilt.cast_slot);
 auto unknown=w;auto hazard=source(62,{1,1});hazard.complete=hazard.observed=false;hazard.monsters[0].observed=false;hazard.monsters[0].hp=0;hazard.monsters[0].range=3;hazard.monsters[0].strength=500;unknown.groups.push_back(hazard);p10_b15::BPolicy unsafe;d=unsafe.step(unknown,Arm::active);assert(!d.safety_verified&&unsafe.guardHits==0);
 auto mobile=frame(Champion::varg);p10_b15::BPolicy m;d=m.step(mobile,Arm::active);const int old=m.source;mobile.round=1;mobile.receipts={{Receipt::Kind::blocked,1,old,mobile.self.id}};d=m.step(mobile,Arm::active);assert(m.traces<=2&&m.blockedSource==old&&m.source!=old);
}
static void replacement_schedules(){
 auto w=frame(Champion::marcella);w.groups[0]=source(70,{2,0});w.groups[0].gems=20;w.groups[0].clear_turns=3;w.groups[0].respawn=40;w.groups[1]=source(71,{2,2});w.groups[1].monsters[0].hp=w.groups[1].hp=40;w.groups[1].clear_turns=1;w.groups[1].gems=14;w.groups[1].respawn=100;
 auto enemy=body(9,Champion::prism,{7,2});enemy.ally=false;enemy.hp=100;enemy.bag=40;w.enemies={enemy};w.allies={body(2,Champion::ren,{6,2})};p10_b16::BPolicy p;auto d=p.step(w,Arm::active);assert(p.traces<=2&&p.schedules[0].feasible&&p.schedules[1].feasible&&p.schedules[2].feasible&&p.schedules[3].feasible);assert(p.schedules[2].failedHitSafe&&p.schedules[2].unknownSubstitutes&&p.schedules[2].adverseReentry==0);
 if(p.replacementOwned){w.round=1;w.enemies.clear();d=p.step(w,Arm::active);assert(p.traces<=2&&d.group==p.source&&d.safety_verified);}
 auto crew=frame(Champion::ren);crew.self.hp=1000;enemy.position={2,1};enemy.hp=30;enemy.strength=0;crew.enemies={enemy};p10_b16::BPolicy raider;d=raider.step(crew,Arm::active);assert(raider.attempts.used==1&&d.actor==enemy.id);crew.round=8;crew.enemies.clear();raider.step(crew,Arm::active);assert(raider.attempts.used==1&&!raider.attempts.active);
}
static int low_fuel(){return 1000;}
static void low_budget(){p10::set_work_fuel_reader(low_fuel);auto w=frame(Champion::marcella);p10_b11::BPolicy a;p10_b12::BPolicy b;p10_b13::BPolicy c;p10_b14::BPolicy d;p10_b15::BPolicy e;p10_b16::BPolicy f;for(auto*unused:{&w}){(void)unused;}a.step(w,Arm::active);b.step(w,Arm::active);c.step(w,Arm::active);d.step(w,Arm::active);e.step(w,Arm::active);f.step(w,Arm::active);assert(a.traces==0&&b.traces==0&&c.traces==0&&d.traces==0&&e.traces==0&&f.traces==0);p10::set_work_fuel_reader(nullptr);}
int main(){receipt_before_channel<p10_b11::BPolicy>(Champion::ryth);receipt_before_channel<p10_b12::BPolicy>(Champion::ren);receipt_before_channel<p10_b13::BPolicy>(Champion::marcella);receipt_before_channel<p10_b14::BPolicy>(Champion::cassi);receipt_before_channel<p10_b15::BPolicy>(Champion::varg);receipt_before_channel<p10_b16::BPolicy>(Champion::marcella);mobile_first_income();owned_frontier();late_arena();distinct_race();linked_bound_and_custody();compiled_vocabulary();replacement_schedules();low_budget();std::cout<<"PASS scratch B11-16 distinct organization caller fixtures; no native qualification\n";return 0;}
