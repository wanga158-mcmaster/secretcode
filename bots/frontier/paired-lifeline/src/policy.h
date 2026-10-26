#pragma once
#include "model.h"
namespace p10::pair {
struct Policy {
 int variant=0;bool serviceCommitted=false;
 int contactLost=-1,source=-1,serviceStart=-1;bool payout=false,banked=false,renewed=false;Recipient proposal=Recipient::cassi;ServiceOrder service=ServiceOrder::parallel;
 Item fundedRecipe=Item::none;int fundingUntil=-1;
 Directive personal_purchase(const World&w){
  if(fundedRecipe==Item::none)return {};
  const int missing=std::max(0,5-w.self.inventory[int(fundedRecipe)]);
  if(!missing||w.round>fundingUntil){fundedRecipe=Item::none;return {};}
  // This stage needs only the owner's actual wallet and inventory. Returning
  // to shop must not lose the receipt-backed intent when its source disappears
  // from private vision. No future source reward authorizes this purchase.
  if(w.self.at_spawn&&w.own_gold>=w.shard_cost)return buy(fundedRecipe,1,"actual personal twin funds buy before source reinspection");
  if(!w.self.at_spawn&&w.self.bag==0&&w.safe_now&&w.own_gold>=missing*w.shard_cost)return {Task::recover};
  return {};
 }
 static bool current_fight_safe(const Actor&c,const Actor&i,const Group&g){
  if(!usable(g)||g.monsters.size()>48)return false;if(g.cleared)return true;
  bool harmless=true;for(const auto&m:g.monsters)if(m.alive&&m.hp>0)harmless&=m.strength==0;
  if(harmless)return c.hp>1&&i.hp>1&&(c.strength>0||i.strength>0);
  int work=0;double cVolley=0,iVolley=0;
  for(const auto&m:g.monsters)if(m.alive&&m.hp>0){const int dc=distance(c.position,m.position),di=distance(i.position,m.position);
   // A conservative basic-only complete work bound. Spells and resets receive
   // no speculative credit; neither actor moves during this certificate.
   const double damage=(dc<=c.range?c.strength:0)+(di<=i.range?i.strength:0);
   if(damage<=0)return false;work+=int(std::ceil(m.hp/damage))+1;
   if(dc<=m.range&&(di>m.range||dc<=di))cVolley+=m.strength;
   if(di<=m.range&&(dc>m.range||di<=dc))iVolley+=m.strength;
  }
  if(work>96)return false;
  const double cLoss=cVolley*(work+2)*(c.jungle_emblem?.7:1.),iLoss=iVolley*(work+2)*(i.jungle_emblem?.7:1.);
  return c.hp-cLoss>std::max(1.,c.max_hp*.15)&&i.hp-iLoss>std::max(1.,i.max_hp*.15);
 }
 static Recipient proposed_recipient(const Actor&cassi,const Actor&issac,const Group&g,Arm arm){
  if(arm==Arm::control)return Recipient::cassi;
  // This is the declared protection-first funding rule for the next circuit,
  // not an inference about either wallet. The same public gear and named
  // source produce the same proposal in both physical instances. A conservative
  // sixteen-phase front stance remains the same while an owner visits shop or
  // the current generation is cleared; it grants no permission to fight now.
  double pressure=0;for(const auto&m:g.monsters)if(m.max_hp>0&&m.strength>0&&distance(g.approach,m.position)<=m.range)pressure+=16*m.strength;
  const bool protectedIssac=issac.inventory[int(Item::jungle_health)]>=5;
  const double currentPressure=pressure*(issac.jungle_emblem?.7:1.);
  const bool protectionLimited=!protectedIssac&&currentPressure>issac.max_hp*.5;
  bool burstLimited=false;const double burst=2*cassi.focus*(cassi.monster_emblem?1.1:1.);
  const int missing=std::max(0,5-cassi.inventory[int(Item::monster_focus)]);
  const double fundedBurst=2*(cassi.focus+missing*cassi.focus_growth)*1.1;
  for(const auto&m:g.monsters)burstLimited|=m.max_hp>burst&&m.max_hp<=fundedBurst;
  // An already safe Issac leaves a genuine caster threshold to Cassi. When
  // both limits exist, protect the linked body before adding optional damage.
  if(protectionLimited&&(!burstLimited||currentPressure>=issac.max_hp*.8))return Recipient::issac;
  return Recipient::cassi;
 }
 static PairSchedule proposed_schedule(const PairService&in,Recipient recipient){
  PairSchedule best;for(auto order:{ServiceOrder::parallel,ServiceOrder::cassi_first,ServiceOrder::issac_first}){auto candidate=schedule(in,recipient,order);if(!candidate.feasible)continue;
   if(!best.feasible||candidate.renewal<best.renewal)best=candidate;
  }return best;
 }
 Directive service_step(const World&w,const Actor&partner){
  const Actor&c=w.self.type==Champion::cassi?w.self:partner;const Actor&i=w.self.type==Champion::issac?w.self:partner;
  if(serviceStart<0){serviceStart=w.round;bool parallelSafe=c.hp>c.max_hp*.4&&i.hp>i.max_hp*.4;
   for(const auto&e:w.enemies)if(e.visible&&e.alive&&(distance(e.position,c.position)<=e.range+1||distance(e.position,i.position)<=e.range+1))parallelSafe=false;
   if(!serviceCommitted||variant){if(parallelSafe)service=ServiceOrder::parallel;else {const bool cassiFirst=c.hp/std::max(1.,c.max_hp)<i.hp/std::max(1.,i.max_hp)||(c.hp*i.max_hp==i.hp*c.max_hp&&c.bag>=i.bag);service=cassiFirst?ServiceOrder::cassi_first:ServiceOrder::issac_first;}}}
  const bool ownFirst=service==ServiceOrder::parallel||(service==ServiceOrder::cassi_first)==(w.self.type==Champion::cassi);
  if(w.self.bag>0){if(ownFirst||partner.bag==0||partner.channel==Channel::tribute||w.round-serviceStart>=4)return bank("executed twin bank order protects each actual bag");
   if(w.self.type==Champion::cassi&&w.self.abilities[1].ready)for(const auto&e:w.enemies)if(e.visible&&e.alive&&distance(w.self.position,e.position)<=w.self.abilities[1].range&&distance(partner.position,e.position)<=e.range+1){Directive d;d.task=Task::cast;d.cast_slot=1;d.actor=e.id;d.reason="staggered bank: actual Cassi CC covers first twin";return d;}
   return wait("bounded stagger: visible first twin converts before second loaded body");}
  if(w.self.hp<w.self.max_hp*.95){return {Task::recover};}
  if(partner.bag>0)return wait("banked twin supplies bounded visible cover while other converts");
  serviceStart=-1;serviceCommitted=false;return Directive{};
 }
 Directive step(const World&w,Arm arm){
  if(w.own_payout&&source<0)source=w.last_payout_group;
  if(w.own_payout&&w.last_payout_group==source)payout=true;if(payout&&w.own_bank_completed)banked=true;if(banked&&w.productive_group>=0)renewed=true;
  if(w.self.channel!=Channel::none&&w.safe_now)return wait("preserve safe twin service");
  if(w.self.type!=Champion::cassi&&w.self.type!=Champion::issac)return ordinary(w);
  auto purchase=personal_purchase(w);if(purchase.task!=Task::independent)return purchase;
  const Actor*partner=nullptr;for(const auto&a:w.allies)if(a.visible&&a.alive&&((w.self.type==Champion::cassi&&a.type==Champion::issac)||(w.self.type==Champion::issac&&a.type==Champion::cassi))){partner=&a;break;}
  if(!partner){if(contactLost<0)contactLost=w.round;if(w.self.bag>0)return bank("protect own bag while twin contact is lost");if(!w.safe_now||w.round-contactLost>=4)return {Task::recover};Directive d;d.task=Task::move;d.destination=w.spawn;d.reason="bounded twin rally; no inferred partner arrival";return d;}
  contactLost=-1;if((variant&&!w.safe_now)||joint_death(w.self.hp,partner->hp))return {Task::recover};
  if(w.self.bag>0||serviceStart>=0){auto action=service_step(w,*partner);if(action.task!=Task::independent)return action;}
  PairService input;input.contact=true;input.exitsSafe=true;input.dependencyKnown=true;input.remainingBodySafe=true;
  input.cassi.actor=w.self.type==Champion::cassi?w.self:*partner;input.issac.actor=w.self.type==Champion::issac?w.self:*partner;
  input.cassi.ownWallet=w.self.type==Champion::cassi;input.issac.ownWallet=w.self.type==Champion::issac;
  input.cassi.actualGold=input.cassi.ownWallet?w.own_gold:-1;input.issac.actualGold=input.issac.ownWallet?w.own_gold:-1;
  input.cassi.focusMonsterPartial=std::min(5,input.cassi.actor.inventory[int(Item::monster_focus)]);input.issac.healthJunglePartial=std::min(5,input.issac.actor.inventory[int(Item::jungle_health)]);
  auto common_distance=[&](const Group&g){return std::max(distance(input.cassi.actor.position,g.position),distance(input.issac.actor.position,g.position));};
  auto before=[&](const Group&a,const Group&b){return common_distance(a)<common_distance(b)||(common_distance(a)==common_distance(b)&&a.id<b.id);};
  const Group*first=nullptr,*next=nullptr;for(const auto&g:w.groups)if(usable(g)&&g.to_bank>=0&&g.travel>=0){if(!first||before(g,*first)){next=first;first=&g;}else if(!next||before(g,*next))next=&g;}
  if(!first)return ordinary(w);if(!next)next=first;
  input.source=first->id;input.nextSource=next->id;input.receiptGold=int(first->gold);input.receiptGems=first->gems;input.travel=first->travel;input.hold=w.capture_turns+1;input.returnTravel=next->travel;
  input.exactCombatTurns=std::min(16,std::max(0,first->clear_turns));input.cassiIncoming=0;input.issacIncoming=0;
  // Exact currently positioned nearest assignments; a tie is charged to both
  // as an adverse alternative, never asserted to damage both simultaneously.
  for(const auto&m:first->monsters)if(m.alive){int c=distance(input.cassi.actor.position,m.position),i=distance(input.issac.actor.position,m.position);
   if(c<=m.range&&c<=i)input.cassiIncoming+=m.strength*input.exactCombatTurns;if(i<=m.range&&i<=c)input.issacIncoming+=m.strength*input.exactCombatTurns;}
  proposal=proposed_recipient(input.cassi.actor,input.issac.actor,*first,arm);source=first->id;
  input.cassiBankTravel=input.issacBankTravel=first->to_bank;input.cassiTicks=w.ticks(input.cassi.actor.bag+(proposal==Recipient::cassi?first->gems:0));input.issacTicks=w.ticks(input.issac.actor.bag+(proposal==Recipient::issac?first->gems:0));
  // Personal affordability can veto this instance's candidate, but cannot
  // change the publicly prescribed future recipient in the other instance.
  auto chosen=proposed_schedule(input,proposal);if(chosen.feasible){service=chosen.order;serviceCommitted=true;}
  const bool recipient=(proposal==Recipient::cassi)==(w.self.type==Champion::cassi);
  const Item recipe=w.self.type==Champion::issac?Item::jungle_health:Item::monster_focus;
  const int owned=w.self.inventory[int(recipe)];
  if(recipient&&owned<5){fundedRecipe=recipe;fundingUntil=w.round+160;auto action=personal_purchase(w);if(action.task!=Task::independent)return action;}
  if(!recipient&&first->cleared){
   bool partnerOnSpawn=false,selfOnSpawn=false;for(auto p:first->spawns){partnerOnSpawn|=p==partner->position;selfOnSpawn|=p==w.self.position;}
   if(partnerOnSpawn){const Item recipientRecipe=proposal==Recipient::issac?Item::jungle_health:Item::monster_focus;
    const bool observedThreshold=partner->inventory[int(recipientRecipe)]>=5&&partner->channel==Channel::none;
    if(observedThreshold&&next->id!=first->id&&next->safe_solo)return farm(next->id,"visible actual recipient gear and custody release safe independent twin work");
    Directive d;d.task=Task::release;d.group=first->id;d.reason="actual proposed recipient holds; helper vacates reward region through fresh legal pads";d.template_id=141;return d;}
   if(selfOnSpawn)return wait("sole current holder finishes; no invented twin succession");
   return wait("future recipient enters first; preserve distinct positions");
  }
  if(first->cleared)return farm(first->id,"actual sole twin custody receives the funded next-regime payout");
  const auto linked=variant?LinkedClear{}:linked_clear(input.cassi.actor,input.issac.actor,*first);
  if(linked.feasible||current_fight_safe(input.cassi.actor,input.issac.actor,*first)){
   auto d=farm(first->id,recipient?"current real twin gear clears before its own future receipt":"current coupled exit budget excludes unpurchased future capital");d.strict_tank=first->incoming>0;
   if(linked.feasible){const bool cassi=w.self.type==Champion::cassi;d.monster=cassi?linked.cassiTarget:linked.issacTarget;d.cast_slot=cassi?linked.cassiCast:linked.issacCast;d.reserve_reset=true;d.template_id=140;
    d.task=d.cast_slot>=0&&!w.self.abilities[d.cast_slot].reset?Task::cast:Task::attack;
    if(d.cast_slot>=0&&w.self.abilities[d.cast_slot].reset)d.second_monster=d.monster;
    bool external=false;for(const auto&e:w.enemies)external|=e.visible&&e.alive&&distance(e.position,w.self.position)<=8;
    for(const auto&g:w.groups)if(g.id!=first->id)for(const auto&m:g.monsters)if(m.alive&&m.strength>0&&(distance(m.position,input.cassi.actor.position)<=m.range||distance(m.position,input.issac.actor.position)<=m.range))external|=!g.observed||m.awake;
    if(!external&&w.geometry_complete&&w.observation_complete){d.safety_verified=true;d.safety_round=w.round;d.minimum_start_hp=w.self.hp;}
   }
   return d;}
  // A funded forecast for the NEXT circuit cannot finance today's exposure.
  // Approach a sleeping source without an attack, then recheck both bodies;
  // a positioned unsafe encounter must find different current work or recover.
  bool awake=false;for(const auto&m:first->monsters)awake|=m.alive&&m.awake;
  bool canWork=false;for(const auto&m:first->monsters)canWork|=m.alive&&distance(w.self.position,m.position)<=w.self.range;
  if(!awake&&!canWork){Point pad{};int bestDistance=1000000;for(auto candidate:first->approach_pads){if(candidate==partner->position||candidate==w.self.position)continue;
    const int d=distance(candidate,w.self.position);if(d<bestDistance){bestDistance=d;pad=candidate;}}
   if(pad.x>=0&&w.self.can_move){Directive d;d.task=Task::move;d.destination=pad;d.group=first->id;d.reason="unfunded twin approach reveals stance; no productive attack before current-gear admission";return d;}}
  for(const auto&other:w.groups)if(other.id!=first->id&&current_fight_safe(input.cassi.actor,input.issac.actor,other)){source=other.id;auto d=farm(other.id,"current-gear alternative preserves honest twin first income");d.strict_tank=other.incoming>0;return d;}
  return {Task::recover};
 }
};
} // namespace p10::pair
