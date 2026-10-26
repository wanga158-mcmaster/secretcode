#pragma once
#include "../../common/src/contract.h"
namespace p10::pair {
inline const Profile profile{"p10-paired-lifeline",{Champion::cassi,Champion::marcella,Champion::tavi,Champion::varg,Champion::willow}};
enum class Recipient {cassi,issac};enum class ServiceOrder {parallel,cassi_first,issac_first};
struct PersonalCapital {Actor actor{};int actualGold=-1;int healthJunglePartial=0,focusMonsterPartial=0;bool ownWallet=false;};
struct PairService {
 PersonalCapital cassi{},issac{};int source=-1,nextSource=-1,receiptGold=0;Gems receiptGems=0;
 double issacIncoming=0,cassiIncoming=0,requiredBurst=0;int exactCombatTurns=0,travel=0,hold=6,shopTravel=0,returnTravel=0;
 int cassiBankTravel=0,issacBankTravel=0,cassiTicks=1,issacTicks=1,recall=5,heal=1;
 bool contact=false,exitsSafe=false,dependencyKnown=false,remainingBodySafe=false,parallelSafe=true;
};
struct PairSchedule {
 Recipient recipient=Recipient::cassi;ServiceOrder order=ServiceOrder::parallel;
 int cassiCost=0,issacCost=0,cassiFutureGold=0,issacFutureGold=0;
 double cassiFocus=0,issacMaxHp=0,cassiExit=0,issacExit=0;
 Gems linkedBagLoss=0,banked=0;int linkedDowntime=0,renewal=0,cassiBankAt=0,issacBankAt=0;bool feasible=false;
};
inline PairSchedule schedule(const PairService&s,Recipient recipient,ServiceOrder order){
 PairSchedule p;p.recipient=recipient;p.order=order;if(!s.contact||!s.exitsSafe||!s.dependencyKnown||s.exactCombatTurns>16||s.exactCombatTurns<0)return p;
 const auto&c=s.cassi;const auto&i=s.issac;
 // Unknown partner money remains unavailable; only the future recipient's
 // witnessed payout may fund a recipe independently of that unknown wallet.
 p.cassiFutureGold=std::max(0,c.actualGold)+(recipient==Recipient::cassi?s.receiptGold:0);
 p.issacFutureGold=std::max(0,i.actualGold)+(recipient==Recipient::issac?s.receiptGold:0);
 const int cMissing=std::max(0,5-c.focusMonsterPartial),iMissing=std::max(0,5-i.healthJunglePartial);
 p.cassiCost=recipient==Recipient::cassi?30*cMissing:0;p.issacCost=recipient==Recipient::issac?30*iMissing:0;
 if(p.cassiCost>p.cassiFutureGold||p.issacCost>p.issacFutureGold)return p;
 p.cassiFocus=c.actor.focus+(p.cassiCost?cMissing*c.actor.focus_growth:0);
 p.issacMaxHp=i.actor.max_hp+(p.issacCost?iMissing*i.actor.health_growth:0);
 // Jungle protection belongs to Issac only and is applied once, never twice.
 const bool jungle=i.actor.jungle_emblem||p.issacCost>0;
 p.cassiExit=c.actor.hp-s.cassiIncoming;p.issacExit=i.actor.hp+(p.issacCost?iMissing*i.actor.health_growth:0)-s.issacIncoming*(jungle?.7:1.);
 p.linkedBagLoss=c.actor.bag+i.actor.bag+s.receiptGems;p.linkedDowntime=2*60+s.returnTravel*2;
 const int ready=s.travel+s.exactCombatTurns+s.hold;
 p.cassiBankAt=ready+s.cassiBankTravel+s.cassiTicks;p.issacBankAt=ready+s.issacBankTravel+s.issacTicks;
 if(order==ServiceOrder::cassi_first)p.issacBankAt=std::max(p.issacBankAt,p.cassiBankAt+s.recall+s.heal);
 if(order==ServiceOrder::issac_first)p.cassiBankAt=std::max(p.cassiBankAt,p.issacBankAt+s.recall+s.heal);
 if(order!=ServiceOrder::parallel&&!s.remainingBodySafe)return p;
 if(order==ServiceOrder::parallel&&!s.parallelSafe)return p;
 p.renewal=std::max(p.cassiBankAt,p.issacBankAt)+s.shopTravel+s.recall+s.heal+s.returnTravel;
 p.banked=p.linkedBagLoss;p.feasible=2*p.cassiFocus>=s.requiredBurst&&p.cassiExit>std::max(1.,c.actor.max_hp*.15)&&p.issacExit>std::max(1.,p.issacMaxHp*.15)&&p.renewal<=160&&s.nextSource>=0;
 return p;
}
inline PairSchedule select(const PairService&s,Arm arm){PairSchedule best;
 for(Recipient r:{Recipient::cassi,Recipient::issac})for(ServiceOrder order:{ServiceOrder::parallel,ServiceOrder::cassi_first,ServiceOrder::issac_first}){
  if(arm==Arm::control&&r!=Recipient::cassi)continue;auto p=schedule(s,r,order);if(!p.feasible)continue;
  const double minMargin=std::min(p.cassiExit,p.issacExit),bestMargin=std::min(best.cassiExit,best.issacExit);
  // Once both survive, protect the currently riskier loaded body before the
  // other body's recovery. Concurrent altar service itself has no capacity cap.
  int exposureTime=int(s.cassi.actor.bag)*p.cassiBankAt+int(s.issac.actor.bag)*p.issacBankAt;
  int bestExposure=int(s.cassi.actor.bag)*best.cassiBankAt+int(s.issac.actor.bag)*best.issacBankAt;
  if(!best.feasible||p.renewal<best.renewal||(p.renewal==best.renewal&&(minMargin>bestMargin||(minMargin==bestMargin&&exposureTime<bestExposure))))best=p;
 }
 return best;}
inline bool joint_death(double cassiHp,double issacHp){return cassiHp<=0||issacHp<=0;}
struct LinkedClear {bool feasible=false;int turns=0;double cassiHp=0,issacHp=0;int cassiTarget=-1,issacTarget=-1,cassiCast=-1,issacCast=-1;};
inline LinkedClear linked_clear(const Actor&c,const Actor&i,const Group&g){
 LinkedClear out;out.cassiHp=c.hp;out.issacHp=i.hp;
 if(!usable(g)||g.monsters.size()>48||!c.visible||!i.visible)return out;
 std::array<double,48>hp{};std::array<int,48>stun{};for(int j=0;j<int(g.monsters.size());++j){hp[j]=g.monsters[j].alive?g.monsters[j].hp:0;stun[j]=g.monsters[j].stun;}
 auto cc=c.cooldown,ic=i.cooldown;
 for(int turn=0;turn<16;++turn){bool remains=false;for(int j=0;j<int(g.monsters.size());++j)remains|=hp[j]>0;
  if(!remains){out.feasible=true;out.turns=turn;return out;}
  auto act=[&](const Actor&a,std::array<int,2>&cd,int&firstTarget,int&firstCast){
   int target=-1;for(int j=0;j<int(g.monsters.size());++j)if(hp[j]>0&&(target<0||hp[j]<hp[target])){bool reachable=distance(a.position,g.monsters[j].position)<=a.range;for(const auto&ab:a.abilities)reachable|=(ab.damage||ab.cc)&&distance(a.position,g.monsters[j].position)<=ab.range;if(reachable)target=j;}
   if(target<0)return;const auto&m=g.monsters[target];if(turn==0)firstTarget=m.id;
   const double mult=a.monster_emblem?1.1:1.;bool cast=false;
   for(int slot=0;slot<2&&!cast;++slot){const auto&ab=a.abilities[slot];if(cd[slot]>0||(!a.can_cast&&turn==0)||!(ab.damage||ab.cc)||distance(a.position,m.position)>ab.range)continue;
    for(int j=0;j<int(g.monsters.size());++j)if(hp[j]>0&&(j==target||(ab.aoe&&distance(m.position,g.monsters[j].position)<=ab.radius))){hp[j]-=ab.modifier*a.focus*mult;if(ab.cc)stun[j]=std::max(0,ab.duration-1);}
    cd[slot]=ab.max_cooldown;cast=true;if(turn==0)firstCast=slot;
   }
   if(hp[target]>0&&distance(a.position,m.position)<=a.range&&(a.can_attack||turn>0))hp[target]-=a.strength*mult;
   if(!cast)for(int slot=0;slot<2;++slot)if(cd[slot]<=0&&a.abilities[slot].reset&&hp[target]>0&&distance(a.position,m.position)<=a.range){hp[target]-=a.strength*mult;cd[slot]=a.abilities[slot].max_cooldown;if(turn==0)firstCast=slot;break;}
  };
  // Draft order is public and fixed inside the pair. Both bodies' prefixes
  // are recomputed in their own fresh instance before execution.
  act(c,cc,out.cassiTarget,out.cassiCast);act(i,ic,out.issacTarget,out.issacCast);
  for(int j=0;j<int(g.monsters.size());++j)if(hp[j]>0){if(stun[j]>0){--stun[j];continue;}const auto&m=g.monsters[j];int dc=distance(c.position,m.position),di=distance(i.position,m.position);
   if(dc<=m.range&&dc<=di)out.cassiHp-=m.strength*(c.jungle_emblem?.7:1.);
   if(di<=m.range&&di<=dc)out.issacHp-=m.strength*(i.jungle_emblem?.7:1.);
  }
  if(out.cassiHp<=std::max(1.,c.max_hp*.15)||out.issacHp<=std::max(1.,i.max_hp*.15))return out;
  for(int&v:cc)v=std::max(0,v-1);for(int&v:ic)v=std::max(0,v-1);
 }return out;
}
} // namespace p10::pair
