#include "../investment-frontier/src/c-policy.h"
#include "../triadic-arena/src/c-policy.h"
#include "../conversion-race/src/c-policy.h"
#include "../paired-lifeline/src/c-policy.h"
#include "../realtime-guild/src/c-policy.h"
#include "../progression-interdiction/src/c-policy.h"
#include <cassert>
#include <iostream>
using namespace p10;
static_assert(!p10_c11::CPolicy::observe_ally_details);
static_assert(!p10_c12::CPolicy::observe_ally_details);
static_assert(!p10_c13::CPolicy::observe_ally_details);
static_assert(!p10_c14::CPolicy::observe_ally_details);
static_assert(!p10_c15::CPolicy::observe_ally_details);
static_assert(!p10_c16::CPolicy::observe_ally_details);
static_assert(p10_c11::CPolicy::observe_ally_detail_types==514);
static_assert(p10_c12::CPolicy::observe_ally_detail_types==0);
static_assert(p10_c13::CPolicy::observe_ally_detail_types==64);
static_assert(p10_c14::CPolicy::observe_ally_detail_types==6144);
static_assert(p10_c15::CPolicy::observe_ally_detail_types==0);
static_assert(p10_c16::CPolicy::observe_ally_detail_types==0);
static_assert(p10_c12::CPolicy::observe_enemy_exits&&p10_c16::CPolicy::observe_enemy_exits);

static Actor body(int id,Champion type,Point at={1,0}){Actor a;a.id=id;a.pick=id;a.type=type;a.position=at;a.visible=a.alive=a.ally=a.can_move=a.can_attack=a.can_cast=true;a.hp=a.max_hp=1000;a.strength=40;a.focus=100;a.range=1;a.strength_growth=10;a.health_growth=100;a.focus_growth=10;return a;}
static Group source(int id,Point at={2,0},int members=1){Group g;g.id=id;g.position=at;g.approach={at.x-1,at.y};g.observed=g.complete=g.route_known=g.living=true;g.travel=0;g.to_bank=g.to_spawn=2;g.clear_turns=3*members;g.gems=14;g.gold=40;g.respawn=40;
 for(int n=0;n<members;++n){Monster m;m.id=100*id+n;m.group=id;m.position={at.x+n,at.y};m.observed=m.alive=true;m.hp=m.max_hp=120;g.hp+=m.hp;g.monsters.push_back(m);g.spawns.push_back(m.position);}
 for(int x=at.x-1;x<=at.x+members;++x)for(int y=at.y-1;y<=at.y+1;++y){Point p{x,y};if(x<0||y<0||std::find(g.spawns.begin(),g.spawns.end(),p)!=g.spawns.end())continue;g.approach_pads.push_back(p);}return g;
}
static World frame(Champion type){World w;w.self=body(0,type);w.groups={source(10,{2,0},3),source(11,{2,2})};w.groups[1].gold=15;w.groups[1].gems=7;w.groups[1].monsters[0].hp=w.groups[1].hp=40;w.groups[1].clear_turns=1;w.altars.push_back({0,{0,1},{1,1},true,false,false,true,2,0});w.spawn={0,0};w.to_spawn=0;w.geometry_complete=w.observation_complete=w.opposition_complete=true;return w;}
static void post_emblems(Actor&a){a.monster_emblem=a.jungle_emblem=true;a.inventory[int(Item::monster_strength)]=5;a.inventory[int(Item::jungle_health)]=5;}
static void clear_source(Group&g){g.cleared=true;g.living=false;g.hp=0;for(auto&m:g.monsters){m.alive=false;m.hp=0;}}
// Caller-side purchase acknowledgement updates only actual paid inventory and
// growth. A fifth Power purchase supplies its sixth unit; no per-buy full heal.
static void acknowledge(World&w,const Directive&d){assert(d.task==Task::buy&&d.buy_count==1&&w.own_gold>=w.shard_cost&&!d.safety_verified);const int idx=int(d.item),old=w.self.inventory[idx];const bool power=d.item==Item::strength||d.item==Item::health||d.item==Item::focus;const int units=power?p10_c::power_units(old,1):1;
 ++w.self.inventory[idx];w.own_gold-=w.shard_cost;
 if(d.item==Item::strength||d.item==Item::monster_strength||d.item==Item::jungle_strength)w.self.strength+=units*w.self.strength_growth;
 if(d.item==Item::focus||d.item==Item::monster_focus||d.item==Item::jungle_focus)w.self.focus+=units*w.self.focus_growth;
 if(d.item==Item::health||d.item==Item::monster_health||d.item==Item::jungle_health){const double hp=units*w.self.health_growth;w.self.max_hp+=hp;w.self.hp+=hp;if(w.self.type==Champion::ryth)w.self.strength+=hp*.01;}
 if(w.self.inventory[idx]>=5&&(d.item==Item::monster_strength||d.item==Item::monster_health||d.item==Item::monster_focus))w.self.monster_emblem=true;
 if(w.self.inventory[idx]>=5&&(d.item==Item::jungle_health||d.item==Item::jungle_strength||d.item==Item::jungle_focus))w.self.jungle_emblem=true;
 ++w.round;w.receipts={{Receipt::Kind::purchase,w.round,-1,w.self.id,idx}};
}
template<class P>static void purchases(P&p,World&w,int count,Arm arm=Arm::active){for(int n=0;n<count;++n){auto d=p.step(w,arm);assert(d.task==Task::buy&&p.traces==0);acknowledge(w,d);}}
template<class P>static void receipt_first(Champion type){P p;auto w=frame(type);w.self.channel=Channel::tribute;w.receipts={{Receipt::Kind::payout,3,10,w.self.id,0,14,40}};w.round=8;auto d=p.step(w,Arm::active);assert(d.task==Task::wait&&p.economy.payouts==1&&p.traces==0);p.step(w,Arm::active);assert(p.economy.payouts==1);}
static void recurring_regions(){auto w=frame(Champion::ryth);post_emblems(w.self);w.self.at_spawn=true;w.own_gold=300;p10_c11::CPolicy p;purchases(p,w,10);assert(w.own_gold==0&&w.self.inventory[int(Item::strength)]==10&&w.self.strength==160&&p.capital.completed==1);
 auto pressure=frame(Champion::ryth);post_emblems(pressure.self);pressure.self.at_spawn=true;pressure.groups[0].incoming=200;pressure.own_gold=300;p10_c11::CPolicy mix;purchases(mix,pressure,10);assert(pressure.self.inventory[int(Item::strength)]==5&&pressure.self.inventory[int(Item::health)]==5&&pressure.self.max_hp==1600);
 auto poor=frame(Champion::stellar);post_emblems(poor.self);poor.self.at_spawn=true;poor.own_gold=0;poor.allies={body(3,Champion::ryth)};poor.allies[0].inventory[int(Item::strength)]=100;p10_c11::CPolicy q;assert(q.step(poor,Arm::active).task!=Task::buy&&!q.capital.active);
}
static void district_epochs(){auto w=frame(Champion::varg);post_emblems(w.self);w.self.at_spawn=true;w.own_gold=300;p10_c12::CPolicy a;purchases(a,w,10);assert(w.self.inventory[int(Item::strength)]==10&&a.capital.completed==1);
 auto ren=frame(Champion::ren);post_emblems(ren.self);ren.self.at_spawn=true;ren.own_gold=150;p10_c12::CPolicy c;purchases(c,ren,5,Arm::control);assert(ren.self.inventory[int(Item::focus)]==5);
 auto fight=frame(Champion::varg);post_emblems(fight.self);fight.self.at_spawn=true;fight.own_gold=150;fight.groups[0].monsters[0].awake=true;fight.groups[0].monsters[0].range=3;p10_c12::CPolicy x;x.source=x.district[1]=10;assert(x.step(fight,Arm::active).task!=Task::buy&&!x.capital.active&&x.traces<=2);
}
static void race_regimes(){auto w=frame(Champion::marcella);post_emblems(w.self);w.self.at_spawn=true;w.own_gold=150;assert(std::ceil(120./44)==std::ceil(120./55));p10_c13::CPolicy p;auto d=p.step(w,Arm::active);assert(d.task==Task::buy&&d.item==Item::strength&&p.projectedBranches>0&&p.traces==0);acknowledge(w,d);purchases(p,w,4);assert(w.self.strength==100);
 auto terminal=frame(Champion::marcella);post_emblems(terminal.self);terminal.self.at_spawn=true;terminal.own_gold=150;terminal.score=990;terminal.self.bag=10;p10_c13::CPolicy close;assert(close.step(terminal,Arm::active).task==Task::bank&&!close.capital.active);
 auto raw=frame(Champion::marcella);post_emblems(raw.self);raw.self.at_spawn=true;raw.own_gold=150;raw.groups[0].gems=2000;p10_c13::CPolicy not_a_win;assert(not_a_win.step(raw,Arm::active).task==Task::buy); // raw gems never prove terminal executability
}
static World linked(){auto w=frame(Champion::issac);w.self.inventory[int(Item::jungle_health)]=5;w.self.jungle_emblem=true;auto c=body(1,Champion::cassi,{1,1});c.inventory[int(Item::monster_focus)]=5;c.monster_emblem=true;c.inventory[int(Item::focus)]=5;w.allies={c};return w;}
static void financier_custody(){auto w=linked();w.self.at_spawn=true;w.own_gold=300;p10_c14::CPolicy p;purchases(p,w,5);assert(p.capitalRecipient==pair::Recipient::issac&&w.self.inventory[int(Item::health)]==5);w.allies[0].inventory[int(Item::focus)]=10;purchases(p,w,5);assert(w.self.inventory[int(Item::strength)]==5&&w.own_gold==0);
 auto startup=linked();startup.self.inventory[int(Item::jungle_health)]=0;startup.self.jungle_emblem=false;startup.self.at_spawn=true;startup.own_gold=150;p10_c14::CPolicy prerequisite;purchases(prerequisite,startup,5);assert(startup.self.jungle_emblem&&startup.self.inventory[int(Item::jungle_health)]==5);
 auto oneBody=linked();oneBody.self=oneBody.allies[0];oneBody.self.id=0;oneBody.self.at_spawn=true;oneBody.allies[0]=body(1,Champion::issac,{1,1});oneBody.own_gold=300;p10_c14::CPolicy control;purchases(control,oneBody,10,Arm::control);assert(oneBody.self.inventory[int(Item::focus)]==15); // Issac's missing recipe cannot freeze control growth
 auto custody=linked();custody.self.type=Champion::cassi;custody.self.inventory=custody.allies[0].inventory;custody.self.monster_emblem=true;custody.self.position={2,0};custody.allies[0]=body(1,Champion::issac,{3,0});custody.allies[0].inventory[int(Item::jungle_health)]=5;custody.allies[0].jungle_emblem=true;clear_source(custody.groups[0]);custody.groups[0].spawns.push_back({3,0});custody.capture_active=true;p10_c14::CPolicy release;auto d=release.step(custody,Arm::active);assert(d.task==Task::release&&release.recipient==pair::Recipient::issac);
 auto poor=linked();poor.self.at_spawn=true;poor.own_gold=0;p10_c14::CPolicy no_pool;assert(no_pool.step(poor,Arm::active).task!=Task::buy&&!no_pool.capital.active);
 auto service=linked();service.self.bag=14;service.allies[0].bag=2;p10_c14::CPolicy ordered;d=ordered.step(service,Arm::active);assert(d.task==Task::bank&&ordered.committedService&&ordered.order==pair::ServiceOrder::issac_first);
 auto independent=frame(Champion::tavi);post_emblems(independent.self);independent.own_gold=300;p10_c14::CPolicy outside;purchases(outside,independent,10);assert(independent.self.inventory[int(Item::strength)]==10);
}
static void productive_maintenance(){auto w=frame(Champion::varg);post_emblems(w.self);w.self.at_spawn=true;w.own_gold=300;p10_b15::BPolicy base;auto old=base.step(w,Arm::active);assert(old.safety_verified&&old.task!=Task::buy);p10_c15::CPolicy p;purchases(p,w,10);assert(w.self.inventory[int(Item::strength)]==10&&p.branchSavings>=0&&p.compiles==0);
 auto caster=frame(Champion::stellar);post_emblems(caster.self);caster.self.at_spawn=true;caster.self.strength=200;caster.self.focus=40;caster.own_gold=150;caster.groups={source(15)};caster.groups[0].hp=caster.groups[0].monsters[0].hp=80;auto&a=caster.self.abilities[0];a.targets_camps=a.damage=true;a.modifier=1;p10_c15::CPolicy compile;auto d=compile.step(caster,Arm::active);assert(d.task==Task::buy&&d.item==Item::focus&&compile.newSkills>0);
}
static void closed_border_capital(){auto w=frame(Champion::marcella);post_emblems(w.self);w.self.at_spawn=true;w.own_gold=300;p10_c16::CPolicy p;p.replacementOwned=true;p.acquiredRound=5;p.source=10;auto d=p.step(w,Arm::active);assert(p.deferredReplacement&&d.task!=Task::buy&&!p.capital.active&&p.traces<=2);
 p.replacementOwned=false;w.self.bag=14;w.round=1;d=p.step(w,Arm::active);assert(d.task==Task::bank&&p.deferredReplacement&&!p.capital.active);w.self.bag=0;
 w.round=20;w.receipts={{Receipt::Kind::bank,20,-1,w.self.id,0,14,0}};purchases(p,w,10);assert(w.self.inventory[int(Item::strength)]==10&&w.own_gold==0&&p.economy.banks==1);
 auto crew=frame(Champion::ren);post_emblems(crew.self);crew.self.at_spawn=true;crew.own_gold=150;auto e=body(9,Champion::prism,{2,1});e.ally=false;e.bag=14;e.hp=10;e.strength=0;crew.enemies={e};p10_c16::CPolicy attempt;attempt.attempts.begin(crew.round,e.id);d=attempt.step(crew,Arm::active);assert(d.task!=Task::buy&&!attempt.capital.active&&d.actor==e.id);
 auto ren=frame(Champion::ren);post_emblems(ren.self);ren.self.at_spawn=true;ren.own_gold=150;p10_c16::CPolicy common;purchases(common,ren,5,Arm::control);assert(ren.self.inventory[int(Item::focus)]==5);
}
template<class P>static void retained_work(Champion type){auto w=frame(type);P p;auto d=p.step(w,Arm::active);assert(p.traces<=2&&d.task!=Task::buy);if(d.safety_verified)assert(fresh_certificate(w,d));}
static int low_fuel(){return 1000;}
static void bounded_fuel(){set_work_fuel_reader(low_fuel);auto w=frame(Champion::varg);post_emblems(w.self);w.self.at_spawn=true;w.own_gold=150;p10_c15::CPolicy p;auto d=p.step(w,Arm::active);assert(d.task!=Task::buy&&p.traces==0&&!p.capital.active);set_work_fuel_reader(nullptr);}
int main(){receipt_first<p10_c11::CPolicy>(Champion::ryth);receipt_first<p10_c12::CPolicy>(Champion::ren);receipt_first<p10_c13::CPolicy>(Champion::marcella);receipt_first<p10_c14::CPolicy>(Champion::cassi);receipt_first<p10_c15::CPolicy>(Champion::varg);receipt_first<p10_c16::CPolicy>(Champion::marcella);recurring_regions();district_epochs();race_regimes();financier_custody();productive_maintenance();closed_border_capital();retained_work<p10_c11::CPolicy>(Champion::ryth);retained_work<p10_c12::CPolicy>(Champion::varg);retained_work<p10_c13::CPolicy>(Champion::marcella);retained_work<p10_c14::CPolicy>(Champion::tavi);retained_work<p10_c15::CPolicy>(Champion::varg);retained_work<p10_c16::CPolicy>(Champion::marcella);bounded_fuel();std::cout<<"PASS C11-16 actual recurring-capital callers; no native strength or fuel qualification\n";}
