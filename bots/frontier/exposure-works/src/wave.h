#pragma once
#include "../../common/src/combat.h"
namespace p10_exposure {
struct Policy:LegacyPolicy {
 int variant=0,fresh_trajectories=0,preparedGroup=-1,releasedFrom=-1;
 using FuelReader=int(*)();FuelReader fuel_reader=nullptr;
 void set_fuel_reader(FuelReader reader){fuel_reader=reader;}
 bool budget(const p10::World&w)const{return fresh_trajectories<2&&(fuel_reader?fuel_reader():w.fuel)>w.action_reserve+2500;}
 static bool strictly_nearest(const p10::World&w,const p10::Monster&m,const p10::Actor&ren){
  const int own=p10::distance(m.position,ren.position);if(own>m.range)return false;
  if(w.self.id!=ren.id&&w.self.alive&&p10::distance(m.position,w.self.position)<=own)return false;
  for(const auto&a:w.allies)if(a.visible&&a.alive&&a.id!=ren.id&&p10::distance(m.position,a.position)<=own)return false;
  for(const auto&a:w.enemies)if(a.visible&&a.alive&&p10::distance(m.position,a.position)<=own)return false;
  return true;
 }
 static double neighbors(const p10::World&w,int group,p10::Point point){
  double total=0;for(const auto&g:w.groups)if(g.id!=group&&!g.respawning&&!g.cleared){
   double incoming=p10::phase_damage(g,point);if(incoming>0&&(!g.observed||!g.complete))return std::numeric_limits<double>::infinity();total+=incoming;
  }
  return total;
 }
 p10::Directive independent(const p10::World&w,const p10::Group&g){
  using namespace p10;if(g.cleared)return farm(g.id,"exposure worker retains its own productive custody");
  if(!usable(g)){Directive d;d.task=Task::inspect;d.group=g.id;d.destination=g.approach;d.reason="approach/inspect the actual exposure organization before a reaction exists";return w.safe_now?d:Directive{Task::recover};}
  if(!budget(w))return ordinary(w);
  World conservative=w;if(conservative.self.type==Champion::ren)conservative.self.type=Champion::other;
  const auto stance=distance(w.self.position,g.position)<=w.self.range?w.self.position:g.approach;
  ++fresh_trajectories;const auto proof=stationary_work(conservative,g,stance,64);if(!proof.feasible)return ordinary(w);
  if(!(stance==w.self.position)){if(!w.safe_now)return {Task::recover};Directive d;d.task=Task::move;d.group=g.id;d.destination=stance;d.reason="real approach/prepare prefix before current retaliation reinspection";d.template_id=40;return d;}
  auto d=proof.first;d.reason="independent real work remains executable without invented retaliation";d.template_id=40;return d;
 }
 p10::Directive step(const p10::World&w,p10::Arm arm){
  using namespace p10;if(variant==1||arm==Arm::control)return LegacyPolicy::step(w,arm);
  fresh_trajectories=0;if(w.own_payout){witnessedIncome=true;lastIncome=w.round;}if(w.own_purchase)lastBuy=w.round;
  if(w.self.channel!=Channel::none||w.capture_active)return wait("preserve actual reaction payout/service custody");
  if(w.self.bag>0)return bank("secure actual exposure output before another service cycle");
  if(witnessedIncome&&lastBuy<lastIncome&&w.own_gold>=w.shard_cost&&(w.self.at_spawn||w.self.type==Champion::tavi))return buy(w.self.type==Champion::ren||w.self.type==Champion::willow?Item::focus:Item::strength,1,"reaction/healing capital spends only the service actor's own wallet");
  const Actor*ren=w.self.type==Champion::ren?&w.self:nullptr;
  for(const auto&a:w.allies)if(a.type==Champion::ren&&a.visible&&a.alive)ren=&a;
  const Group*g=preparedGroup>=0?w.group(preparedGroup):nullptr;
  if(!g||g->respawning||g->contested){g=nullptr;for(const auto&candidate:w.groups)if(candidate.route_known&&candidate.program_available&&candidate.to_bank>=0&&!candidate.respawning&&!candidate.contested&&
     (!g||distance(candidate.position,ren?ren->position:w.self.position)<distance(g->position,ren?ren->position:w.self.position)||
      (distance(candidate.position,ren?ren->position:w.self.position)==distance(g->position,ren?ren->position:w.self.position)&&candidate.id<g->id))){g=&candidate;}
   preparedGroup=g?g->id:-1;
  }
  if(!g)return ordinary(w);source=g->id;
  if(w.self.type==Champion::willow&&ren&&ren->hp<ren->max_hp&&w.self.can_cast){
   for(int slot=0;slot<2;++slot)if(w.self.abilities[slot].heal&&w.self.abilities[slot].ready&&distance(w.self.position,ren->position)<=w.self.abilities[slot].range){
    Directive d;d.task=Task::heal;d.actor=ren->id;d.cast_slot=slot;d.group=g->id;d.reason="pay an actual missing-HP reaction service, then release to independent income";d.template_id=42;return d;}
  }
  if(ren&&w.self.id!=ren->id){
   bool holder=false;for(auto p:g->spawns)holder|=p==ren->position;
   const double charge=neighbors(w,g->id,ren->position)*(w.capture_turns+2);
   if(g->cleared&&g->observed&&g->complete&&!g->contested&&holder&&w.enemies.empty()&&ren->hp>charge+std::max(1.,ren->max_hp*.1)&&releasedFrom!=g->id){
    releasedFrom=g->id;Directive d;d.task=Task::release;d.group=g->id;d.reason="safe actual Ren custody releases the priced healer/worker before conversion";return d;
   }
   const Group*other=nullptr;for(const auto&candidate:w.groups)if(candidate.id!=g->id&&usable(candidate)&&candidate.to_bank>=0&&candidate.travel>=0&&
     (!other||candidate.travel<other->travel||(candidate.travel==other->travel&&candidate.id<other->id)))other=&candidate;
   return other?independent(w,*other):Directive{Task::recover};
  }
  // The reaction program is exactly one direct kill elsewhere, then one
  // positively damaging, unstunned, strictly-nearest attacker paying its own
  // remaining HP through retaliation. Other monster targets need no Ren tie.
  if(w.self.type==Champion::ren&&usable(*g)&&w.self.can_attack&&w.self.shield<=0&&w.geometry_complete&&w.observation_complete&&w.enemies.empty()&&budget(w)){
   const Monster*weak=nullptr,*target=nullptr;bool observed=true;
   for(const auto&m:g->monsters){observed&=m.observed;if(m.alive&&m.strength>0&&m.stun<=0&&m.hp>0&&m.hp<=.2*w.self.focus&&strictly_nearest(w,m,w.self))weak=&m;}
   if(weak)for(const auto&m:g->monsters)if(m.alive&&m.id!=weak->id&&m.hp>0&&m.hp<=w.self.strength*(w.self.monster_emblem?1.1:1.)&&distance(w.self.position,m.position)<=w.self.range){if(target){target=nullptr;break;}target=&m;}
   int living=0;for(const auto&m:g->monsters)living+=m.alive&&m.hp>0;
   const double after=weak?weak->strength*(w.self.jungle_emblem?.7:1.):0;
   const double minimum=after+neighbors(w,g->id,w.self.position)*3+std::max(1.,w.self.max_hp*.1);
   if(observed&&weak&&target&&living==2&&w.self.hp>minimum){
    ++fresh_trajectories;Directive d;d.task=Task::attack;d.group=g->id;d.monster=target->id;d.reserve_reset=d.strict_tank=true;
    d.safety_verified=true;d.safety_round=d.valid_round=w.round;d.minimum_start_hp=minimum;d.template_id=41;
    d.reason="kill the other monster now; only the strictly-nearest damaging attacker pays retaliation";return d;
   }
  }
  return independent(w,*g);
 }
};
}
