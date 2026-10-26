#include "../capital/src/capital.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
using namespace plan4;namespace c=plan4::capital;
int checks=0;void check(bool value,const char*message){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
bool near(float a,float b){return std::abs(a-b)<.001f;}
c::Definitions definitions(){c::Definitions d;d.public_source=123;d.quote_law=77;d.queried_public=true;d.health_growth=100;d.strength_growth=5;d.focus_growth=10;
 d.item[0]={30,5,0,1,0,0,5,0,.1f,true};d.item[1]={30,5,1,0,0,5,0,0,.3f,true};d.item[2]={30,5,0,1,0,0,6,0,0,true};return d;}
c::Stats base(){return {1000,50,100,1,1};}
c::ShopAccess spawn(){return {true,false,false,true};}c::ShopAccess field(){return {false,false,true,true};}
c::Step phase(c::Kind kind,int rounds=0,int group=-1,Point target={}){c::Step s;s.kind=kind;s.rounds=rounds;s.group=group;s.target=target;s.legal_quote=true;return s;}
c::Stock stock(int id,Point p,unsigned gems,unsigned gold,float hp=1350,float incoming=20){c::Stock s;s.id=id;s.spawn=p;s.lifecycle=Lifecycle::living;s.current_health=s.full_health=hp;s.unmodified_incoming=incoming;s.gems=gems;s.gold=gold;s.respawn_rounds=200;s.assured=s.occupancy_known=true;return s;}
bool travel(c::Path&p,Point target,int rounds,const c::Definitions&d){return c::step(p,phase(c::Kind::travel,rounds,-1,target),d);}
bool bank(c::Path&p,const c::Definitions&d){return c::step(p,phase(c::Kind::bank,tribute_ticks(p.bag,Settings{})),d);}
int main(){auto d=definitions();c::Equipment eq;auto q=c::public_quote(base(),eq,c::Family::power_strength,5,spawn(),d,nullptr);
 check(q.supported&&q.cost==150&&q.after.strength==80,"power combination replaces ingredients with six points and debits only five shards");
 q=c::public_quote(base(),eq,c::Family::monster_strength,10,spawn(),d,nullptr);check(near(q.after.camp_outgoing,1.1f)&&q.after.strength==100,"duplicate emblem points stack while passive applies once");
 eq.total_shards[0]=4;auto four=base();four.strength=70;q=c::public_quote(four,eq,c::Family::monster_strength,1,spawn(),d,nullptr);
 check(q.cost==30&&q.after.strength==75&&near(q.after.camp_outgoing,1.1f),"fifth ingredient has no duplicate emblem gold or stat charge");eq={};
 q=c::public_quote(base(),eq,c::Family::jungle_health,5,field(),d,nullptr);check(q.after.maximum_health==1500&&q.spawn_healing==0,"Tavi field Health raises capacity without healing");
 q=c::public_quote(base(),eq,c::Family::jungle_health,5,spawn(),d,nullptr);check(q.spawn_healing==500,"spawn health purchase healing equals queried growth gain");
 c::ShopAccess dead{false,true,false,true};q=c::public_quote(base(),eq,c::Family::jungle_health,5,dead,d,nullptr);check(q.supported&&q.spawn_healing==0,"dead purchase adds capacity without fabricated living health");
 check(!c::public_quote(base(),eq,c::Family::monster_strength,1,{false,false,false,true},d,nullptr).supported,"non-Tavi field shopping rejected");
 c::Stats boundary=base();boundary.strength=75;boundary.camp_outgoing=1.1f;
 check(c::conservative_combat(boundary,1000,954.112548828125f,0).rounds==9,"combat quote preserves common float-division ceil boundary instead of inventing tenth clear round");
 auto active=std::make_unique<c::Path>(),control=std::make_unique<c::Path>();
 check(c::begin(*active,10,128,{0,0},300,299,0,base(),eq,d,true),"synthetic conditional path accepts public definitions");
 check(!c::production_supported(*active),"synthetic definitions never claim actual purchase receipt parity or production support");
 check(c::shop(*active,spawn(),d),"fixed inherited purchase event executes");
 check(active->equipment.total_shards[0]==5&&active->equipment.total_shards[1]==4&&active->equipment.total_shards[2]==0&&active->cash==29,"affordability follows fixed priority without skipping health for power");
 check(active->purchase_count==2&&active->purchases[0].cash_before==299&&active->purchases[0].cash_after==149&&active->purchases[1].cash_after==29,"purchase cash is debited exactly once in chronological receipts");
 c::begin(*active,10,128,{0,0},300,300,0,base(),eq,d,true);c::begin(*control,10,128,{0,0},300,300,0,base(),eq,d,false);
 c::shop(*active,spawn(),d);c::shop(*control,spawn(),d);
 check(active->cash==control->cash&&active->equipment.total_shards==control->equipment.total_shards,"disabled arm forecasts identical purchases and cash");
 check(active->accounting.maximum_health==control->accounting.maximum_health&&control->effective.maximum_health==1000&&active->effective.maximum_health==1500,"disabled cuts only purchase-to-effective-combat edge");
 check(active->health==800&&control->health==300,"health gain influences future combat only in active forecast");
 c::begin(*active,0,128,{0,0},1000,0,0,base(),eq,d,true);c::begin(*control,0,128,{0,0},1000,0,0,base(),eq,d,false);
 auto first=stock(10,{1,0},10,150);first.lifecycle=Lifecycle::cleared;first.current_health=0;
 auto second=stock(20,{2,0},20,150);auto third=stock(30,{4,0},44,0,2700,40);
 for(auto*p:{active.get(),control.get()}){check(c::add_stock(*p,first)&&c::add_stock(*p,second)&&c::add_stock(*p,third),"same assured stocks initialize both arms");check(c::begin_macro(*p),"first fixed macro starts");travel(*p,{1,0},1,d);check(c::step(*p,phase(c::Kind::capture,6,0),d),"first known capture earns its real own cash and bag");check(p->secured==0,"capture never becomes secured score before bank");const auto oldBag=p->bag;check(!c::step(*p,phase(c::Kind::capture,6,0),d)&&p->bag==oldBag,"captured stock cannot pay twice");travel(*p,{3,0},2,d);check(bank(*p,d)&&p->secured==10,"first conversion completes before equipment funding is used");travel(*p,{0,0},3,d);check(c::shop(*p,spawn(),d)&&p->cash==0,"known gold funds inherited Monster purchase at legal shop event");}
 const auto qa=c::conservative_combat(active->effective,active->health,1350,20),qc=c::conservative_combat(control->effective,control->health,1350,20);
 check(qa.rounds<qc.rounds&&qa.loss<qc.loss,"purchase changes subsequent clear duration and propagated incoming loss");
 for(auto*p:{active.get(),control.get()}){c::begin_macro(*p);travel(*p,{2,0},2,d);check(c::step(*p,phase(c::Kind::clear,0,1),d),"second clear admits under frozen effective stats");c::step(*p,phase(c::Kind::capture,6,1),d);travel(*p,{3,0},1,d);bank(*p,d);const auto hp=p->health;check(c::shop(*p,field(),d)&&p->cash==0,"second known gold funds inherited Jungle Health in Tavi field");check(p->health==hp,"field buy gives neither arm instant healing");travel(*p,{0,0},3,d);auto restore=phase(c::Kind::restore,1);restore.shop=spawn();check(c::step(*p,restore,d),"future legal spawn restoration uses effective capacity");c::begin_macro(*p);travel(*p,{4,0},4,d);}
 check(c::step(*active,phase(c::Kind::clear,0,2),d)&&!c::step(*control,phase(c::Kind::clear,0,2),d),"fixed investment permits later safe work rejected by cut-edge control");
 check(c::step(*active,phase(c::Kind::capture,6,2),d),"active later group fully captured");travel(*active,{3,0},1,d);check(bank(*active,d)&&active->secured==74&&control->secured==30,"only completed later conversion contributes causal output difference within128");
 check(active->cash==0&&control->cash==0&&active->purchase_count==control->purchase_count,"same purchase schedule has no speculative gold-to-gem bonus");
 c::begin(*active,0,2,{0,0},1000,0,20,base(),eq,d,true);check(!c::step(*active,phase(c::Kind::bank,3),d)&&active->secured==0&&active->bag==20,"beyond-horizon tribute gives no fractional score or refund");
 c::begin(*active,0,128,{0,0},1000,0,0,base(),eq,d,true);auto unknown=first;unknown.assured=false;c::add_stock(*active,unknown);travel(*active,{1,0},1,d);check(!c::step(*active,phase(c::Kind::capture,6,0),d),"unknown stock cannot finance a purchase");
 check(!c::add_stock(*active,unknown),"same resource ID cannot create independent stock copy");
 auto changed=d;changed.item[0].cost=0;check(!c::shop(*active,spawn(),changed),"malformed or changed quote definitions cannot divide by zero");
 c::begin(*active,0,128,{0,0},0,150,0,base(),eq,d,true,false);check(c::shop(*active,dead,d)&&active->health==0,"dead path can shop without becoming alive or healed");
 check(!c::shop(*active,spawn(),d),"shop location life flags must match physical path");
 eq.total_shards={5,5,0};c::begin(*active,0,128,{0,0},1000,9000,0,base(),eq,d,true);
 check(c::shop(*active,spawn(),d)&&active->equipment.total_shards[2]==300&&active->purchase_count==3&&active->cash==0,
       "inherited three attempts can each buy the100 Power cap after both earlier stages finish");eq={};
 c::begin(*active,0,128,{1,0},1000,0,0,base(),eq,d,true);auto shared=first;shared.gems=5;shared.gold=91;shared.eligible_allied_occupants=2;shared.own_is_lowest_occupant=false;c::add_stock(*active,shared);
 check(c::step(*active,phase(c::Kind::capture,6,0),d)&&active->cash==45&&active->bag==2,"only actor-local split reward finances later purchases, never team gold");
 c::begin(*active,0,128,{1,0},1000,0,0,base(),eq,d,true);shared.occupancy_known=false;c::add_stock(*active,shared);check(!c::step(*active,phase(c::Kind::capture,6,0),d)&&active->cash==0,"unknown capture occupants cannot supply assured investment cash");
 c::begin(*active,0,128,{0,0},1000,0,0,base(),eq,d,true);for(int n=0;n<8;n++)check(c::begin_macro(*active),"bounded macro slot available");check(!c::begin_macro(*active),"ninth macro refused");
 std::printf("plan4-capital: %d checks passed\n",checks);
}
