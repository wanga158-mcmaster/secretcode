#if P10_C01_READY
#include "../catalyst-forge/src/c-policy.h"
#endif
#include "../shield-foundry/src/c-policy.h"
#include "../two-spindle/src/c-policy.h"
#include "../metered-works/src/c-policy.h"
#include "../orchards/src/c-policy.h"
#define P10_OBSERVATION_PURE_TEST
#include "../common/src/sdk.h"
#ifndef P10_C01_READY
#define P10_C01_READY 0
#endif
#include <cassert>
#include <cstdio>
using namespace p10;
static World base(Champion type=Champion::varg){World w;w.round=10;w.max_rounds=400;w.spawn={0,0};w.self.id=1;w.self.pick=0;w.self.type=type;w.self.position={1,1};w.self.visible=w.self.alive=w.self.ally=true;w.self.can_move=w.self.can_attack=w.self.can_cast=true;w.self.hp=w.self.max_hp=1000;w.self.strength=70;w.self.focus=100;w.self.range=1;w.action_reserve=40000;return w;}
static Monster mob(int id,int group,Point p,double hp,double damage=0,int range=0){Monster m;m.id=id;m.group=group;m.position=p;m.hp=m.max_hp=hp;m.strength=damage;m.range=range;m.observed=m.alive=true;return m;}
static Group source(int id,std::vector<Monster> monsters,std::vector<Point>pads,int gems=30){Group g;g.id=id;g.monsters=std::move(monsters);g.approach_pads=std::move(pads);g.position=g.monsters[0].position;g.approach=g.approach_pads[0];g.observed=g.complete=g.route_known=g.living=g.program_available=true;g.travel=0;g.to_spawn=g.to_bank=1;g.gems=gems;g.respawn=20;for(const auto&m:g.monsters){g.spawns.push_back(m.position);g.hp+=m.hp;g.incoming+=m.strength;}return g;}
static Actor ally(const World&w,int id,Champion type,Point p,int pick=1){auto a=w.self;a.id=id;a.pick=pick;a.position=p;a.type=type;return a;}
static void clear(Group&g){g.cleared=true;g.living=false;g.hp=0;for(auto&m:g.monsters){m.alive=false;m.hp=0;}}
static void reset(){p10_mobile::default_cache={};}
static void budget_ok(int traces){assert(traces>=0&&traces<=2);}
#if P10_C01_READY
static void forge_callers(){
 reset();auto w=base();w.self.strength=40;auto&r=w.self.abilities[0];r.ready=r.reset=r.targets_self=true;r.max_cooldown=8;
 w.groups={source(10,{mob(100,10,{2,1},160,20,3),mob(101,10,{3,1},110),mob(102,10,{2,2},110),mob(103,10,{3,2},110)},{{1,1},{1,2}},60),source(11,{mob(110,11,{0,2},90)},{{0,1},{1,1}},20)};w.visible_open_tiles={{1,1},{1,2},{0,1}};auto ven=ally(w,2,Champion::vendetta,{1,2});ven.strength=70;w.allies={ven};
#if P10_C01_READY
 p10_c_forge::CPolicy p;auto d=p.step(w,Arm::active);assert(d.template_id==1011&&p.preparation_events==1&&d.group==10);assert(d.task==Task::attack&&d.cast_slot==-1&&d.reserve_reset&&fresh_certificate(w,d));budget_ok(p.fresh_traces);assert(p.fresh_traces==1);
#endif
#if P10_C01_READY
 p10_c_forge::CPolicy control;auto c=control.step(w,Arm::control);assert(c.template_id==1012&&control.preparation_events==0);budget_ok(control.fresh_traces);
#endif
#if P10_C01_READY
 auto sw=w;sw.self.type=Champion::stellar;sw.self.range=3;sw.self.abilities={};sw.allies.clear();p10_c_forge::CPolicy stellar;auto independent=stellar.step(sw,Arm::active);assert(independent.group==11&&independent.template_id==1013&&fresh_certificate(sw,independent));
#endif
 // A non-reaching remote Ven never creates a preparation claim.
#if P10_C01_READY
 w.allies[0].position={9,9};p10_c_forge::CPolicy absent;auto no=absent.step(w,Arm::active);assert(no.template_id!=1011&&absent.preparation_events==0);
#endif
 // Economic pulses are consumed before a channel return.
#if P10_C01_READY
 auto paid=w;paid.own_payout=true;paid.last_payout_round=7;paid.self.channel=Channel::tribute;p10_c_forge::CPolicy income;income.step(paid,Arm::active);assert(income.funded&&income.last_receipt==7);
#endif
}
#endif

static World carousel(){auto w=base();w.self.position={1,0};w.self.shield=10;w.self.shield_until=w.round+1;w.visible_open_tiles={{1,0},{2,0},{1,1},{1,2},{2,1}};auto a=source(20,{mob(200,20,{0,0},210,20,3)},{{1,0},{1,1},{2,0}},40);a.monsters[0].awake=true;auto b=source(21,{mob(210,21,{3,2},70)},{{2,1},{2,0}},20);w.groups={a,b};auto heart=ally(w,2,Champion::heart,{1,2});heart.shield=500;heart.shield_until=w.round+4;w.allies={heart};return w;}
static void foundry_callers(){
 reset();auto w=carousel();p10_c_foundry::CPolicy p;p.cells={20,21};p.source=20;auto d=p.step(w,Arm::active);assert(d.template_id==2023&&d.task==Task::move&&d.destination==Point(2,0)&&p.transfers==1&&p.source==21);assert(fresh_certificate(w,d)&&d.reserve_reset&&p.fresh_traces==2);
 // The other actor has a real later owner turn and rechecks the entry geometry.
 auto entry=w;entry.self=w.allies[0];entry.allies={w.self};entry.allies[0].position=d.destination;p10_c_foundry::CPolicy replacement;replacement.cells={20,21};auto e=replacement.step(entry,Arm::active);assert(e.template_id==2025&&e.task==Task::move&&e.destination==Point(1,1)&&fresh_certificate(entry,e));budget_ok(replacement.fresh_traces);
 // Earlier actor ordering, shield expiry, low HP and current occupancy deny transfer.
 auto before=w;before.allies[0].pick=-1;p10_c_foundry::CPolicy p2;p2.cells={20,21};p2.source=20;assert(p2.step(before,Arm::active).template_id!=2023);
 auto expiry=w;expiry.allies[0].shield_until=w.round;p10_c_foundry::CPolicy p3;p3.cells={20,21};p3.source=20;assert(p3.step(expiry,Arm::active).template_id!=2023);
 auto low=w;low.self.hp=60;p10_c_foundry::CPolicy p4;p4.cells={20,21};p4.source=20;assert(p4.step(low,Arm::active).template_id!=2023);
 auto occupied=w;occupied.allies.push_back(ally(w,3,Champion::stellar,{1,1},2));p10_c_foundry::CPolicy p5;p5.cells={20,21};p5.source=20;assert(p5.step(occupied,Arm::active).template_id!=2023);
 auto sw=base(Champion::willow);sw.self.focus=100;sw.self.abilities[0].ready=sw.self.abilities[0].heal=true;sw.self.abilities[0].range=4;sw.self.abilities[0].modifier=3;auto hurt=ally(sw,2,Champion::varg,{2,1});hurt.hp=100;sw.allies={hurt};auto funding=source(22,{mob(220,22,{2,2},0)},{{1,1}},20);clear(funding);sw.groups={funding};sw.visible_open_tiles={{1,1}};p10_c_foundry::CPolicy support;auto cold=support.step(sw,Arm::active);assert(cold.template_id==2024&&cold.task==Task::capture&&support.cells[0]<0&&support.cells[1]<0);sw.own_payout=true;sw.last_payout_round=9;sw.own_gold=30;sw.self.at_spawn=true;auto buy=support.step(sw,Arm::active);assert(support.own_income&&buy.task==Task::heal);sw.own_payout=false;sw.own_gold=0;sw.self.at_spawn=false;auto heal=support.step(sw,Arm::active);assert(heal.task==Task::heal&&heal.actor==2&&heal.template_id==2021);
}
static World ordered(){auto w=base();w.self.position={1,1};auto a=source(30,{mob(300,30,{1,1},0)},{{1,0},{2,1}},101);clear(a);auto b=source(31,{mob(310,31,{3,1},70)},{{1,1},{2,1},{2,2}},20);w.groups={a,b};w.visible_open_tiles={{1,1},{2,1},{2,2},{1,2},{1,0}};w.allies={ally(w,2,Champion::marcella,{1,0})};return w;}
static void spindle_callers(){
 reset();auto w=ordered();p10_c_spindle::CPolicy p;p.current=30;auto d=p.step(w,Arm::active);assert(d.template_id==3031&&d.task==Task::move&&p.pending==30&&p.next_job==31&&p.exposed_count<=2&&fresh_certificate(w,d));budget_ok(p.fresh_traces);
 // A missed later move resets custody instead of pretending continuous capture.
 auto missed=w;++missed.round;missed.self.position=d.destination;missed.allies.clear();auto r=p.step(missed,Arm::active);assert(r.template_id==3033&&r.task==Task::capture&&p.missed_arrivals==1&&p.pending==-1);
 auto reverse=w;reverse.allies[0].pick=-1;p10_c_spindle::CPolicy earlier;earlier.current=30;assert(earlier.step(reverse,Arm::active).template_id==3032&&earlier.pending==-1);
 auto frail=w;frail.allies[0].hp=20;p10_c_spindle::CPolicy exposed;exposed.current=30;assert(exposed.step(frail,Arm::active).template_id==3032&&exposed.pending==-1);
 // Collector claims the actually free single spawn on its own later turn.
 auto cw=w;cw.self=w.allies[0];cw.allies={w.self};cw.allies[0].position=d.destination;p10_c_spindle::CPolicy collector;auto collect=collector.step(cw,Arm::active);assert(collect.task==Task::capture&&collect.group==30&&collect.template_id==3032);
 auto full=w;p10_c_spindle::CPolicy capped;capped.current=30;capped.exposed={30,99};capped.exposed_count=2;assert(capped.step(full,Arm::active).template_id==3032&&capped.pending==-1&&capped.exposed_count==2);
 // An actual witnessed takeover releases the opener to its separately proved job.
 p10_c_spindle::CPolicy witness;w.allies[0].pick=1;auto first=witness.step(w,Arm::active);assert(first.template_id==3031);auto after=w;++after.round;after.self.position=first.destination;after.allies[0].position={1,1};auto follow=witness.step(after,Arm::active);assert(witness.pending==-1&&witness.current==31&&follow.group==31&&follow.template_id==3034);budget_ok(witness.fresh_traces);
}
static World batching(){auto w=base(Champion::tavi);w.self.range=3;w.self.position={1,1};auto a=source(40,{mob(400,40,{2,1},70),mob(401,40,{2,2},70)},{{1,1},{1,2}},101);auto b=source(41,{mob(410,41,{4,1},70),mob(411,41,{4,2},70)},{{3,1},{3,2},{1,1},{2,3}},103);a.respawn=30;w.groups={a,b};w.visible_open_tiles={{1,1},{1,2},{1,3},{2,3},{3,3},{3,2},{3,1}};w.allies={ally(w,2,Champion::willow,{1,3})};return w;}
static void metered_callers(){
 reset();auto w=batching();p10_c_metered::CPolicy p;p.first_income=true;auto d=p.step(w,Arm::active);assert(p.chosen&&p.selected.kind==p10_c_metered::Kind::two_sources&&p.selected.party==metered_works::Program::parallel&&p.selected.service.conditional);assert(p.selected.secured==204&&p.selected.service.shares[0][0]==51&&p.selected.service.shares[0][1]==50&&p.selected.service.shares[1][0]==52&&p.selected.service.shares[1][1]==51);assert(p.selected.service.service_end[0]-p.selected.service.service_start[0]==11);assert(d.template_id==4041&&fresh_certificate(w,d)&&p.fresh_traces==2);
 p10_c_metered::CPolicy c;c.first_income=true;auto control=c.step(w,Arm::control);(void)control;assert(c.selected.party==metered_works::Program::sole&&c.selected.service.service_end[0]-c.selected.service.service_start[0]==21);
 auto generation=w;generation.groups.resize(1);generation.groups[0].respawn=4;p10_c_metered::CPolicy g;g.first_income=true;auto gd=g.step(generation,Arm::active);assert(g.chosen&&g.selected.kind==p10_c_metered::Kind::two_generations&&gd.template_id==4042);
 // The announced party becomes actual only with exactly the declared holders.
 auto ready=w;clear(ready.groups[0]);ready.self.position={2,1};ready.allies[0].position={2,2};auto cap=p.step(ready,Arm::active);assert(cap.template_id==4043&&p.actual_parallel==1);
 auto missing=ready;missing.allies[0].position={1,3};auto prospective=p.step(missing,Arm::active);assert(prospective.template_id==4041&&p.actual_parallel==1);
 // No cardinal terrain appointment -> sole service, without free future ally travel.
 auto blocked=w;blocked.visible_open_tiles={{1,1}};blocked.allies[0].position={1,5};p10_c_metered::CPolicy unavailable;unavailable.first_income=true;unavailable.step(blocked,Arm::active);assert(unavailable.chosen&&unavailable.selected.party==metered_works::Program::sole);
 // Actual receipts survive channel turns and determine which capture is next.
 auto paid=w;paid.own_payout=true;paid.last_payout_round=9;paid.self.channel=Channel::tribute;p.step(paid,Arm::active);assert(p.payouts==1&&p.index==1&&p.last_receipt==9);
}
static World field(){auto w=base(Champion::tavi);w.round=1;w.max_rounds=200;w.self.position={2,2};w.self.range=3;w.self.strength=35;w.self.strength_growth=5;auto fast=source(50,{mob(500,50,{5,2},35)},{{2,2}},20);auto rich=source(51,{mob(510,51,{5,3},35)},{{2,2}},40);fast.respawn=10;rich.respawn=100;w.groups={fast,rich};for(int x=2;x<=4;++x)for(int y=2;y<=3;++y)w.visible_open_tiles.push_back({x,y});return w;}
static void orchard_callers(){
 reset();auto w=field();p10_c_orchard::CPolicy p;auto d=p.step(w,Arm::active);assert(p.source==50&&p.repeating&&p.selected_calendar.banked>0&&d.template_id==5051&&fresh_certificate(w,d)&&p.fresh_traces==2);
 p10_c_orchard::CPolicy control;auto c=control.step(w,Arm::control);assert(control.source==51&&c.group==51);budget_ok(control.fresh_traces);
 auto late=w;late.round=180;p10_c_orchard::CPolicy end;auto e=end.step(late,Arm::active);assert(end.source==51&&e.group==51&&end.selected_calendar.banked==40);
 auto gear=w;gear.max_rounds=2000;gear.groups.resize(1);gear.groups[0].monsters[0].hp=gear.groups[0].monsters[0].max_hp=75;gear.groups[0].hp=75;gear.own_gold=30;p10_c_orchard::CPolicy investment;auto buy=investment.step(gear,Arm::active);assert(buy.task==Task::buy&&buy.item==Item::monster_strength&&buy.buy_count==1&&buy.template_id==5054&&investment.threshold_purchases==1);
 auto poor=gear;poor.own_gold=29;p10_c_orchard::CPolicy unfunded;assert(unfunded.step(poor,Arm::active).task!=Task::buy);
 auto one=w;one.own_gold=30;p10_c_orchard::CPolicy redundant;assert(redundant.step(one,Arm::active).task!=Task::buy);
 auto purchased=gear;++purchased.round;purchased.self.strength=40;purchased.self.strength_shards=1;purchased.own_gold=0;purchased.own_purchase=true;auto after=investment.step(purchased,Arm::active);assert(investment.observed_purchases==1&&investment.threshold_purchases==1&&after.task==Task::attack&&fresh_certificate(purchased,after));budget_ok(investment.fresh_traces);
 // Field stock remains on its tour. A cheap real move vacates renewal spawns;
 // no heal/recall trip is attached to a harmless payout.
 auto paid=w;paid.self.position={5,2};paid.self.bag=20;paid.own_payout=true;paid.last_payout_group=50;paid.last_payout_round=paid.round;clear(paid.groups[0]);paid.groups[0].cleared=false;paid.groups[0].respawning=true;paid.groups[0].respawn_remaining=10;p10_c_orchard::CPolicy tour;tour.source=50;tour.repeating=true;tour.bank_after=2;tour.wait_pad={2,2};auto leave=tour.step(paid,Arm::active);assert(tour.completed==1&&leave.task==Task::move&&leave.template_id==5052&&leave.destination==Point(4,2)&&fresh_certificate(paid,leave)&&tour.fresh_traces==0);
 auto channel=paid;channel.self.channel=Channel::tribute;p10_c_orchard::CPolicy durable;durable.source=50;durable.step(channel,Arm::active);assert(durable.completed==1&&durable.birth==11);
 // The actual adapter exports only the current 3x3 neighborhood. Immutable
 // near-source pads complete the longer field leg without a fake full view.
 auto local=w;local.visible_open_tiles={{1,1},{2,1},{3,1},{1,2},{2,2},{3,2},{1,3},{2,3},{3,3}};
 local.groups[0].approach_pads={{4,1},{4,2},{4,3}};local.groups[1].approach_pads={{4,2},{4,3},{4,4}};
 p10_c_orchard::CPolicy actual_view;auto observed=actual_view.step(local,Arm::active);assert(actual_view.repeating&&observed.task==Task::attack&&actual_view.wait_pad==Point(4,2));
 static_assert(p10_c::Physical::observe_open_tiles);
 // Damaging recurring work receives no imaginary healing or repeated full HP.
 auto danger=w;danger.groups.resize(1);danger.groups[0].monsters[0].hp=danger.groups[0].monsters[0].max_hp=140;danger.groups[0].monsters[0].strength=20;danger.groups[0].monsters[0].range=3;p10_c_orchard::CPolicy current;auto finite=current.step(danger,Arm::active);assert(!current.repeating&&finite.template_id==5055&&fresh_certificate(danger,finite));danger.self.bag=20;assert(current.step(danger,Arm::active).task==Task::bank);
}
static void cold_openings(){
 auto w=field();for(auto&g:w.groups){g.observed=g.complete=false;for(auto&m:g.monsters)m.observed=false;}
#if P10_C01_READY
 p10_c_forge::CPolicy a;assert(a.step(w,Arm::active).task==Task::inspect);
#endif
 p10_c_foundry::CPolicy b;assert(b.step(w,Arm::active).task==Task::inspect);
 p10_c_spindle::CPolicy c;assert(c.step(w,Arm::active).task==Task::inspect);
 p10_c_metered::CPolicy d;auto batch=d.step(w,Arm::active);assert(batch.task==Task::inspect&&batch.group==51&&d.fresh_traces==0);
 p10_c_orchard::CPolicy e;auto field_=e.step(w,Arm::active);assert(field_.task==Task::inspect&&field_.group==50&&e.fresh_traces==0);
}
static int blocked_fuel(){return 40200;}
static int ordinary_fuel(){return 45000;}
static void caller_fuel_boundaries(){
 auto f=field();p10_c_orchard::CPolicy stopped;stopped.set_fuel_reader(blocked_fuel);auto no=stopped.step(f,Arm::active);assert(no.task==Task::wait&&stopped.work_aborted&&stopped.fresh_traces==0);
 // Receptive control logic still consumes real receipts under low headroom.
 auto paid=ordered();paid.own_payout=true;paid.last_payout_round=2;paid.self.channel=Channel::tribute;p10_c_spindle::CPolicy receipt;receipt.set_fuel_reader(blocked_fuel);receipt.step(paid,Arm::active);assert(receipt.last_receipt==2&&receipt.fresh_traces==0);
 p10_c_orchard::CPolicy ordinary;ordinary.set_fuel_reader(ordinary_fuel);auto yes=ordinary.step(f,Arm::active);assert(yes.task==Task::attack&&ordinary.fresh_traces<=2&&fresh_certificate(f,yes));
}
static World capital_world(Champion type=Champion::vendetta){auto w=base(type);w.max_rounds=2000;w.self.strength=100;w.self.strength_growth=5;w.self.strength_shards=12;w.self.at_spawn=true;w.own_gold=120;w.to_spawn=0;auto g=source(60,{mob(600,60,{2,1},240)},{{1,1}},100);g.respawn=10;w.groups={g};w.visible_open_tiles={{1,1},{1,2},{2,2}};return w;}
static void purchase(World&w,double strength=0,double focus=0,double health=0){++w.round;w.own_purchase=true;w.receipts={{Receipt::Kind::purchase,w.round,-1,w.self.id}};w.own_gold-=w.shard_cost;w.self.strength+=strength;w.self.focus+=focus;w.self.max_hp+=health;if(strength>0)++w.self.strength_shards;if(focus>0)++w.self.focus_shards;if(health>0)++w.self.health_shards;}
#if P10_C01_READY
static void forge_capital(){
#if P10_C01_READY
 reset();auto w=capital_world();p10_c_forge::CPolicy p;auto d=p.step(w,Arm::active);assert(d.task==Task::buy&&d.buy_count==1&&d.template_id==6101&&p.capital_packet==4&&p.wallet.issued==1&&p.fresh_traces==0);
#endif
#if P10_C01_READY
 p10_c_forge::CPolicy control;auto c=control.step(w,Arm::control);assert(c.task!=Task::buy&&control.wallet.issued==0);
#endif
 auto pending=w;++pending.round;auto wait=p.step(pending,Arm::active);assert(wait.task==Task::attack&&p.wallet.issued==1&&p.wallet.pending&&fresh_certificate(pending,wait));
 for(int i=0;i<3;++i){purchase(w,5);auto next=p.step(w,Arm::active);assert(next.task==Task::buy&&next.buy_count==1&&p.wallet.confirmed==i+1&&p.wallet.issued==i+2);}
 purchase(w,5);assert(p.step(w,Arm::active).task==Task::attack&&p.wallet.confirmed==4&&p.wallet.issued==4);
 // A new funded source threshold restarts investment beyond both old menu caps.
 w.own_purchase=false;w.receipts.clear();w.own_gold=90;w.own_bank_completed=true;w.last_bank_round=w.round;w.groups[0].id=61;w.groups[0].monsters[0].group=61;w.groups[0].monsters[0].id=610;w.groups[0].monsters[0].hp=w.groups[0].monsters[0].max_hp=270;auto repeat=p.step(w,Arm::active);assert(repeat.task==Task::buy&&p.wallet.issued==5&&w.self.strength_shards>10);
#if P10_C01_READY
 auto immediate=capital_world();immediate.self.strength_growth=100;immediate.groups[0].monsters[0].hp=immediate.groups[0].monsters[0].max_hp=600;immediate.own_gold=30;p10_c_forge::CPolicy strong;assert(strong.step(immediate,Arm::control).task==Task::buy);
#endif
}
#endif

static void foundry_capital(){
 reset();auto w=capital_world(Champion::heart);w.groups.push_back(source(61,{mob(610,61,{1,2},70)},{{1,1}},20));w.self.strength_growth=0;w.self.focus=100;w.self.focus_growth=10;w.self.focus_shards=7;w.own_gold=60;w.groups[0].monsters[0].strength=100;w.groups[0].monsters[0].range=3;auto&a=w.self.abilities[0];a.ready=a.shield=a.targets_self=true;a.modifier=2.5;a.max_cooldown=8;a.duration=5;p10_c_foundry::CPolicy p;p.cells={60,61};p.source=60;auto d=p.step(w,Arm::active);assert(d.task==Task::buy&&d.item==Item::jungle_focus&&d.buy_count==1&&p.capital_packet==2&&p.wallet.issued==1);
 p10_c_foundry::CPolicy control;control.cells={60,61};control.source=60;assert(control.step(w,Arm::control).task!=Task::buy);
 purchase(w,0,10);assert(p.step(w,Arm::active).task==Task::buy&&p.wallet.confirmed==1&&p.wallet.issued==2);purchase(w,0,10);assert(p.step(w,Arm::active).task!=Task::buy&&p.wallet.confirmed==2);
 auto willow=capital_world(Champion::willow);willow.groups.push_back(source(61,{mob(610,61,{1,2},70)},{{1,1}},20));willow.self.focus=100;willow.self.focus_growth=12;willow.self.strength_growth=0;willow.own_gold=90;willow.groups[0].monsters[0].strength=50;willow.groups[0].monsters[0].range=3;auto&heal=willow.self.abilities[0];heal.ready=heal.heal=true;heal.modifier=3;heal.range=4;auto patient=ally(willow,2,Champion::varg,{2,2});patient.hp=700;willow.allies={patient};p10_c_foundry::CPolicy unfunded;unfunded.cells={60,61};unfunded.source=60;assert(unfunded.step(willow,Arm::active).task!=Task::buy);
 p10_c_foundry::CPolicy funded;funded.cells={60,61};funded.source=60;funded.own_income=true;auto own=funded.step(willow,Arm::active);assert(own.task==Task::buy&&own.item==Item::jungle_focus&&funded.capital_packet==3);
}
static World queue_capital(){auto w=capital_world();auto next=source(61,{mob(610,61,{1,2},240)},{{1,1}},100);next.respawn=10;w.groups.push_back(next);w.allies={ally(w,2,Champion::marcella,{1,0})};return w;}
static void spindle_capital(){
 reset();auto w=queue_capital();p10_c_spindle::CPolicy p;auto d=p.step(w,Arm::active);assert(d.task==Task::buy&&d.item==Item::monster_strength&&d.buy_count==1&&p.capital_packet==4&&p.wallet.issued==1);
 p10_c_spindle::CPolicy control;assert(control.step(w,Arm::control).task!=Task::buy);
 purchase(w,5);assert(p.step(w,Arm::active).task==Task::buy&&p.wallet.issued==2&&p.wallet.confirmed==1);
 auto occupied=w;occupied.own_purchase=false;occupied.receipts.clear();p10_c_spindle::CPolicy busy;busy.current=60;busy.exposed={60,-1};busy.exposed_count=1;assert(busy.step(occupied,Arm::active).task!=Task::buy&&busy.wallet.issued==0);
}
static World batch_capital(){auto w=queue_capital();w.self.type=Champion::tavi;w.self.range=3;w.self.at_spawn=false;w.allies.clear();w.groups[0].gems=101;w.groups[1].gems=103;return w;}
static void metered_capital(){
 reset();auto w=batch_capital();p10_c_metered::CPolicy p;p.first_income=true;auto d=p.step(w,Arm::active);assert(d.task==Task::buy&&d.buy_count==1&&d.template_id==6401&&p.capital_packet==4&&p.wallet.issued==1);
 p10_c_metered::CPolicy control;control.first_income=true;assert(control.step(w,Arm::control).task!=Task::buy);
 purchase(w,5);assert(p.step(w,Arm::active).task==Task::buy&&p.wallet.confirmed==1&&p.wallet.issued==2);
 // A proposed shop trip keeps only a stat/source endpoint. Arrival does not
 // manufacture private combat observations or resume a physical trajectory.
 auto trip=batch_capital();trip.self.type=Champion::marcella;trip.to_spawn=2;for(auto&g:trip.groups){const int id=g.monsters[0].id;const auto point=g.monsters[0].position;g.monsters.push_back(mob(id+2,g.id,{point.x+1,point.y},240));g.monsters.push_back(mob(id+3,g.id,{point.x+1,point.y+1},240));}p10_c_metered::CPolicy shopper;shopper.first_income=true;auto route=shopper.step(trip,Arm::active);assert(route.task==Task::recover&&shopper.strength_goal>trip.self.strength&&shopper.wallet.trips==1);
 ++trip.round;trip.self.at_spawn=true;for(auto&g:trip.groups){g.observed=g.complete=false;for(auto&m:g.monsters)m.observed=false;}auto shop=shopper.step(trip,Arm::active);assert(shop.task==Task::buy&&shopper.wallet.issued==1&&shopper.fresh_traces==0);
}
static World field_capital(double hp=267,int gold=90){auto w=field();w.max_rounds=2000;w.groups.resize(1);w.groups[0].monsters[0].hp=w.groups[0].monsters[0].max_hp=hp;w.self.strength=120;w.self.strength_growth=5;w.self.strength_shards=12;w.own_gold=gold;return w;}
static void orchard_capital(){
 reset();auto w=field_capital();p10_c_orchard::CPolicy p;auto d=p.step(w,Arm::active);assert(d.task==Task::buy&&d.buy_count==1&&p.capital_target.shards==3&&p.capital_target.cost==90&&p.wallet.issued==1);
 p10_c_orchard::CPolicy control;assert(control.step(w,Arm::control).task!=Task::buy);
 auto poor=w;poor.own_gold=60;p10_c_orchard::CPolicy unfunded;assert(unfunded.step(poor,Arm::active).task!=Task::buy&&unfunded.wallet.issued==0);
 auto pending=w;++pending.round;assert(p.step(pending,Arm::active).task==Task::attack&&p.wallet.issued==1&&p.wallet.pending);
 auto many=field_capital(247,90);many.groups[0].monsters.push_back(mob(501,50,{5,3},257));many.groups[0].monsters.push_back(mob(502,50,{4,3},267));many.groups[0].spawns={{5,2},{5,3},{4,3}};p10_c_orchard::CPolicy recurring;assert(recurring.step(many,Arm::active).task==Task::buy);
 purchase(many,5);assert(recurring.step(many,Arm::active).task==Task::buy&&recurring.wallet.issued==2);purchase(many,5);assert(recurring.step(many,Arm::active).task==Task::buy&&recurring.wallet.issued==3&&recurring.wallet.confirmed==2);
 auto bounded=field_capital(360,360);p10_c_orchard::CPolicy maximum;assert(maximum.step(bounded,Arm::active).task==Task::buy&&maximum.capital_target.shards==12);bounded.own_gold=359;p10_c_orchard::CPolicy shortfall;assert(shortfall.step(bounded,Arm::active).task!=Task::buy);
}
static void capital_receipts_and_horizon(){
 auto w=capital_world();w.self.channel=Channel::tribute;w.own_purchase=true;w.receipts={{Receipt::Kind::purchase,7,-1,w.self.id}};
#if P10_C01_READY
p10_c_forge::CPolicy a;a.step(w,Arm::active);assert(a.wallet.confirmed==1&&a.wallet.last_purchase==7);
#endif
 p10_c_foundry::CPolicy b;b.step(w,Arm::active);assert(b.wallet.confirmed==1);
 p10_c_spindle::CPolicy c;c.step(w,Arm::active);assert(c.wallet.confirmed==1);
 p10_c_metered::CPolicy d;d.step(w,Arm::active);assert(d.wallet.confirmed==1);
 p10_c_orchard::CPolicy e;e.step(w,Arm::active);assert(e.wallet.confirmed==1);
#if P10_C01_READY
 auto late=capital_world();late.max_rounds=late.round+10;p10_c_forge::CPolicy no;assert(no.step(late,Arm::active).task!=Task::buy&&no.wallet.issued==0);
#endif
#if P10_C01_READY
 auto low=capital_world();low.own_gold=119;p10_c_forge::CPolicy shortfall;assert(shortfall.step(low,Arm::active).task!=Task::buy);
#endif
}
static void capital_low_headroom(){
 auto w=capital_world();
#if P10_C01_READY
p10_c_forge::CPolicy a;a.set_fuel_reader(blocked_fuel);a.step(w,Arm::active);assert(a.wallet.issued==0&&a.fresh_traces==0);
#endif
 p10_c_foundry::CPolicy b;b.set_fuel_reader(blocked_fuel);b.cells={60,60};b.step(w,Arm::active);assert(b.wallet.issued==0&&b.fresh_traces==0);
 auto q=queue_capital();p10_c_spindle::CPolicy c;c.set_fuel_reader(blocked_fuel);c.step(q,Arm::active);assert(c.wallet.issued==0&&c.fresh_traces==0);
 auto batch=batch_capital();p10_c_metered::CPolicy d;d.first_income=true;d.set_fuel_reader(blocked_fuel);d.step(batch,Arm::active);assert(d.wallet.issued==0&&d.fresh_traces==0);
 auto field_=field_capital();p10_c_orchard::CPolicy e;e.set_fuel_reader(blocked_fuel);e.step(field_,Arm::active);assert(e.wallet.issued==0&&e.fresh_traces==0);
 static_assert(p10_c::Physical::owns_capital);
}
static const int*falling_phase=nullptr;static int falling_at=0,falling_after=0,phase_reads=0,total_reads=0;
static int falling_fuel(){++total_reads;if(falling_phase&&*falling_phase==falling_at&&++phase_reads>=falling_after)return 41000;return 100000;}
static void arm_falling(const int&phase,int at,int after){falling_phase=&phase;falling_at=at;falling_after=after;phase_reads=total_reads=0;}
static void live_postproof_fuel(){
 // Metered's participant route/exposure and service algebra used to run after
 // both physical traces without a live reader boundary. The actual caller
 // must refuse a partial organization, then restart from this next real frame.
 for(int at:{3,4}){reset();auto w=batching();p10_c_metered::CPolicy p;p.first_income=true;p.set_fuel_reader(falling_fuel);arm_falling(p.work_phase,at,at==3?12:1);auto d=p.step(w,Arm::active);assert(d.task==Task::wait&&!d.safety_verified&&p.work_aborted&&!p.chosen&&p.fresh_traces<=2&&total_reads<20000);++w.round;p.set_fuel_reader(ordinary_fuel);auto next=p.step(w,Arm::active);assert(!p.work_aborted&&p.chosen&&fresh_certificate(w,next)&&p.fresh_traces<=2);}
 // Orchard's nested neutral-pad / all-spawn L-route / full-world hazard work,
 // bounded calendar and post-proof threshold all use the injected live reader.
 for(int at:{3,4,7}){reset();auto w=field();p10_c_orchard::CPolicy p;p.set_fuel_reader(falling_fuel);arm_falling(p.work_phase,at,at==3?30:1);auto d=p.step(w,Arm::active);assert(d.task==Task::wait&&!d.safety_verified&&p.work_aborted&&p.fresh_traces<=2&&total_reads<20000);if(at<7)assert(p.source<0&&p.calendar_choices==0);++w.round;p.set_fuel_reader(ordinary_fuel);auto next=p.step(w,Arm::active);assert(!p.work_aborted&&fresh_certificate(w,next)&&p.fresh_traces<=2);}
 // Receipt processing and sole-holder custody precede the optional work guard.
 auto paid=field();paid.self.position={5,2};paid.self.bag=20;paid.own_payout=true;paid.last_payout_group=50;paid.last_payout_round=paid.round;clear(paid.groups[0]);paid.groups[0].cleared=false;paid.groups[0].respawning=true;paid.groups[0].respawn_remaining=10;p10_c_orchard::CPolicy tour;tour.source=50;tour.repeating=true;tour.bank_after=2;tour.wait_pad={2,2};tour.set_fuel_reader(falling_fuel);arm_falling(tour.work_phase,6,8);auto stopped=tour.step(paid,Arm::active);assert(stopped.task==Task::wait&&tour.work_aborted&&tour.completed==1&&tour.fresh_traces==0);
 auto custody=paid;custody.capture_active=true;custody.productive_group=50;tour.set_fuel_reader(blocked_fuel);assert(tour.step(custody,Arm::active).task==Task::capture);
 falling_phase=nullptr;std::puts("B04/B05 falling-fuel actual callers: guarded participant, graph, calendar, threshold, return; fresh restart PASS");
}
static void capital_falling_callers(){
 for(int after:{1,3,5}){reset();auto w=batch_capital();p10_c_metered::CPolicy p;p.first_income=true;p.set_fuel_reader(falling_fuel);arm_falling(p.work_phase,7,after);auto d=p.step(w,Arm::active);assert(d.task==Task::wait&&p.work_aborted&&p.wallet.issued==0&&p.fresh_traces<=2);++w.round;p.set_fuel_reader(ordinary_fuel);auto next=p.step(w,Arm::active);assert(next.task==Task::buy&&!p.work_aborted&&p.wallet.issued==1);}
 for(int after:{1,5,12}){reset();auto w=field_capital();p10_c_orchard::CPolicy p;p.set_fuel_reader(falling_fuel);arm_falling(p.work_phase,7,after);auto d=p.step(w,Arm::active);assert(d.task==Task::wait&&p.work_aborted&&p.wallet.issued==0&&!p.capital_target.valid&&p.fresh_traces<=2);++w.round;p.set_fuel_reader(ordinary_fuel);auto next=p.step(w,Arm::active);assert(next.task==Task::buy&&!p.work_aborted&&p.wallet.issued==1);}
 std::puts("Rebased C04/C05 capital falling-fuel actual callers: partial endpoint discarded, fresh-frame repricing PASS");
}
#if P10_C01_READY
static World forge_bootstrap_frame(){
 World w;w.round=40;w.max_rounds=2000;w.spawn={38,2};w.fuel=100000;w.self.id=0;w.self.type=Champion::varg;w.self.position={35,2};w.self.hp=w.self.max_hp=1000;w.self.strength=40;w.self.focus=100;w.self.range=1;w.self.visible=w.self.alive=w.self.ally=true;w.self.can_attack=w.self.can_cast=w.self.can_move=true;
 auto&a=w.self.abilities[0];a.reset=a.ready=a.targets_self=true;a.max_cooldown=8;
 auto&s=w.self.abilities[1];s.shield=s.ready=s.targets_self=true;s.modifier=2.5;s.duration=5;s.max_cooldown=8;
 Group g;g.id=18;g.position={36,2};g.gems=132;g.gold=30;g.travel=0;g.to_bank=g.to_spawn=3;g.observed=g.complete=g.living=g.route_known=g.program_available=true;g.approach={35,2};
 for(int y=1;y<=5;++y)for(int x=35;x<=38;++x)if(x==35||x==38||y==1||y==5)g.approach_pads.push_back({x,y});
 const int ids[3][2]={{50,22},{54,18},{52,20}};for(int y=2;y<=4;++y)for(int x=36;x<=37;++x){Monster m;m.id=ids[y-2][x-36];m.group=18;m.position={x,y};m.hp=m.max_hp=x==36?120:2400;m.strength=x==36?0:60;m.range=x==36?3:4;m.observed=m.alive=true;g.hp+=m.hp;g.monsters.push_back(m);g.spawns.push_back(m.position);}
 Group small;small.id=19;small.position={34,2};small.gems=8;small.travel=0;small.to_bank=small.to_spawn=3;small.approach={35,2};small.approach_pads={{35,2},{34,1},{34,3},{33,2}};small.observed=small.complete=small.living=small.route_known=small.program_available=true;Monster m;m.id=99;m.group=19;m.position={34,2};m.hp=m.max_hp=40;m.observed=m.alive=true;small.monsters={m};small.spawns={m.position};small.hp=40;w.groups={g,small};return w;
}
#endif

#if P10_C01_READY
static int no_headroom(){return 40200;}
#endif

#if P10_C01_READY
static void forge_bootstrap_callers(){
#if P10_C01_READY
 p10_mobile::default_cache={};auto w=forge_bootstrap_frame();p10_c_forge::CPolicy p;auto first=p.step(w,Arm::active);
#endif
 assert(p.refusal_events==1&&p.fresh_traces<=2&&first.group==19&&fresh_certificate(w,first));
 ++w.round;assert(p.rejected(w,w.groups[0]));auto next=p.step(w,Arm::active);assert(next.group==19&&next.task!=Task::inspect&&p.refusal_events==1&&p.fresh_traces<=2);
 // Changed real source HP/capital invalidates its old refusal. No forecast
 // result, helper damage or wallet is carried into the new physical frame.
 auto changed=w;changed.groups[0].monsters[1].hp-=40;assert(!p.rejected(changed,changed.groups[0]));changed=w;changed.self.strength+=5;assert(!p.rejected(changed,changed.groups[0]));
 // A transient fuel refusal cannot poison the source or select a fictitious
 // productive continuation. The card can retry when a fresh budget arrives.
#if P10_C01_READY
 p10_c_forge::CPolicy low;low.set_fuel_reader(no_headroom);auto wait=low.step(w,Arm::active);assert(wait.task==Task::inspect&&low.refusal_events==0&&low.fresh_traces==0);
#endif
 std::puts("CForge actual caller: measured long-clear refusal reselects; fresh premises and temporary fuel PASS");
}
#endif



static constexpr std::uint64_t type_bit(Champion t){return std::uint64_t{1}<<unsigned(t);}
#if P10_C01_READY
static_assert(!p10_c_forge::CPolicy::observe_ally_details&&!p10_c_forge::CPolicy::observe_open_tiles);
#endif
#if P10_C01_READY
static_assert(p10_c_forge::CPolicy::observe_ally_detail_types==0);
#endif
static_assert(p10_c_foundry::CPolicy::observe_ally_detail_types==(type_bit(Champion::heart)|type_bit(Champion::varg)));
static_assert(p10_c_spindle::CPolicy::observe_ally_detail_types==type_bit(Champion::marcella));
static_assert(p10_c_metered::CPolicy::observe_ally_detail_types==0&&p10_c_orchard::CPolicy::observe_ally_detail_types==0);
static_assert(p10_c_foundry::CPolicy::observe_open_tiles&&p10_c_spindle::CPolicy::observe_open_tiles&&p10_c_metered::CPolicy::observe_open_tiles&&p10_c_orchard::CPolicy::observe_open_tiles);
static Actor raw_light(Actor a){
 // actor_view begins with immutable design, then unpacks raw stats/order/bag.
 // Its early return precedes these combat-only queries. Keep raw fields and
 // use the actual shared completeness marker rather than a synthetic ready kit.
 a.shield=0;a.shield_until=-1;a.inventory={};a.strength_shards=a.health_shards=a.focus_shards=0;a.monster_emblem=a.jungle_emblem=false;
 p10::withhold_combat_details(a);return a;
}
template<class Policy>static World mask_frame(World w){for(auto&a:w.allies)if(!(Policy::observe_ally_detail_types&type_bit(a.type)))a=raw_light(a);return w;}
static void observation_trait_callers(){
 reset();auto f=base();f.self.strength=40;auto&r=f.self.abilities[0];r.ready=r.reset=r.targets_self=true;r.max_cooldown=8;
 f.groups={source(10,{mob(100,10,{2,1},160,20,3),mob(101,10,{3,1},110),mob(102,10,{2,2},110),mob(103,10,{3,2},110)},{{1,1},{1,2}},60),source(11,{mob(110,11,{0,2},90)},{{0,1},{1,1}},20)};f.visible_open_tiles={{1,1},{1,2},{0,1}};
 auto ven=ally(f,2,Champion::vendetta,{1,2});ven.strength=70;ven.shield=99;ven.jungle_emblem=true;ven.cooldown={3,4};f.allies={ven};auto enemy=ven;enemy.id=8;enemy.ally=false;enemy.position={99,99};f.enemies={enemy};
#if P10_C01_READY
 auto fl=mask_frame<p10_c_forge::CPolicy>(f);assert(!fl.allies[0].combat_details_complete&&fl.allies[0].shield==0&&!fl.allies[0].jungle_emblem&&fl.allies[0].cooldown[0]==-1);assert(fl.allies[0].strength==70&&fl.allies[0].can_move&&fl.self.combat_details_complete&&fl.enemies[0].shield==99);
#endif
#if P10_C01_READY
 p10_c_forge::CPolicy fp;auto fd=fp.step(fl,Arm::active);assert(fd.template_id==1011&&fp.preparation_events==1&&fresh_certificate(fl,fd));budget_ok(fp.fresh_traces);
#endif
 reset();auto h=carousel();h.allies.push_back(ally(h,3,Champion::stellar,{99,99},2));auto hl=mask_frame<p10_c_foundry::CPolicy>(h);assert(hl.allies[0].combat_details_complete&&hl.allies[0].shield==500&&!hl.allies[1].combat_details_complete);
 p10_c_foundry::CPolicy hp;hp.cells={20,21};hp.source=20;auto hd=hp.step(hl,Arm::active);assert(hd.template_id==2023&&hp.transfers==1&&fresh_certificate(hl,hd));budget_ok(hp.fresh_traces);
 auto missing=h;missing.allies[0]=raw_light(missing.allies[0]);p10_c_foundry::CPolicy denied;denied.cells={20,21};denied.source=20;assert(denied.step(missing,Arm::active).template_id!=2023);
 reset();auto s=ordered();s.allies[0].jungle_emblem=true;s.allies[0].inventory[int(Item::jungle_health)]=5;auto sl=mask_frame<p10_c_spindle::CPolicy>(s);assert(sl.allies[0].combat_details_complete&&sl.allies[0].jungle_emblem&&sl.allies[0].inventory[int(Item::jungle_health)]==5);
 p10_c_spindle::CPolicy sp;sp.current=30;auto sd=sp.step(sl,Arm::active);assert(sd.template_id==3031&&sp.pending==30&&fresh_certificate(sl,sd));budget_ok(sp.fresh_traces);
 reset();auto m=batching();m.allies[0].shield=999;m.allies[0].cooldown={5,6};auto ml=mask_frame<p10_c_metered::CPolicy>(m);assert(!ml.allies[0].combat_details_complete&&ml.allies[0].bag==m.allies[0].bag&&ml.allies[0].priority==m.allies[0].priority);
 p10_c_metered::CPolicy mp;mp.first_income=true;auto md=mp.step(ml,Arm::active);assert(md.template_id==4041&&mp.chosen&&mp.selected.secured==204&&mp.selected.participant==2&&fresh_certificate(ml,md));budget_ok(mp.fresh_traces);
 reset();auto o=field();o.allies={ally(o,2,Champion::vendetta,{0,0})};auto ol=mask_frame<p10_c_orchard::CPolicy>(o);assert(!ol.allies[0].combat_details_complete);p10_c_orchard::CPolicy op;auto od=op.step(ol,Arm::active);assert(od.template_id==5051&&op.source==50&&fresh_certificate(ol,od));budget_ok(op.fresh_traces);
 std::puts("C01-05 prepared observation traits: actual policy callers preserve split prep/carousel/custody/party/tour; self/enemies full PASS");
}

static void metered_first_service_regression(){
 for(Arm arm:{Arm::active,Arm::control}){
  reset();auto w=base(Champion::tavi);w.max_rounds=2000;w.self.range=3;w.own_gold=120;w.own_payout=false;w.last_payout_round=-1;w.receipts.clear();
  w.groups={source(70,{mob(700,70,{2,1},140)},{{1,1}},0)};w.groups[0].gold=40;
  p10_c_metered::CPolicy p;p.set_fuel_reader(+[]{return 100000;});
  auto d=p.step(w,arm);assert(d.task==Task::attack&&d.group==70&&d.template_id==4045&&fresh_certificate(w,d)&&!p.first_income&&!p.chosen&&p.wallet.issued==0&&p.fresh_traces==1);
  auto other=w;++other.round;other.own_purchase=true;other.receipts={{Receipt::Kind::payout,other.round,70,w.self.id+1},{Receipt::Kind::bank,other.round,-1,w.self.id+1},{Receipt::Kind::purchase,other.round,-1,w.self.id}};
  p.step(other,arm);assert(!p.first_income&&!p.chosen&&p.wallet.issued==0&&p.wallet.confirmed==1);
  auto custody=w;++custody.round;clear(custody.groups[0]);custody.self.position={2,1};custody.capture_active=true;custody.productive_group=70;
  auto cap=p.step(custody,arm);assert(cap.task==Task::capture&&cap.group==70&&!p.first_income&&!p.chosen&&p.fresh_traces==0);
  auto own=w;++own.round;own.self.channel=Channel::tribute;own.receipts={{Receipt::Kind::payout,own.round,70,w.self.id}};
  assert(p.step(own,arm).task==Task::wait&&p.first_income&&p.first_source==-1&&p.fresh_traces==0);
  p10_c_metered::CPolicy banked;banked.set_fuel_reader(+[]{return 100000;});auto bank=w;bank.own_bank_completed=true;bank.self.channel=Channel::tribute;
  assert(banked.step(bank,arm).task==Task::wait&&banked.first_income&&banked.fresh_traces==0);
 }
}

int main(){
metered_first_service_regression();
observation_trait_callers();
#if P10_C01_READY
forge_callers();
#endif
foundry_callers();spindle_callers();metered_callers();orchard_callers();cold_openings();caller_fuel_boundaries();
#if P10_C01_READY
forge_capital();
#endif
foundry_capital();spindle_capital();metered_capital();orchard_capital();capital_receipts_and_horizon();capital_low_headroom();live_postproof_fuel();capital_falling_callers();
#if P10_C01_READY
forge_bootstrap_callers();
#endif
std::puts("C02-05 actual callers: PASS; current C01 checked separately, native fuel and strength unmeasured");
}
