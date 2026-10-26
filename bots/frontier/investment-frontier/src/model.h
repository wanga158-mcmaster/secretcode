#pragma once
#include "../../common/src/contract.h"

namespace p10::investment {
inline const Profile profile{"p10-investment-frontier",{Champion::ryth,Champion::ren,Champion::stellar,Champion::marcella,Champion::tavi}};
struct Counts {int h=0,s=0,f=0;int total()const{return h+s+f;} bool operator==(const Counts&)const=default;};
enum class Family {monster,jungle};
struct InvestmentCertificate {
 int ownCost=0;Counts itemCounts{};Family family=Family::monster;bool exactPassive=false;
 int changedProgram=-1,ownerAfter=-1,releasedWorker=-1,releasedSource=-1;
 double health=0,strength=0,focus=0;int detour=0,clearTurns=0,bankAt=0,renewAt=0;
 bool feasible=false,approximation=false;
};
struct ThresholdProgram {
 int source=-1,owner=-1,helper=-1,independentSource=-1;
 double requiredHealth=0,requiredStrength=0,requiredFocus=0;
 int approach=0,clear=0,hold=6,bankTravel=0,tribute=1,renewal=0,shopDetour=0;
 bool observed=false,helperVisible=false,independentFeasible=false,exposureKnown=true;
};
inline std::array<Counts,56> triples(){std::array<Counts,56>a{};int i=0;for(int h=0;h<=5;++h)for(int s=0;s<=5-h;++s)for(int f=0;f<=5-h-s;++f)a[i++]={h,s,f};return a;}
inline bool common_bundle(Counts c){return (c.h==0&&c.f==0) || (c.s==0&&c.f==0) || (c.s==0&&c.h==0) || (c.h==3&&c.s==2&&c.f==0) || (c.h==0&&c.s==3&&c.f==2) || (c.h==3&&c.s==0&&c.f==2);}
inline bool dominates(const InvestmentCertificate&a,const InvestmentCertificate&b){
 return a.family==b.family&&a.exactPassive==b.exactPassive&&a.ownCost<=b.ownCost&&a.detour<=b.detour&&a.health>=b.health&&a.strength>=b.strength&&a.focus>=b.focus&&
 (a.ownCost<b.ownCost||a.detour<b.detour||a.health>b.health||a.strength>b.strength||a.focus>b.focus);
}
inline std::vector<InvestmentCertificate> catalogue(const Actor&a,int gold,int shardCost,Counts monsterPartial={},Counts junglePartial={}){
 std::vector<InvestmentCertificate> out;out.reserve(112);
 for(Family family:{Family::monster,Family::jungle})for(auto c:triples()){
  if(c.total()*shardCost>gold)continue;
  InvestmentCertificate x;x.itemCounts=c;x.family=family;x.ownCost=c.total()*shardCost;
  // A family completes per matching stat variant, never by pooling mixed stats.
  const Counts partial=family==Family::monster?monsterPartial:junglePartial;
  x.exactPassive=(family==Family::monster?a.monster_emblem:a.jungle_emblem)||std::max({c.h+partial.h,c.s+partial.s,c.f+partial.f})>=5;
  x.health=a.max_hp+c.h*a.health_growth;x.strength=a.strength+c.s*a.strength_growth;
  if(a.type==Champion::ryth)x.strength+=c.h*a.health_growth*.01;
  x.focus=a.focus+c.f*a.focus_growth;out.push_back(x);
 }
 return out;
}
inline InvestmentCertificate certify(InvestmentCertificate x,const ThresholdProgram&p){
 x.changedProgram=p.source;x.ownerAfter=p.owner;x.releasedWorker=p.helper;x.releasedSource=p.independentSource;x.detour=p.shopDetour;
 x.clearTurns=p.clear;x.bankAt=p.shopDetour+p.approach+p.clear+p.hold+p.bankTravel+p.tribute;
 x.renewAt=x.bankAt+p.renewal;
 x.feasible=p.observed&&p.helperVisible&&p.independentFeasible&&p.exposureKnown&&p.helper>=0&&p.independentSource>=0&&
  x.health>=p.requiredHealth&&x.strength>=p.requiredStrength&&x.focus>=p.requiredFocus&&x.renewAt<=96;
 return x;
}
inline InvestmentCertificate select(const std::vector<InvestmentCertificate>&all,const ThresholdProgram&p,Arm arm){
 InvestmentCertificate best;
 for(auto x:all){if(arm==Arm::control&&!common_bundle(x.itemCounts))continue;x=certify(x,p);if(!x.feasible)continue;
  if(!best.feasible||std::array{x.ownCost,x.renewAt,x.itemCounts.total(),x.itemCounts.h,x.itemCounts.s}<std::array{best.ownCost,best.renewAt,best.itemCounts.total(),best.itemCounts.h,best.itemCounts.s})best=x;
 }return best;
}
struct Screen {std::array<InvestmentCertificate,4> retained{};int count=0;bool approximation=false;};
inline Screen screen(const std::vector<InvestmentCertificate>&all,const ThresholdProgram&p){
 std::vector<InvestmentCertificate> frontier;
 for(auto x:all){x=certify(x,p);if(!x.feasible)continue;bool dominated=false;for(const auto&y:all)if(dominates(y,x)){dominated=true;break;}if(!dominated)frontier.push_back(x);}
 Screen s;s.approximation=frontier.size()>4;
 for(int criterion=0;criterion<4;++criterion){const InvestmentCertificate*best=nullptr;for(const auto&x:frontier){bool have=false;for(int i=0;i<s.count;++i)have|=s.retained[i].itemCounts==x.itemCounts&&s.retained[i].family==x.family;if(have)continue;
   if(!best||(criterion==0?x.ownCost<best->ownCost:criterion==1?x.health>best->health:criterion==2?x.strength>best->strength:x.focus>best->focus))best=&x;}
  if(best){s.retained[s.count]=*best;s.retained[s.count++].approximation=s.approximation;}}
 return s;
}
// Per-monster stationary combat schedule. A whole group is represented or the
// programme is unavailable; ties/hidden eligible attackers never grant aggro.
struct CombatTrace {bool feasible=false;int turns=0;double hp=0;int incomingHits=0;};
// Static breakpoint screen only. It never grants permission to fight: the
// selected vector and current gear must still pass the two fresh traces.
inline double screened_work(const Actor&a,const Group&g,const InvestmentCertificate&x){
 double work=0;const double multiplier=x.family==Family::monster&&x.exactPassive?1.1:(a.monster_emblem?1.1:1.);
 double burst=0;const Ability*spell=nullptr;for(const auto&ab:a.abilities)if((ab.damage||ab.aoe||ab.cc)&&(!spell||ab.modifier>spell->modifier))spell=&ab;
 if(spell)burst=spell->modifier*x.focus*multiplier;Point center=g.monsters.empty()?g.position:g.monsters.front().position;bool used=false;
 for(const auto&m:g.monsters)if(m.alive&&m.hp>0){
  double local=0;if(spell&&distance(g.approach,center)<=spell->range&&((spell->aoe&&distance(m.position,center)<=spell->radius)||!used)){local=burst;used=true;}
  work+=std::ceil(std::max(0.,m.hp-local)/std::max(1.,x.strength*multiplier));
 }
 if(a.type==Champion::varg)work/=std::max(1.,std::min(3.,double(g.monsters.size())));
 return work;
}
inline double screened_loss(const Actor&a,const Group&g,const InvestmentCertificate&x){
 // Algebraic death-time bounds: no movement/cooldown trajectory is executed
 // for the catalogue. The eventual selected vector receives that fresh trace.
 std::array<std::pair<double,int>,48>order{};int count=0,target=-1;const Ability*spell=nullptr;
 for(int j=0;j<int(g.monsters.size());++j)if(g.monsters[j].alive&&g.monsters[j].hp>0&&(target<0||g.monsters[j].hp<g.monsters[target].hp))target=j;
 for(const auto&ab:a.abilities)if(ab.cooldown<=0&&(ab.damage||ab.aoe||ab.cc)&&target>=0&&distance(g.approach,g.monsters[target].position)<=ab.range&&(!spell||ab.modifier>spell->modifier))spell=&ab;
 for(int j=0;j<int(g.monsters.size())&&j<48;++j)if(g.monsters[j].alive&&g.monsters[j].hp>0){double hp=g.monsters[j].hp;
  if(spell&&(j==target||(spell->aoe&&distance(g.monsters[j].position,g.monsters[target].position)<=spell->radius)))hp=std::max(0.,hp-spell->modifier*x.focus*(a.monster_emblem?1.1:1.));
  if(hp>0)order[count++]={hp,j};
 }
 std::sort(order.begin(),order.begin()+count);double elapsed=0,loss=0;
 for(int k=0;k<count;++k){const auto&m=g.monsters[order[k].second];elapsed+=std::ceil(order[k].first/std::max(1.,x.strength*(a.monster_emblem?1.1:1.)));
  if(distance(g.approach,m.position)<=m.range)loss+=m.strength*(elapsed+1);
 }
 return loss*((a.jungle_emblem||(x.family==Family::jungle&&x.exactPassive))?.7:1.);
}
inline InvestmentCertificate schedule_candidate(const std::vector<InvestmentCertificate>&all,const Actor&a,const Group&g,const ThresholdProgram&p,Arm arm){
 InvestmentCertificate best;double bestWork=1e30;
 InvestmentCertificate current;current.health=a.max_hp;current.strength=a.strength;current.focus=a.focus;
 const double currentWork=screened_work(a,g,current);
 for(auto x:all){if(x.ownCost<=0||(arm==Arm::control&&!common_bundle(x.itemCounts)))continue;
  x=certify(x,p);if(!x.feasible)continue;const double work=screened_work(a,g,x);
  if(x.health<=screened_loss(a,g,x)+x.health*.15)continue;
  // Health and output are independent axes. Keep a mixed vector when it
  // crosses both an actual hit boundary and the inspected exposure reserve.
  if(work>=currentWork&&x.health<=a.max_hp)continue;
  if(!best.feasible||work<bestWork||(work==bestWork&&x.ownCost<best.ownCost))best=x,bestWork=work;
 }return best;
}
inline CombatTrace solo(const Actor&a,const Group&g,double maxhp,double strength,double focus){
 CombatTrace out;if(!usable(g)||g.monsters.empty()||g.monsters.size()>48)return out;
 std::vector<double> hp;std::vector<int>stuns;for(const auto&m:g.monsters){hp.push_back(m.alive?m.hp:0.);stuns.push_back(m.stun);}
 double own=a.hp+(a.at_spawn?std::max(0.,maxhp-a.max_hp):0.);int cd0=a.cooldown[0],cd1=a.cooldown[1];
 const Point stand=g.approach.x>=0?g.approach:a.position;
 for(int turn=0;turn<80;++turn){int target=-1;for(int i=0;i<int(hp.size());++i)if(hp[i]>0&&(target<0||hp[i]<hp[target]))target=i;
  if(target<0){out.feasible=own>std::max(1.,maxhp*.15);out.turns=turn;out.hp=own;return out;}
  bool cast=false;for(int slot=0;slot<2;++slot){const auto&ab=a.abilities[slot];int&cd=slot==0?cd0:cd1;if(cast||cd>0||!(ab.damage||ab.aoe||ab.cc)||distance(stand,g.monsters[target].position)>ab.range)continue;
   for(int j=0;j<int(hp.size());++j)if(hp[j]>0&&(j==target||(ab.aoe&&distance(g.monsters[j].position,g.monsters[target].position)<=ab.radius))){hp[j]-=focus*ab.modifier*(a.monster_emblem?1.1:1.);if(ab.cc)stuns[j]=std::max(0,ab.duration-1);}
   cd=ab.max_cooldown;cast=true;}
  if(hp[target]>0&&distance(stand,g.monsters[target].position)<=a.range){hp[target]-=strength*(a.monster_emblem?1.1:1.);if(a.type==Champion::varg)for(int j=0;j<int(hp.size());++j)if(j!=target&&distance(g.monsters[j].position,g.monsters[target].position)<=1)hp[j]-=strength;}
  for(int j=0;j<int(hp.size());++j)if(hp[j]>0){if(stuns[j]>0){--stuns[j];continue;}if(distance(stand,g.monsters[j].position)<=g.monsters[j].range){const double hit=g.monsters[j].strength*(a.jungle_emblem?.7:1.);own-=hit;out.incomingHits+=hit>0;if(a.type==Champion::ren&&hit>0)hp[j]-=.2*focus;}}
  if(own<=std::max(1.,maxhp*.15))return out;cd0=std::max(0,cd0-1);cd1=std::max(0,cd1-1);
 }return out;
}
} // namespace p10::investment
