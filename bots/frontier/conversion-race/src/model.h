#pragma once
#include "../../common/src/contract.h"
#include <span>
namespace p10::race {
inline const Profile profile{"p10-conversion-race",{Champion::marcella,Champion::tavi,Champion::varg,Champion::vesper,Champion::willow}};
struct RaceEvent {int actor=-1,sourceShare=-1;Gems bag=0;int channelStart=0;std::array<int,2> completionInterval{};Gems score=0;bool conditional=false,interrupted=false;};
struct RivalScenario {std::array<RaceEvent,6> events{};int count=0,period=0;bool inventoryUnknown=true;};
struct TerminalFacts {Gems ownGold=0,enemyGold=0;int ownKills=0,enemyKills=0;bool goldKnown=false,killsKnown=false,ownTeamB=false;};
struct CampaignProgram {
 int id=0,regimes=1,ownCost=0,firstOwner=-1,helper=-1;std::array<RaceEvent,6> events{};int count=0,period=0,repeatFrom=0;
 Gems peakExposure=0;int renewal=0;bool feasible=false,stopPurchase=false,concentrateFutureOwner=false,needsSupport=false;
};
struct RaceKey {std::array<int,3> original{},sorted{};int latestWin=0;Gems minimumScore=0,peak=0;int firstBank=0,id=0;bool feasible=false;};
inline int finish(const CampaignProgram&p,Gems score,Gems target,int limit){
 int best=limit+1;if(score>=target)return 0;
 if(p.count<0||p.count>6||p.period<0)return best;
 int previous=-1;for(int i=0;i<p.count;++i){const auto&e=p.events[i];if(e.completionInterval[1]<previous)return best;previous=e.completionInterval[1];if(e.completionInterval[1]>limit||e.interrupted)continue;score+=e.bag;if(score>=target)return e.completionInterval[1];}
 if(!p.period)return best;Gems perCycle=0;for(int i=p.repeatFrom;i<p.count;++i)if(!p.events[i].interrupted)perCycle+=p.events[i].bag;if(perCycle<=0)return best;
 const Gems skip=(target-score-1)/perCycle;score+=skip*perCycle;
 for(int i=p.repeatFrom;i<p.count;++i){const auto&e=p.events[i];if(e.interrupted)continue;const std::int64_t when=e.completionInterval[1]+(skip+1)*p.period;score+=e.bag;if(score>=target)return when<=limit?int(when):best;}
 return best;
}
inline Gems secured(const CampaignProgram&p,Gems score,int endpoint){if(p.count<0||p.count>6||p.period<0)return score;
 for(int i=0;i<p.count;++i){const auto&e=p.events[i];if(e.interrupted||e.completionInterval[1]>endpoint)continue;const int n=p.period&&i>=p.repeatFrom?(endpoint-e.completionInterval[1])/p.period+1:1;score+=e.bag*n;}return score;
}
inline CampaignProgram rival_program(const RivalScenario&s){CampaignProgram p;p.feasible=true;p.events=s.events;p.count=s.count;p.period=s.period;return p;}
inline int terminal_tie(Gems own,Gems enemy,const TerminalFacts&f){if(own!=enemy)return own>enemy?1:-1;if(!f.goldKnown)return 0;if(f.ownGold!=f.enemyGold)return f.ownGold>f.enemyGold?1:-1;if(!f.killsKnown)return 0;if(f.ownKills!=f.enemyKills)return f.ownKills>f.enemyKills?1:-1;return f.ownTeamB?1:-1;}
inline RaceKey evaluate(const CampaignProgram&p,Gems ownScore,Gems enemyScore,Gems target,int remaining,const std::array<RivalScenario,3>&rivals,const TerminalFacts&facts){
 RaceKey k;k.feasible=p.feasible&&p.regimes<=3&&p.count<=6&&p.renewal<=remaining;k.id=p.id;k.peak=p.peakExposure;k.firstBank=remaining+1;k.minimumScore=std::numeric_limits<Gems>::max();if(!k.feasible)return k;
 for(int i=0;i<p.count;++i)if(!p.events[i].interrupted)k.firstBank=std::min(k.firstBank,p.events[i].completionInterval[1]);
 const int ours=finish(p,ownScore,target,remaining);
 for(int b=0;b<3;++b){const auto rp=rival_program(rivals[b]);const int theirs=finish(rp,enemyScore,target,remaining);int outcome=0;
  if(ours<=remaining&&ours<theirs)outcome=1;else if(theirs<=remaining&&theirs<ours)outcome=-1;
  else if(ours>remaining&&theirs>remaining&&!rivals[b].inventoryUnknown)outcome=terminal_tie(secured(p,ownScore,remaining),secured(rp,enemyScore,remaining),facts);
  k.original[b]=outcome;k.latestWin=std::max(k.latestWin,outcome==1?ours:remaining+1);
  k.minimumScore=std::min(k.minimumScore,secured(p,ownScore,std::min(remaining,std::min(ours,theirs))));
 }k.sorted=k.original;std::sort(k.sorted.begin(),k.sorted.end());return k;
}
inline bool better(const RaceKey&a,const RaceKey&b){if(a.feasible!=b.feasible)return a.feasible;if(!a.feasible)return false;if(a.sorted!=b.sorted)return a.sorted>b.sorted;
 if(a.sorted[0]==1&&a.latestWin!=b.latestWin)return a.latestWin<b.latestWin;if(a.minimumScore!=b.minimumScore)return a.minimumScore>b.minimumScore;if(a.peak!=b.peak)return a.peak<b.peak;if(a.firstBank!=b.firstBank)return a.firstBank<b.firstBank;return a.id<b.id;}
struct InterruptOption {int enemy=-1,arrival=0,cooldown=0;bool visible=false,stun=false,lethal=false,lock=false,legalPath=false;};
inline int campaign_clear(const Actor&a,const Group&g,double strength){
 int turns=0;double burst=0;const Ability*spell=nullptr;
 for(const auto&ab:a.abilities)if(ab.ready&&(ab.damage||ab.aoe)&&(!spell||ab.modifier>spell->modifier))spell=&ab;
 if(spell)burst=spell->modifier*a.focus*(a.monster_emblem?1.1:1.);
 bool used=false;Point center{};
 for(const auto&m:g.monsters)if(m.alive&&m.hp>0){double hp=m.hp;
  if(spell&&!used&&distance(g.approach,m.position)<=spell->range){center=m.position;used=true;}
  if(spell&&used&&((spell->aoe&&distance(center,m.position)<=spell->radius)||m.position==center))hp=std::max(0.,hp-burst);
  turns+=int(std::ceil(hp/std::max(1.,strength*(a.monster_emblem?1.1:1.))));
 }
 return std::max(1,turns);
}
inline bool campaign_available(const Actor&a,const Group&g){
 if(!usable(g)||g.travel<0||g.to_bank<0||g.monsters.size()>48)return false;
 double loss=0;const int turns=campaign_clear(a,g,a.strength);
 for(const auto&m:g.monsters)if(m.alive&&distance(g.approach,m.position)<=m.range)loss+=m.strength*(turns+2)*(a.jungle_emblem?.7:1.);
 return g.cleared||a.hp>loss+std::max(1.,a.max_hp*.2);
}
inline bool interrupts(const Actor&holder,int completion,std::span<const InterruptOption>options){for(const auto&o:options)if(o.visible&&o.legalPath&&std::max(o.arrival,o.cooldown)<completion&&(o.lock||o.lethal||(o.stun&&holder.type!=Champion::marcella)))return true;return false;}
} // namespace p10::race
