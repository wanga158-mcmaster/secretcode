#include "../catalyst-forge/src/b-policy.h"
#include "../shield-foundry/src/b-policy.h"
#include "../two-spindle/src/b-policy.h"
#include "../metered-works/src/b-policy.h"
#include "../orchards/src/b-policy.h"
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
static void forge_callers(){
 reset();auto w=base();w.self.strength=40;auto&r=w.self.abilities[0];r.ready=r.reset=r.targets_self=true;r.max_cooldown=8;
 w.groups={source(10,{mob(100,10,{2,1},160,20,3),mob(101,10,{3,1},110),mob(102,10,{2,2},110),mob(103,10,{3,2},110)},{{1,1},{1,2}},60),source(11,{mob(110,11,{0,2},90)},{{0,1},{1,1}},20)};w.visible_open_tiles={{1,1},{1,2},{0,1}};auto ven=ally(w,2,Champion::vendetta,{1,2});ven.strength=70;w.allies={ven};
 // Exercise the funded complete-work mechanism here. The pre-income finite
 // prefix and its separately charged failure branch have their own caller test.
 p10_b_forge::BPolicy p;p.funded=true;auto d=p.step(w,Arm::active);assert(d.template_id==1011&&p.preparation_events==1&&d.group==10);assert(d.task==Task::attack&&d.cast_slot==-1&&d.reserve_reset&&fresh_certificate(w,d));budget_ok(p.fresh_traces);assert(p.fresh_traces==1);
 p10_b_forge::BPolicy control;control.funded=true;auto c=control.step(w,Arm::control);assert(c.template_id==1012&&control.preparation_events==0);budget_ok(control.fresh_traces);
 auto sw=w;sw.self.type=Champion::stellar;sw.self.range=3;sw.self.abilities={};sw.allies.clear();p10_b_forge::BPolicy stellar;auto independent=stellar.step(sw,Arm::active);assert(independent.group==11&&independent.template_id==1013&&fresh_certificate(sw,independent));
 // A non-reaching remote Ven never creates a preparation claim.
 w.allies[0].position={9,9};p10_b_forge::BPolicy absent;auto no=absent.step(w,Arm::active);assert(no.template_id!=1011&&absent.preparation_events==0);
 // Economic pulses are consumed before a channel return.
 auto paid=w;paid.own_payout=true;paid.last_payout_round=7;paid.self.channel=Channel::tribute;p10_b_forge::BPolicy income;income.step(paid,Arm::active);assert(income.funded&&income.last_receipt==7);
}
static World carousel(){auto w=base();w.self.position={1,0};w.self.shield=10;w.self.shield_until=w.round+1;w.visible_open_tiles={{1,0},{2,0},{1,1},{1,2},{2,1}};auto a=source(20,{mob(200,20,{0,0},210,20,3)},{{1,0},{1,1},{2,0}},40);a.monsters[0].awake=true;auto b=source(21,{mob(210,21,{3,2},70)},{{2,1},{2,0}},20);w.groups={a,b};auto heart=ally(w,2,Champion::heart,{1,2});heart.shield=500;heart.shield_until=w.round+4;w.allies={heart};return w;}
static void foundry_callers(){
 reset();auto w=carousel();p10_b_foundry::BPolicy p;p.cells={20,21};p.source=20;auto d=p.step(w,Arm::active);assert(d.template_id==2023&&d.task==Task::move&&d.destination==Point(2,0)&&p.transfers==1&&p.source==21);assert(fresh_certificate(w,d)&&d.reserve_reset&&p.fresh_traces==2);
 // The other actor has a real later owner turn and rechecks the entry geometry.
 auto entry=w;entry.self=w.allies[0];entry.allies={w.self};entry.allies[0].position=d.destination;p10_b_foundry::BPolicy replacement;replacement.cells={20,21};auto e=replacement.step(entry,Arm::active);assert(e.template_id==2025&&e.task==Task::move&&e.destination==Point(1,1)&&fresh_certificate(entry,e));budget_ok(replacement.fresh_traces);
 // Earlier actor ordering, shield expiry, low HP and current occupancy deny transfer.
 auto before=w;before.allies[0].pick=-1;p10_b_foundry::BPolicy p2;p2.cells={20,21};p2.source=20;assert(p2.step(before,Arm::active).template_id!=2023);
 auto expiry=w;expiry.allies[0].shield_until=w.round;p10_b_foundry::BPolicy p3;p3.cells={20,21};p3.source=20;assert(p3.step(expiry,Arm::active).template_id!=2023);
 auto low=w;low.self.hp=60;p10_b_foundry::BPolicy p4;p4.cells={20,21};p4.source=20;assert(p4.step(low,Arm::active).template_id!=2023);
 auto occupied=w;occupied.allies.push_back(ally(w,3,Champion::stellar,{1,1},2));p10_b_foundry::BPolicy p5;p5.cells={20,21};p5.source=20;assert(p5.step(occupied,Arm::active).template_id!=2023);
 auto sw=base(Champion::willow);sw.self.focus=100;sw.self.abilities[0].ready=sw.self.abilities[0].heal=true;sw.self.abilities[0].range=4;sw.self.abilities[0].modifier=3;auto hurt=ally(sw,2,Champion::varg,{2,1});hurt.hp=100;sw.allies={hurt};auto funding=source(22,{mob(220,22,{2,2},0)},{{1,1}},20);clear(funding);sw.groups={funding};sw.visible_open_tiles={{1,1}};p10_b_foundry::BPolicy support;auto cold=support.step(sw,Arm::active);assert(cold.template_id==2020&&cold.task==Task::capture);sw.own_payout=true;sw.last_payout_round=9;sw.own_gold=30;sw.self.at_spawn=true;auto buy=support.step(sw,Arm::active);assert(support.own_income&&buy.task==Task::buy&&buy.item==Item::jungle_focus);sw.own_payout=false;sw.own_gold=0;sw.self.at_spawn=false;auto heal=support.step(sw,Arm::active);assert(heal.task==Task::heal&&heal.actor==2&&heal.template_id==2021);
}
static World ordered(){auto w=base();w.self.position={1,1};auto a=source(30,{mob(300,30,{1,1},0)},{{1,0},{2,1}},101);clear(a);auto b=source(31,{mob(310,31,{3,1},70)},{{1,1},{2,1},{2,2}},20);w.groups={a,b};w.visible_open_tiles={{1,1},{2,1},{2,2},{1,2},{1,0}};w.allies={ally(w,2,Champion::marcella,{1,0})};return w;}
static void spindle_callers(){
 reset();auto w=ordered();p10_b_spindle::BPolicy p;p.current=30;auto d=p.step(w,Arm::active);assert(d.template_id==3031&&d.task==Task::move&&p.pending==30&&p.next_job==31&&p.exposed_count<=2&&fresh_certificate(w,d));budget_ok(p.fresh_traces);
 // A missed later move resets custody instead of pretending continuous capture.
 auto missed=w;++missed.round;missed.self.position=d.destination;missed.allies.clear();auto r=p.step(missed,Arm::active);assert(r.template_id==3033&&r.task==Task::capture&&p.missed_arrivals==1&&p.pending==-1);
 auto reverse=w;reverse.allies[0].pick=-1;p10_b_spindle::BPolicy earlier;earlier.current=30;assert(earlier.step(reverse,Arm::active).template_id==3032&&earlier.pending==-1);
 auto frail=w;frail.allies[0].hp=20;p10_b_spindle::BPolicy exposed;exposed.current=30;assert(exposed.step(frail,Arm::active).template_id==3032&&exposed.pending==-1);
 // Collector claims the actually free single spawn on its own later turn.
 auto cw=w;cw.self=w.allies[0];cw.allies={w.self};cw.allies[0].position=d.destination;p10_b_spindle::BPolicy collector;auto collect=collector.step(cw,Arm::active);assert(collect.task==Task::capture&&collect.group==30&&collect.template_id==3032);
 auto full=w;p10_b_spindle::BPolicy capped;capped.current=30;capped.exposed={30,99};capped.exposed_count=2;assert(capped.step(full,Arm::active).template_id==3032&&capped.pending==-1&&capped.exposed_count==2);
 // An actual witnessed takeover releases the opener to its separately proved job.
 p10_b_spindle::BPolicy witness;w.allies[0].pick=1;auto first=witness.step(w,Arm::active);assert(first.template_id==3031);auto after=w;++after.round;after.self.position=first.destination;after.allies[0].position={1,1};auto follow=witness.step(after,Arm::active);assert(witness.pending==-1&&witness.current==31&&follow.group==31&&follow.template_id==3034);budget_ok(witness.fresh_traces);
}
static World batching(){auto w=base(Champion::tavi);w.self.range=3;w.self.position={1,1};auto a=source(40,{mob(400,40,{2,1},70),mob(401,40,{2,2},70)},{{1,1},{1,2}},101);auto b=source(41,{mob(410,41,{4,1},70),mob(411,41,{4,2},70)},{{3,1},{3,2},{1,1},{2,3}},103);a.respawn=30;w.groups={a,b};w.visible_open_tiles={{1,1},{1,2},{1,3},{2,3},{3,3},{3,2},{3,1}};w.allies={ally(w,2,Champion::willow,{1,3})};return w;}
static void metered_callers(){
 reset();auto w=batching();p10_b_metered::BPolicy p;auto d=p.step(w,Arm::active);assert(p.chosen&&p.selected.kind==p10_b_metered::Kind::two_sources&&p.selected.party==metered_works::Program::parallel&&p.selected.service.conditional);assert(p.selected.secured==204&&p.selected.service.shares[0][0]==51&&p.selected.service.shares[0][1]==50&&p.selected.service.shares[1][0]==52&&p.selected.service.shares[1][1]==51);assert(p.selected.service.service_end[0]-p.selected.service.service_start[0]==11);assert(d.template_id==4041&&fresh_certificate(w,d)&&p.fresh_traces==2);
 p10_b_metered::BPolicy c;auto control=c.step(w,Arm::control);(void)control;assert(c.selected.party==metered_works::Program::sole&&c.selected.service.service_end[0]-c.selected.service.service_start[0]==21);
 auto generation=w;generation.groups.resize(1);generation.groups[0].respawn=4;p10_b_metered::BPolicy g;auto gd=g.step(generation,Arm::active);assert(g.chosen&&g.selected.kind==p10_b_metered::Kind::two_generations&&gd.template_id==4042);
 // The announced party becomes actual only with exactly the declared holders.
 auto ready=w;clear(ready.groups[0]);ready.self.position={2,1};ready.allies[0].position={2,2};auto cap=p.step(ready,Arm::active);assert(cap.template_id==4043&&p.actual_parallel==1);
 auto missing=ready;missing.allies[0].position={1,3};auto prospective=p.step(missing,Arm::active);assert(prospective.template_id==4041&&p.actual_parallel==1);
 // No cardinal terrain appointment -> sole service, without free future ally travel.
 auto blocked=w;blocked.visible_open_tiles={{1,1}};blocked.allies[0].position={1,5};p10_b_metered::BPolicy unavailable;unavailable.step(blocked,Arm::active);assert(unavailable.chosen&&unavailable.selected.party==metered_works::Program::sole);
 // Actual receipts survive channel turns and determine which capture is next.
 auto paid=w;paid.own_payout=true;paid.last_payout_round=9;paid.self.channel=Channel::tribute;p.step(paid,Arm::active);assert(p.payouts==1&&p.index==1&&p.last_receipt==9);
}
static World field(){auto w=base(Champion::tavi);w.round=1;w.max_rounds=200;w.self.position={2,2};w.self.range=3;w.self.strength=35;w.self.strength_growth=5;auto fast=source(50,{mob(500,50,{5,2},35)},{{2,2}},20);auto rich=source(51,{mob(510,51,{5,3},35)},{{2,2}},40);fast.respawn=10;rich.respawn=100;w.groups={fast,rich};for(int x=2;x<=4;++x)for(int y=2;y<=3;++y)w.visible_open_tiles.push_back({x,y});return w;}
static void orchard_callers(){
 reset();auto w=field();p10_b_orchard::BPolicy p;auto d=p.step(w,Arm::active);assert(p.source==50&&p.repeating&&p.selected_calendar.banked>0&&d.template_id==5051&&fresh_certificate(w,d)&&p.fresh_traces==2);
 p10_b_orchard::BPolicy control;auto c=control.step(w,Arm::control);assert(control.source==51&&c.group==51);budget_ok(control.fresh_traces);
 auto late=w;late.round=180;p10_b_orchard::BPolicy end;auto e=end.step(late,Arm::active);assert(end.source==51&&e.group==51&&end.selected_calendar.banked==40);
 auto gear=w;gear.groups.resize(1);gear.groups[0].monsters[0].hp=gear.groups[0].monsters[0].max_hp=75;gear.groups[0].hp=75;gear.own_gold=30;p10_b_orchard::BPolicy investment;auto buy=investment.step(gear,Arm::active);assert(buy.task==Task::buy&&buy.item==Item::monster_strength&&buy.buy_count==1&&buy.template_id==5054&&investment.threshold_purchases==1);
 auto poor=gear;poor.own_gold=29;p10_b_orchard::BPolicy unfunded;assert(unfunded.step(poor,Arm::active).task!=Task::buy);
 auto one=w;one.own_gold=30;p10_b_orchard::BPolicy redundant;assert(redundant.step(one,Arm::active).task!=Task::buy);
 auto purchased=gear;++purchased.round;purchased.self.strength=40;purchased.self.strength_shards=1;purchased.own_gold=0;purchased.own_purchase=true;auto after=investment.step(purchased,Arm::active);assert(investment.observed_purchases==1&&investment.threshold_purchases==1&&after.task==Task::attack&&fresh_certificate(purchased,after));budget_ok(investment.fresh_traces);
 // Field stock remains on its tour. A cheap real move vacates renewal spawns;
 // no heal/recall trip is attached to a harmless payout.
 auto paid=w;paid.self.position={5,2};paid.self.bag=20;paid.own_payout=true;paid.last_payout_group=50;paid.last_payout_round=paid.round;clear(paid.groups[0]);paid.groups[0].cleared=false;paid.groups[0].respawning=true;paid.groups[0].respawn_remaining=10;p10_b_orchard::BPolicy tour;tour.source=50;tour.repeating=true;tour.bank_after=2;tour.wait_pad={2,2};auto leave=tour.step(paid,Arm::active);assert(tour.completed==1&&leave.task==Task::move&&leave.template_id==5052&&leave.destination==Point(4,2)&&fresh_certificate(paid,leave)&&tour.fresh_traces==0);
 auto channel=paid;channel.self.channel=Channel::tribute;p10_b_orchard::BPolicy durable;durable.source=50;durable.step(channel,Arm::active);assert(durable.completed==1&&durable.birth==11);
 // The actual adapter exports only the current 3x3 neighborhood. Immutable
 // near-source pads complete the longer field leg without a fake full view.
 auto local=w;local.visible_open_tiles={{1,1},{2,1},{3,1},{1,2},{2,2},{3,2},{1,3},{2,3},{3,3}};
 local.groups[0].approach_pads={{4,1},{4,2},{4,3}};local.groups[1].approach_pads={{4,2},{4,3},{4,4}};
 p10_b_orchard::BPolicy actual_view;auto observed=actual_view.step(local,Arm::active);assert(actual_view.repeating&&observed.task==Task::attack&&actual_view.wait_pad==Point(4,2));
 static_assert(p10_b::Physical::observe_open_tiles);
 // Damaging recurring work receives no imaginary healing or repeated full HP.
 auto danger=w;danger.groups.resize(1);danger.groups[0].monsters[0].hp=danger.groups[0].monsters[0].max_hp=140;danger.groups[0].monsters[0].strength=20;danger.groups[0].monsters[0].range=3;p10_b_orchard::BPolicy current;auto finite=current.step(danger,Arm::active);assert(!current.repeating&&finite.template_id==5055&&fresh_certificate(danger,finite));danger.self.bag=20;assert(current.step(danger,Arm::active).task==Task::bank);
}
static void cold_openings(){
 auto w=field();for(auto&g:w.groups){g.observed=g.complete=false;for(auto&m:g.monsters)m.observed=false;}
 p10_b_forge::BPolicy a;assert(a.step(w,Arm::active).task==Task::inspect);
 p10_b_foundry::BPolicy b;assert(b.step(w,Arm::active).task==Task::inspect);
 p10_b_spindle::BPolicy c;assert(c.step(w,Arm::active).task==Task::inspect);
 p10_b_metered::BPolicy d;auto batch=d.step(w,Arm::active);assert(batch.task==Task::inspect&&batch.group==51&&d.fresh_traces==0);
 p10_b_orchard::BPolicy e;auto field_=e.step(w,Arm::active);assert(field_.task==Task::inspect&&field_.group==50&&e.fresh_traces==0);
}
static int blocked_fuel(){return 40200;}
static int ordinary_fuel(){return 45000;}
static void caller_fuel_boundaries(){
 auto f=field();p10_b_orchard::BPolicy stopped;stopped.set_fuel_reader(blocked_fuel);auto no=stopped.step(f,Arm::active);assert(no.task==Task::wait&&stopped.work_aborted&&stopped.fresh_traces==0);
 // Receptive control logic still consumes real receipts under low headroom.
 auto paid=ordered();paid.own_payout=true;paid.last_payout_round=2;paid.self.channel=Channel::tribute;p10_b_spindle::BPolicy receipt;receipt.set_fuel_reader(blocked_fuel);receipt.step(paid,Arm::active);assert(receipt.last_receipt==2&&receipt.fresh_traces==0);
 p10_b_orchard::BPolicy ordinary;ordinary.set_fuel_reader(ordinary_fuel);auto yes=ordinary.step(f,Arm::active);assert(yes.task==Task::attack&&ordinary.fresh_traces<=2&&fresh_certificate(f,yes));
}
static const int*falling_phase=nullptr;static int falling_at=0,falling_after=0,phase_reads=0,total_reads=0;
static int falling_fuel(){++total_reads;if(falling_phase&&*falling_phase==falling_at&&++phase_reads>=falling_after)return 41000;return 100000;}
static void arm_falling(const int&phase,int at,int after){falling_phase=&phase;falling_at=at;falling_after=after;phase_reads=total_reads=0;}
static void live_postproof_fuel(){
 // Metered's participant route/exposure and service algebra used to run after
 // both physical traces without a live reader boundary. The actual caller
 // must refuse a partial organization, then restart from this next real frame.
 for(int at:{3,4}){reset();auto w=batching();p10_b_metered::BPolicy p;p.set_fuel_reader(falling_fuel);arm_falling(p.work_phase,at,at==3?12:1);auto d=p.step(w,Arm::active);assert(d.task==Task::wait&&!d.safety_verified&&p.work_aborted&&!p.chosen&&p.fresh_traces<=2&&total_reads<20000);++w.round;p.set_fuel_reader(ordinary_fuel);auto next=p.step(w,Arm::active);assert(!p.work_aborted&&p.chosen&&fresh_certificate(w,next)&&p.fresh_traces<=2);}
 // Orchard's nested neutral-pad / all-spawn L-route / full-world hazard work,
 // bounded calendar and post-proof threshold all use the injected live reader.
 for(int at:{3,4,5}){reset();auto w=field();p10_b_orchard::BPolicy p;p.set_fuel_reader(falling_fuel);arm_falling(p.work_phase,at,at==3?30:1);auto d=p.step(w,Arm::active);assert(d.task==Task::wait&&!d.safety_verified&&p.work_aborted&&p.fresh_traces<=2&&total_reads<20000);if(at<5)assert(p.source<0&&p.calendar_choices==0);++w.round;p.set_fuel_reader(ordinary_fuel);auto next=p.step(w,Arm::active);assert(!p.work_aborted&&fresh_certificate(w,next)&&p.fresh_traces<=2);}
 // Receipt processing and sole-holder custody precede the optional work guard.
 auto paid=field();paid.self.position={5,2};paid.self.bag=20;paid.own_payout=true;paid.last_payout_group=50;paid.last_payout_round=paid.round;clear(paid.groups[0]);paid.groups[0].cleared=false;paid.groups[0].respawning=true;paid.groups[0].respawn_remaining=10;p10_b_orchard::BPolicy tour;tour.source=50;tour.repeating=true;tour.bank_after=2;tour.wait_pad={2,2};tour.set_fuel_reader(falling_fuel);arm_falling(tour.work_phase,6,8);auto stopped=tour.step(paid,Arm::active);assert(stopped.task==Task::wait&&tour.work_aborted&&tour.completed==1&&tour.fresh_traces==0);
 auto custody=paid;custody.capture_active=true;custody.productive_group=50;tour.set_fuel_reader(blocked_fuel);assert(tour.step(custody,Arm::active).task==Task::capture);
 falling_phase=nullptr;std::puts("B04/B05 falling-fuel actual callers: guarded participant, graph, calendar, threshold, return; fresh restart PASS");
}
int main(){forge_callers();foundry_callers();spindle_callers();metered_callers();orchard_callers();cold_openings();caller_fuel_boundaries();live_postproof_fuel();std::puts("B01-05 actual callers: PASS; native fuel and strength unmeasured");}
