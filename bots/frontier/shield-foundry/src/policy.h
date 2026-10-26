#pragma once
#include "../../common/src/contract.h"
#include "model.h"
#include "search.h"
namespace shield_foundry {
inline constexpr p10::Profile profile{"p10-shield-foundry",{p10::Champion::varg,p10::Champion::heart,p10::Champion::willow,p10::Champion::marcella,p10::Champion::tavi}};
class AlternatePolicy{
 int source_=-1,lead_=-1,stage_=0;int transfer_round_=-1;double expected_hp_=0;
 static bool front(const p10::Actor&a){return a.type==p10::Champion::varg||a.type==p10::Champion::heart;}
 static Body body(const p10::Actor&a,const p10::World&w){return {a.id,{a.position.x,a.position.y},a.hp,a.max_hp,a.shield,a.focus,a.strength,a.shield_until<0?-1:a.shield_until-w.round,a.cooldown[a.type==p10::Champion::heart?0:1],a.type==p10::Champion::heart,a.visible&&a.alive};}
 static bool strict_after(const p10::World&w,const p10::Group&g,const p10::Actor&old,const p10::Actor&next,p10::Point old_tile){
  for(const auto&m:g.monsters)if(m.alive&&m.strength>0){
   const int d=p10::distance(m.position,next.position);if(d>m.range||d>=p10::distance(m.position,old_tile))return false;
   for(const auto&a:w.allies)if(a.id!=old.id&&a.id!=next.id&&a.visible&&a.alive&&p10::distance(m.position,a.position)<=d)return false;
  }
  return true;
 }
 public:p10::Directive step(const p10::World&w,p10::Arm arm){
  if(w.self.channel!=p10::Channel::none)return p10::wait("preserve foundry service channel");
  if(w.self.bag>0&&(w.own_payout||stage_==3||w.remaining()<96)){stage_=3;return p10::bank("foundry actual personal conversion");}
  if(w.own_bank_completed){stage_=4;source_=-1;lead_=-1;}
  if(w.own_gold>=w.shard_cost&&(w.self.at_spawn||w.self.type==p10::Champion::tavi)){
   if(w.self.type==p10::Champion::willow&&w.self.focus_shards<5)return p10::buy(p10::Item::jungle_focus,1,"Willow actual own payout funds service");
   if(front(w.self)&&w.self.health_shards<5)return p10::buy(p10::Item::jungle_health,1,"funded frontliner next-source endurance");
   if(w.self.strength_shards<5)return p10::buy(p10::Item::monster_strength);
  }
  const p10::Group*g=w.group(source_);if(g&&!p10::usable(*g)){g=nullptr;source_=-1;}
  if(!g){int screened=0;for(const auto&v:w.groups)if(p10::usable(v)&&v.monsters.size()<=12&&v.travel>=0&&++screened<=4){
    if((v.safe_solo||w.self.hp+w.self.shield>2*v.incoming)&&(!g||v.travel<g->travel))g=&v;
   }
   if(g)source_=g->id;}
  if(!g)return p10::ordinary(w);
  if(g->cleared){stage_=2;p10::Directive d;d.task=p10::Task::capture;d.group=g->id;d.reason="foundry safe actual custody";return d;}
  stage_=1;const p10::Actor*partner=nullptr;
  for(const auto&a:w.allies)if(a.visible&&a.alive&&front(a)&&a.id!=w.self.id&&a.channel==p10::Channel::none&&p10::distance(a.position,g->position)<=6)partner=&a;
  double incoming=0;for(const auto&m:g->monsters)if(m.alive&&m.awake&&p10::distance(m.position,w.self.position)<=m.range)incoming+=m.strength;
  if(w.self.type==p10::Champion::willow&&w.self.abilities[0].ready&&w.self.can_cast){
   const p10::Actor*need=nullptr;double deficit=0;
   for(const auto&a:w.allies)if(a.visible&&a.alive&&a.channel==p10::Channel::none&&p10::distance(a.position,w.self.position)<=4){double danger=0;for(const auto&m:g->monsters)if(m.alive&&m.awake&&p10::distance(a.position,m.position)<=m.range)danger+=m.strength;
    const double missing=a.max_hp-a.hp;if(missing>0&&(a.hp+a.shield<danger*3||missing>=3*w.self.focus)&&(!need||a.hp+a.shield-danger*2<deficit)){need=&a;deficit=a.hp+a.shield-danger*2;}}
   if(need){p10::Directive d;d.task=p10::Task::heal;d.actor=need->id;d.cast_slot=0;d.reason="lower-bound urgent discrete heal pulse";return d;}
  }
  if(front(w.self)){
   const int shield_slot=w.self.type==p10::Champion::heart?0:1;
   if(partner&&arm==p10::Arm::active&&w.self.can_move&&w.self.shield>0){
    // This owner observes the first owner's completed vacate, then enters a
    // now-free pad. It never treats a predicted partner destination as occupied.
    const std::array<p10::Point,6>delta{{{1,0},{-1,0},{0,1},{0,-1},{1,1},{-1,-1}}};
    const double volley=std::max(g->incoming,1.);
    if(partner->hp+partner->shield<volley*5&&w.self.hp+w.self.shield>volley*3)
     for(auto v:delta){const p10::Point pad{w.self.position.x+v.x,w.self.position.y+v.y};auto future=w.self;future.position=pad;
      if(pad==partner->position||!strict_after(w,*g,*partner,future,partner->position))continue;
      p10::Directive d;d.task=p10::Task::move;d.destination=pad;d.strict_tank=true;d.group=g->id;d.template_id=3;d.reason="observed vacate permits ordered second-owner strict-nearest entry";return d;
     }
   }
   // Both arms retain naturally injured Heart work. HP and shield are distinct.
   if(incoming>0&&w.self.can_cast&&w.self.abilities[shield_slot].ready&&w.self.shield<incoming*2&&w.self.hp<incoming*5){p10::Directive d;d.task=p10::Task::shield;d.cast_slot=shield_slot;d.group=g->id;d.strict_tank=true;d.reserve_reset=true;
    // Compound execution is shield, fresh allowance/mask check, one exact basic
    // on this in-range monster. It includes neither approach nor a second cast.
    for(const auto&m:g->monsters)if(m.observed&&m.alive&&p10::distance(w.self.position,m.position)<=w.self.range){d.monster=m.id;break;}
    d.reason="staggered shield then positioned productive basic";return d;}
   if(partner&&arm==p10::Arm::active&&incoming>0&&w.self.hp+w.self.shield<incoming*5&&partner->hp+partner->shield>incoming*3){
    const std::array<p10::Point,6>delta{{{1,0},{-1,0},{0,1},{0,-1},{1,1},{-1,-1}}};int evaluated=0;
    for(auto v:delta){p10::Point destination{w.self.position.x+v.x,w.self.position.y+v.y};if(destination==partner->position)continue;
     auto arriving=*partner;bool ordered=false;
     if(!strict_after(w,*g,w.self,arriving,destination)){
      const bool partner_after=w.self.priority>partner->priority||(w.self.priority==partner->priority&&w.self.pick<partner->pick);
      if(!partner_after||!partner->can_move||partner->shield<=0||p10::distance(partner->position,w.self.position)>1||w.self.hp+w.self.shield<=incoming*2)continue;
      arriving.position=w.self.position;ordered=strict_after(w,*g,w.self,arriving,destination);if(!ordered)continue;
     }
     // Instantiate only two trajectories; geometry screening is not a rollout.
     if(evaluated++>=2)break;Trajectory t;t.bodies={body(w.self,w),body(*partner,w)};t.count=int(g->monsters.size());t.length=ordered?2:1;t.clearance=std::max(1,g->clear_turns);t.capture=w.capture_turns;t.bank_travel=std::max(0,g->to_bank);t.bank_ticks=w.ticks(w.self.bag+g->gems);t.renew_travel=std::max(0,g->travel);t.missed_attacks=ordered?2:1;t.phases[0]={0,{destination.x,destination.y},true,false,false,false,0};if(ordered)t.phases[1]={1,{arriving.position.x,arriving.position.y},true,false,false,false,0};
     for(int i=0;i<t.count;++i){const auto&m=g->monsters[i];t.monsters[i]={{m.position.x,m.position.y},m.strength,m.range,m.stun>0?m.stun:-1,m.awake};}
     auto result=run(t,true);if(result.admissible){lead_=partner->id;transfer_round_=w.round;expected_hp_=result.hp[0];p10::Directive d;d.task=p10::Task::move;d.destination=destination;d.group=g->id;d.strict_tank=true;d.reason="ordered strict-nearest physical tank handoff";d.valid_round=w.round;d.template_id=2;return d;}
    }
   }
   if(incoming*2>=w.self.hp+w.self.shield){p10::Directive d;d.task=p10::Task::recover;d.reason="no admissible continuation and two-phase escape";return d;}
  }
  // A fixed lead is selected from exactly the same visible tactical candidates.
  if(lead_<0||arm==p10::Arm::control){lead_=w.self.id;if(partner&&partner->hp+partner->shield>w.self.hp+w.self.shield)lead_=partner->id;}
  auto d=p10::farm(g->id,lead_==w.self.id?"fixed/established foundry productive lead":"off-watch productive combat or named solo fallback");
  // Preserve a tank pad only while this owner can actually do positioned work.
  // Awake ranged camp exposure alone must not freeze a melee actor outside its
  // basic range; the ordinary executor still validates its approach step afresh.
  bool productive_position=false;
  for(const auto&m:g->monsters)if(m.observed&&m.alive&&p10::distance(w.self.position,m.position)<=w.self.range)productive_position=true;
  d.strict_tank=incoming>0&&productive_position;d.reserve_reset=incoming>0&&w.self.hp<incoming*5;d.expected_min=w.round+std::max(0,g->clear_turns)+w.capture_turns+1;d.expected_max=d.expected_min+std::max(0,g->to_bank)+w.ticks(w.self.bag+g->gems);return d;
 }
};
class Policy{
 AlternatePolicy alternate_{};int source_=-1;bool income_=false;
 using FuelReader=int(*)();FuelReader fuel_reader_=nullptr;
 static bool front(const p10::Actor&a){return a.type==p10::Champion::varg||a.type==p10::Champion::heart;}
 public:int variant=0; // 0: planned complete shield clearance; 1: emergency handoff.
 static constexpr bool observe_enemy_exits=true;
 void set_fuel_reader(FuelReader r){fuel_reader_=r;}
 p10::Directive step(const p10::World&w,p10::Arm arm){
  if(variant==1)return alternate_.step(w,arm);
  if(w.own_payout)income_=true;if(w.own_bank_completed)source_=-1;
  if(w.self.channel!=p10::Channel::none)return p10::wait("foundry actual personal service");
  if(w.capture_active){p10::Directive d;d.task=p10::Task::capture;d.group=w.productive_group;return d;}
  if(w.self.bag>0)return p10::bank("foundry complete actual holder conversion");
  if(w.own_gold>=w.shard_cost&&(w.self.at_spawn||w.self.type==p10::Champion::tavi)){
   if(w.self.type==p10::Champion::willow&&w.self.focus_shards<5)return p10::buy(p10::Item::jungle_focus,1,"support's actual own income funds foundry service");
   if(front(w.self)&&w.self.strength_shards<5)return p10::buy(p10::Item::monster_strength,1,"clearance time and shield-gap threshold");
   if(front(w.self)&&w.self.health_shards<5)return p10::buy(p10::Item::jungle_health,1,"funded next foundry operating cycle");
  }
  const p10::Group*g=w.group(source_);if(g&&(!g->route_known||g->respawning||g->contested))g=nullptr;
  if(!g){double best=-1;for(const auto&v:w.groups)if(v.route_known&&v.program_available&&!v.respawning&&!v.contested&&v.travel>=0&&v.to_bank>=0&&v.monsters.size()<=12){
   // A stable source appointment recruits the shield pair before damage,
   // rather than relying on independent nearest-source accidents.
   const double value=(v.gems*(income_?8.:5.)+v.gold*.05+1)/(8+p10::distance(w.spawn,v.position));if(value>best){best=value;g=&v;}}
   if(g)source_=g->id;}
  if(!g)return p10::ordinary(w);
  if(!g->observed||!g->complete){auto d=p10::farm(g->id,"privately verify joint foundry appointment");d.task=p10::Task::inspect;return d;}
  if(g->cleared){p10::Directive d;d.task=p10::Task::capture;d.group=g->id;d.template_id=43;return d;}
  const p10::Actor*partner=nullptr,*willow=nullptr;
  for(const auto&a:w.allies)if(a.visible&&a.alive&&a.channel==p10::Channel::none&&p10::distance(a.position,g->position)<=6){if(front(a)&&a.id!=w.self.id)partner=&a;if(a.type==p10::Champion::willow)willow=&a;}
  const int fuel=fuel_reader_?fuel_reader_():w.fuel;if(fuel<w.action_reserve+2000)return p10::ordinary(w);
  auto fixed=clearance(w,*g,partner,willow,{},fuel_reader_);
  Clearance transfer;
  if(front(w.self)&&partner&&w.self.can_move&&arm==p10::Arm::active){
   double old=0;for(const auto&m:g->monsters)if(m.alive&&m.awake&&m.strength>0&&p10::distance(w.self.position,m.position)<=m.range)old+=m.strength;
   if(old>0&&(w.self.shield_until<=w.round+2||w.self.hp+w.self.shield<old*6)){
    p10::Point destination{};double best=-1;
    for(auto p:w.visible_open_tiles)if(p10::distance(p,w.self.position)==1&&!(p==partner->position)){
     bool strict=true;for(const auto&m:g->monsters)if(m.alive&&m.strength>0){const int d=p10::distance(partner->position,m.position);if(d>m.range||d>=p10::distance(p,m.position))strict=false;for(const auto&a:w.allies)if(a.id!=partner->id&&a.alive&&a.visible&&p10::distance(a.position,m.position)<=d)strict=false;}
     const double q=partner->hp+partner->shield-p10::distance(p,g->position);if(strict&&q>best){best=q;destination=p;}}
    if(destination.x>=0)transfer=clearance(w,*g,partner,willow,destination,fuel_reader_);
   }
  }
  if(transfer.complete&&transfer.transfers>0&&(!fixed.complete||(transfer.bank<=fixed.bank+1&&transfer.own_hp>fixed.own_hp)))return transfer.first;
  if(fixed.complete)return fixed.first;
  // A lone actor still needs an explicit real program, including its shield;
  // ordinary fallback does not become a fictitious partner certificate.
  bool contact=false;for(const auto&m:g->monsters)if(m.alive&&p10::distance(w.self.position,m.position)<=w.self.range)contact=true;
  if(!contact){auto d=p10::farm(g->id,"approach actual joint-clearance contact before admitting shield work");d.task=p10::Task::inspect;d.template_id=44;return d;}
  return p10::ordinary(w);
 }
};
} // namespace shield_foundry
