#pragma once
#include "../../common/src/contract.h"
namespace p10::guild {
inline const Profile profile{"p10-realtime-guild",{Champion::varg,Champion::marcella,Champion::tavi,Champion::stellar,Champion::willow}};
enum class SkillKind {small_source,splash_reset,burst_hold,loaded_conversion,recovery,persistent_pulse,partner_service,strength_threshold};
struct FuelContract {int guard=0,routeRepair=0,action=0,exit=0,error=0,optional=0;bool measured=false;int required()const{return guard+routeRepair+action+exit+error;}};
// Initial source budgets are deliberately unqualified. Current-engine Release
// profiling must replace them before any measured fuel-safety claim is made.
inline FuelContract provisional_budget(){return {2000,5000,6000,7000,3000,3000,false};}
struct Guard {int round=-1,source=-1,partner=-1,groupEpoch=-1;double minimumHp=0;int maximumCooldown=0;bool routeClear=false,channelIntact=true,partnerPresent=true;};
struct Skill {int id=0,source=-1,nextSource=-1;SkillKind kind=SkillKind::small_source;Guard guard{};FuelContract fuel{};Gems firstScore=0;int firstBank=0,renewal=0;bool productive=false;};
struct OnlineTrace {bool feasible=false;int phases=0,operations=0;double remainingHp=0;};
inline OnlineTrace online_predict(const Skill&s,const Actor&a,const Group&g){
 OnlineTrace out;if(!usable(g)||g.monsters.empty()||g.monsters.size()>48)return out;
 std::array<double,48>hp{};for(int i=0;i<int(g.monsters.size());++i)hp[i]=g.monsters[i].alive?g.monsters[i].hp:0;
 double own=a.hp+a.shield;int cooldown[2]{a.cooldown[0],a.cooldown[1]};const Point stand=g.approach;
 for(int turn=0;turn<48;++turn){int target=-1;for(int j=0;j<int(g.monsters.size());++j){++out.operations;if(hp[j]>0&&(target<0||hp[j]<hp[target]))target=j;}
  if(target<0){out.feasible=own>a.max_hp*.2;out.phases=turn;out.remainingHp=own;return out;}
  auto hit=[&](double damage,bool splash){damage*=a.monster_emblem?1.1:1.;hp[target]-=damage;for(int j=0;j<int(g.monsters.size());++j){++out.operations;if(splash&&j!=target&&hp[j]>0&&distance(g.monsters[j].position,g.monsters[target].position)<=1)hp[j]-=damage;}};
  bool cast=false;if(s.kind==SkillKind::burst_hold)for(int slot=0;slot<2;++slot){const auto&ab=a.abilities[slot];if(cooldown[slot]<=0&&(ab.damage||ab.aoe||ab.cc)&&distance(stand,g.monsters[target].position)<=ab.range){hit(ab.modifier*a.focus,ab.aoe);cooldown[slot]=ab.max_cooldown;cast=true;break;}}
  if(hp[target]>0&&distance(stand,g.monsters[target].position)<=a.range)hit(a.strength,a.type==Champion::varg);
  if(hp[target]>0&&s.kind==SkillKind::splash_reset&&!cast)for(int slot=0;slot<2;++slot)if(cooldown[slot]<=0&&a.abilities[slot].reset){hit(a.strength,a.type==Champion::varg);cooldown[slot]=a.abilities[slot].max_cooldown;break;}
  for(int j=0;j<int(g.monsters.size());++j){++out.operations;const auto&m=g.monsters[j];if(hp[j]>0&&distance(stand,m.position)<=m.range)own-=m.strength*(a.jungle_emblem?.7:1.);}
  if(own<=a.max_hp*.2)return out;for(int&cd:cooldown)cd=std::max(0,cd-1);
 }return out;
}
struct Fresh {int round=0,source=-1,groupEpoch=-1,cooldown=0;double hp=0;bool routeClear=true,channelIntact=true,partnerPresent=true,moveFailed=false;};
inline bool valid(const Skill&s,const Fresh&f){return s.guard.source==f.source&&s.guard.groupEpoch==f.groupEpoch&&f.hp>=s.guard.minimumHp&&f.cooldown<=s.guard.maximumCooldown&&f.routeClear&&s.guard.channelIntact==f.channelIntact&&f.partnerPresent&&!f.moveFailed;}
struct Admission {int selected=-1,preparationSpent=0;bool measured=false;};
inline Admission select(const std::array<Skill,8>&menu,int count,const Fresh&f,int remaining,int reserve,Arm arm){
 Admission out;count=std::clamp(count,0,8);int available=remaining-std::max(reserve,0);
 // Both arms see the exact same physical menu. Online evaluation pays fresh
 // trajectory preparation in declared order; compiled guard admission does not.
 for(int i=0;i<count;++i){const auto&s=menu[i];int prep=arm==Arm::control?s.fuel.optional:0;
  if(available<prep+s.fuel.required())break;available-=prep;out.preparationSpent+=prep;
  if(!s.productive||s.firstScore<=0||s.nextSource<0||s.renewal<=s.firstBank||!valid(s,f))continue;
  if(out.selected<0||s.firstBank<menu[out.selected].firstBank)out.selected=i;
  if(arm==Arm::active)break;
 }if(out.selected>=0)out.measured=menu[out.selected].fuel.measured;return out;
}
enum class Phase {approach,clear,hold,bank,recover,return_to_work};
struct Continuation {int skill=-1,source=-1,round=-1;Phase phase=Phase::approach;bool bankWitness=false,purchaseWitness=false;
 bool refresh(const Skill&s,const Fresh&f){if(round>=0&&f.round>round+1){skill=-1;return false;}round=f.round;if(!valid(s,f)){skill=-1;return false;}return true;}
};
} // namespace p10::guild
