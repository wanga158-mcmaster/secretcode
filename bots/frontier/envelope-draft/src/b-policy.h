#pragma once
#include "policy.h"
#include "../../common/src/physical-turn.h"
namespace p10_envelope {
struct BPolicy:Policy {
 std::array<int,6> physicalSources{},physicalFallback{};int motif=0;bool income=false,paidGear=false;
 BPolicy(){physicalSources.fill(-1);physicalFallback.fill(-1);}
 p10::Profile draft(const p10::DraftContext&d,p10::Arm arm){
  std::int64_t broad=0,narrow=0;
  for(const auto&g:d.groups){int lx=100000,hx=-1,ly=100000,hy=-1;for(const auto&m:g.monsters){lx=std::min(lx,m.position.x);hx=std::max(hx,m.position.x);ly=std::min(ly,m.position.y);hy=std::max(hy,m.position.y);}
   if(hx-lx>1||hy-ly>1)broad+=g.gems;else narrow+=g.gems;
  }
  // A complementary ranged geometry package is a real roster choice. The
  // counterfactual fixes Willow/Varg/Marcella before any gameplay observation.
  selected=arm==p10::Arm::control?0:broad>narrow?2:1;motif=selected==2?1:0;
  organization=package(selected,d.picks);assigned={};return roster(selected);
 }
 p10::Profile fastrestore(const std::vector<p10::Champion>&actual){auto out=Policy::fastrestore(actual);motif=selected==2?1:0;return out;}
 static int sector(p10::Point origin,p10::Point source){int dx=source.x-origin.x,dy=source.y-origin.y;return std::abs(dx)>=std::abs(dy)?(dx>=0?0:1):(dy>=0?2:3);}
 void appoint_general(const p10::World&w,bool fixed){
  physicalSources.fill(-1);physicalFallback.fill(-1);organization=package(std::max(0,selected),w.picks);
  std::array<int,6>used{};used.fill(-1);int usedCount=0;
  auto cost=[&](const p10::Group&g,int duty){return p10::distance(w.spawn,g.position)*8+(fixed||sector(w.spawn,g.position)==(duty+motif)%4?0:40);};
  for(int duty=0;duty<organization.actors;++duty){
   if(organization.duties[duty].partner>=0&&!(organization.paired&&duty==1))continue;
   const p10::Group*chosen=nullptr;int value=100000;
   for(const auto&g:w.groups){if(std::find(used.begin(),used.begin()+usedCount,g.id)!=used.begin()+usedCount)continue;int q=cost(g,duty);
    if(!chosen||q<value||(q==value&&g.id<chosen->id)){chosen=&g;value=q;}}
   if(chosen){physicalSources[duty]=chosen->id;used[usedCount++]=chosen->id;}
  }
  for(int duty=0;duty<organization.actors;++duty)if(organization.duties[duty].partner>=0){int lead=organization.paired&&duty<=2?1:organization.duties[duty].partner;physicalSources[duty]=physicalSources[lead];}
  for(int duty=0;duty<organization.actors;++duty){const p10::Group*other=nullptr;int value=100000;
   for(const auto&g:w.groups)if(g.id!=physicalSources[duty]){int q=cost(g,(duty+1)%organization.actors);if(!other||q<value||(q==value&&g.id<other->id)){other=&g;value=q;}}
   physicalFallback[duty]=other?other->id:-1;
  }
 }
 // Only immutable package geometry is retained. Current HP, occupancy,
 // lifecycle, action allowances and work proofs are still read in step().
 std::vector<std::shared_ptr<const p10::SourceGeometry>> appointment_geometry;
 int appointment_selection=-1,appointment_picks=-1,appointment_motif=-1;bool appointment_fixed=false,appointment_cached=false;p10::Point appointment_spawn{};
 bool appoint(const p10::World&w,bool fixed){
  const auto reader=p10::work_fuel_reader?p10::work_fuel_reader:fuel_reader;
  auto enough=[&](){return !reader||reader()>=w.action_reserve+500;};
  bool trusted=w.geometry_complete;bool same=appointment_cached&&appointment_selection==selected&&appointment_picks==w.picks&&appointment_motif==motif&&appointment_fixed==fixed&&appointment_spawn==w.spawn&&appointment_geometry.size()==w.groups.size();
  for(std::size_t i=0;i<w.groups.size();++i){if(!enough())return false;const auto&g=w.groups[i];const auto&owner=g.source_geometry;
   if(!owner||owner->source!=g.id||owner->spawns.empty()||g.position!=owner->spawns.front()){trusted=false;same=false;continue;}
   if(same){const auto&old=appointment_geometry[i];if(old.get()!=owner.get()||old.owner_before(owner)||owner.owner_before(old))same=false;}
  }
  if(trusted&&same)return true;
  appoint_general(w,fixed);
  appointment_cached=false;appointment_geometry.clear();
  if(trusted){for(const auto&g:w.groups)appointment_geometry.push_back(g.source_geometry);appointment_selection=selected;appointment_picks=w.picks;appointment_motif=motif;appointment_fixed=fixed;appointment_spawn=w.spawn;appointment_cached=true;}
  return true;
 }
 p10::Directive step(const p10::World&w,p10::Arm arm){
  using namespace p10;if(w.own_payout)income=true;if(w.own_purchase)paidGear=true;
  p10_b_local::Turn turn(w);auto done=[&](Directive d){fresh_trajectories=turn.budget.spent;return d;};
  if(w.self.channel!=Channel::none||w.capture_active)return done(wait("physical envelope keeps actual custody/channel"));
  const Actor*twin=nullptr;
  if(selected==3&&(w.self.type==Champion::cassi||w.self.type==Champion::issac)){
   const auto type=w.self.type==Champion::cassi?Champion::issac:Champion::cassi;
   for(const auto&a:w.allies)if(a.type==type&&a.visible&&a.alive)twin=&a;
   if(!twin)return done(Directive{Task::recover});
  }
  if(w.self.bag>0)return done(bank("each complementary-envelope actor banks its own actual bag"));
  if(income&&!paidGear&&w.own_gold>=w.shard_cost&&(w.self.at_spawn||w.self.type==Champion::tavi))return done(buy(w.self.range>1?Item::focus:Item::strength,1,"complementary package extension uses only the current personal wallet"));
  if(selected<0||!w.geometry_complete)return done(ordinary(w));
  if(!appoint(w,arm==Arm::control))return done(wait("package assignment validation preserves current action fuel"));int duty=-1;for(int i=0;i<organization.actors;++i)if(champion(organization.duties[i].kit)==w.self.type)duty=i;
  if(duty<0)return done(ordinary(w));
  appointmentDuty=duty;appointmentGroup=physicalSources[duty];fallbackGroup=physicalFallback[duty];
  const auto*g=w.group(appointmentGroup),*other=w.group(fallbackGroup);if(!g)return done(ordinary(w));
  appointmentPad=physical_pad(w,*g,w.self.range);
  int lead=organization.duties[duty].partner;const Actor*actualLead=nullptr;
  if(lead>=0)for(const auto&a:w.allies)if(a.visible&&a.alive&&a.type==champion(organization.duties[lead].kit))actualLead=&a;
  if(lead>=0&&w.self.type==Champion::willow){
   if(actualLead&&actualLead->hp<actualLead->max_hp&&w.self.can_cast)for(int slot=0;slot<2;++slot)if(w.self.abilities[slot].heal&&w.self.abilities[slot].ready&&distance(w.self.position,actualLead->position)<=w.self.abilities[slot].range){Directive d;d.task=Task::heal;d.actor=actualLead->id;d.cast_slot=slot;d.group=g->id;d.template_id=72;d.reason="actual complementary package heal; absent/full-health lead releases this duty";return done(d);}
   return done(other?turn.work(*other,"released envelope support executes its immutable complementary mobile job",71):ordinary(w));
  }
  if(g->respawning||g->contested)return done(other?turn.work(*other,"immutable complementary-envelope fallback",71):ordinary(w));
  if(twin&&usable(*g)&&g->living&&distance(w.self.position,g->position)<=w.self.range+2){
   const auto proof=turn.prove(*g);double incoming=0;bool known=true;
   for(const auto&source:w.groups)if(!source.cleared&&!source.respawning){double phase=phase_damage(source,twin->position);known&=phase==0||(source.observed&&source.complete);incoming+=phase;}
   if(!proof.feasible||!known||twin->hp<=incoming*(proof.clear_turns+2)+std::max(1.,twin->max_hp*.1))return done(Directive{Task::recover});
   return done(p10_b_local::Turn::first(proof,"both actual linked bodies fit the complete mobile exposure bound",70+motif));
  }
  return done(turn.work(*g,"geometry package's literal mobile clearance/custody job",70+motif));
 }
};
}
