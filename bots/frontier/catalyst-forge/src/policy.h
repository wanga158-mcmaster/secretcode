#pragma once
#include "../../common/src/contract.h"
#include "model.h"
#include "search.h"
namespace catalyst_forge {
inline constexpr p10::Profile profile{"p10-catalyst-forge",{p10::Champion::varg,p10::Champion::stellar,p10::Champion::vendetta,p10::Champion::willow,p10::Champion::marcella}};
class LegacyPolicy {
 int source_=-1,stage_=0;double premise_hp_=-1;int premise_round_=-1;bool template_veto_=false,income_witnessed_=false;
 static bool on_spawn(const p10::Actor&a,const p10::Group&g){for(auto p:g.spawns)if(a.position==p)return true;return false;}
 static int kills(const p10::Group&g,p10::Point center,int radius,double damage){int n=0;for(const auto&m:g.monsters)if(m.observed&&m.alive&&p10::distance(m.position,center)<=radius&&m.hp<=damage+1e-5)++n;return n;}
 public:
 p10::Directive step(const p10::World&w,p10::Arm arm){
  if(w.self.channel!=p10::Channel::none)return p10::wait("preserve capture/bank program channel");
  if(w.own_payout)income_witnessed_=true;
  if(w.self.bag>0&&(w.own_payout||stage_==3||w.remaining()<80)){stage_=3;return p10::bank("forge actual holder conversion");}
  if(w.own_bank_completed){stage_=4;source_=-1;template_veto_=false;}
  if(w.own_gold>=w.shard_cost&&(w.self.at_spawn||w.self.type==p10::Champion::tavi)){
   const auto stat=w.self.type==p10::Champion::stellar?p10::Item::monster_focus:p10::Item::monster_strength;
   double boundary=0;for(const auto&g:w.groups)if(p10::usable(g))for(const auto&m:g.monsters)if(m.alive)boundary=boundary?std::min(boundary,m.hp):m.hp;
   const double base=w.self.type==p10::Champion::stellar?1.4*w.self.focus:w.self.strength;
   const double growth=w.self.type==p10::Champion::stellar?1.4*w.self.focus_growth:w.self.strength_growth;
   if(growth>0&&boundary>base){int n=int(std::ceil((boundary-base)/growth));if(n>0&&n<=w.own_gold/w.shard_cost&&n<=5)return p10::buy(stat,n,"personally funded hit-count boundary");}
   if(w.self.strength_shards+w.self.focus_shards<5)return p10::buy(stat,1,"first-income forge capital");
  }
  if(w.self.type==p10::Champion::willow&&w.self.abilities[0].ready&&w.self.can_cast){
   for(const auto&a:w.allies)if(a.visible&&a.alive&&a.channel==p10::Channel::none&&p10::distance(a.position,w.self.position)<=4&&a.max_hp-a.hp>=std::min(3*w.self.focus,a.max_hp*.35)){
    p10::Directive d;d.task=p10::Task::heal;d.actor=a.id;d.cast_slot=0;d.reason="scheduled productive heal";return d;}
  }
  const p10::Group*source=w.group(source_);
  if(source&&(!p10::usable(*source)||source->monsters.size()>12)){source_= -1;source=nullptr;}
  // Gold-only captures are a bootstrap, not an indefinitely repeated scoring
  // circuit. Once this actor actually receives first income, promote a current
  // complete positive-gem program over a gold renewal. No predicted purchase,
  // unseen helper or weaker safety certificate can authorize this transition.
  if(income_witnessed_&&!w.capture_active&&(!source||source->gems==0)){
   const p10::Group*conversion=nullptr;
   for(const auto&g:w.groups)if(p10::usable(g)&&g.gems>0&&g.safe_solo&&g.monsters.size()<=12&&g.travel>=0&&g.to_bank>=0&&p10::cycle_ticks(w,g)<=std::min(96,w.remaining()))
    if(!conversion||g.travel<conversion->travel||(g.travel==conversion->travel&&g.id<conversion->id))conversion=&g;
   if(conversion){source=conversion;source_=source->id;}
  }
  if(!source){int inspected=0;for(const auto&g:w.groups)if(p10::usable(g)&&g.travel>=0&&g.to_bank>=0&&g.monsters.size()<=12&&++inspected<=4){
    bool feasible=g.safe_solo;int visible_crew=1;for(const auto&a:w.allies)if(a.visible&&a.alive&&a.channel==p10::Channel::none&&p10::distance(a.position,g.position)<=4)++visible_crew;
    // Two phase safety must stand even without predicted remote helper damage.
    feasible|=visible_crew>1&&w.self.hp+w.self.shield>2*g.incoming&&g.clear_turns>=0&&g.clear_turns<=16;
    if(feasible&&(!source||g.travel<source->travel||(g.travel==source->travel&&g.id<source->id)))source=&g;
   }
   if(source)source_=source->id;}
  if(!source)return p10::ordinary(w);
  stage_=source->cleared?2:1;
  if(source->cleared){p10::Directive d;d.task=p10::Task::capture;d.group=source->id;d.reason="sole safe forge payout owner";
   // A holder leaves only after a distinct-spawn successor is actually visible.
   if(w.self.type!=p10::Champion::marcella&&source->spawns.size()>1&&on_spawn(w.self,*source))for(const auto&a:w.allies)if(a.visible&&a.alive&&a.type==p10::Champion::marcella&&on_spawn(a,*source)){
     for(const auto&next:w.groups)if(next.id!=source->id&&p10::usable(next)&&next.safe_solo&&next.living)return p10::farm(next.id,"released worker named next source");}
   return d;}
  if(w.last.kind==p10::Receipt::Kind::blocked||(!w.safe_now&&premise_round_==w.round-1))template_veto_=true;
  const bool active=arm==p10::Arm::active&&!template_veto_;const p10::Actor*vendetta=nullptr;
  for(const auto&a:w.allies)if(a.visible&&a.alive&&a.type==p10::Champion::vendetta&&a.channel==p10::Channel::none)vendetta=&a;
  const p10::Monster*target=nullptr;int best_hits=-1;bool preparation=false;
  for(const auto&m:source->monsters)if(m.observed&&m.alive){
   int hits=1;if(w.self.type==p10::Champion::varg){hits=0;for(const auto&n:source->monsters)if(n.alive&&p10::distance(n.position,m.position)<=1)++hits;}
   bool prepares=false;if(active&&vendetta&&w.self.type==p10::Champion::varg&&vendetta->cooldown[0]==0&&p10::distance(vendetta->position,m.position)<=2)
    for(const auto&n:source->monsters)if(n.alive&&n.strength==0&&p10::distance(n.position,m.position)<=1&&n.hp>w.self.strength&&n.hp-w.self.strength<=vendetta->strength)prepares=true;
   if(active&&w.self.type==p10::Champion::vendetta&&w.self.cooldown[0]==0&&m.hp<=w.self.strength&&m.strength==0)hits+=20;
   if(prepares)hits+=30;
   if(!target||hits>best_hits||(hits==best_hits&&m.hp<target->hp)){target=&m;best_hits=hits;preparation=prepares;}
  }
  if(!target)return p10::farm(source->id);
  // A geometrically unknown collateral group is never admitted as zero hazard.
  if(w.self.type==p10::Champion::varg)for(const auto&g:w.groups)if(g.id!=source->id)for(const auto&m:g.monsters)if(m.alive&&p10::distance(m.position,target->position)<=1&&(!g.observed||!g.complete||!g.safe_solo))return p10::ordinary(w);
  p10::Directive d=p10::farm(source->id,"positioned whole-group forge suffix");d.monster=target->id;d.valid_round=w.round;d.template_id=preparation?1:0;d.reserve_reset=preparation;
  if(p10::distance(w.self.position,target->position)<=w.self.range){d.task=p10::Task::attack;
   if(w.self.type==p10::Champion::stellar&&w.self.can_cast){
    const int area_kills=kills(*source,target->position,1,1.4*w.self.focus);const bool preserve=active&&vendetta&&vendetta->cooldown[0]>0;
    if(w.self.abilities[1].ready&&area_kills>1&&!preserve){d.task=p10::Task::cast;d.cast_slot=1;d.monster=-1;d.destination=target->position;d.reason="area-first actual hit set cleanup";}
    else if(w.self.abilities[0].ready){const p10::Monster*single=target;for(const auto&m:source->monsters)if(m.observed&&m.alive&&m.strength>0&&p10::distance(w.self.position,m.position)<=4)single=&m;d.task=p10::Task::cast;d.cast_slot=0;d.monster=single->id;d.reason="single target preserves future catalyst kills";}
   }
  }
  premise_hp_=source->hp;premise_round_=w.round;d.expected_min=w.round+std::max(0,source->clear_turns)+w.capture_turns+1;d.expected_max=d.expected_min+std::max(0,source->to_bank)+w.ticks(w.self.bag+source->gems);return d;
 }
};
class Policy {
 LegacyPolicy alternate_{};int source_=-1;bool income_=false;
 using FuelReader=int(*)();FuelReader fuel_reader_=nullptr;
 public:
 int variant=0; // 0: positioned crew suffix; 1: local preparation priority.
 static constexpr bool observe_enemy_exits=true;
 void set_fuel_reader(FuelReader r){fuel_reader_=r;}
 p10::Directive step(const p10::World&w,p10::Arm arm){
  if(variant==1)return alternate_.step(w,arm);
  if(w.own_payout)income_=true;
  if(w.own_bank_completed)source_=-1;
  if(w.self.channel!=p10::Channel::none)return p10::wait("forge committed personal service");
  if(w.capture_active){p10::Directive d;d.task=p10::Task::capture;d.group=w.productive_group;return d;}
  if(w.self.bag>0)return p10::bank("forge actual production receipt conversion");
  if(w.own_gold>=w.shard_cost&&(w.self.at_spawn||w.self.type==p10::Champion::tavi)&&w.self.strength_shards+w.self.focus_shards<10)
   return p10::buy(w.self.type==p10::Champion::stellar?p10::Item::monster_focus:p10::Item::monster_strength,1,"forge personally funded program threshold");
  if(w.self.type==p10::Champion::willow&&w.self.can_cast&&w.self.abilities[0].ready)
   for(const auto&a:w.allies)if(a.visible&&a.alive&&a.channel==p10::Channel::none&&p10::distance(a.position,w.self.position)<=4&&a.max_hp-a.hp>=std::min(3*w.self.focus,a.max_hp*.3)){
    p10::Directive d;d.task=p10::Task::heal;d.actor=a.id;d.cast_slot=0;return d;}
  const p10::Group*chosen=nullptr;double best=-1;
  // Screen the card's geometry cheaply; only the two chosen organizations get
  // a fresh positioned trajectory. No actor borrows unseen crew damage.
  for(const auto&g:w.groups)if(p10::usable(g)&&g.travel>=0&&g.to_bank>=0&&g.monsters.size()<=12){
   if(g.cleared){chosen=&g;break;}
   double splash=0;for(const auto&m:g.monsters)if(m.alive){int n=0;for(const auto&v:g.monsters)if(v.alive&&p10::distance(v.position,m.position)<=1)++n;splash=std::max(splash,double(n));}
   const double value=(g.gems*(income_?8.:4.)+g.gold*.08+1+splash*2)/(g.travel+6+g.hp/std::max(1.,w.self.strength*std::max(1.,splash)));
   if(value>best){best=value;chosen=&g;}
  }
  if(!chosen)return p10::ordinary(w);
  source_=chosen->id;
  if(chosen->cleared){p10::Directive d;d.task=p10::Task::capture;d.group=source_;d.template_id=4;return d;}
  const int fuel=fuel_reader_?fuel_reader_():w.fuel;
  if(fuel<std::max(w.action_reserve,24000)+2000)return p10::ordinary(w);
  auto a=compile(w,*chosen,arm==p10::Arm::active?0:1,fuel_reader_);
  auto b=arm==p10::Arm::active?compile(w,*chosen,1,fuel_reader_):a; // strong same-roster positioned greedy comparator
  const auto&pick=arm==p10::Arm::active&&better(a.result,b.result)?a:b;
  if(pick.result.legal&&pick.result.safe&&pick.result.complete&&pick.first.task!=p10::Task::independent)return pick.first;
  // A crew too far apart first earns ordinary visible income. This is not a
  // hypothetical cooperative clearance or a claimed successful forge template.
  return p10::ordinary(w);
 }
};
} // namespace catalyst_forge
