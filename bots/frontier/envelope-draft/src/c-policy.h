#pragma once
#include "b-policy.h"
#include "../../common/src/c-capital-06-10.h"
namespace p10_envelope {
struct CPolicy:BPolicy {
 static constexpr bool owns_capital=true;
 static constexpr bool observe_ally_details=false;
 static constexpr std::uint64_t observe_ally_detail_types=std::uint64_t{0};
 p10_c_capital::Income capitalIncome;
 p10::Profile draft(const p10::DraftContext&d,p10::Arm){return BPolicy::draft(d,p10::Arm::active);}
 p10::Item reinvest(const p10::World&w)const{
  using namespace p10;using p10_c_capital::stock;
  const bool spell=w.self.type==Champion::heart||w.self.type==Champion::prism||w.self.type==Champion::ren;
  if(w.self.type==Champion::willow){if(stock(w,Item::monster_strength)<5)return Item::monster_strength;return stock(w,Item::focus)/5<=stock(w,Item::strength)/5?Item::focus:Item::strength;}
  const Item work=spell?Item::monster_focus:Item::monster_strength;
  if(stock(w,work)<5)return work;
  const Item protect=spell?Item::jungle_focus:Item::jungle_health;
  if(stock(w,protect)<5)return protect;
  return spell?Item::focus:Item::strength;
 }
 // The package owns this appointment; the local clearance helper does not
 // route an isolated actor from the global map to its selected source graph.
 p10::Directive approach_package(const p10::World&w,p10::Arm arm){
  using namespace p10;auto budget=p10_mobile::budget_for(w);
  if(selected<0||!w.geometry_complete||!w.actor_observation_complete||!w.safe_now||!w.self.can_move)return {};
  if(selected==3&&(w.self.type==Champion::cassi||w.self.type==Champion::issac)){
   const auto type=w.self.type==Champion::cassi?Champion::issac:Champion::cassi;bool present=false;
   for(const auto&a:w.allies)present|=a.type==type&&a.visible&&a.alive;
   if(!present)return {}; // B retains its absent-linked-body recovery.
  }
  if(!appoint(w,arm==Arm::control)||!budget.fuel_ok())return wait("package appointment validation preserves current action fuel");
  int duty=-1;for(int i=0;i<organization.actors;++i)if(champion(organization.duties[i].kit)==w.self.type)duty=i;
  if(duty<0)return {};
  appointmentDuty=duty;appointmentGroup=physicalSources[duty];fallbackGroup=physicalFallback[duty];
  const auto*g=w.group(appointmentGroup),*other=w.group(fallbackGroup);if(!g)return {};
  const Group*recipient=g;const int lead=organization.duties[duty].partner;const Actor*actualLead=nullptr;
  if(lead>=0)for(const auto&a:w.allies)if(a.visible&&a.alive&&a.type==champion(organization.duties[lead].kit))actualLead=&a;
  if(lead>=0&&w.self.type==Champion::willow){
   if(actualLead&&actualLead->hp<actualLead->max_hp&&w.self.can_cast)for(int slot=0;slot<2;++slot)if(w.self.abilities[slot].heal&&w.self.abilities[slot].ready&&distance(w.self.position,actualLead->position)<=w.self.abilities[slot].range)return {};
   recipient=other; // Preserve the released support's existing fallback duty.
  }else if(g->respawning||g->contested)recipient=other;
  if(!recipient||!recipient->observed||!recipient->complete||!recipient->living||recipient->cleared||recipient->respawning||recipient->contested||!recipient->route_known||recipient->travel<0||!recipient->program_available)return {};
  if(!p10_mobile::current_geometry_binding(*recipient))return wait("package appointment refuses a changed physical geometry binding");
  bool living=false,positioned=w.self.can_attack;
  for(const auto&m:recipient->monsters){if(!budget.fuel_ok())return wait("package approach preserves current action fuel");if(m.alive&&m.hp>0){living=true;positioned&=distance(w.self.position,m.position)<=w.self.range;}}
  if(living&&positioned)return {};
  for(const auto*points:{&recipient->spawns,&recipient->approach_pads})for(auto p:*points){if(!budget.fuel_ok())return wait("package approach preserves current action fuel");if(p10_mobile::manhattan(w.self.position,p)<=1)return {};}
  const auto pad=physical_pad(w,*recipient,w.self.range);if(!budget.fuel_ok())return wait("package pad selection preserves current action fuel");
  if(recipient==g)appointmentPad=pad;else fallbackPad=pad;
  Directive d;d.task=Task::inspect;d.group=recipient->id;d.valid_round=w.round;d.reserve_reset=true;
  // Runtime's guarded inspection walks the source goals with fresh occupancy;
  // an occupied nominal pad is never encoded as an immediate legal move.
  if(!p10_mobile::occupied(w,pad))d.destination=pad;
  d.reason="execute the cached package duty's real approach before source-local work";return d;
 }
 p10::Directive step(const p10::World&w,p10::Arm arm){
  using namespace p10;using namespace p10_c_capital;capitalIncome.read(w);fresh_trajectories=0;
  if(protected_turn(w))return wait("complementary package finishes real custody before its capital visit");
  if(w.self.bag>0)return bank("each package duty banks only its own existing bag");
  paidGear=true;
  if(capitalIncome.funded&&affordable(w)>0&&legal_shop(w)){
   const auto item=reinvest(w);const int missing=(item==Item::strength||item==Item::focus)?5:5-stock(w,item);
   return purchase(w,item,missing,"actual package duty buys its recurring personal work/protection recipe");
  }
  // The fixed-appointment control retains the exact selected roster and menu.
  // Returning capital is not credited to another actor or a hypothetical body.
  if(capitalIncome.funded&&w.own_gold>=5*w.shard_cost&&!legal_shop(w)&&w.safe_now){Directive d;d.task=Task::recover;d.reason="funded complementary duty replenishes its own package at spawn";return d;}
  auto ingress=approach_package(w,arm);if(ingress.task!=Task::independent)return ingress;
  return BPolicy::step(w,arm);
 }
};
}
