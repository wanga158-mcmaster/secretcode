#pragma once
#include "model.h"
namespace p10::guild {
struct Policy {
 int variant=0;
 std::array<Skill,8>menu{};int count=0,source=-1;Continuation continuation{};bool payout=false,banked=false,renewed=false;
 std::array<int,2>circuits{-1,-1};int onlineEvaluations=0,onlineOperations=0;
 static int resource(const World&w){
#ifdef P10_LIVE
  (void)w;return int(cambc::fuel_remaining());
#else
  return w.fuel;
#endif
 }
 Directive execute_skill(const World&w,const Skill&s){
  if(w.self.bag>0){continuation.phase=Phase::bank;return bank("compiled loaded conversion skill");}
  if(!w.safe_now||w.self.hp<w.self.max_hp*.25){continuation.phase=Phase::recover;return {Task::recover};}
  const auto*g=w.group(s.source);if(!g)return ordinary(w);
  if(g->cleared){continuation.phase=Phase::hold;Directive d;d.task=Task::capture;d.group=g->id;d.reason="compiled burst then safe physical hold";return d;}
  continuation.phase=g->travel>0?Phase::approach:Phase::clear;auto d=farm(g->id,"compiled conservative small-source basic sequence");d.reserve_reset=true;d.template_id=int(s.kind)+1;
  if(s.kind==SkillKind::splash_reset){d.reserve_reset=false;d.reason="positioned splash with one cast and actual attack-reset successor";int hits=-1;for(const auto&m:g->monsters)if(m.alive){int n=0;for(const auto&other:g->monsters)n+=other.alive&&distance(m.position,other.position)<=1;if(n>hits){hits=n;d.monster=m.id;}}}
  if(s.kind==SkillKind::burst_hold){d.reason="exclusive burst prefix then fresh hold guard";for(int slot=0;slot<2;++slot)if(w.self.abilities[slot].ready&&w.self.abilities[slot].damage){d.cast_slot=slot;break;}}
  if(s.kind==SkillKind::loaded_conversion)return bank("compiled loaded conversion stage");
  if(s.kind==SkillKind::recovery)return {Task::recover};
  if(s.kind==SkillKind::persistent_pulse){d.pulse=true;d.reason="compiled productive pulse with explicit camp-phase exit";}
  return d;
 }
 void compile(const World&w){count=0;int instantiated=0;
  if(!variant){
   // Recompile physical circuits after a real invalidation; a single early
   // visible source cannot permanently monopolize both circuit slots.
   circuits={-1,-1};const Group*jobs[2]{nullptr,nullptr};
   for(const auto&g:w.groups)if(usable(g)&&g.to_bank>=0&&g.travel>=0&&g.gems>0){
    if(!jobs[0]||cycle_ticks(w,g)<cycle_ticks(w,*jobs[0])){jobs[1]=jobs[0];jobs[0]=&g;}else if(!jobs[1]||cycle_ticks(w,g)<cycle_ticks(w,*jobs[1]))jobs[1]=&g;
   }
   for(int j=0;j<2;++j)if(jobs[j]){const auto&g=*jobs[j];circuits[j]=g.id;Skill s;s.id=count;s.source=g.id;s.nextSource=jobs[1-j]?jobs[1-j]->id:g.id;s.fuel=provisional_budget();
    s.kind=SkillKind::small_source;
    if(w.self.type==Champion::varg)for(const auto&ab:w.self.abilities)if(ab.ready&&ab.reset)s.kind=SkillKind::splash_reset;
    for(const auto&ab:w.self.abilities)if(ab.ready&&(ab.damage||ab.aoe)&&ab.modifier*w.self.focus>w.self.strength)s.kind=SkillKind::burst_hold;
    s.guard={w.round,g.id,-1,g.last_payout,std::max(1.,w.self.max_hp*.2),8,true,true,true};
    if(onlineEvaluations>=2)break;auto traced=online_predict(s,w.self,g);++onlineEvaluations;onlineOperations+=traced.operations;
    s.firstScore=g.gems;s.firstBank=g.travel+traced.phases+w.capture_turns+1+g.to_bank+w.ticks(g.gems);s.renewal=s.firstBank+g.to_bank+1;
    s.productive=traced.feasible&&s.renewal<=w.remaining();menu[count++]=s;
   }
   return;
  }
  // The roster is fixed. Pick physical circuits from the bounded live frame;
  // rebuilding full draft geometry in every champion instance exhausted fuel.
  if(circuits[0]<0)for(const auto&g:w.groups)if(g.observed&&g.complete&&g.route_known&&g.to_bank>=0&&g.program_available&&g.gems>0){if(circuits[0]<0)circuits[0]=g.id;else if(circuits[1]<0){circuits[1]=g.id;break;}}
  for(const auto&g:w.groups){if(instantiated>=2)break;if(circuits[0]>=0&&g.id!=circuits[0]&&g.id!=circuits[1])continue;if(!g.observed||!g.complete||!g.route_known||g.travel<0||g.to_bank<0)continue;++instantiated;
   for(int variant=0;variant<4&&count<8;++variant){auto&s=menu[count];s={};s.id=count;s.source=g.id;s.nextSource=g.id;s.kind=static_cast<SkillKind>(variant);s.fuel=provisional_budget();
    s.guard={w.round,g.id,-1,g.last_payout,std::max(w.self.max_hp*.2,g.incoming*(std::max(0,g.clear_turns)+2)),w.self.cooldown[0],true,true,true};
    s.firstScore=g.gems;s.firstBank=cycle_ticks(w,g);s.renewal=s.firstBank+g.to_bank+1;
    s.productive=usable(g)&&g.safe_solo&&s.renewal<=w.remaining()&&g.gems>0;
    // All four skills describe the same complete cycle with different physical
    // legal prefixes; an inapplicable prefix stays in the shared menu.
    if(variant==1)s.productive&=w.self.type==Champion::varg||w.self.type==Champion::stellar;
    if(variant==2)s.productive&=w.self.can_cast;if(variant==3)s.productive&=w.self.bag>0;++count;
   }
  }}
 Directive step(const World&w,Arm arm){
  onlineEvaluations=0;onlineOperations=0;
  if(w.own_payout&&w.last_payout_group==source){payout=true;continuation.phase=Phase::bank;}if(payout&&w.own_bank_completed){banked=true;continuation.bankWitness=true;continuation.phase=Phase::return_to_work;}if(banked&&w.productive_group>=0)renewed=true;
  if(w.own_purchase)continuation.purchaseWitness=true;
  if(w.self.channel!=Channel::none&&w.safe_now)return wait("guarded channel continuation");if(w.self.bag>0)return bank("compiled loaded conversion skill");if(!w.safe_now)return {Task::recover};
  bool blocked=false;for(const auto&r:w.receipts)blocked|=r.kind==Receipt::Kind::blocked&&r.round>=w.round-1;
  if(continuation.skill>=0&&continuation.skill<count){const auto&s=menu[continuation.skill];const auto*g=w.group(s.source);
   Fresh f{w.round,s.source,g?g->last_payout:-2,w.self.cooldown[0],w.self.hp,g&&g->route_known,true,true,blocked};
   if(g&&usable(*g)&&continuation.refresh(s,f)&&resource(w)>=(variant?w.action_reserve+s.fuel.required():std::max(w.action_reserve,s.fuel.required()))){
    if(arm==Arm::active)return execute_skill(w,s);
    auto trace=online_predict(s,w.self,*g);++onlineEvaluations;onlineOperations+=trace.operations;if(trace.feasible)return execute_skill(w,s);
   }
   continuation.skill=-1;
  }
  // No unfinished trajectory is resumed. A fresh guard is sufficient for the
  // compiled menu; optional online prediction stays within its explicit budget.
  compile(w);Admission best;
  for(int i=0;i<count;++i){const auto&s=menu[i];const auto*g=w.group(s.source);if(!g)continue;Fresh f{w.round,g->id,g->last_payout,w.self.cooldown[0],w.self.hp,g->route_known,true,true,blocked};
   if(resource(w)<(variant?std::max(w.action_reserve,int(w.entry_fuel*.4))+s.fuel.required():std::max(w.action_reserve,s.fuel.required())))break;
   if(!s.productive||!valid(s,f))continue;
   if(arm==Arm::control&&variant){if(onlineEvaluations>=2)break;auto trace=online_predict(s,w.self,*g);++onlineEvaluations;onlineOperations+=trace.operations;if(!trace.feasible)continue;}
   if(best.selected<0||s.firstBank<menu[best.selected].firstBank)best.selected=i;
   if(arm==Arm::active&&variant)break;
  }
  if(best.selected>=0){const auto&s=menu[best.selected];source=s.source;continuation={best.selected,source,w.round,Phase::approach,false,false};return execute_skill(w,s);}
  // Capital may simplify actual targeting/HP branches at a real 120HP stage.
  if(w.self.at_spawn&&w.own_gold>=w.shard_cost)for(const auto&g:w.groups)if(usable(g)&&g.hp>0&&g.hp<=w.self.strength+w.self.strength_growth&&g.hp>w.self.strength)return buy(Item::monster_strength,1,"funded one-hit skill threshold");
  return ordinary(w);
 }
};
} // namespace p10::guild
