#pragma once
#include "model.h"
namespace p10::interdiction {
struct Policy {
 int variant=0;std::array<int,2>appointments{-1,-1};
 AttemptBudget attempts{};Witness witness{};int source=-1;bool payout=false,banked=false,renewed=false;
 static Directive intercept(const World&w,const Actor&target){
  Directive d;d.task=Task::attack;d.actor=target.id;d.reason="same favorable bounded interception executor in both arms";
  for(int slot=0;slot<2;++slot){const auto&ab=w.self.abilities[slot];if(ab.ready&&ab.damage&&distance(w.self.position,target.position)<=ab.range){d.cast_slot=slot;return d;}}
  if(distance(w.self.position,target.position)>w.self.range&&w.self.can_move){d.task=Task::move;d.destination={target.position.x+(w.self.position.x<target.position.x?-1:w.self.position.x>target.position.x?1:0),target.position.y+(w.self.position.y<target.position.y?-1:w.self.position.y>target.position.y?1:0)};d.reason="bounded interception approach with independent retreat";}
  return d;
 }
 Directive step(const World&w,Arm arm){
  witness.observe(w);if(w.own_payout&&source<0)source=w.last_payout_group;
  if(w.own_payout&&w.last_payout_group==source)payout=true;if(payout&&w.own_bank_completed)banked=true;if(banked&&w.productive_group>=0)renewed=true;
  if(w.self.channel!=Channel::none&&w.safe_now)return wait();if(w.self.bag>0)return bank("two-stage own productive receipt conversion");if(!w.safe_now){attempts.current(w.round,false,false);return {Task::recover};}
  if(w.self.type==Champion::ren||w.self.type==Champion::vendetta){
   const Actor*target=nullptr;for(const auto&e:w.enemies)if(e.visible&&e.alive&&distance(e.position,w.self.position)<=4&&w.self.hp>e.strength*3+w.self.max_hp*.2&&e.hp<=w.self.strength*3+w.self.focus&&
    (e.type==Champion::prism||e.type==Champion::cassi||e.type==Champion::issac)){target=&e;break;}
   if(attempts.active){const auto*current=w.actor(attempts.target);if(attempts.current(w.round,current&&current->visible&&current->alive,w.safe_now))return intercept(w,*current);}
   if(target&&attempts.begin(w.round,target->id))return intercept(w,*target);
   return ordinary(w);
  }
  if(w.self.type!=Champion::marcella||arm==Arm::control)return ordinary(w);
  if(witness.stage==Stage::replacement||witness.stage==Stage::hold){const auto*g=w.group(witness.source);if(g&&(variant?(usable(*g)&&g->safe_solo):appointment_source(w.self,*g))){source=g->id;auto d=farm(source,"independent producer continues own appointment even when the hit fails or rival leaves");d.template_id=160;return d;}witness.stage=Stage::home;}
  if(witness.stage==Stage::renewal||witness.stage==Stage::complete)return ordinary(w);
  // Marcella receives no remote kill notification. She admits a source duty
  // only from her own visible appointment, independent safe work and exits.
  const Actor*capability=nullptr;for(const auto&e:w.enemies)if(e.visible&&e.alive&&(e.type==Champion::prism||e.type==Champion::cassi||e.type==Champion::issac)){capability=&e;break;}if(!capability)return ordinary(w);
  bool crewSeen=false;for(const auto&a:w.allies)crewSeen|=a.visible&&a.alive&&(a.type==Champion::ren||a.type==Champion::vendetta)&&distance(a.position,capability->position)<=4;if(!crewSeen)return ordinary(w);
  ReentryCampaign c;c.observedCapability={capability->id,capability->type,capability->bag,capability->focus,true,capability->type==Champion::cassi||capability->type==Champion::issac,false};
  c.favorableIntercept=capability->hp<=capability->max_hp*.4;c.interceptionTurns=8;c.producer=w.self.id;c.expiry=w.round+160;c.unknownWallet=true;
  // Unknown purchasing is retained, but cannot make a capability-dependent
  // source compulsory. A witnessed known cooldown/bag loss may establish a
  // nominal branch; unresolved funding conservatively vetoes strong denial.
  const Group*home=nullptr,*replacement=nullptr;for(const auto&g:w.groups)if(variant?(usable(g)&&g.safe_solo&&g.to_bank>=0):appointment_source(w.self,g)){if(!home||cycle_ticks(w,g)<cycle_ticks(w,*home))home=&g;if(distance(capability->position,g.position)<=8&&(!replacement||distance(capability->position,g.position)<distance(capability->position,replacement->position)))replacement=&g;}
  if(!home||!replacement||home->id==replacement->id)return ordinary(w);
  if(!variant){appointments={home->id,replacement->id};
   c.homeBank=home->gems*std::max(1,160/std::max(1,std::max(home->respawn,cycle_ticks(w,*home))));
   c.lostHomeDeposits=home->gems*std::max(1,cycle_ticks(w,*replacement)/std::max(1,std::max(home->respawn,cycle_ticks(w,*home))));
  }
  c.observedCapability.productiveDependency=(capability->type==Champion::prism&&capability->bag>0)||capability->type==Champion::cassi||capability->type==Champion::issac;
  c.homeCompletion=distance(capability->position,replacement->position)+std::max(0,replacement->clear_turns)+w.capture_turns+1;if(variant){c.homeBank=home->gems*2;c.lostHomeDeposits=home->gems/2;}
  c.sourceCount=1;const int captured=replacement->travel+std::max(0,replacement->clear_turns)+w.capture_turns+1;
  c.replacementSources[0]={replacement->id,w.self.id,std::max(0,replacement->clear_turns),captured,cycle_ticks(w,*replacement),cycle_ticks(w,*replacement)+home->travel,replacement->respawn,60+distance(capability->position,replacement->position),replacement->gems,true,true,true,true};
  if(!variant)for(const auto&g:w.groups)if(c.sourceCount<3&&g.id!=replacement->id&&g.id!=home->id&&appointment_source(w.self,g)&&distance(capability->position,g.position)<=8){
   c.replacementSources[c.sourceCount++]={g.id,w.self.id,std::max(0,g.clear_turns),g.travel+std::max(0,g.clear_turns)+w.capture_turns+1,cycle_ticks(w,g),cycle_ticks(w,g)+home->travel,g.respawn,w.respawn_turns+distance(capability->position,g.position),g.gems,true,true,true,true};
  }
  for(const auto&g:w.groups)if(c.alternativeCount<7&&g.id!=replacement->id&&g.observed&&g.route_known){c.alternatives[c.alternativeCount++]={SubstituteKind::other_source,g.id,capability->id,distance(capability->position,g.position)+std::max(0,g.clear_turns)+w.capture_turns+1,true,false};}
  if(c.alternativeCount<7)c.alternatives[c.alternativeCount++]={SubstituteKind::shop_recall_respawn,-1,capability->id,0,false,true};
  auto chosen=select(c,arm);if(chosen.acquire){source=chosen.source;witness.source=source;witness.producer=w.self.id;witness.stage=Stage::replacement;auto d=farm(source,chosen.unresolved?"productive replacement appointment; failed hit/funding branch retains unknown denial":"second-stage changed re-entry source custody then bank and renewal");d.template_id=160;return d;}
  return ordinary(w);
 }
};
} // namespace p10::interdiction
