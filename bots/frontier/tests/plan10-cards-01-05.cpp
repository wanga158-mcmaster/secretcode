#include "../catalyst-forge/src/policy.h"
#include "../shield-foundry/src/policy.h"
#include "../two-spindle/src/policy.h"
#include "../metered-works/src/policy.h"
#include "../orchards/src/policy.h"
#include <cassert>
#include <cstdio>

static p10::World world(p10::Champion type=p10::Champion::varg){
 p10::World w;w.round=1;w.self.id=10;w.self.pick=0;w.self.type=type;w.self.position={0,0};w.self.visible=w.self.alive=w.self.ally=true;w.self.can_move=w.self.can_attack=w.self.can_cast=true;w.self.hp=w.self.max_hp=1000;w.self.strength=40;w.self.focus=100;w.self.strength_growth=5;w.self.focus_growth=10;w.self.range=1;w.to_spawn=0;w.spawn={0,0};
 p10::Group g;g.id=0;g.position=g.approach={4,0};g.spawns={{4,0},{4,1}};g.observed=g.complete=g.route_known=g.safe_solo=g.program_available=g.living=true;g.travel=4;g.to_bank=2;g.to_spawn=4;g.hp=120;g.clear_turns=3;g.gems=7;g.gold=40;g.respawn=50;
 p10::Monster m;m.id=20;m.group=0;m.position={4,0};m.observed=true;m.hp=m.max_hp=120;m.respawn=50;g.monsters={m};w.groups={g};
 p10::Altar a;a.id=0;a.position={2,0};a.approach={1,0};a.visible=a.route_known=true;a.travel=1;w.altars={a};return w;
}
static void forge_fixtures(){
 using namespace catalyst_forge;using Policy=catalyst_forge::LegacyPolicy;
 ForgeSuffix s;s.count=3;s.actors_count=2;s.monsters[0]={1,0,{0,0},80,20,3,false,true};s.monsters[1]={2,0,{1,0},120,0,3,false,true};s.monsters[2]={3,0,{1,1},120,0,3,false,true};
 s.actors[0]={10,Kit::varg,{0,-1},1000,40,100,1,false,{0,0}};s.actors[1]={11,Kit::vendetta,{2,1},800,70,100,1,false,{0,0}};
 s.step_count=5;s.steps[0]={0,0,Op::basic,0,{},HitSet{7,true}};s.steps[1]={0,1,Op::basic,0,{},HitSet{7,true}};s.steps[2]={1,1,Op::basic,1,{},HitSet{2,true}};s.steps[3]={1,1,Op::reset};s.steps[4]={1,1,Op::basic,2,{},HitSet{4,true}};
 const auto r=execute(s);assert(r.legal&&r.safe&&r.complete&&r.kills==3&&r.casts==1&&r.bank_round>r.clear_round&&r.renew_round<=96);
 auto illegal=s;illegal.steps[5]={1,1,Op::reset};illegal.step_count=6;assert(!execute(illegal).legal); // kill refresh cannot authorize a second cast
 auto hidden=s;hidden.collateral_complete=false;assert(!execute(hidden).legal);
 auto changed=s;changed.monsters[1].hp=119;assert(!same_premises(s,changed));
 ForgeSuffix stellar;stellar.count=stellar.actors_count=1;stellar.monsters[0]={1,0,{1,0},120,0,3,false,true};stellar.actors[0]={1,Kit::stellar,{0,0},750,30,85,3,true,{0,0}};stellar.step_count=1;stellar.steps[0]={0,0,Op::area,0,{1,0},HitSet{1,true}};
 assert(!execute(stellar).complete&&execute(stellar).remaining[0]>.9f);stellar.steps[1]={0,0,Op::basic,0,{},HitSet{1,true}};stellar.step_count=2;assert(execute(stellar).complete);
 auto outside=stellar;outside.steps[0].tile={3,3};assert(!execute(outside).legal);assert(personal_threshold_shards(120,119,12,30)==1&&personal_threshold_shards(120,119,12,0)==0);
 auto w=world();w.self.position={3,0};w.groups[0].monsters[0].hp=80;auto cluster=w.groups[0].monsters[0];cluster.id=21;cluster.position={4,1};cluster.strength=0;cluster.hp=80;w.groups[0].monsters.push_back(cluster);w.groups[0].hp=160;
 auto ally=w.self;ally.id=11;ally.pick=2;ally.type=p10::Champion::vendetta;ally.position={5,0};ally.strength=70;w.allies={ally};Policy active,control;auto a=active.step(w,p10::Arm::active),b=control.step(w,p10::Arm::control);assert(a.reserve_reset&&!b.reserve_reset);
 w.allies.clear();Policy negative;assert(!negative.step(w,p10::Arm::active).reserve_reset);
 w.allies={ally};w.groups[0].monsters[0].hp=40;w.groups[0].monsters[1].hp=40;assert(!active.step(w,p10::Arm::active).reserve_reset);
 // A gold-only bootstrap must enter a currently affordable physical gem
 // economy once real personal gear makes the complete clear safe.
 auto income=world();income.groups[0].gems=0;auto gold=income.groups[0].monsters[0];gold.id=21;gold.position={4,1};income.groups[0].monsters.push_back(gold);income.groups[0].hp=240;income.groups[0].clear_turns=6;
 auto gems=income.groups[0];gems.id=1;gems.position=gems.approach={10,10};gems.travel=10;gems.to_bank=10;gems.gems=39;gems.gold=40;gems.hp=1950;gems.incoming=20;gems.clear_turns=49;gems.safe_solo=false;gems.monsters.clear();gems.spawns.clear();
 const std::array<p10::Point,6>ring{{{9,9},{9,10},{9,11},{10,11},{11,11},{11,10}}};
 for(int i=0;i<6;++i){p10::Monster m;m.id=30+i;m.group=1;m.position=ring[i];m.observed=true;m.hp=m.max_hp=i==0?1350:120;m.strength=i==0?20:0;m.range=3;m.respawn=50;gems.monsters.push_back(m);gems.spawns.push_back(m.position);}income.groups.push_back(gems);
 Policy circuit;auto opening=circuit.step(income,p10::Arm::active);assert(opening.group==0);
 income.round=20;income.own_payout=true;income.last_payout_group=0;income.groups[0].respawning=true;income.self.position={4,0};income.own_gold=40;auto not_yet=circuit.step(income,p10::Arm::active);assert(not_yet.group!=1); // no anticipated gear grants admission
 income.own_payout=false;income.round=25;income.self.position=income.spawn;income.self.at_spawn=true;auto funded=circuit.step(income,p10::Arm::active);assert(funded.task==p10::Task::buy&&funded.item==p10::Item::monster_strength&&funded.buy_count==1);
 income.own_gold=10;income.self.strength=45;income.self.strength_shards=1;income.own_purchase=true;income.groups[1].safe_solo=true;income.groups[1].clear_turns=45;income.groups[0].respawning=false;
 auto conversion=circuit.step(income,p10::Arm::active);assert(conversion.group==1&&(conversion.task==p10::Task::farm||conversion.task==p10::Task::attack));income.own_purchase=false;
 income.round=80;income.self.at_spawn=false;income.self.position=ring[0];income.groups[1].cleared=true;income.groups[1].living=false;income.groups[1].hp=0;income.groups[1].incoming=0;income.groups[1].clear_turns=0;for(auto&m:income.groups[1].monsters)m.alive=false;
 assert(circuit.step(income,p10::Arm::active).task==p10::Task::capture);income.round=87;income.own_payout=true;income.self.bag=39;income.own_gold=50;income.groups[1].cleared=false;income.groups[1].respawning=true;assert(circuit.step(income,p10::Arm::active).task==p10::Task::bank);
 income.round=105;income.own_payout=false;income.own_bank_completed=true;income.self.bag=0;income.self.position=income.spawn;income.self.at_spawn=true;income.groups[1].respawning=false;income.groups[1].living=true;income.groups[1].hp=1950;income.groups[1].incoming=20;income.groups[1].clear_turns=45;for(auto&m:income.groups[1].monsters)m.alive=true;
 auto after_bank=circuit.step(income,p10::Arm::active);assert(after_bank.task==p10::Task::buy);income.own_bank_completed=false;income.own_gold=20;income.self.strength=50;income.self.strength_shards=2;auto renewal=circuit.step(income,p10::Arm::active);assert(renewal.group==1&&renewal.task==p10::Task::farm);
 auto unavailable=income;unavailable.groups[1].safe_solo=false;unavailable.self.at_spawn=false;unavailable.self.position={3,0};Policy gold_fallback;unavailable.own_payout=true;unavailable.last_payout_group=0;auto fallback=gold_fallback.step(unavailable,p10::Arm::active);assert(fallback.group==0); // unavailable gems do not erase useful ordinary income
}
static void foundry_fixtures(){
 using namespace shield_foundry;using Policy=shield_foundry::AlternatePolicy;
 Trajectory t;t.count=1;t.length=2;t.clearance=4;t.monsters[0]={{0,0},20,3,-1,true};t.bodies[0]={10,{1,0},100,100,0,100,40,-1,0,false,true};t.bodies[1]={11,{2,1},200,200,100,100,40,5,0,true,true};
 t.phases[0]={0,{2,0},true,false,false,false,0};t.phases[1]={1,{1,0},true,false,false,false,0};
 auto transfer=run(t,true);assert(transfer.admissible&&transfer.transfer_count==1&&transfer.hp[0]==100);assert(!run(t,false).admissible);
 auto occupied=t;occupied.phases[0].destination={2,1};assert(!run(occupied,true).admissible);
 auto tie=t;tie.length=1;assert(!run(tie,true).admissible);
 auto expiry=t;expiry.bodies[1].shield_until=0;assert(run(expiry,true).hp[1]<transfer.hp[1]);
 auto cast=t;cast.phases[0].shield=cast.phases[0].landing=true;assert(!run(cast,true).admissible);
 auto changed=t;changed.bodies[1].shield=0;assert(shield_foundry::changed(t,changed));
 assert(ruin_multiplier(t.bodies[1])==1);auto injury=t.bodies[1];injury.hp=100;assert(ruin_multiplier(injury)==1.25);injury.shield=10000;assert(ruin_multiplier(injury)==1.25);
 auto w=world();w.self.hp=85;w.self.max_hp=100;w.self.position={1,0};w.groups[0].position={0,0};w.groups[0].incoming=20;w.groups[0].clear_turns=4;w.groups[0].monsters[0].position={0,0};w.groups[0].monsters[0].strength=20;w.groups[0].monsters[0].range=3;w.groups[0].monsters[0].awake=true;auto partner=w.self;partner.id=11;partner.pick=1;partner.type=p10::Champion::heart;partner.hp=200;partner.max_hp=200;partner.shield=100;partner.shield_until=10;partner.position={2,1};w.allies={partner};Policy active,control;auto a=active.step(w,p10::Arm::active),b=control.step(w,p10::Arm::control);assert(a.task==p10::Task::move&&a.strict_tank&&b.task!=p10::Task::move);
 w.allies[0].can_move=false;Policy invalid;assert(invalid.step(w,p10::Arm::active).task!=p10::Task::move);
 auto approach=world();approach.self.position={3,0};approach.groups[0].position={0,0};approach.groups[0].incoming=20;approach.groups[0].monsters[0].position={0,0};approach.groups[0].monsters[0].strength=20;approach.groups[0].monsters[0].range=4;approach.groups[0].monsters[0].awake=true;Policy approaching;
 auto inward=approaching.step(approach,p10::Arm::active);assert(inward.task==p10::Task::farm&&!inward.strict_tank); // range4 exposure cannot strand a range1 worker
 approach.self.position={1,0};auto positioned=approaching.step(approach,p10::Arm::active);assert(positioned.task==p10::Task::farm&&positioned.strict_tank);
}
static void spindle_fixtures(){
 using namespace two_spindle;using Policy=two_spindle::AlternatePolicy;
 Workcell c;c.count=2;c.unclaimed=1;c.collector_present=true;c.second_travel=1;c.collector_return=1;
 for(int i=0;i<2;++i){auto&j=c.jobs[i];j.source=i;j.observed=true;j.clear_work=i?3:0;j.capture=5;j.reward=40;j.bank_travel=2;j.holder_id=11;j.clearer_id=10;j.spawn_count=2;j.renewal=50;}
 c.jobs[0].custodyMode=Custody::witnessed_overlap;c.jobs[0].holder_witnessed=c.jobs[0].free_successor_spawn=true;
 auto serial=schedule(c,Dispatch::serial),overlap=choose(c,true);assert(overlap.complete&&overlap.dispatch==Dispatch::overlap&&overlap.finish<serial.finish&&overlap.second_clear<overlap.banks[0]);assert(overlap.owners[0]==11&&overlap.owners[1]==10);
 auto late=c;late.collector_ready=120;assert(choose(late,true).dispatch==Dispatch::serial);
 auto one=c;one.jobs[0].spawn_count=1;assert(!can_release(one.jobs[0])&&choose(one,true).dispatch==Dispatch::serial);
 assert(capture_share(15,2,10,10)==8&&capture_share(15,2,11,10)==7);assert(!admit(c,11,10));c.unclaimed=2;assert(!admit(c,10,10));
 auto changed=c.jobs[0];changed.holder_witnessed=false;assert(two_spindle::changed(c.jobs[0],changed));
 auto delayed=c;delayed.jobs[0].occupation_delay=10;auto delay=schedule(delayed,Dispatch::serial);assert(delay.renewal[0]==delay.payout[0]+60); // occupied spawn is not early renewal
 auto sequential=one;sequential.jobs[0].custodyMode=Custody::conditional_sequence;sequential.jobs[0].reset_cost=5;sequential.jobs[0].missed_arrival_cost=2;assert(schedule(sequential,Dispatch::overlap).payout[0]>=13);
 auto w=world(p10::Champion::vendetta);auto next=w.groups[0];next.id=1;next.position={7,0};next.spawns={{7,0},{7,1}};next.travel=7;next.clear_turns=2;next.monsters[0].id=21;next.monsters[0].group=1;next.monsters[0].position={7,0};w.groups.push_back(next);Policy active,control;active.step(w,p10::Arm::active);control.step(w,p10::Arm::control);
 w.self.position={4,0};w.groups[0].travel=0;w.groups[1].travel=3;w.groups[0].cleared=true;w.groups[0].living=false;w.groups[0].clear_turns=0;auto collector=w.self;collector.id=11;collector.pick=1;collector.type=p10::Champion::marcella;collector.position={4,1};w.allies={collector};auto a=active.step(w,p10::Arm::active),b=control.step(w,p10::Arm::control);assert(a.task==p10::Task::farm&&a.group==1&&b.task==p10::Task::capture);
 w.groups[1].cleared=true;w.groups[1].living=false;w.self.position={7,0};w.allies[0].position={7,1};auto third=next;third.id=2;third.position={9,0};third.spawns={{9,0},{9,1}};third.monsters[0].id=22;third.monsters[0].group=2;third.monsters[0].position={9,0};w.groups.push_back(third);assert(active.step(w,p10::Arm::active).task==p10::Task::capture); // second admission was recorded before release
 w.groups[1].observed=false;auto resumed=active.step(w,p10::Arm::active);assert(resumed.task==p10::Task::capture&&resumed.group==0);w.groups[0].cleared=false;w.groups[0].living=true;auto productive=active.step(w,p10::Arm::active);assert(productive.task==p10::Task::farm&&productive.group==0); // reinspection adopts an observed unpaid job
}
static void metered_fixtures(){
 using namespace metered_works;using Policy=metered_works::AlternatePolicy;
 std::array<int,5>ids{11,10};assert(share(15,ids,2,10)==8&&share(15,ids,2,11)==7&&share(15,ids,2,12)==0);assert(tributeTicks(10)==1&&tributeTicks(11)==2);assert(tributeTicks(10,{.2,3,4})==5);
 Batch b;b.workers_count=2;b.party_verified=true;b.workers[0]={10,0,1,1,1,true,false};b.workers[1]={11,0,1,1,9,false,true};for(int i=0;i<2;++i)b.sources[i]={i,0,5,101,40,true,false,false,false,-1,-1};
 auto sole=plan(b,Program::sole),parallel=choose(b,true);assert(parallel.complete&&parallel.kind==Program::parallel&&parallel.finish<sole.finish);assert(parallel.shares[0][0]==51&&parallel.shares[0][1]==50);assert(parallel.tribute_gold[0]==2*parallel.banked[0]&&parallel.tribute_gold[1]==0);assert(parallel.banked[0]+parallel.banked[1]==212);assert(sole.banked[1]==9);
 auto tiny=b;tiny.sources[0].gems=tiny.sources[1].gems=1;tiny.workers[1].arrival=40;assert(choose(tiny,true).kind==Program::sole);
 auto changed=b;changed.workers_count=3;assert(invalidated(b,changed));changed=b;changed.locked=true;assert(!choose(changed,true).complete&&invalidated(b,changed));
 auto cross=b;cross.workers_count=1;cross.workers[0].bag=1;cross.real_seed=1;cross.seed_owner=10;cross.sources[1].adjacent_verified=cross.sources[1].phase_calibrated=true;cross.sources[1].payout_earliest=cross.sources[1].payout_latest=20;auto coalesced=plan(cross,Program::crossdock);assert(coalesced.complete&&coalesced.service_start[0]==19&&coalesced.service_end[0]==20&&coalesced.banked[0]==203);
 cross.sources[1].payout_latest=21;assert(!plan(cross,Program::crossdock).complete);cross.real_seed=0;assert(!plan(cross,Program::crossdock).complete);
 auto w=world(p10::Champion::marcella);w.self.position={4,0};w.self.bag=1;w.groups[0].gems=101;w.groups[0].cleared=true;w.groups[0].living=false;w.groups[0].clear_turns=0;auto next=w.groups[0];next.id=1;next.position={7,0};next.spawns={{7,0},{7,1}};next.cleared=false;next.living=true;next.clear_turns=3;next.monsters[0].id=21;next.monsters[0].group=1;next.monsters[0].position={7,0};w.groups.push_back(next);auto willow=w.self;willow.id=11;willow.pick=2;willow.type=p10::Champion::willow;willow.position={4,1};willow.bag=9;w.allies={willow};Policy active,control;
 auto first=active.step(w,p10::Arm::active),single=control.step(w,p10::Arm::control);assert(first.task==p10::Task::capture&&first.template_id==2&&single.template_id==1);
 w.round=10;w.own_payout=true;w.last_payout_group=0;w.self.bag=52;w.groups[0].cleared=false;w.groups[0].respawning=true;auto second=active.step(w,p10::Arm::active);assert(second.task==p10::Task::farm&&second.group==1); // first receipt does not reset the declared two-capture batch
 w.own_payout=false;w.self.position={7,0};w.allies[0].position={7,1};w.groups[1].cleared=true;w.groups[1].living=false;assert(active.step(w,p10::Arm::active).task==p10::Task::capture);
 w.round=20;w.own_payout=true;w.last_payout_group=1;w.self.bag=103;w.groups[1].cleared=false;w.groups[1].respawning=true;assert(active.step(w,p10::Arm::active).task==p10::Task::bank); // actual selected parallel organization converts both receipts
}
static void orchard_fixtures(){
 using namespace orchards;using Policy=orchards::AlternatePolicy;
 Tour fast;fast.count=1;fast.deadline=150;fast.altar_travel=1;fast.return_travel=1;fast.recovery=1;fast.sources[0]={0,0,1,5,20,0,1,-1,0,10,12,40,true,true,false};
 auto recurring=calendar(fast);assert(recurring.regenerative&&recurring.complete&&recurring.generations>=4&&recurring.banked>=48);
 auto mixed=fast;mixed.sources[0].longest_cooldown=50;assert(calendar(mixed).generations<recurring.generations);
 auto held=fast;held.sources[0].occupation_delay=25;assert(calendar(held).generations<recurring.generations);
 auto unavailable=fast;unavailable.sources[0].available=false;assert(!calendar(unavailable).regenerative&&invalidated(fast,unavailable));
 auto gear=fast;gear.buy=true;gear.gear={1,30,30,1,0,0,true};assert(calendar(gear).purchased);gear.gear.own_gold=29;assert(!calendar(gear).regenerative);
 auto two=fast;two.bank_after=2;assert(calendar(two).banked>0);auto final=fast;final.deadline=8;auto tail=calendar(final);assert(tail.banked==0&&tail.stranded==12); // capture without time for tribute receives no score
 auto injury=fast;injury.health_loss=1;assert(!calendar(injury).regenerative);injury.restored_health=1;assert(calendar(injury).regenerative);
 auto serviced=fast;serviced.sources[0].service_travel=10;serviced.sources[0].heal_ticks=3;serviced.health_loss=30;serviced.restored_health=30;assert(calendar(serviced).first_bank==recurring.first_bank+13); // source recovery cannot be free calendar capacity
 std::array<Tour,2>opening{fast,fast};opening[0].sources[0].opening=30;opening[0].sources[0].gems=20;opening[0].sources[0].longest_cooldown=100;assert(choose(opening,true)==1); // lower immediate source buys extra actual banked generations
 auto w=world(p10::Champion::tavi);w.groups[0].to_bank=30;Policy policy;auto first=policy.step(w,p10::Arm::active);assert(first.task==p10::Task::farm&&first.template_id==2);
 w.round=13;w.own_payout=true;w.last_payout_group=0;w.self.position={4,0};w.self.bag=7;w.groups[0].living=false;w.groups[0].respawning=true;w.groups[0].respawn_remaining=50;auto recover=policy.step(w,p10::Arm::active);assert(recover.task==p10::Task::move&&recover.destination==w.spawn);
 w.round=17;w.own_payout=false;w.self.position=w.spawn;w.self.at_spawn=true;w.groups[0].respawn_remaining=46;assert(policy.step(w,p10::Arm::active).task==p10::Task::wait); // retain first bag off-source for the planned second tour
 auto invalidated_policy=policy;auto contested=w;contested.groups[0].contested=true;assert(invalidated_policy.step(contested,p10::Arm::active).task==p10::Task::bank);
 w.round=63;w.groups[0].respawning=false;w.groups[0].living=true;w.groups[0].respawn_remaining=-1;assert(policy.step(w,p10::Arm::active).task==p10::Task::farm);
 w.round=67;w.self.at_spawn=false;w.self.position={4,0};w.groups[0].cleared=true;w.groups[0].living=false;assert(policy.step(w,p10::Arm::active).task==p10::Task::capture);
 w.round=73;w.own_payout=true;w.self.bag=14;w.groups[0].cleared=false;w.groups[0].respawning=true;assert(policy.step(w,p10::Arm::active).task==p10::Task::move);
 w.round=77;w.own_payout=false;w.self.position=w.spawn;w.self.at_spawn=true;assert(policy.step(w,p10::Arm::active).task==p10::Task::bank);
}
template<class Policy>static void cold_start(p10::Champion type){
 auto w=world(type);w.groups[0].hp=1350;w.groups[0].clear_turns=34;w.groups[0].incoming=20;w.groups[0].monsters[0].hp=w.groups[0].monsters[0].max_hp=1350;w.groups[0].monsters[0].strength=20;w.groups[0].monsters[0].range=3;Policy p;auto d=p.step(w,p10::Arm::active);assert(d.task==p10::Task::farm||d.task==p10::Task::attack||d.task==p10::Task::cast);
 w.self.position=w.groups[0].spawns[0];w.groups[0].cleared=true;w.groups[0].living=false;w.groups[0].hp=0;w.groups[0].clear_turns=0;w.groups[0].monsters[0].alive=false;w.round=5;d=p.step(w,p10::Arm::active);assert(d.task==p10::Task::capture);
 bool bank=false;for(int event=0;event<4&&!bank;++event){w.round=42+event*110;w.self.position=w.groups[0].spawns[0];w.self.at_spawn=false;w.groups[0].cleared=false;w.groups[0].respawning=true;w.groups[0].respawn_remaining=w.groups[0].respawn;w.own_payout=true;w.self.bag+=7;w.own_gold+=40;w.last_payout_group=0;w.last_payout_round=w.round;d=p.step(w,p10::Arm::active);w.own_payout=false;
  if(d.task==p10::Task::move&&d.destination==w.spawn){w.round+=w.groups[0].to_spawn;w.self.position=w.spawn;w.self.at_spawn=true;d=p.step(w,p10::Arm::active);}bank=d.task==p10::Task::bank;}
 assert(bank);w.round+=3;w.self.bag=0;w.own_bank_completed=true;w.self.position=w.spawn;w.self.at_spawn=true;w.groups[0].cleared=false;w.groups[0].respawning=false;w.groups[0].living=true;w.groups[0].hp=1350;w.groups[0].clear_turns=34;w.groups[0].monsters[0].alive=true;w.groups[0].monsters[0].hp=1350;d=p.step(w,p10::Arm::active);w.own_bank_completed=false;
 if(d.task!=p10::Task::buy){w.own_gold=150;d=p.step(w,p10::Arm::active);}assert(d.task==p10::Task::buy);w.own_gold-=30*d.buy_count;w.own_purchase=true;w.self.strength+=5*d.buy_count;w.self.strength_shards+=d.buy_count;w.groups[0].clear_turns=int(std::ceil(120/w.self.strength));d=p.step(w,p10::Arm::active);w.own_purchase=false;
 // A finite personal purchase sequence is followed by actual renewed work.
 for(int i=0;i<8&&d.task==p10::Task::buy;++i){w.own_gold=std::max(0,w.own_gold-30);++w.self.strength_shards;d=p.step(w,p10::Arm::active);}
 assert(d.task==p10::Task::farm||d.task==p10::Task::attack||d.task==p10::Task::cast||d.task==p10::Task::inspect);
}

static p10::World physical(p10::Champion type){
 auto w=world(type);w.self.position={3,1};auto&g=w.groups[0];g.approach={3,1};g.approach_pads={{3,0},{3,1},{5,0},{5,1}};g.travel=0;g.safe_solo=false;
 auto m=g.monsters[0];m.id=21;m.position={4,1};g.monsters.push_back(m);g.hp=240;g.clear_turns=200;
 for(int x=0;x<=10;++x)for(int y=-2;y<=4;++y)if(std::find(g.spawns.begin(),g.spawns.end(),p10::Point{x,y})==g.spawns.end())w.visible_open_tiles.push_back({x,y});
 return w;
}
static void first_wave_callers(){
 // Forge calls its literal positioned interpreter. Removing the actual crew
 // or collateral knowledge changes the admission, not a shared scalar rank.
 auto f=physical(p10::Champion::varg);auto&fg=f.groups[0];fg.monsters[0].hp=fg.monsters[0].max_hp=900;fg.monsters[0].strength=20;fg.monsters[0].range=3;
 fg.monsters[1].hp=80;auto extra=fg.monsters[1];extra.id=22;extra.position={5,2};fg.monsters.push_back(extra);fg.spawns.push_back(extra.position);fg.hp=1060;fg.incoming=20;
 auto v=f.self;v.id=11;v.pick=2;v.type=p10::Champion::vendetta;v.position={5,3};v.strength=70;f.allies={v};
 const auto program=catalyst_forge::compile(f,fg,0);assert(program.suffix.step_count>0&&program.result.legal&&program.result.safe&&program.result.complete);
 catalyst_forge::Policy forge,greedy;auto fd=forge.step(f,p10::Arm::active),gd=greedy.step(f,p10::Arm::control);
 assert(fd.template_id==10&&gd.template_id==11&&fd.group==0&&fd.monster!=gd.monster);assert(fd.task==p10::Task::attack||fd.task==p10::Task::cast);
 auto hidden=f;hidden.geometry_complete=false;assert(!catalyst_forge::compile(hidden,hidden.groups[0],0).result.complete);
 catalyst_forge::Policy forge_alt;forge_alt.variant=1;assert(forge_alt.step(f,p10::Arm::active).template_id<10);
 // A frontliner with a real cooldown gap cannot finish fixed. A legal physical
 // retreat transfers all future volleys to an actually visible partner.
 auto h=physical(p10::Champion::varg);h.self.position={1,0};h.self.hp=120;h.self.cooldown={7,7};auto&hg=h.groups[0];hg.position=hg.approach={0,0};hg.travel=0;hg.to_bank=2;hg.to_spawn=1;hg.monsters.resize(1);auto&hm=hg.monsters[0];hm.position={0,0};hm.hp=hm.max_hp=900;hm.strength=30;hm.range=3;hm.awake=true;hg.hp=900;hg.incoming=30;hg.gems=30;hg.spawns={{0,0}};
 auto heart=h.self;heart.id=11;heart.pick=1;heart.type=p10::Champion::heart;heart.position={1,1};heart.hp=heart.max_hp=1000;heart.strength=100;heart.cooldown={0,0};h.allies={heart};h.visible_open_tiles={{2,0},{2,-1},{2,1}};
 shield_foundry::Policy foundry,fixed;const auto hd=foundry.step(h,p10::Arm::active),hc=fixed.step(h,p10::Arm::control);assert(hd.task==p10::Task::move&&hd.template_id==42&&hc.task!=p10::Task::move);
 h.allies.clear();shield_foundry::Policy absent;assert(absent.step(h,p10::Arm::active).template_id!=42);
 // A collector receives the same immutable source appointment before it is
 // ready. Actual distinct-spawn custody then releases only one extra job.
 auto s=physical(p10::Champion::marcella);s.self.pick=1;two_spindle::Policy collector;auto appointment=collector.step(s,p10::Arm::active);assert(appointment.group==0&&appointment.template_id==10);
 s.self.type=p10::Champion::vendetta;s.self.pick=0;s.self.strength=70;auto second=s.groups[0];second.id=1;second.position={7,0};second.approach={6,1};second.approach_pads={{6,0},{6,1},{8,0},{8,1}};second.spawns={{7,0},{7,1}};second.travel=3;second.gems=7;for(int i=0;i<2;++i){second.monsters[i].id=30+i;second.monsters[i].group=1;second.monsters[i].position=second.spawns[i];}s.groups.push_back(second);
 two_spindle::Policy spindle,serial;spindle.step(s,p10::Arm::active);serial.step(s,p10::Arm::control);
 s.self.position={4,0};s.groups[0].cleared=true;s.groups[0].living=false;for(auto&m:s.groups[0].monsters)m.alive=false;auto c=s.self;c.id=11;c.pick=1;c.type=p10::Champion::marcella;c.position={4,1};s.allies={c};
 auto release=spindle.step(s,p10::Arm::active);assert(release.group==1&&release.template_id==12&&serial.step(s,p10::Arm::control).task==p10::Task::capture);
 auto single=s;single.groups[0].spawns.resize(1);two_spindle::Policy no_fake;assert(no_fake.step(single,p10::Arm::active).task==p10::Task::capture);
 // Recipient participation is planned while monsters are still alive. It is
 // conditional until both personally observed actors occupy actual spawns.
 auto m=physical(p10::Champion::marcella);m.self.pick=0;m.groups[0].gems=101;m.groups[0].clear_turns=6;auto ms=m.groups[0];ms.id=1;ms.position={7,0};ms.approach={6,1};ms.approach_pads={{6,0},{6,1}};ms.spawns={{7,0},{7,1}};ms.travel=3;for(int i=0;i<2;++i){ms.monsters[i].id=40+i;ms.monsters[i].group=1;ms.monsters[i].position=ms.spawns[i];}m.groups.push_back(ms);
 auto willow=m.self;willow.id=11;willow.pick=2;willow.type=p10::Champion::willow;willow.position={5,1};willow.bag=9;m.allies={willow};metered_works::Policy metered,sole;
 auto future=metered.step(m,p10::Arm::active),one=sole.step(m,p10::Arm::control);assert(future.template_id==21&&one.template_id==1);
 m.self.position={4,0};m.allies[0].position={4,1};m.groups[0].cleared=true;m.groups[0].living=false;assert(metered.step(m,p10::Arm::active).template_id==22);
 m.round=15;m.own_payout=true;m.last_payout_round=15;m.last_payout_group=0;m.self.bag=51;m.groups[0].cleared=false;m.groups[0].respawning=true;auto next=metered.step(m,p10::Arm::active);assert(next.task!=p10::Task::bank&&next.group==1);
 // A harmless generation never forces a spawn recovery. The alternative is
 // still independently selectable and retains its explicitly priced service.
 auto o=physical(p10::Champion::tavi);o.groups[0].clear_turns=6;o.groups[0].to_bank=30;orchards::Policy orchard;auto opening=orchard.step(o,p10::Arm::active);assert(opening.group==0);
 o.round=12;o.own_payout=true;o.last_payout_round=12;o.last_payout_group=0;o.self.position={4,0};o.self.bag=7;o.groups[0].living=false;o.groups[0].respawning=true;o.groups[0].respawn_remaining=50;
 auto field=orchard.step(o,p10::Arm::active);assert(field.task!=p10::Task::recover&&!(field.task==p10::Task::move&&field.destination==o.spawn));
 orchards::Policy old;old.variant=1;auto fresh=physical(p10::Champion::tavi);fresh.groups[0].clear_turns=6;fresh.groups[0].to_bank=30;fresh.groups[0].safe_solo=true;old.step(fresh,p10::Arm::active);fresh=o;const auto spawn_service=old.step(fresh,p10::Arm::active);assert(spawn_service.task==p10::Task::move&&spawn_service.destination==fresh.spawn);
 auto no_wallet=physical(p10::Champion::tavi);no_wallet.own_gold=29;orchards::Policy funded;assert(funded.step(no_wallet,p10::Arm::active).task!=p10::Task::buy);no_wallet.own_gold=30;assert(funded.step(no_wallet,p10::Arm::active).task==p10::Task::buy);
}
int main(){first_wave_callers();forge_fixtures();foundry_fixtures();spindle_fixtures();metered_fixtures();orchard_fixtures();cold_start<catalyst_forge::LegacyPolicy>(p10::Champion::varg);cold_start<shield_foundry::AlternatePolicy>(p10::Champion::varg);cold_start<two_spindle::AlternatePolicy>(p10::Champion::vendetta);cold_start<metered_works::AlternatePolicy>(p10::Champion::marcella);cold_start<orchards::AlternatePolicy>(p10::Champion::tavi);std::puts("Plan10 cards01-05: mechanisms, invalidation, cold-start/capture/purchase/bank/renewal passed");}
