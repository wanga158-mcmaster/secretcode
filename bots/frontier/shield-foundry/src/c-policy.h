#pragma once
#include "../../common/src/c-capital-01-05.h"
#include "model.h"
#include "local-pair.h"
namespace p10_c_foundry {
struct CPolicy:p10_c::Physical {
 static constexpr bool observe_ally_details=false;
 static constexpr std::uint64_t observe_ally_detail_types=(std::uint64_t{1}<<unsigned(p10::Champion::heart))|(std::uint64_t{1}<<unsigned(p10::Champion::varg));
 p10_foundry_local::Pair local;
 p10_c::Wallet wallet;int capital_target=-1,capital_packet=0,capital_events=0;
 std::array<int,2>cells{-1,-1};int source=-1,last_receipt=-2,transfers=0;bool own_income=false;
 int funding_source=-1;
 static bool front(const p10::Actor&a){return a.type==p10::Champion::varg||a.type==p10::Champion::heart;}
 void appoint(const p10::World&w){if(!own_income&&funding_source>=0&&!local.valid(w,cells)){local.deferred=true;local.geometry.prepare(w,reader?reader:p10::work_fuel_reader);source=funding_source;return;}local.appoint(w,cells,reader);if(!local.contains(cells,source))source=-1;}
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
 static int basics(const p10::Group&g,double strength){int n=0;for(const auto&m:g.monsters)n+=int(std::ceil(m.max_hp/std::max(1.,strength)));return n;}
 const p10::Group*funding_cell(const p10::World&w,p10_mobile::Budget&b,bool&fuel_ready)const{
  if(!w.geometry_complete||!w.actor_observation_complete||w.groups.size()>128)return nullptr;
  const double strength=w.self.strength*(w.self.monster_emblem?1.1:1.);if(!(strength>0)||!std::isfinite(strength))return nullptr;
  const p10::Group*chosen=nullptr;double best=-1;
  // A short service-stock proposal for this actual body, not an unobserved
  // partner clear or future shield. Current physical work still gets48 turns.
  for(const auto&g:w.groups){if(!b.fuel_ok()){fuel_ready=false;return nullptr;}
   if(!g.route_known||!g.program_available||g.respawning||g.contested||g.travel<0||g.to_bank<0||g.monsters.empty()||g.monsters.size()>12||local.refused(w,g))continue;
   int work=0;double wave=0;bool plausible=true;
   for(const auto&m:g.monsters){if(!b.fuel_ok()){fuel_ready=false;return nullptr;}if(!(m.max_hp>0)||!std::isfinite(m.max_hp)){plausible=false;break;}const double hits=std::ceil(m.max_hp/strength);if(hits>12-work){plausible=false;break;}work+=int(hits);wave+=std::max(0.,m.strength);}
   if(!plausible||w.self.hp<=wave*(work+2)*(w.self.jungle_emblem?.7:1.)+std::max(30.,w.self.max_hp*.1)||std::int64_t(g.travel)+work+w.capture_turns+1+g.to_bank+w.ticks(g.gems)>w.remaining())continue;
   const double value=(g.gems*6+g.gold*.05+1)/(8+double(g.travel)+work+g.to_bank);
   if(value>best){best=value;chosen=&g;}
  }
  return chosen;
 }
 p10::Directive capital(const p10::World&w,p10::Arm arm){
  if(reader&&reader()<w.action_reserve+1200)return {};
  if(!wallet.available(w)||(w.self.type==p10::Champion::willow&&!own_income))return {};
  const p10::Group*g=w.group(source);if(!g)g=w.group(cells[w.self.type==p10::Champion::heart?1:0]);if(!g||g->to_bank<0||!g->route_known||g->monsters.empty())return {};
  double wave=0;for(const auto&m:g->monsters){if(m.max_hp<=0)return {};wave+=m.strength;}wave*=w.self.jungle_emblem?.7:1.;
  const int trip=p10_c::Wallet::detour(w,*g),old=basics(*g,w.self.strength*(w.self.monster_emblem?1.1:1.));const int period=old+w.capture_turns+1+2*g->to_bank+w.ticks(g->gems)+std::max(0,g->respawn);
  double shield_modifier=0,heal_modifier=0;for(const auto&a:w.self.abilities){if(a.shield&&a.targets_self)shield_modifier=std::max(shield_modifier,a.modifier);if(a.heal)heal_modifier=std::max(heal_modifier,a.modifier);}
  double deficit=0;for(const auto&a:w.allies)if(a.visible&&a.alive&&front(a))deficit=std::max(deficit,a.max_hp-a.hp);
  int score=0,packet=0;auto item=p10::Item::none;
  for(int n=1;n<=std::min(12,w.own_gold/w.shard_cost);++n){if(reader&&reader()<w.action_reserve+1000)break;if(w.remaining()<period+n+trip)continue;const int cycles=arm==p10::Arm::active?std::min(8,(w.remaining()-n-trip)/std::max(1,period)):1;
   auto offer=[&](p10::Item proposed,int gain){const int value=gain*cycles-n-trip;if(value>score){score=value;packet=n;item=proposed;}};
   if(front(w.self)&&w.self.strength_growth>0)offer(p10::Item::monster_strength,old-basics(*g,(w.self.strength+n*w.self.strength_growth)*(w.self.monster_emblem?1.1:1.)));
   if(front(w.self)&&w.self.focus_growth>0&&shield_modifier>0&&wave>0){const double target=wave*(arm==p10::Arm::active?3:1);if(w.self.focus*shield_modifier<target&&(w.self.focus+n*w.self.focus_growth)*shield_modifier>=target)offer(p10::Item::jungle_focus,arm==p10::Arm::active?3:1);}
   if(front(w.self)&&w.self.health_growth>0&&wave>0){const double target=wave*(arm==p10::Arm::active?3:1)+30;if(w.self.max_hp<target&&w.self.max_hp+n*w.self.health_growth>=target)offer(p10::Item::jungle_health,arm==p10::Arm::active?3:1);}
   if(w.self.type==p10::Champion::willow&&w.self.focus_growth>0&&heal_modifier>0){const double demand=arm==p10::Arm::active?std::max(deficit,wave*8):deficit;
    const int before=int(std::ceil(demand/std::max(1.,w.self.focus*heal_modifier))),after=int(std::ceil(demand/std::max(1.,(w.self.focus+n*w.self.focus_growth)*heal_modifier)));offer(p10::Item::jungle_focus,(before-after)*8);}
  }
  if(item==p10::Item::none)return {};capital_target=g->id;capital_packet=packet;++capital_events;
  return wallet.issue(w,item,g->id,6201,"personally funded recurring carousel shield/heal/clearance service threshold");
 }

 p10::Directive local_failed(const p10::World&w,const p10::Group&g,p10_mobile::Failure failure){
  if(failure==p10_mobile::Failure::health&&w.self.hp<w.self.max_hp*.9){p10::Directive d;d.task=p10::Task::recover;d.reason="local Foundry body recovers before its independent full-clear retry";return d;}
  if(local.refuse(w,g,failure)){if(funding_source==g.id)funding_source=-1;local.appoint(w,cells,reader,true);source=-1;const int id=cells[w.self.type==p10::Champion::heart?1:0];if(id>=0&&id!=g.id)return p10_c::inspect(id,"cold Foundry pair reappointed after current independent work refusal",2027);return p10::wait("local Foundry refuses the unchanged cold pair until a fresh admission");}
  return p10_c::inspect(g.id,"local Foundry private observation or temporary physical warmup",2022);
 }
 p10::Directive local_ingress(const p10::World&w,const p10::Group&g,p10_mobile::Budget&b,int tag)const{
  auto approach=[&](){auto d=p10_c::inspect(g.id,"Foundry approaches the same appointed duty before its source-local proof",tag);d.valid_round=w.round;return d;};
  auto reserve=[](){return p10::wait("Foundry current duty ingress preserves the action reserve");};
  if(!b.fuel_ok())return reserve();
  if(g.monsters.empty()||g.monsters.size()>12||!p10_mobile::current_geometry_binding(g))return approach();
  bool positioned=true,living=false;
  for(const auto&m:g.monsters){if(!b.fuel_ok())return reserve();if(m.alive&&m.hp>0){living=true;positioned&=p10::distance(w.self.position,m.position)<=w.self.range;}}
  if(living&&positioned)return {};
  if(g.spawns.size()>p10_mobile::max_raw_geometry||g.approach_pads.size()>p10_mobile::max_raw_geometry)return approach();
  const auto total=g.spawns.size()+g.monsters.size()+g.approach_pads.size();if(total>p10_mobile::max_raw_geometry)return approach();
  // This admits only literal present graph contact. It promises neither an
  // ally vacate nor a safe move, and grants no work certificate or new pair.
  for(std::size_t i=0;i<total;++i){if((i&7)==0&&!b.fuel_ok())return reserve();const auto p=p10_mobile::raw_point(g,int(i)).p;
   if(p10_mobile::manhattan(w.self.position,p)<=1&&!p10_mobile::occupied(w,p))return {};
  }
  return approach();
 }
 p10::Directive local_job(const p10::World&w,p10_mobile::Budget&b,int tag,p10::Arm arm){
  const p10::Group*g=w.group(!own_income&&funding_source>=0?funding_source:cells[0]);if(g&&(g->respawning||g->contested))g=w.group(cells[1]);
  // Actual ready custody can fund the healer before a two-source appointment
  // exists. It does not invent a second workcell or a partner's future clear.
  if(!g)for(const auto&ready:w.groups){if(!b.fuel_ok())break;if(p10::usable(ready)&&ready.route_known&&ready.cleared&&!ready.contested&&(!g||ready.travel<g->travel))g=&ready;}
  if(!g){g=w.group(funding_source);if(g&&(!g->route_known||g->contested||local.refused(w,*g)))g=nullptr;
   if(!g){bool fuel_ready=true;g=funding_cell(w,b,fuel_ready);if(!fuel_ready)return p10::wait("Foundry own funding-cell admission preserves the action reserve");}
   if(g)funding_source=g->id;
  }
  if(!g)return p10::wait("local Foundry waits for an independently plausible funding or service cell");source=g->id;
  if(g->respawning){if(p10_c::on_spawn(w.self,*g)){p10::Directive d;d.task=p10::Task::release;d.group=g->id;return d;}return p10::wait("local Foundry waits off renewed source spawns");}
  if(!g->observed||!g->complete)return p10_c::inspect(g->id,"local Foundry first-income/service duty needs personal current observation",tag);
  if(g->cleared)return p10_c::capture(g->id,"local Foundry keeps actual source custody before leaving its cell",2024);
  auto ingress=local_ingress(w,*g,b,tag);if(ingress.task!=p10::Task::independent)return ingress;
  if(!local.contains(cells,g->id)){
   auto investment=capital(w,arm);if(investment.task!=p10::Task::independent)return investment;
  }
  if(b.spent>=2||!b.fuel_ok())return p10_c::inspect(g->id,"local Foundry fresh work waits for its actual budget",tag);
  auto proof=prove(w,*g,b);if(proof.feasible){++local.witnesses;auto d=proof.first;d.template_id=tag;d.reason="independently certified local Foundry income between actual shield/service events";return d;}return local_failed(w,*g,proof.failure);
 }
 p10::Directive step(const p10::World&w,p10::Arm arm){
  fresh_traces=0;wallet.observe(w);
  if(w.own_payout&&last_receipt!=p10_c::receipt_round(w)){last_receipt=p10_c::receipt_round(w);own_income=true;source=-1;}
  if(w.own_bank_completed)source=-1;appoint(w);local.observe(w,cells);
  if(w.self.channel!=p10::Channel::none)return p10::wait("carousel actual service");
  if(w.self.bag>0)return p10::bank("carousel actual personal stock conversion");
  if(w.capture_active)return p10_c::capture(w.productive_group,"carousel witnessed custody",2024);
  if((!own_income&&funding_source>=0)||((local.deferred||cells[0]<0||cells[1]<0)&&w.self.type!=p10::Champion::willow)){auto b=budget(w);auto usage=watch(b);return local_job(w,b,w.self.type==p10::Champion::willow?2020:2028,arm);}
  auto returned=local.return_to_pair(w,cells);if(returned.task!=p10::Task::independent)return returned;
  auto investment=capital(w,arm);if(investment.task!=p10::Task::independent)return investment;
  auto b=budget(w);auto usage=watch(b);
  if(w.self.type==p10::Champion::willow){
   if(!own_income)return local_job(w,b,2020,arm);
   const p10::Actor*patient=nullptr;double urgency=1e30;for(const auto&a:w.allies)if(a.visible&&a.alive&&front(a)&&a.channel==p10::Channel::none){double danger=0;for(const auto&g:w.groups)for(const auto&m:g.monsters)if(m.alive&&m.awake&&p10::distance(a.position,m.position)<=m.range)danger+=m.strength;
    if(a.max_hp-a.hp>0&&(a.max_hp-a.hp>=3*w.self.focus||a.hp+a.shield<danger*3)&&a.hp+a.shield-danger*2<urgency){patient=&a;urgency=a.hp+a.shield-danger*2;}}
   if(patient&&w.self.can_cast&&w.self.abilities[0].ready&&p10::distance(w.self.position,patient->position)<=4){p10::Directive d;d.task=p10::Task::heal;d.actor=patient->id;d.cast_slot=0;d.template_id=2021;d.reason="actual funded carousel heal pulse";return d;}
   for(int id:cells){const auto*g=w.group(id);if(g&&p10::usable(*g)&&g->cleared)return p10_c::capture(id,"Willow observed service-share funding",2020);}
   if(patient){p10::Directive move;for(auto p:p10_c::cardinals(w.self.position))if(p10::distance(p,patient->position)<p10::distance(w.self.position,patient->position)&&p10_c::safe_vacate(w,p,move)){move.template_id=2021;return move;}}
   return local_job(w,b,2020,arm);
  }
  const p10::Actor*partner=nullptr;for(const auto&a:w.allies)if(a.visible&&a.alive&&front(a)&&a.id!=w.self.id&&a.channel==p10::Channel::none)partner=&a;
  // A replacement has its own executable owner-turn: move only, then replan.
  // It never inherits a promised partner vacate or an unobserved aggro label.
  if(arm==p10::Arm::active&&front(w.self)&&partner&&w.self.can_move&&w.self.shield>0&&w.self.shield_until>w.round+1&&partner->shield_until<=w.round+1){
   for(int id:cells){const auto*v=w.group(id);if(!v||!p10::usable(*v)||v->cleared)continue;
    for(auto q:p10_c::cardinals(w.self.position))if(p10_c::free_cardinal(w,q)&&strict_at(w,*v,w.self,q,partner->position)&&!strict_at(w,*v,w.self,w.self.position,partner->position)){
     const auto replacement=replacement_work(w,w.self,*v,q,b);if(replacement.feasible){p10::Directive d;if(p10_c::safe_vacate(w,q,d)){source=v->id;d.group=v->id;d.template_id=2025;d.reason="actual replacement's own ordered move; full solo continuation repriced now";return d;}}break;
    }
   }
  }
  const p10::Group*g=w.group(source);if(g&&(g->respawning||g->contested||!g->route_known))g=nullptr;
  if(!g){int primary=w.self.type==p10::Champion::heart?1:0;g=w.group(cells[primary]);if(!g||g->respawning||g->contested)g=w.group(cells[1-primary]);if(g)source=g->id;}
  if(!g)return p10::ordinary(w);if(!g->observed||!g->complete)return p10_c::inspect(g->id,"carousel source arrival needs own observation",2022);
  if(g->cleared)return p10_c::capture(g->id,"frontliner finishes actual production custody",2024);
  const p10::Group*next=w.group(cells[0]==g->id?cells[1]:cells[0]);
  const bool partner_after=partner&&(w.self.priority>partner->priority||(w.self.priority==partner->priority&&w.self.pick<partner->pick));
  if(arm==p10::Arm::active&&front(w.self)&&partner&&partner_after&&next&&next->id!=g->id&&w.self.can_move&&partner->can_move&&partner->shield>0&&partner->shield_until>w.round+1){
   const double incoming=p10::phase_damage(*g,w.self.position);const int own_expiry=w.self.shield_until-w.round;
   if(incoming>0&&(own_expiry<=1||w.self.hp+w.self.shield<incoming*5)&&b.spent<2&&b.fuel_ok()){
    p10::Point leave{},entry{};bool geometry=false;
    // A single outgoing step cannot reverse a strict distance lead. Enumerate
    // the actual ordered pair: our move, then the present later actor's move.
    for(auto p:p10_c::cardinals(w.self.position))if(p10_c::free_cardinal(w,p)&&!p10_mobile::visible_enemy_can_intervene(w,p)){
     for(auto q:p10_c::cardinals(partner->position))if(!(q==p)&&p10_mobile::manhattan(q,partner->position)==1&&standable(w,q,w.self.id)&&strict_at(w,*g,*partner,q,p)){
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
       if(transfer.admissible&&transfer.transfer_count>0){p10::Directive d;if(p10_c::safe_vacate(w,leave,d)){source=next->id;++transfers;d.group=g->id;d.template_id=2023;d.reason="ordered shield carousel; missed entry survives independently, future entry rechecks";return d;}}
      }
     }
    }
   }
  }
  auto ingress=local_ingress(w,*g,b,2022);if(ingress.task!=p10::Task::independent)return ingress;
  if(b.spent<2){auto proof=prove(w,*g,b);if(proof.feasible){++local.witnesses;auto d=proof.first;d.template_id=2022;d.reason="independent carousel production between actual service/handoff events";return d;}return local_failed(w,*g,proof.failure);}
  return p10_c::inspect(g->id,"carousel physical appointment/warmup",2022);
 }
};
}
