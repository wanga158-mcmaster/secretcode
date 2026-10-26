#include "../catalyst-depots/src/c-policy.h"
#include <cassert>
#include <cstdio>
#ifdef NDEBUG
#error Production reset-stock assertions must stay enabled.
#endif
using namespace p10;

static World world() {
 World w;w.round=10;w.action_reserve=35000;w.fuel=100000;
 w.self.id=1;w.self.type=Champion::marcella;w.self.position={1,1};
 w.self.visible=w.self.alive=w.self.ally=w.self.can_move=w.self.can_attack=w.self.can_cast=true;
 w.self.hp=w.self.max_hp=1000;w.self.strength=70;w.self.focus=100;w.self.range=1;
 w.spawn={0,1};return w;
}
static Group group(int id,Point p,double hp) {
 Group g;g.id=id;g.position=p;g.approach={1,1};g.spawns={p};g.approach_pads={{1,1}};
 g.observed=g.complete=g.living=g.route_known=g.program_available=true;
 g.travel=0;g.to_bank=g.to_spawn=2;g.gems=7;g.gold=40;g.respawn=50;g.hp=hp;
 Monster m;m.id=id*10;m.group=id;m.position=p;m.hp=m.max_hp=hp;m.observed=m.alive=true;
 g.monsters={m};return g;
}
static World supplied() {
 auto w=world();w.own_payout=true;w.groups={group(1,{2,1},120),group(2,{1,2},70)};
 Altar altar;altar.id=0;altar.position={7,1};altar.approach={6,1};
 altar.visible=altar.route_known=true;altar.travel=5;w.altars={altar};
 Actor customer=w.self;customer.id=2;customer.type=Champion::vendetta;
 customer.position={1,0};customer.bag=500;customer.cooldown[1]=6;
 customer.abilities[1].dash=true;customer.abilities[1].range=2;w.allies={customer};return w;
}

static void production_sequence() {
 auto w=supplied();p10_depots::CPolicy producer,control;
 const auto make=producer.step(w,Arm::active),ordinary=control.step(w,Arm::control);
 assert(make.task==Task::attack&&make.template_id==62&&make.monster==10&&make.cast_slot==-1);
 assert(make.safety_verified&&producer.price.worthwhile&&producer.producerJob==2);
 assert(ordinary.template_id!=62&&producer.depot<0);
 // A proposal neither changes HP nor establishes stock; only a later frame
 // with the production receipt and a living lethal survivor confirms it.
 ++w.round;w.own_payout=false;w.groups[0].monsters[0].hp=w.groups[0].hp=50;
 w.receipts={{Receipt::Kind::attack,w.round-1,1,w.self.id,0,0,0,70}};
 const auto next=producer.step(w,Arm::active);
 assert(producer.depot==1&&producer.producerJob==2&&next.group==2&&next.template_id!=62);
 auto carrier=w;carrier.self=w.allies[0];carrier.allies.clear();carrier.receipts.clear();
 p10_depots::CPolicy consumer,bankControl;
 const auto use=consumer.step(carrier,Arm::active),bank=bankControl.step(carrier,Arm::control);
 assert(use.task==Task::attack&&use.template_id==61&&use.monster==10&&use.cast_slot==1);
 assert(use.destination==carrier.altars[0].approach&&consumer.confirmedReset<0);
 assert(bank.task==Task::bank&&bank.cast_slot==-1);
 // This intentionally shows the current bookkeeping limit: attack+death is
 // enough for confirmedReset even if no dash acknowledgment is present.
 // A native observer must NOT promote that internal field to reset-use proof.
 ++carrier.round;carrier.groups[0].monsters[0].alive=false;carrier.groups[0].monsters[0].hp=0;
 carrier.groups[0].living=false;carrier.groups[0].cleared=true;carrier.groups[0].hp=0;
 carrier.self.channel=Channel::tribute;
 carrier.receipts={{Receipt::Kind::attack,carrier.round-1,1,carrier.self.id,0,0,0,50}};
 consumer.step(carrier,Arm::active);
 assert(consumer.confirmedReset==carrier.round-1&&carrier.self.cooldown[1]==6);
 carrier.self.channel=Channel::none;carrier.self.bag=0;carrier.own_bank_completed=true;
 const auto liquidate=consumer.step(carrier,Arm::active);
 assert(liquidate.task==Task::farm&&liquidate.group==1&&consumer.bankReceipt==carrier.round);
 std::puts("PASS C06 production supplier -> stock receipt -> independent job; loaded consumer/control -> kill intent -> bank/liquidation; internal confirmedReset is NOT native dash proof");
}

static void refusal_and_recovery() {
 for(int variant=0;variant<7;++variant) {
  auto w=supplied();
  if(variant==0)w.allies[0].bag=28; // Strictly greater than twice 7+7.
  if(variant==1)w.own_payout=false;
  if(variant==2)w.allies[0].cooldown[1]=1;
  if(variant==3)w.allies[0].visible=false;
  if(variant==4)w.groups[0].monsters[0].strength=10;
  if(variant==5)w.observation_complete=false;
  if(variant==6)w.capture_active=true;
  p10_depots::CPolicy p;const auto d=p.step(w,Arm::active);assert(d.template_id!=62);
 }
 auto w=supplied();p10_depots::CPolicy p;p.step(w,Arm::active);
 ++w.round;w.own_payout=false;w.groups[0].monsters[0].hp=50;
 p.step(w,Arm::active);assert(p.depot<0); // Missing actual attack receipt.
 auto carrier=supplied();carrier.self=carrier.allies[0];carrier.allies.clear();carrier.own_payout=false;
 carrier.groups[0].monsters[0].hp=carrier.groups[0].hp=50;
 for(int variant=0;variant<5;++variant) {
  auto no=carrier;
  if(variant==0)no.self.cooldown[1]=0;
  if(variant==1)no.self.bag=0;
  if(variant==2)no.self.can_cast=false;
  if(variant==3)no.groups[0].monsters[0].hp=no.groups[0].monsters[0].max_hp;
  if(variant==4){no.groups[1].monsters[0].position={1,1};no.groups[1].monsters[0].hp=1;}
  p10_depots::CPolicy refuse;assert(refuse.step(no,Arm::active).template_id!=61);
 }
 std::puts("PASS C06 production refusals: priced bag threshold, own income, cooldown, visibility, harmlessness, observation, custody, missing receipt, ready dash, empty bag, cast unavailable, untouched stock, substitute");
}
#if P10_DEPOT_CALLER_TRACE
static void caller_receipts() {
 auto w=world();w.self.type=Champion::vendetta;w.self.bag=7;w.self.cooldown[1]=0;
 Altar unknown;unknown.id=0;unknown.position={1,2};unknown.travel=0;
 Altar locked=unknown;locked.id=1;locked.route_known=true;locked.enemy_lock=true;
 Altar ready=unknown;ready.id=2;ready.position={17,29};ready.approach={16,28};ready.route_known=true;ready.travel=4;
 w.altars={unknown,locked,ready};p10_depots::CPolicy p;
 auto d=p.step(w,Arm::active);assert(d.task==Task::bank&&p.caller_c_path==2&&p.caller_b_entered==1&&p.caller_b_bank==2);
 w.capture_active=true;d=p.step(w,Arm::active);assert(d.task==Task::wait&&p.caller_c_path==1&&p.caller_b_entered==0&&p.caller_b_bank==-1);
 w.capture_active=false;w.self.channel=Channel::tribute;d=p.step(w,Arm::active);assert(d.task==Task::wait&&p.caller_c_path==1&&p.caller_b_entered==0);
 w.self.channel=Channel::none;d=p.step(w,Arm::control);assert(d.task==Task::bank&&p.caller_c_path==3&&p.caller_b_entered==0);
 w.altars.clear();d=p.step(w,Arm::active);assert(d.task==Task::bank&&p.caller_b_entered==1&&p.caller_b_bank==-1);
 for(int i=0;i<40;++i)p.step(w,Arm::active);
 assert(p.caller_loaded==3);w.self.bag=0;w.capture_active=true;
 for(int i=0;i<10;++i)p.step(w,Arm::active);
 assert(p.caller_loaded==3);p10_depots::CPolicy low;w.self.bag=7;w.fuel=64999;
 low.step(w,Arm::active);assert(low.caller_seen==1&&low.caller_skipped==1&&low.caller_loaded==0);
 w.fuel=65000;low.step(w,Arm::active);assert(low.caller_seen==2&&low.caller_skipped==1&&low.caller_loaded==1);
 std::puts("PASS C06 actual caller tags, small-bag return, actual B bank candidate, protected/control/no-route cases and bounded diagnostics; no executor or engine effect simulated");
}
#endif
int main(){production_sequence();refusal_and_recovery();
#if P10_DEPOT_CALLER_TRACE
 caller_receipts();
#endif
}
