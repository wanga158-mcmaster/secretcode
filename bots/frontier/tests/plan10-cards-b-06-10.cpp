#include "../catalyst-depots/src/b-policy.h"
#include "../envelope-draft/src/b-policy.h"
#include "../deferred-forks/src/b-policy.h"
#include "../exposure-works/src/b-policy.h"
#include "../initiative-works/src/b-policy.h"
#include <cassert>
#include <cstdio>
#ifdef NDEBUG
#error Assertions must remain enabled at O3.
#endif
using namespace p10;
static World world(Champion type){
 World w;w.round=10;w.action_reserve=35000;w.fuel=45000;w.self.id=1;w.self.pick=0;w.self.type=type;w.self.position={1,1};
 w.self.visible=w.self.alive=w.self.ally=w.self.can_move=w.self.can_attack=w.self.can_cast=true;
 w.self.hp=w.self.max_hp=1000;w.self.strength=70;w.self.focus=100;w.self.range=1;w.spawn={0,1};return w;
}
static Monster monster(int id,int group,Point position,double hp,double strength=0,int range=0){Monster m;m.id=id;m.group=group;m.position=position;m.hp=m.max_hp=hp;m.strength=strength;m.range=range;m.observed=m.alive=true;return m;}
static Group group(int id,std::vector<Monster>monsters,std::vector<Point>pads,int travel=0){
 Group g;g.id=id;g.monsters=std::move(monsters);g.approach_pads=std::move(pads);g.position=g.monsters[0].position;g.approach=g.approach_pads[0];g.observed=g.complete=g.living=g.route_known=g.program_available=true;
 g.travel=travel;g.to_bank=2;g.to_spawn=2;g.gems=7;g.gold=40;g.respawn=40;for(const auto&m:g.monsters){g.spawns.push_back(m.position);g.hp+=m.hp;g.incoming+=m.strength;}return g;
}
static Group square(int id=1){return group(id,{monster(100,id,{2,1},140,20,3),monster(101,id,{3,1},70),monster(102,id,{2,2},70),monster(103,id,{3,2},70)},{{1,1},{1,2}});}
static Group single(int id,Point position,std::vector<Point>pads,double hp=70,int travel=0){return group(id,{monster(id*10,id,position,hp)},std::move(pads),travel);}
static Altar altar(){Altar a;a.id=0;a.position={7,1};a.approach={6,1};a.route_known=a.visible=true;a.travel=5;return a;}
static void loaded_depots(){
 auto w=world(Champion::marcella);w.own_payout=true;w.groups={single(1,{2,1},{{1,1}},120),single(2,{1,2},{{1,1}})};w.altars={altar()};
 Actor customer=w.self;customer.id=2;customer.type=Champion::vendetta;customer.position={1,0};customer.bag=500;customer.cooldown[1]=6;customer.abilities[1].dash=true;customer.abilities[1].range=2;w.allies={customer};
 p10_depots::BPolicy supplier;const auto prepare=supplier.step(w,Arm::active);assert(prepare.task==Task::attack&&prepare.monster==10&&prepare.reserve_reset&&prepare.cast_slot==-1);
 assert(supplier.price.foregoneGems==14&&supplier.price.foregoneGold==80&&supplier.price.worthwhile&&supplier.depot<0);assert(supplier.fresh_trajectories==1);
 ++w.round;w.own_payout=false;w.groups[0].monsters[0].hp=50;w.receipts={{Receipt::Kind::attack,w.round-1,1,w.self.id,0,0,0,70}};
 auto independent=supplier.step(w,Arm::active);assert(supplier.depot==1&&independent.group==2&&supplier.producerJob==2&&supplier.fresh_trajectories<=2);
 auto loaded=w;loaded.self=customer;loaded.allies.clear();loaded.receipts.clear();p10_depots::BPolicy carrier;
 auto use=carrier.step(loaded,Arm::active);assert(use.task==Task::attack&&use.monster==10&&use.cast_slot==1&&carrier.confirmedReset<0);
 ++loaded.round;loaded.groups[0].monsters[0].alive=false;loaded.groups[0].monsters[0].hp=0;loaded.groups[0].cleared=true;loaded.self.channel=Channel::tribute;
 loaded.receipts={{Receipt::Kind::attack,loaded.round-1,1,loaded.self.id,0,0,0,50}};carrier.step(loaded,Arm::active);assert(carrier.confirmedReset==loaded.round-1);
 loaded.self.channel=Channel::none;loaded.self.bag=0;loaded.own_bank_completed=true;auto liquidate=carrier.step(loaded,Arm::active);assert(liquidate.group==1&&liquidate.task==Task::farm&&carrier.bankReceipt==loaded.round);
 auto cheap=w;cheap.round=10;cheap.own_payout=true;cheap.groups[0].monsters[0].hp=120;cheap.allies[0].bag=7;cheap.receipts.clear();p10_depots::BPolicy priced;const auto no=priced.step(cheap,Arm::active);assert(!priced.price.worthwhile&&no.group==2);
 std::puts("PASS B06 fully priced loaded-return manufacture / observed reset-bank-liquidation / independent supplier");
}
static void complementary_envelope(){
 p10::DraftContext draft;draft.picks=3;auto broad=square();broad.gems=200;auto narrow=single(2,{5,1},{{4,1}});narrow.gems=7;draft.groups={broad,narrow};
 // A real3x3 footprint triggers the complementary ranged roster.
 broad.monsters.push_back(monster(104,1,{4,3},70));draft.groups[0]=broad;
 p10_envelope::BPolicy picked,fixed;assert(picked.draft(draft,Arm::active).draft[0]==Champion::tavi);assert(picked.selected==2);fixed.draft(draft,Arm::control);assert(fixed.selected==0);
 auto w=world(Champion::vendetta);w.groups={square(),single(2,{6,1},{{5,1}},70,4),single(3,{1,6},{{1,5}},70,4)};
 auto&reset=w.self.abilities[0];reset.reset=reset.ready=reset.targets_self=true;reset.max_cooldown=8;
 p10_envelope::BPolicy a,b;a.fastrestore({Champion::vendetta,Champion::heart,Champion::marcella});b.fastrestore({Champion::vendetta,Champion::heart,Champion::marcella});
 auto first=a.step(w,Arm::active);assert(first.group==1&&first.safety_verified&&a.fresh_trajectories==1);
 auto changed=w;changed.self.position={4,4};std::reverse(changed.groups.begin(),changed.groups.end());for(auto&g:changed.groups){g.travel=100-g.id;g.approach={g.position.x+1,g.position.y};}
 b.step(changed,Arm::active);assert(a.physicalSources==b.physicalSources&&a.physicalFallback==b.physicalFallback&&a.appointmentPad==b.appointmentPad);
 auto service=world(Champion::willow);service.self.range=3;service.groups={single(1,{2,1},{{1,1}}),single(2,{1,3},{{1,2}})};
 Actor lead=service.self;lead.id=2;lead.type=Champion::varg;lead.position={2,0};lead.hp=500;service.allies={lead};service.self.abilities[0].heal=service.self.abilities[0].ready=true;service.self.abilities[0].range=4;
 p10_envelope::BPolicy healer;healer.fastrestore({Champion::willow,Champion::varg,Champion::marcella});assert(healer.step(service,Arm::active).task==Task::heal);
 service.allies[0].hp=1000;auto free=healer.step(service,Arm::active);assert(free.task!=Task::wait&&free.group!=healer.appointmentGroup&&healer.fresh_trajectories<=2);
 auto lost=world(Champion::cassi);lost.groups={square()};p10_envelope::BPolicy linked;linked.fastrestore({Champion::cassi,Champion::willow,Champion::marcella,Champion::issac});assert(linked.step(lost,Arm::active).task==Task::recover&&linked.fresh_trajectories==0);
 std::puts("PASS B07 actual complementary/fixed rosters / immutable physical duties and fallback / mobile caller");
}
static void deferred_capital(){
 auto w=world(Champion::tavi);w.self.range=3;w.self.strength_growth=10;
 auto a=single(1,{5,2},{{4,1},{4,2}},75,3);a.approach={4,2};auto b=single(2,{5,4},{{4,3},{4,4}},200,3);b.approach={4,3};w.groups={a,b};w.visible_open_tiles={{2,2},{3,2},{3,3}};
 p10_forks::BPolicy fork;auto prefix=fork.step(w,Arm::active);assert(prefix.template_id==80&&prefix.task==Task::move);const auto ids=fork.sources;const int created=fork.fork.createdRound;
 ++w.round;w.self.position={2,2};w.groups[0].travel=10;w.groups[1].travel=1;std::reverse(w.groups.begin(),w.groups.end());w.own_payout=true;w.own_gold=30;
 auto purchase=fork.step(w,Arm::active);assert(purchase.task==Task::buy&&fork.sources==ids&&fork.fork.createdRound==created&&fork.actualPurchase<0&&fork.committedSource<0&&fork.fresh_trajectories<=2);
 ++w.round;w.own_payout=false;w.own_gold=0;assert(fork.step(w,Arm::active).task==Task::wait);
 w.own_purchase=true;w.self.strength=80;w.self.channel=Channel::tribute;fork.step(w,Arm::active);assert(fork.actualPurchase==w.round);
 w.self.channel=Channel::none;w.own_purchase=false;auto actual=fork.step(w,Arm::active);assert(actual.safety_verified&&fork.committedSource==1&&fork.fresh_trajectories<=2);
 auto empty=w;empty.own_payout=true;empty.own_gold=0;empty.self.strength=70;p10_forks::BPolicy poor;assert(poor.step(empty,Arm::active).task!=Task::buy);
 std::puts("PASS B08 stable productive prefix / actual-wallet threshold / purchase receipt before mobile commitment");
}
static void paid_exposure(){
 auto w=world(Champion::ren);w.self.strength=35;w.groups={group(1,{monster(10,1,{2,1},15,10,3),monster(11,1,{1,2},30)},{{1,1}})};
 Actor other=w.self;other.id=3;other.type=Champion::stellar;other.position={0,2};w.allies={other};p10_exposure::BPolicy ren;
 auto work=ren.step(w,Arm::active);assert(work.monster==11&&work.template_id==91&&work.safety_verified&&ren.fresh_trajectories==1);
 auto tie=w;tie.allies[0].position={3,1};p10_exposure::BPolicy refused;assert(refused.step(tie,Arm::active).template_id!=91);
 auto shield=w;shield.self.shield=100;p10_exposure::BPolicy zero;assert(zero.step(shield,Arm::active).template_id!=91);
 auto healer=world(Champion::willow);healer.self.position={0,1};healer.self.range=3;healer.self.abilities[0].heal=healer.self.abilities[0].ready=true;healer.self.abilities[0].range=4;
 healer.groups={w.groups[0],single(2,{0,2},{{0,1}})};Actor collector=w.self;collector.id=2;collector.position={1,1};collector.hp=200;collector.bag=50;healer.allies={collector};p10_exposure::BPolicy service;
 auto paid=service.step(healer,Arm::active);assert(paid.task==Task::heal&&service.serviceTurns==1&&service.independentJob==2&&service.foregoneJobGems>0&&service.fresh_trajectories==1);
 healer.allies[0].hp=1000;healer.allies[0].bag=0;auto free=service.step(healer,Arm::active);assert(free.group==2&&free.task!=Task::heal&&service.fresh_trajectories<=2);
 healer.allies[0].position={2,1};for(auto&m:healer.groups[0].monsters){m.alive=false;m.hp=0;}healer.groups[0].cleared=true;healer.groups[0].living=false;
 assert(service.step(healer,Arm::active).task==Task::release&&service.serviceTurns==2);
 std::puts("PASS B09 positive strictly-nearest reaction / real paid healer loss / custody release to named production");
}
static void custody_pipeline(){
 auto w=world(Champion::vendetta);w.self.pick=1;w.groups={square(),single(2,{1,5},{{1,2},{1,3},{1,4}},70,4)};
 w.groups[0].cleared=true;w.groups[0].living=false;for(auto&m:w.groups[0].monsters){m.alive=false;m.hp=0;}
 Actor holder=w.self;holder.id=2;holder.pick=0;holder.type=Champion::marcella;holder.position={2,1};holder.priority=0;w.allies={holder};p10_initiative::BPolicy pipeline;
 auto issue=pipeline.step(w,Arm::active);assert(issue.task!=Task::release&&!pipeline.priorityAdopted&&issue.priority!=0);
 ++w.round;w.self.priority=p10_initiative::priority(p10_initiative::Dispatch::completion,1);w.allies[0].priority=p10_initiative::priority(p10_initiative::Dispatch::loaded,0);
 auto released=pipeline.step(w,Arm::active);assert(released.task==Task::release&&pipeline.observedAdoption==w.round);
 ++w.round;w.self.position={1,2};auto job=pipeline.step(w,Arm::active);assert(job.group==2&&job.safety_verified&&pipeline.fresh_trajectories==1);
 auto unsafe=w;unsafe.allies[0].hp=1;p10_initiative::BPolicy fail;assert(fail.step(unsafe,Arm::active).task!=Task::release);
 auto receipt=w;receipt.own_payout=receipt.own_purchase=true;receipt.self.channel=Channel::tribute;p10_initiative::BPolicy bookkeeping;assert(bookkeeping.step(receipt,Arm::active).task==Task::wait&&bookkeeping.income&&bookkeeping.gear);
 std::puts("PASS B10 observed priority adoption / safe actual custody / independent mobile worker before bank");
}
int main(){loaded_depots();std::fflush(stdout);complementary_envelope();std::fflush(stdout);paid_exposure();std::fflush(stdout);custody_pipeline();std::fflush(stdout);deferred_capital();std::puts("PASS five scratch BPolicy actual callers; no engines or native fuel qualification");}
