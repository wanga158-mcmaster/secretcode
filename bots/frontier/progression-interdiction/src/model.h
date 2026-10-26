#pragma once
#include "../../common/src/contract.h"
namespace p10::interdiction {
inline const Profile profile{"p10-progression-interdiction",{Champion::ren,Champion::vendetta,Champion::marcella,Champion::tavi,Champion::varg}};
enum class SubstituteKind {original_source,other_source,other_altar,shop_recall_respawn,cooldown_wait,other_worker,mobility};
struct Substitute {SubstituteKind kind=SubstituteKind::other_source;int source=-1,actor=-1,completion=0;bool feasible=false,unknown=false;};
struct ReplacementSource {int source=-1,producer=-1,clear=0,capture=0,bank=0,renewal=0,respawn=0,enemyReturn=0;Gems ownBank=0;bool observed=false,funded=false,solo=false,exitSafe=false;};
struct Capability {int actor=-1;Champion champion=Champion::other;Gems bag=0;double focus=0;bool visible=false,linked=false,productiveDependency=false;};
struct ReentryCampaign {
 Capability observedCapability{};bool favorableIntercept=false;int interceptionTurns=0,expiry=0,homeCompletion=0;
 std::array<ReplacementSource,3>replacementSources{};int sourceCount=0,producer=-1;
 std::array<Substitute,7>alternatives{};int alternativeCount=0;Gems lostHomeDeposits=0,homeBank=0;
 bool unknownWallet=true;int ownService=0;bool interceptionSucceeded=false;
};
// Cheap self-owned production admission. A lost interception never authorizes
// a source whose complete current-gear cycle is unsafe for its producer.
inline bool appointment_source(const Actor&a,const Group&g){
 if(!usable(g)||g.travel<0||g.to_bank<0||g.monsters.size()>48)return false;
 double work=0,damage=0;for(const auto&m:g.monsters)if(m.alive){
  double hp=m.hp;for(const auto&ab:a.abilities)if(ab.ready&&ab.damage&&ab.aoe)hp=std::max(0.,hp-ab.modifier*a.focus);
  work+=std::ceil(hp/std::max(1.,a.strength*(a.monster_emblem?1.1:1.)));if(distance(g.approach,m.position)<=m.range)damage+=m.strength;
 }
 return g.cleared||a.hp>damage*(work+2)*(a.jungle_emblem?.7:1.)+a.max_hp*.2;
}
struct Decision {bool intercept=false,acquire=false,unresolved=false;int source=-1,producer=-1,nominalReentry=0,adverseReentry=0;Gems ownSecured=0;int renewal=0;};
inline Decision select(const ReentryCampaign&c,Arm arm){Decision out;
 out.intercept=c.favorableIntercept&&c.observedCapability.visible&&c.interceptionTurns<=8&&c.interceptionTurns>0;
 if(!out.intercept||!c.observedCapability.productiveDependency||arm==Arm::control)return out;
 int bestAlternative=160;bool unresolved=c.unknownWallet;
 for(int i=0;i<std::min(7,c.alternativeCount);++i){const auto&a=c.alternatives[i];unresolved|=a.unknown;if(a.feasible)bestAlternative=std::min(bestAlternative,a.completion);}
 out.unresolved=unresolved;
 for(int i=0;i<std::min(3,c.sourceCount);++i){const auto&s=c.replacementSources[i];if(!s.observed||!s.funded||!s.solo||!s.exitSafe||s.producer!=c.producer||s.source<0||s.renewal>160||s.ownBank<=c.lostHomeDeposits)continue;
  // Early capture that renews exactly at re-entry is no interdiction. Occupying
  // after payout is never credited as a free perpetual blockade.
  const int respawnAt=s.capture+s.respawn;const int delayed=std::min(bestAlternative,std::max(s.enemyReturn,respawnAt));
  if(respawnAt<=s.enemyReturn||delayed<=c.homeCompletion||s.ownBank>=c.homeBank)continue;
  if(!out.acquire||delayed>out.nominalReentry||(delayed==out.nominalReentry&&s.ownBank>out.ownSecured)){
   out.acquire=true;out.source=s.source;out.producer=s.producer;out.nominalReentry=delayed;out.adverseReentry=unresolved?0:bestAlternative;out.ownSecured=s.ownBank;out.renewal=s.renewal;}
 }return out;
}
struct AttemptBudget {int used=0,start=-1,target=-1,lastRound=-1;bool active=false;
 bool begin(int round,int actor){if(active||used>=2)return false;++used;start=lastRound=round;target=actor;active=true;return true;}
 bool current(int round,bool visible,bool ownSafe){lastRound=round;if(active&&(!visible||!ownSafe||round-start>=8))active=false;return active;}
};
enum class Stage {home,intercept,replacement,hold,bank,renewal,complete};
struct Witness {Stage stage=Stage::home;int source=-1,producer=-1;bool payout=false,bank=false,renewed=false;int observedEnemyCompletion=-1;
 void observe(const World&w){if(source<0)return;if(w.own_payout&&w.last_payout_group==source){payout=true;stage=Stage::bank;}if(payout&&w.own_bank_completed){bank=true;stage=Stage::renewal;}if(bank&&w.productive_group>=0&&w.productive_group!=source){renewed=true;stage=Stage::complete;}}
};
} // namespace p10::interdiction
