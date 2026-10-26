#pragma once
#include "../../common/src/combat.h"
namespace p10_envelope {
struct Policy:LegacyPolicy {
 int variant=0,appointmentGroup=-1,fallbackGroup=-1,appointmentDuty=-1;
 p10::Point appointmentPad{},fallbackPad{};
 static bool physical_before(const p10::World&w,const p10::Group&a,const p10::Group&b){
  const int da=p10::distance(w.spawn,a.position),db=p10::distance(w.spawn,b.position);
  return da!=db?da<db:a.id<b.id;
 }
 static p10::Point physical_pad(const p10::World&w,const p10::Group&g,int range,bool support=false,p10::Point lead={}){
  p10::Point chosen=g.position;bool found=false;int best=100000;
  for(auto pad:g.approach_pads){bool reaches=false;for(const auto&m:g.monsters)reaches|=p10::distance(pad,m.position)<=range;
   if(!reaches||(support&&(pad==lead||p10::distance(pad,lead)>4)))continue;
   int cost=p10::distance(w.spawn,pad);if(support){cost=0;for(const auto&m:g.monsters)if(p10::distance(pad,m.position)<=m.range)cost+=int(m.strength);}
   if(!found||cost<best||(cost==best&&(pad.x<chosen.x||(pad.x==chosen.x&&pad.y<chosen.y)))){chosen=pad;best=cost;found=true;}
  }
  return chosen;
 }
 void reconstruct(const p10::World&w,int duty){
  using namespace p10;std::array<const Group*,12> ordered{};int count=0;
  for(const auto&g:w.groups){int at=0;while(at<count&&physical_before(w,*ordered[at],g))++at;
   if(at>=12)continue;for(int n=std::min(count,11);n>at;--n)ordered[n]=ordered[n-1];ordered[at]=&g;count=std::min(12,count+1);}
  if(!count)return;
  assigned={};assigned.packageId=selected;organization=package(std::max(0,selected),w.picks);
  for(int i=0,ordinal=organization.paired?1:0;i<organization.actors&&assigned.circuitsUsed<6;++i){
   if(organization.duties[i].partner>=0)continue;
   auto&c=assigned.circuits[assigned.circuitsUsed++];c.source=ordered[ordinal%count]->id;c.lead=i;c.holder=i;
   for(int h=0;h<organization.actors;++h)if(organization.duties[h].partner==i)c.helper=h;
   if(i==duty||c.helper==duty){appointmentGroup=c.source;fallbackGroup=ordered[(ordinal+std::max(1,organization.actors))%count]->id;appointmentDuty=duty;}
   ++ordinal;
  }
  // Linked Cassi/Issac share one physical appointment, with Willow's actual
  // healing duty, while the remaining selections retain distinct circuits.
  if(organization.paired&&duty<=2){appointmentGroup=ordered[0]->id;fallbackGroup=ordered[1%count]->id;}
 }
 p10::Directive execute_circuit(const p10::World&w,const p10::Group&g,p10::Point pad){
  using namespace p10;
  if(!g.observed||!g.complete){if(!w.safe_now)return {Task::recover};Directive d;d.task=Task::inspect;d.group=g.id;d.destination=pad;d.reason="privately inspect immutable envelope appointment";return d;}
  if(g.cleared)return farm(g.id,"retain deterministic envelope payout custody");
  if(!usable(g)||fresh_trajectories>=2||(fuel_reader?fuel_reader():w.fuel)<=w.action_reserve+2500)return ordinary(w);
  const Point stance=distance(w.self.position,g.position)<=w.self.range?w.self.position:pad;
  ++fresh_trajectories;const auto proof=stationary_work(w,g,stance,64);
  if(!proof.feasible)return ordinary(w);
  if(!(stance==w.self.position)){if(!w.safe_now)return {Task::recover};Directive d;d.task=Task::move;d.group=g.id;d.destination=stance;d.reason="approach reconstructed envelope circuit pad; recheck on arrival";return d;}
  auto d=proof.first;d.template_id=20+std::max(0,selected);d.reason="execute this physical member's independently certified envelope duty";return d;
 }
 p10::Directive step(const p10::World&w,p10::Arm arm){
  using namespace p10;if(variant==1||arm==Arm::control)return LegacyPolicy::step(w,arm);
  fresh_trajectories=0;if(w.own_payout)firstPayout=true;if(w.own_purchase)bought=true;
  if(w.self.channel!=Channel::none||w.capture_active)return wait("preserve observed envelope capture/channel");
  if(w.self.bag>0)return bank("envelope member converts its actual privately owned bag");
  if(firstPayout&&!bought&&w.own_gold>=w.shard_cost&&(w.self.at_spawn||w.self.type==Champion::tavi))return buy(w.self.type==Champion::willow||w.self.type==Champion::prism||w.self.type==Champion::cassi?Item::focus:Item::strength,1,"envelope extension uses this actor's real wallet");
  if(!w.geometry_complete)return ordinary(w);
  int duty=-1;for(int i=0;i<organization.actors;++i)if(champion(organization.duties[i].kit)==w.self.type)duty=i;
  if(duty<0||selected<0)return ordinary(w);
  reconstruct(w,duty);const auto*g=w.group(appointmentGroup),*fallback=w.group(fallbackGroup);
  if(!g)return ordinary(w);
  int lead=duty;if(organization.duties[duty].partner>=0)lead=organization.duties[duty].partner;
  const Actor*leader=w.self.type==champion(organization.duties[lead].kit)?&w.self:nullptr;
  for(const auto&a:w.allies)if(a.visible&&a.alive&&a.type==champion(organization.duties[lead].kit))leader=&a;
  const Point leadPad=physical_pad(w,*g,leader?leader->range:1);
  appointmentPad=physical_pad(w,*g,w.self.range,lead!=duty,leadPad);
  if(lead!=duty&&w.self.type==Champion::willow){
   if(leader&&leader->visible&&leader->alive&&leader->hp<leader->max_hp&&w.self.can_cast){
    for(int slot=0;slot<2;++slot)if(w.self.abilities[slot].heal&&w.self.abilities[slot].ready&&distance(w.self.position,leader->position)<=w.self.abilities[slot].range){
     Directive d;d.task=Task::heal;d.actor=leader->id;d.cast_slot=slot;d.group=g->id;d.reason="actual missing HP releases a paid envelope healing service";return d;}
   }
   // No permanent helper parking: a missing/full-health lead releases the
   // healer to the same immutable complementary fallback on every actor turn.
   if(fallback)return execute_circuit(w,*fallback,physical_pad(w,*fallback,w.self.range));
  }
  if(organization.paired&&(w.self.type==Champion::cassi||w.self.type==Champion::issac)){
   const auto twin=w.self.type==Champion::cassi?Champion::issac:Champion::cassi;const Actor*other=nullptr;
   for(const auto&a:w.allies)if(a.type==twin&&a.visible&&a.alive)other=&a;
   if(!other||other->hp<=phase_damage(*g,other->position)*3+1)return {Task::recover};
  }
  if(g->respawning||g->contested){if(fallback)return execute_circuit(w,*fallback,physical_pad(w,*fallback,w.self.range));return ordinary(w);}
  return execute_circuit(w,*g,appointmentPad);
 }
};
}
