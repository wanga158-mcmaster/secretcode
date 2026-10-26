#pragma once
#include "policy.h"
#include "independent-tactics.h"
#include "../../common/src/economic-observation.h"
namespace p10_b12 {
using namespace p10;
struct BPolicy {
 static constexpr bool observe_enemy_exits=true;
 p10_b::Economy economy{};std::array<int,2>district{-1,-1};int source=-1,traces=0,encounters=0;
 independent::Choice direct{};bool fallbackConditional=false;arena::ArenaProgram last{};
 static bool open_pad(const World&w,Point p){return p.x>=0&&!p10_mobile::occupied(w,p);}
 Directive step(const World&w,Arm arm){
  economy.consume(w);traces=0;auto budget=p10_mobile::budget_for(w);const auto options=p10_b::single_options();
  if(w.self.channel!=Channel::none)return wait("arena district preserves owned channel after receipt consumption");
  if(w.self.bag>0&&(economy.batch>=2||w.self.bag>=24||p10_b::altar_trip(w)<=3||w.score+w.self.bag>=w.win_gems))return bank("border district closes actual loaded batch before another encounter");
  if(w.capture_active)return wait("one genuine holder finishes the uncontested region");
  if(source>=0&&district[1]==source){if(const auto*g=w.group(source);g&&p10_b::work_source(*g)){
   if(g->cleared)return farm(g->id,"late encounter turns into actual uncontested custody");
   auto continuation=p10_mobile::mobile_work(w,*g,budget,options);traces=budget.spent;
   if(continuation.feasible)return p10_b::witnessed(w,continuation,1213,"won/abandoned encounter continues into actual mobile clearance and bank");
  }}
  const Group*home=nullptr;
  // A border district must pay through its own sources. Proximity to an enemy
  // is only a tie-break after complete own circuit value, never free bounty.
  for(const auto&g:w.groups)if(p10_b::work_source(g)){
   auto value=[&](const Group&h){return (h.gems*4+h.gold*.04)/(1+h.travel+std::max(1,h.clear_turns)+h.to_bank+w.ticks(w.self.bag+h.gems)+w.capture_turns);};
   if(!home||value(g)>value(*home))home=&g;
  }
  if(!home){if(w.self.bag>0)return bank();return w.safe_now?Directive{}:Directive{Task::recover};}
  district[0]=home->id;
  if(home->cleared){source=home->id;return farm(home->id,"productive district holds actual cleared source");}
  auto work=p10_mobile::mobile_work(w,*home,budget,options);traces=budget.spent;
  auto own_continuation=[&](){return work.feasible?p10_b::witnessed(w,work,1210,"border worker executes its own mobile production circuit"):w.safe_now?p10_b::approach(*home,1211,"safe reveal retains productive border appointment"):Directive{Task::recover};};
  direct=independent::choose(w,*home,work,budget);
  auto independent_continuation=[&](){return direct.selected&&!direct.fuel_aborted?direct.first:own_continuation();};
  if(arm==Arm::control)return independent_continuation();
  if(w.self.type!=Champion::ren&&w.self.type!=Champion::varg&&w.self.type!=Champion::vendetta)return independent_continuation();
  const Group*camp=nullptr;const Actor*rival=nullptr;
  for(const auto&g:w.groups)if(g.id!=home->id&&p10_b::work_source(g)&&!g.cleared){bool awake=false;for(const auto&m:g.monsters)awake|=m.alive&&m.awake;
   if(!awake)continue;double ownBurst=w.self.strength*2;for(const auto&ab:w.self.abilities)if(ab.ready&&(ab.damage||ab.aoe||ab.cc))ownBurst=std::max(ownBurst,w.self.strength*2+ab.modifier*w.self.focus);
   if(g.hp>ownBurst)continue;for(const auto&r:w.enemies)if(r.visible&&r.alive&&distance(r.position,g.position)<=5&&distance(w.self.position,g.position)<=5){camp=&g;rival=&r;break;}if(camp)break;
  }
  if(!camp||!rival||budget.spent>=2||!budget.fuel_ok())return independent_continuation();
  int nearby=0;for(const auto&e:w.enemies)nearby+=e.visible&&e.alive&&distance(e.position,camp->position)<=8;if(nearby!=1)return independent_continuation();
  arena::Encounter e;e.own=w.self;e.rival=*rival;e.camp=*camp;e.phaseCount=4;e.terrainKnown=w.geometry_complete;e.turnOrderKnown=true;e.ownFirst=w.our_team_starts;e.hiddenEligibleActor=!w.opposition_complete;
  e.fuel_reader=budget.fuel_reader;e.fuel_floor=w.action_reserve+500;e.fallbackSource=home->id;e.fallbackGems=home->gems;e.fallbackBank=work.feasible?work.clear_turns+home->travel+w.capture_turns+1+home->to_bank+w.ticks(w.self.bag+home->gems):cycle_ticks(w,*home);e.ownBank=cycle_ticks(w,*camp);e.renewal=std::max(1,home->travel);
  std::vector<Point>pads=w.visible_open_tiles;for(auto p:camp->approach_pads){if(!budget.fuel_ok())return independent_continuation();if(std::find(pads.begin(),pads.end(),p)==pads.end())pads.push_back(p);}
  int exits=0;for(auto p:pads){if(!budget.fuel_ok())return independent_continuation();if(exits<2&&distance(p,rival->position)==1&&open_pad(w,p)){e.rivalExits[exits]=p;e.openExit[exits]=true;Point at=p;
   for(int t=0;t<4;++t){if(t){Point next=at;for(auto q:pads){if(!budget.fuel_ok())return independent_continuation();if(distance(q,at)==1&&open_pad(w,q)&&distance(q,camp->position)>distance(next,camp->position))next=q;}at=next;}e.exitPath[exits][t]=at;}++exits;
  }}
  if(exits!=2)return independent_continuation();
  Point switchAt=rival->position;for(int t=0;t<4;++t){Point next=switchAt;for(auto p:pads){if(!budget.fuel_ok())return independent_continuation();if(distance(p,switchAt)==1&&open_pad(w,p)&&distance(p,w.self.position)<distance(next,w.self.position))next=p;}switchAt=next;e.switchPath[t]=switchAt;e.phases[t]={w.self.position,rival->position,w.self.can_attack,w.self.can_cast&&t==0,false,true,rival->can_cast&&t==0,false,false};}
  Point ownAt=w.self.position;for(int t=1;t<4;++t){Point next=ownAt;for(auto p:pads){if(!budget.fuel_ok())return independent_continuation();if(distance(p,ownAt)==1&&open_pad(w,p)&&distance(p,camp->position)<distance(next,camp->position))next=p;}
   e.phases[t].own=next;e.phases[t].ownMove=next!=ownAt;e.phases[t].ownAttack=!(e.phases[t].ownMove&&w.self.range>1);ownAt=next;
  }
  for(auto p:pads){if(!budget.fuel_ok())return independent_continuation();if(distance(p,w.self.position)==1&&open_pad(w,p)){bool outside=true;for(const auto&m:camp->monsters){if(!budget.fuel_ok())return independent_continuation();outside&=!m.alive||m.strength==0||distance(p,m.position)>m.range;}if(outside){e.phases[3].own=p;e.phases[3].ownMove=true;e.phases[3].ownAttack=e.phases[3].ownCast=false;break;}}}
  arena::Program program=rival->hp<=w.self.strength+w.self.focus?arena::Program::burst_rival:arena::Program::finish_camp;
  ++budget.spent;traces=budget.spent;last=arena::evaluate(e,program);fallbackConditional=!work.feasible;
  // A four-phase encounter is not a complete physical farm witness; retain
  // its conditional fallback and never export a mobile safety certificate.
  const double displaced=home->gems*double(4+e.renewal)/std::max(1,e.fallbackBank);
  bool environmentUsed=false;for(const auto&branch:last.branches)environmentUsed|=branch.campDamageToRival>0;
  const bool pays=last.admitted&&!last.fuel_aborted&&environmentUsed&&last.nominal>home->gems+displaced&&last.adverse>=home->gems;
  if(arm==Arm::active&&pays){district[1]=source=camp->id;++encounters;auto d=arena::Policy::first_action(w,*camp,*rival,program,e);d.template_id=1220+int(program);d.safety_verified=false;d.reason="late existing camp fight pays displaced district production under both exits";return d;}
  return independent_continuation();
 }
};
}
