#include "../catalyst-depots/src/c-policy.h"
#include "../envelope-draft/src/c-policy.h"
#include "../deferred-forks/src/c-policy.h"
#include "../exposure-works/src/c-policy.h"
#include "../initiative-works/src/c-policy.h"
#include <cassert>
#include <cstdio>
#ifdef NDEBUG
#error Actual caller assertions must stay enabled at O3.
#endif
using namespace p10;
static World world(Champion type){World w;w.round=10;w.self.id=1;w.self.pick=0;w.self.type=type;w.self.position={1,1};w.self.visible=w.self.alive=w.self.ally=w.self.can_move=w.self.can_attack=w.self.can_cast=true;w.self.hp=w.self.max_hp=1000;w.self.strength=70;w.self.focus=100;w.self.range=1;w.self.strength_growth=10;w.self.focus_growth=15;w.spawn={0,1};return w;}
static Group group(int id,Point p,double hp=70,int travel=0){Group g;g.id=id;g.position=p;g.approach={p.x-1,p.y};g.approach_pads={g.approach};g.spawns={p};g.travel=travel;g.to_bank=g.to_spawn=2;g.gold=40;g.gems=7;g.observed=g.complete=g.living=g.route_known=g.program_available=true;Monster m;m.id=id*10;m.group=id;m.position=p;m.observed=m.alive=true;m.hp=m.max_hp=hp;g.monsters={m};g.hp=hp;return g;}
static void received( World&w,Receipt::Kind k){w.receipts={{k,w.round,-1,w.self.id}};w.own_payout=k==Receipt::Kind::payout;w.own_purchase=k==Receipt::Kind::purchase;w.last_payout_round=w.own_payout?w.round:w.last_payout_round;}
template<class Policy>static void funded_recurrence(const char*name,Champion type,Item opening,Item established){
 auto w=world(type);w.self.at_spawn=true;w.groups={group(1,{2,1})};w.own_gold=330;received(w,Receipt::Kind::payout);
 Policy active,control;auto a=active.step(w,Arm::active),b=control.step(w,Arm::control);
 assert(a.task==Task::buy&&a.item==opening&&a.buy_count==5);assert(b.task==a.task&&b.item==a.item&&b.buy_count==a.buy_count);
 // A successful earlier receipt is not a terminal flag. These are current
 // observed inventories/stats, not an applied hypothetical directive.
 ++w.round;received(w,Receipt::Kind::purchase);w.self.inventory[int(Item::monster_strength)]=5;w.self.inventory[int(Item::monster_focus)]=5;w.self.inventory[int(Item::jungle_health)]=5;w.self.inventory[int(Item::jungle_focus)]=5;
 w.self.inventory[int(Item::strength)]=45;w.self.inventory[int(Item::focus)]=45;w.self.strength=600;w.self.focus=800;w.own_gold=180;
 a=active.step(w,Arm::active);b=control.step(w,Arm::control);assert(a.task==Task::buy&&a.item==established&&a.buy_count==5);assert(b.item==a.item&&b.buy_count==a.buy_count);
 ++w.round;w.self.inventory[int(a.item)]+=5;w.own_gold=60;received(w,Receipt::Kind::purchase);a=active.step(w,Arm::active);assert(a.task==Task::buy&&a.buy_count==2);
 // Empty personal wallet, or another actor's receipt, cannot pay.
 auto empty=world(type);empty.self.at_spawn=true;empty.own_gold=0;empty.groups=w.groups;empty.receipts={{Receipt::Kind::payout,empty.round,-1,999}};Policy poor;assert(poor.step(empty,Arm::active).task!=Task::buy);
 auto pending=w;pending.self.channel=Channel::tribute;pending.own_gold=330;assert(active.step(pending,Arm::active).task==Task::wait);
 auto returnW=w;returnW.self.at_spawn=false;returnW.self.type=Champion::marcella;returnW.own_gold=150;returnW.visible_open_tiles.clear();assert(active.step(returnW,Arm::active).task==Task::recover);
 std::printf("PASS C%s actual step: same-menu arms, >45-shard recurrence, affordable count, personal receipt, channel, funded return\n",name);
}
static void preserve_mechanisms(){
 auto w=world(Champion::marcella);w.groups={group(1,{2,1},120),group(2,{1,2})};w.groups[1].approach_pads={{1,1}};Altar bankA;bankA.id=0;bankA.position={7,1};bankA.approach={6,1};bankA.route_known=bankA.visible=true;bankA.travel=5;w.altars={bankA};w.own_payout=true;
 Actor customer=w.self;customer.id=2;customer.type=Champion::vendetta;customer.position={1,0};customer.bag=500;customer.cooldown[1]=6;w.allies={customer};p10_depots::CPolicy depot;auto d=depot.step(w,Arm::active);assert(d.template_id==62&&depot.price.worthwhile&&depot.fresh_trajectories==1);
 DraftContext draft;draft.picks=3;auto broad=group(1,{2,1});Monster far=broad.monsters[0];far.id=11;far.position={4,3};broad.monsters.push_back(far);broad.gems=200;draft.groups={broad};p10_envelope::CPolicy envelope,control;const auto a=envelope.draft(draft,Arm::active),b=control.draft(draft,Arm::control);assert(a.draft==b.draft&&envelope.selected==2&&control.selected==2);
 auto fw=world(Champion::tavi);fw.self.range=3;fw.groups={group(1,{5,2},75,3),group(2,{5,4},200,3)};fw.groups[0].approach={4,2};fw.groups[1].approach={4,3};fw.visible_open_tiles={{2,2}};fw.own_gold=150;fw.own_payout=true;p10_forks::CPolicy fork;auto prefix=fork.step(fw,Arm::active);assert(prefix.task==Task::move&&prefix.template_id==80);auto ids=fork.sources;++fw.round;fw.self.position={2,2};fw.visible_open_tiles.clear();auto capital=fork.step(fw,Arm::active);assert(capital.task==Task::buy&&fork.sources==ids);
 auto rw=world(Champion::ren);rw.self.strength=35;rw.groups={group(1,{2,1},15)};rw.groups[0].monsters[0].strength=10;rw.groups[0].monsters[0].range=3;Monster other=rw.groups[0].monsters[0];other.id=11;other.position={1,2};other.strength=0;other.hp=30;rw.groups[0].monsters.push_back(other);rw.groups[0].spawns.push_back(other.position);p10_exposure::CPolicy reaction;assert(reaction.step(rw,Arm::active).template_id==91);
 auto iw=world(Champion::vendetta);iw.self.pick=1;iw.self.priority=p10_initiative::priority(p10_initiative::Dispatch::completion,1);iw.groups={group(1,{2,1}),group(2,{1,5},70,4)};iw.groups[0].cleared=true;iw.groups[0].living=false;iw.groups[0].monsters[0].alive=false;iw.groups[0].monsters[0].hp=0;Actor holder=iw.self;holder.id=2;holder.pick=0;holder.type=Champion::marcella;holder.position={2,1};holder.priority=p10_initiative::priority(p10_initiative::Dispatch::loaded,0);iw.allies={holder};p10_initiative::CPolicy pipeline;assert(pipeline.step(iw,Arm::active).task==Task::release);
 std::puts("PASS C06-10 actual callers preserve loaded-return manufacture, selected-roster duties, common prefix, positive reaction, observed custody release");
}
static void permanent_refusal(){
 auto w=world(Champion::marcella);w.groups={group(1,{2,1},1000000),group(2,{1,2},70,1)};w.groups[1].approach={1,1};w.groups[1].approach_pads={{1,1}};
 p10_depots::BPolicy trapped;auto a=trapped.step(w,Arm::active);assert(a.task==Task::wait);++w.round;auto b=trapped.step(w,Arm::active);assert(b.task==Task::wait);
 p10_b_local::Turn alternative(w);auto productive=alternative.work(w.groups[1],"counterexample alternate",0);assert(productive.safety_verified&&productive.task==Task::attack);
 std::puts("KNOWN B refusal: unchanged nearest million-HP source returns wait in consecutive actual caller rounds while second current source has a productive certified attack");
}
int main(){
 funded_recurrence<p10_depots::CPolicy>("06",Champion::marcella,Item::monster_strength,Item::strength);
 funded_recurrence<p10_envelope::CPolicy>("07",Champion::heart,Item::monster_focus,Item::focus);
 funded_recurrence<p10_forks::CPolicy>("08",Champion::tavi,Item::monster_strength,Item::strength);
 funded_recurrence<p10_exposure::CPolicy>("09",Champion::ren,Item::monster_strength,Item::strength);
 funded_recurrence<p10_initiative::CPolicy>("10",Champion::marcella,Item::monster_strength,Item::strength);
 preserve_mechanisms();permanent_refusal();std::puts("PASS five C capital actual callers; no native launches or strength qualification");
}
