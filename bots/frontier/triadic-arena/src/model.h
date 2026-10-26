#pragma once
#include "../../common/src/contract.h"
namespace p10::arena {
inline const Profile profile{"p10-triadic-arena",{Champion::ren,Champion::varg,Champion::marcella,Champion::tavi,Champion::vendetta}};
enum class RivalBranch {continue_camp,switch_to_us,leave,disrupt};
enum class Program {finish_camp,burst_rival,shield_exit,wait_outside,contest_clear,alternative};
struct Phase {Point own{},rival{};bool ownAttack=false,ownCast=false,ownMove=false,rivalAttack=false,rivalCast=false,rivalGone=false,campStunned=false;};
struct Encounter {
 using FuelReader=int(*)();FuelReader fuel_reader=nullptr;int fuel_floor=0;
 bool fuel_ok()const{return !fuel_reader||fuel_reader()>=fuel_floor;}
 Actor own{},rival{};Group camp{};std::array<Phase,4> phases{};int phaseCount=0;
 std::array<Point,2> rivalExits{};std::array<std::array<Point,4>,2>exitPath{};std::array<Point,4>switchPath{};bool openExit[2]{false,false};
 int fallbackSource=-1,fallbackBank=0,ownBank=0,renewal=0;Gems fallbackGems=0;
 double displacedOutput=0;bool hiddenEligibleActor=true,terrainKnown=false,turnOrderKnown=false,ownFirst=true;
};
struct BranchOutcome {RivalBranch branch=RivalBranch::leave;bool survive=false,escape=false,productive=false,aggroUnresolved=false;double ownHp=0,rivalHp=0,campDamageToRival=0;Gems secured=0;int bankAt=0;bool fuel_aborted=false;};
struct ArenaProgram {Program program=Program::alternative;std::array<BranchOutcome,4> branches{};bool admitted=false;Gems nominal=0,adverse=0;int endpoint=0;bool fuel_aborted=false;};
inline BranchOutcome simulate(const Encounter&e,Program program,RivalBranch branch,int leaveRoute=0){
 BranchOutcome out;out.branch=branch;out.ownHp=e.own.hp+e.own.shield;out.rivalHp=e.rival.hp+e.rival.shield;
 auto fuel=[&](){if(e.fuel_ok())return true;out.fuel_aborted=true;return false;};
 if(!fuel())return out;
 if(!e.camp.observed||!e.camp.complete||e.camp.monsters.size()>48||e.phaseCount<1||e.phaseCount>4||!e.terrainKnown||!e.turnOrderKnown)return out;
 std::vector<double> hp;std::vector<int>stuns;for(const auto&m:e.camp.monsters){if(!fuel())return out;hp.push_back(m.alive?m.hp:0.);stuns.push_back(m.stun);}
 bool awakened=false;for(const auto&m:e.camp.monsters){if(!fuel())return out;awakened|=m.awake;}
 Point own=e.own.position,rival=e.rival.position;
 for(int t=0;t<e.phaseCount;++t){if(!fuel())return out;const auto&p=e.phases[t];if(distance(own,p.own)>(p.ownMove?1:0))return out;own=p.own;
  if(branch==RivalBranch::leave){if(!e.openExit[leaveRoute]||distance(rival,e.exitPath[leaveRoute][t])>1)return out;rival=e.exitPath[leaveRoute][t];}
  else if(branch==RivalBranch::switch_to_us){if(distance(rival,e.switchPath[t])>1)return out;rival=e.switchPath[t];}
  else {if(distance(rival,p.rival)>1)return out;rival=p.rival;}
  if(p.ownMove&&p.ownAttack&&e.own.range>1)return out;
  if(p.ownMove&&p.ownCast)for(const auto&a:e.own.abilities){if(!fuel())return out;if(a.ready&&a.exclusive)return out;}
  bool stunned=p.campStunned;
  auto own_action=[&]{
   if(program==Program::burst_rival&&out.rivalHp>0&&distance(own,rival)<=e.own.range&&p.ownAttack)out.rivalHp-=e.own.strength;
   else if(program==Program::finish_camp||program==Program::contest_clear||(program==Program::burst_rival&&out.rivalHp<=0)){for(int j=0;j<int(hp.size());++j){if(!fuel())return;if(hp[j]>0&&distance(own,e.camp.monsters[j].position)<=e.own.range){if(p.ownAttack){hp[j]-=e.own.strength;awakened=true;}break;}}}
   if(p.ownCast){for(const auto&ab:e.own.abilities){if(!fuel())return;if(ab.ready){if(program==Program::shield_exit&&ab.shield)out.ownHp+=ab.modifier*e.own.focus;
     else if(program==Program::burst_rival&&(ab.damage||ab.aoe||ab.cc)&&distance(own,rival)<=ab.range)out.rivalHp-=ab.modifier*e.own.focus;
     else if((program==Program::finish_camp||program==Program::contest_clear)&&(ab.damage||ab.aoe||ab.cc)){for(int j=0;j<int(hp.size());++j){if(!fuel())return;if(hp[j]>0&&distance(own,e.camp.monsters[j].position)<=ab.range){hp[j]-=ab.modifier*e.own.focus;if(ab.cc)stuns[j]=std::max(0,ab.duration-1);awakened=true;break;}}}break;}}}
  };
  auto rival_action=[&]{if(branch==RivalBranch::leave||out.rivalHp<=0)return;
   if(branch==RivalBranch::switch_to_us&&p.rivalAttack&&distance(own,rival)<=e.rival.range)out.ownHp-=e.rival.strength;
   if(branch==RivalBranch::disrupt&&p.rivalCast){for(const auto&ab:e.rival.abilities){if(!fuel())return;if(ab.ready&&(ab.damage||ab.aoe||ab.cc)&&distance(own,rival)<=ab.range){out.ownHp-=ab.modifier*e.rival.focus;break;}}}
   if(branch==RivalBranch::continue_camp&&p.rivalAttack)for(int j=0;j<int(hp.size());++j){if(!fuel())return;if(hp[j]>0&&distance(rival,e.camp.monsters[j].position)<=e.rival.range){hp[j]-=e.rival.strength;awakened=true;break;}}
  };
  if(e.ownFirst){own_action();if(out.fuel_aborted)return out;rival_action();}else{rival_action();if(out.fuel_aborted)return out;if(out.ownHp>0)own_action();}if(out.fuel_aborted)return out;
  // Camp phases are explicit subsequent round-start events. Each living
  // monster independently chooses strict nearest; ties retain adverse damage.
  if(awakened&&!stunned)for(int j=0;j<int(hp.size());++j){if(!fuel())return out;if(hp[j]>0){if(stuns[j]>0){--stuns[j];continue;}const auto&m=e.camp.monsters[j];const int a=distance(own,m.position),b=distance(rival,m.position);
   bool ownEligible=a<=m.range,rivalEligible=b<=m.range&&out.rivalHp>0;
   if(ownEligible&&(!rivalEligible||a<=b)){out.ownHp-=m.strength;if(rivalEligible&&a==b)out.aggroUnresolved=true;if(e.own.type==Champion::ren&&m.strength>0)hp[j]-=.2*e.own.focus;}
   else if(rivalEligible){out.rivalHp-=m.strength;out.campDamageToRival+=m.strength;}
   out.aggroUnresolved|=e.hiddenEligibleActor;
  }}
  if(out.ownHp<=0)return out;
 }
 out.survive=out.ownHp>std::max(1.,e.own.max_hp*.15);
 bool outside=true;for(const auto&m:e.camp.monsters){if(!fuel())return out;outside&=distance(own,m.position)>m.range||m.strength==0;}
 bool clear=true;for(double h:hp){if(!fuel())return out;clear&=h<=0;}
 // A melee allowance alone is not an escape route. The final represented tile
 // must already be outside every surviving envelope, or all camps must be dead.
 out.escape=outside||clear;
 // Departure must independently leave a complete useful source-to-bank job;
 // enemy damage/kill receives no score credit.
 const bool leave=branch==RivalBranch::leave;
 out.productive=(leave?e.fallbackSource>=0&&e.fallbackGems>0:clear||e.fallbackSource>=0);
 bool rivalHolding=out.rivalHp>0&&std::find(e.camp.spawns.begin(),e.camp.spawns.end(),rival)!=e.camp.spawns.end();
 // The represented rival leaving/dying can unlock custody. An unresolved
 // additional eligible actor still prevents a promised uncontested payout.
 out.secured=clear&&!rivalHolding&&!e.hiddenEligibleActor?e.camp.gems:e.fallbackGems;
 out.bankAt=clear?e.ownBank:e.fallbackBank;
 if(out.bankAt+e.renewal>128)out.productive=false;
 return out;
}
inline ArenaProgram evaluate(const Encounter&e,Program p){ArenaProgram out;out.program=p;out.admitted=true;out.adverse=std::numeric_limits<Gems>::max();
 for(int i=0;i<4;++i){if(!e.fuel_ok()){out.admitted=false;out.fuel_aborted=true;return out;}auto b=simulate(e,p,static_cast<RivalBranch>(i));if(b.fuel_aborted){out.admitted=false;out.fuel_aborted=true;return out;}if(i==2){auto other=simulate(e,p,RivalBranch::leave,1);if(other.fuel_aborted){out.admitted=false;out.fuel_aborted=true;return out;}if(!other.survive||!other.escape||!other.productive||other.secured<b.secured)b=other;}out.branches[i]=b;out.admitted&=b.survive&&b.escape&&b.productive;out.adverse=std::min(out.adverse,b.secured);out.endpoint=std::max(out.endpoint,b.bankAt+e.renewal);}
 out.nominal=out.branches[0].secured;return out;
}
inline bool deploy(const ArenaProgram&p,Gems ordinaryOutput,Arm arm){return arm==Arm::active&&p.admitted&&p.nominal>ordinaryOutput&&p.adverse>=ordinaryOutput;}
} // namespace p10::arena
