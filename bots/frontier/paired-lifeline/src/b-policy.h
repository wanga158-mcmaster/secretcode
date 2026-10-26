#pragma once
#include "policy.h"
#include "../../common/src/economic-observation.h"
namespace p10_b14 {
using namespace p10;
struct BPolicy {
 p10_b::Economy economy{};pair::Recipient recipient=pair::Recipient::cassi;pair::ServiceOrder order=pair::ServiceOrder::parallel;
 int source=-1,nextSource=-1,serviceStart=-1,contactLost=-1,fundingUntil=-1,traces=0;Item recipe=Item::none;
 bool committedService=false,ownWitness=false,partnerWitness=false;
 static const Actor* twin(const World&w){for(const auto&a:w.allies)if(a.visible&&a.alive&&((w.self.type==Champion::cassi&&a.type==Champion::issac)||(w.self.type==Champion::issac&&a.type==Champion::cassi)))return &a;return nullptr;}
 Directive step(const World&w,Arm arm){
  economy.consume(w);traces=0;ownWitness=partnerWitness=false;auto budget=p10_mobile::budget_for(w);const auto options=p10_b::single_options();
  if(w.self.channel!=Channel::none)return wait("paired service consumes receipts before preserving actual channel");
  const bool paired=w.self.type==Champion::cassi||w.self.type==Champion::issac;
  if(!paired){const Group*g=nullptr;for(const auto&h:w.groups)if(p10_b::work_source(h)&&(!g||h.travel<g->travel))g=&h;if(w.self.bag>=24||economy.batch>=2)return bank();if(!g)return ordinary(w);auto r=p10_mobile::mobile_work(w,*g,budget,options);traces=budget.spent;return r.feasible?p10_b::witnessed(w,r,1418,"independent producer retains its own circuit beside linked pair"):p10_b::approach(*g,1419,"independent producer reveals its actual source");}
  if(recipe!=Item::none){const int missing=std::max(0,5-w.self.inventory[int(recipe)]);
   if(!missing||w.round>fundingUntil)recipe=Item::none;
   else if(w.self.bag>0)return bank("actual recipient converts its bag before owned funding trip");
   else if(w.own_gold>=w.shard_cost&&w.self.at_spawn)return buy(recipe,1,"one actual personal twin shard; no shared wallet");
   else if(w.own_gold>=missing*w.shard_cost)return {Task::recover};
  }
  const Actor*partner=twin(w);
  if(!partner){if(contactLost<0)contactLost=w.round;if(w.self.bag>0)return bank("lost twin contact still preserves actual own bag");if(w.self.hp<w.self.max_hp*.3)return {Task::recover};
   if(const auto*g=w.group(source);g&&w.round-contactLost<8)return p10_b::approach(*g,1417,"bounded personally named twin rendezvous without promised arrival");return ordinary(w);
  }
  contactLost=-1;const Actor&c=w.self.type==Champion::cassi?w.self:*partner;const Actor&i=w.self.type==Champion::issac?w.self:*partner;
  if(c.hp<=0||i.hp<=0)return {Task::recover};
  if(w.self.bag>0||partner->bag>0||serviceStart>=0){
   if(!committedService){committedService=true;serviceStart=w.round;bool parallel=c.hp>=c.max_hp*.45&&i.hp>=i.max_hp*.45&&!p10_b::local_threat(w);
    if(parallel)order=pair::ServiceOrder::parallel;else{const double cRisk=c.hp/std::max(1.,c.max_hp)-double(c.bag)*.001,iRisk=i.hp/std::max(1.,i.max_hp)-double(i.bag)*.001;order=cRisk<=iRisk?pair::ServiceOrder::cassi_first:pair::ServiceOrder::issac_first;}
   }
   const bool ownFirst=order==pair::ServiceOrder::parallel||(order==pair::ServiceOrder::cassi_first)==(w.self.type==Champion::cassi);
   if(w.self.bag>0){if(ownFirst||partner->bag==0||partner->channel==Channel::tribute||w.round-serviceStart>=4)return bank("committed real twin bank order, with no automatic full heal");return wait("bounded second twin retains committed first service prerequisite");}
   if(w.self.hp<w.self.max_hp*.3)return {Task::recover};
   if(partner->bag>0){if(w.self.type==Champion::cassi&&w.self.abilities[1].ready)for(const auto&e:w.enemies)if(e.visible&&e.alive&&distance(e.position,w.self.position)<=w.self.abilities[1].range&&distance(e.position,partner->position)<=e.range+1){Directive d;d.task=Task::cast;d.actor=e.id;d.cast_slot=1;d.template_id=1414;d.reason="actual second twin covers first service with legal control";return d;}return wait("bounded visible service cover; recovery is not charged after every payout");}
   committedService=false;serviceStart=-1;
  }
  const Group*job=nullptr,*next=nullptr;
  auto sharedDistance=[&](const Group&g){return std::max(distance(c.position,g.position),distance(i.position,g.position));};
  for(const auto&g:w.groups)if(p10_b::work_source(g)){
   if(g.cleared&&(p10_b::on_region(c,g)||p10_b::on_region(i,g))){job=&g;continue;}
   if(!job||(!job->cleared&&(sharedDistance(g)<sharedDistance(*job)||(sharedDistance(g)==sharedDistance(*job)&&g.id<job->id)))){next=job;job=&g;}else if(!next||sharedDistance(g)<sharedDistance(*next))next=&g;
  }
  if(!job)return ordinary(w);source=job->id;nextSource=next?next->id:-1;
  Group publicJob=*job;if(!publicJob.approach_pads.empty()){publicJob.approach=*std::min_element(publicJob.approach_pads.begin(),publicJob.approach_pads.end(),[](Point a,Point b){return a.y==b.y?a.x<b.x:a.y<b.y;});}
  recipient=pair::Policy::proposed_recipient(c,i,publicJob,arm);const bool ownRecipient=(recipient==pair::Recipient::cassi)==(w.self.type==Champion::cassi);
  const Item ownRecipe=w.self.type==Champion::issac?Item::jungle_health:Item::monster_focus;const int missing=std::max(0,5-w.self.inventory[int(ownRecipe)]);
  if(ownRecipient&&missing&&w.own_gold>=missing*w.shard_cost){recipe=ownRecipe;fundingUntil=w.round+160;if(w.self.at_spawn)return buy(recipe,1,"observed recipient uses actual own funding after real custody");return {Task::recover};}
  if(job->cleared){const Actor&actualRecipient=recipient==pair::Recipient::cassi?c:i;
   if(!ownRecipient&&p10_b::on_region(actualRecipient,*job)){Directive d;d.task=Task::release;d.group=job->id;d.template_id=1411;d.reason="real chosen twin already holds; helper leaves reward region peacefully";return d;}
   if(ownRecipient||p10_b::on_region(w.self,*job))return farm(job->id,"actual current holder retains capture until physical twin succession");
   return wait("future recipient enters before helper can release custody");
  }
  auto own=p10_mobile::mobile_work(w,*job,budget,options);ownWitness=own.feasible;
  // The mobile result certifies only self. Spend the second trace allowance
  // on a separate stationary twin exposure bound across the ENTIRE clear and
  // both settle phases. No twin move, spell, heal, wallet or future gear helps.
  if(own.feasible&&budget.spent<2&&budget.fuel_ok()){
   ++budget.spent;double volley=0;bool known=!p10_mobile::visible_enemy_can_intervene(w,partner->position);
   for(const auto&g:w.groups)if(!g.cleared&&!g.respawning)for(const auto&m:g.monsters){
    if(!budget.fuel_ok()){known=false;break;}if(((m.alive&&m.hp>0)||!g.complete)&&distance(partner->position,m.position)<=m.range){if(m.strength>0&&(!g.observed||!g.complete||!m.observed))known=false;volley+=m.strength;}
   }
   const int phases=std::max(own.frames_used,own.clear_turns+2);const double loss=volley*phases*(partner->jungle_emblem?.7:1.);
   partnerWitness=known&&partner->hp-loss>std::max(1.,partner->max_hp*.15);
  }
  traces=budget.spent;
  if(own.feasible&&partnerWitness)return p10_b::witnessed(w,own,1410,"mobile self clear plus whole-duration stationary twin exposure proof");
  if(w.self.hp<w.self.max_hp*.3||partner->hp<partner->max_hp*.3)return {Task::recover};
  const Group*reveal=next?next:job;return w.safe_now?p10_b::approach(*reveal,1416,"pair seeks honest alternate income instead of using future funded survivability"):Directive{Task::recover};
 }
};
}
