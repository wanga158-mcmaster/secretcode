#pragma once
#include "../../common/src/physical-appointments.h"
#include "model.h"
namespace p10_b_foundry {
struct BPolicy:p10_b::Physical {
 std::array<int,2>cells{-1,-1};int source=-1,last_receipt=-2,transfers=0;bool own_income=false;
 static bool front(const p10::Actor&a){return a.type==p10::Champion::varg||a.type==p10::Champion::heart;}
 void appoint(const p10::World&w){if(cells[0]>=0)return;std::array<double,2>v{-1,-1};for(const auto&g:w.groups)if(g.route_known&&g.program_available&&!g.contested){double q=(g.gems*6+g.gold*.05+1)/(8+p10::distance(w.spawn,g.position));if(q>v[0]){v[1]=v[0];cells[1]=cells[0];v[0]=q;cells[0]=g.id;}else if(q>v[1]){v[1]=q;cells[1]=g.id;}}}
 static bool standable(const p10::World&w,p10::Point p,int leaving=-1){
  bool terrain=std::find(w.visible_open_tiles.begin(),w.visible_open_tiles.end(),p)!=w.visible_open_tiles.end();
  for(const auto&g:w.groups)for(const auto&m:g.monsters)if(m.position==p){if(m.alive&&m.hp>0)return false;terrain|=g.observed&&g.complete&&m.observed;}
  for(const auto&a:w.allies)if(a.id!=leaving&&a.id!=w.self.id&&a.visible&&a.alive&&a.position==p)return false;
  for(const auto&a:w.enemies)if(a.visible&&a.alive&&a.position==p)return false;return terrain;
 }
 static bool strict_at(const p10::World&w,const p10::Group&g,const p10::Actor&lead,p10::Point p,p10::Point other){
  bool damaging=false;for(const auto&m:g.monsters)if(m.alive&&m.strength>0){damaging=true;const int d=p10::distance(p,m.position);if(d>m.range||d>=p10::distance(other,m.position))return false;for(const auto&a:w.allies)if(a.id!=lead.id&&!front(a)&&a.visible&&a.alive&&d>=p10::distance(a.position,m.position))return false;}return damaging;
 }
 static double all_damage(const p10::World&w,p10::Point p,bool&known){double damage=0;for(const auto&g:w.groups)for(const auto&m:g.monsters)if(m.alive&&m.strength>0&&p10::distance(p,m.position)<=m.range){known&=g.observed&&g.complete&&m.observed;damage+=m.strength;}return damage;}
 static p10::SoloForecast replacement_work(const p10::World&w,const p10::Actor&a,const p10::Group&g,p10::Point p,p10_mobile::Budget&b){
  p10::SoloForecast out;if(b.spent>=2||!b.fuel_ok()||!w.geometry_complete||!w.observation_complete||p10_mobile::visible_enemy_can_intervene(w,p))return out;
  // This first turn is the encoded move, with no free melee attack or spell.
  auto projected=a;projected.position=p;projected.can_attack=projected.can_cast=false;++b.spent;
  out=p10::solo_program(projected,g,p,48,w.round,-1,b.fuel_reader,b.action_reserve+500);if(!out.feasible)return out;
  double neighbor=0;bool known=true;for(const auto&v:w.groups)if(v.id!=g.id)for(const auto&m:v.monsters)if(m.alive&&m.strength>0&&p10::distance(p,m.position)<=m.range){known&=v.observed&&v.complete&&m.observed;neighbor+=m.strength;}
  const double multiplier=a.jungle_emblem?.7:1.;out.hp-=(neighbor*(out.turns+2)+p10::phase_damage(g,p)*2)*multiplier;
  if(!known||out.hp<=std::max(30.,a.max_hp*.08))out.feasible=false;return out;
 }
 p10::Directive step(const p10::World&w,p10::Arm arm){
  fresh_traces=0;
  if(w.own_payout&&last_receipt!=p10_b::receipt_round(w)){last_receipt=p10_b::receipt_round(w);own_income=true;source=-1;}
  if(w.own_bank_completed)source=-1;appoint(w);
  if(w.self.channel!=p10::Channel::none)return p10::wait("carousel actual service");
  if(w.self.bag>0)return p10::bank("carousel actual personal stock conversion");
  if(w.capture_active)return p10_b::capture(w.productive_group,"carousel witnessed custody",2024);
  if(own_income&&w.own_gold>=w.shard_cost&&(w.self.at_spawn||w.self.type==p10::Champion::tavi)){
   if(w.self.type==p10::Champion::willow&&w.self.focus_shards<5)return p10::buy(p10::Item::jungle_focus,1,"Willow spends only its real personal funding");
   if(front(w.self)&&w.self.strength_shards<5)return p10::buy(p10::Item::monster_strength,1,"carousel clearance threshold");}
  auto b=budget(w);auto usage=watch(b);
  if(w.self.type==p10::Champion::willow){
   if(!own_income){const p10::Group*funding=nullptr;for(const auto&g:w.groups)if(p10::usable(g)&&g.gems>0&&!g.respawning){bool harmless=true;for(const auto&m:g.monsters)harmless&=m.strength==0;if(harmless&&(!funding||g.travel<funding->travel))funding=&g;}
    if(funding){if(funding->cleared)return p10_b::capture(funding->id,"real first Willow capture funds service",2020);auto p=prove(w,*funding,b);if(p.feasible){auto d=p.first;d.template_id=2020;return d;}return p10_b::inspect(funding->id,"Willow's personally funded opening circuit",2020);}}
   const p10::Actor*patient=nullptr;double urgency=1e30;for(const auto&a:w.allies)if(a.visible&&a.alive&&front(a)&&a.channel==p10::Channel::none){double danger=0;for(const auto&g:w.groups)for(const auto&m:g.monsters)if(m.alive&&m.awake&&p10::distance(a.position,m.position)<=m.range)danger+=m.strength;
    if(a.max_hp-a.hp>0&&(a.max_hp-a.hp>=3*w.self.focus||a.hp+a.shield<danger*3)&&a.hp+a.shield-danger*2<urgency){patient=&a;urgency=a.hp+a.shield-danger*2;}}
   if(patient&&w.self.can_cast&&w.self.abilities[0].ready&&p10::distance(w.self.position,patient->position)<=4){p10::Directive d;d.task=p10::Task::heal;d.actor=patient->id;d.cast_slot=0;d.template_id=2021;d.reason="actual funded carousel heal pulse";return d;}
   for(int id:cells){const auto*g=w.group(id);if(g&&p10::usable(*g)&&g->cleared)return p10_b::capture(id,"Willow observed service-share funding",2020);}
   if(patient){p10::Directive move;for(auto p:p10_b::cardinals(w.self.position))if(p10::distance(p,patient->position)<p10::distance(w.self.position,patient->position)&&p10_b::safe_vacate(w,p,move)){move.template_id=2021;return move;}}
   return p10::ordinary(w);
  }
  const p10::Actor*partner=nullptr;for(const auto&a:w.allies)if(a.visible&&a.alive&&front(a)&&a.id!=w.self.id&&a.channel==p10::Channel::none)partner=&a;
  // A replacement has its own executable owner-turn: move only, then replan.
  // It never inherits a promised partner vacate or an unobserved aggro label.
  if(arm==p10::Arm::active&&front(w.self)&&partner&&w.self.can_move&&w.self.shield>0&&w.self.shield_until>w.round+1&&partner->shield_until<=w.round+1){
   for(int id:cells){const auto*v=w.group(id);if(!v||!p10::usable(*v)||v->cleared)continue;
    for(auto q:p10_b::cardinals(w.self.position))if(p10_b::free_cardinal(w,q)&&strict_at(w,*v,w.self,q,partner->position)&&!strict_at(w,*v,w.self,w.self.position,partner->position)){
     const auto replacement=replacement_work(w,w.self,*v,q,b);if(replacement.feasible){p10::Directive d;if(p10_b::safe_vacate(w,q,d)){source=v->id;d.group=v->id;d.template_id=2025;d.reason="actual replacement's own ordered move; full solo continuation repriced now";return d;}}break;
    }
   }
  }
  const p10::Group*g=w.group(source);if(g&&(g->respawning||g->contested||!g->route_known))g=nullptr;
  if(!g){int primary=w.self.type==p10::Champion::heart?1:0;g=w.group(cells[primary]);if(!g||g->respawning||g->contested)g=w.group(cells[1-primary]);if(g)source=g->id;}
  if(!g)return p10::ordinary(w);if(!g->observed||!g->complete)return p10_b::inspect(g->id,"carousel source arrival needs own observation",2022);
  if(g->cleared)return p10_b::capture(g->id,"frontliner finishes actual production custody",2024);
  const p10::Group*next=w.group(cells[0]==g->id?cells[1]:cells[0]);
  const bool partner_after=partner&&(w.self.priority>partner->priority||(w.self.priority==partner->priority&&w.self.pick<partner->pick));
  if(arm==p10::Arm::active&&front(w.self)&&partner&&partner_after&&next&&next->id!=g->id&&w.self.can_move&&partner->can_move&&partner->shield>0&&partner->shield_until>w.round+1){
   const double incoming=p10::phase_damage(*g,w.self.position);const int own_expiry=w.self.shield_until-w.round;
   if(incoming>0&&(own_expiry<=1||w.self.hp+w.self.shield<incoming*5)&&b.spent<2&&b.fuel_ok()){
    p10::Point leave{},entry{};bool geometry=false;
    // A single outgoing step cannot reverse a strict distance lead. Enumerate
    // the actual ordered pair: our move, then the present later actor's move.
    for(auto p:p10_b::cardinals(w.self.position))if(p10_b::free_cardinal(w,p)&&!p10_mobile::visible_enemy_can_intervene(w,p)){
     for(auto q:p10_b::cardinals(partner->position))if(!(q==p)&&p10_mobile::manhattan(q,partner->position)==1&&standable(w,q,w.self.id)&&strict_at(w,*g,*partner,q,p)){
      bool known=true;const double missed=all_damage(w,p,known);if(known&&w.self.hp>missed*3*(w.self.jungle_emblem?.7:1.)+30){leave=p;entry=q;geometry=true;break;}
     }
     if(geometry)break;
    }
    if(geometry){const auto replacement=replacement_work(w,*partner,*g,entry,b);
     if(replacement.feasible&&b.spent<2&&b.fuel_ok()){
      shield_foundry::Trajectory t;t.count=int(g->monsters.size());t.length=2;t.clearance=replacement.turns;t.capture=w.capture_turns;t.bank_travel=std::max(0,g->to_bank);t.bank_ticks=w.ticks(g->gems);t.renew_travel=std::max(0,next->travel);t.missed_attacks=2;
      t.bodies[0]={w.self.id,{w.self.position.x,w.self.position.y},w.self.hp,w.self.max_hp,w.self.shield,w.self.focus,w.self.strength,own_expiry,0,w.self.type==p10::Champion::heart,true};
      t.bodies[1]={partner->id,{partner->position.x,partner->position.y},partner->hp,partner->max_hp,partner->shield,partner->focus,partner->strength,partner->shield_until-w.round,0,partner->type==p10::Champion::heart,true};
      t.phases[0]={0,{leave.x,leave.y},true,false,false,false,0};t.phases[1]={1,{entry.x,entry.y},true,false,false,false,0};
      if(t.count<=12){for(int i=0;i<t.count;++i){const auto&m=g->monsters[i];t.monsters[i]={{m.position.x,m.position.y},m.strength,m.range,-1,m.awake};}
       ++b.spent;const auto transfer=shield_foundry::run(t,true);
       if(transfer.admissible&&transfer.transfer_count>0){p10::Directive d;if(p10_b::safe_vacate(w,leave,d)){source=next->id;++transfers;d.group=g->id;d.template_id=2023;d.reason="ordered shield carousel; missed entry survives independently, future entry rechecks";return d;}}
      }
     }
    }
   }
  }
  if(b.spent<2){auto proof=prove(w,*g,b);if(proof.feasible){auto d=proof.first;d.template_id=2022;d.reason="independent carousel production between actual service/handoff events";return d;}}
  return p10_b::inspect(g->id,"carousel physical appointment/warmup",2022);
 }
};
}
