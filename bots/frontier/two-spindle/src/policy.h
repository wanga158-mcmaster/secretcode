#pragma once
#include "../../common/src/contract.h"
#include "model.h"
namespace two_spindle {
inline constexpr p10::Profile profile{"p10-two-spindle",{p10::Champion::vendetta,p10::Champion::marcella,p10::Champion::tavi,p10::Champion::varg,p10::Champion::willow}};
class AlternatePolicy{
 std::array<Job,2>jobs_{};int count_=0,current_=-1,stage_=0;
 static bool holder(const p10::Actor&a,const p10::Group&g){if(!a.visible||!a.alive||a.channel!=p10::Channel::none)return false;for(auto p:g.spawns)if(a.position==p)return true;return false;}
 static const p10::Actor*collector(const p10::World&w){const auto*a=w.pick(1);return a&&a->visible&&a->alive&&a->channel==p10::Channel::none?a:nullptr;}
 void update(const p10::World&w){
  for(int i=0;i<count_;++i){auto&j=jobs_[i];const auto*g=w.group(j.source);if(!g||!g->observed)continue;j.observed=true;j.observed_round=w.round;j.contested=g->contested;
   if(g->cleared)j.clearFinish=w.round;
   if(g->respawning){j.paid=true;j.renewal_confirmed=true;j.renewal=std::max(0,g->respawn_remaining);}
   if(w.own_payout&&w.last_payout_group==j.source){j.paid=true;j.actualOwner=w.self.id;}
  }
 }
 public:p10::Directive step(const p10::World&w,p10::Arm arm){
  update(w);if(w.self.channel!=p10::Channel::none)return p10::wait("spindle actual owner service interval");
  if(w.self.bag>0&&(w.own_payout||stage_==3||w.remaining()<128)){stage_=3;return p10::bank("spindle actual payout ownership preserved");}
  if(w.own_bank_completed){stage_=4;current_=-1;for(int i=0;i<count_;++i)if(jobs_[i].paid){jobs_[i]=jobs_[--count_];--i;}}
  if(w.own_gold>=w.shard_cost&&(w.self.at_spawn||w.self.type==p10::Champion::tavi)&&w.self.strength_shards<10)return p10::buy(p10::Item::monster_strength,1,"post-bank personal spindle capital");
  const bool admission_owner=w.self.pick==0||(w.picks>=4&&w.self.pick==3);const auto*c=collector(w);
  int unclaimed=0;for(int i=0;i<count_;++i)if(!jobs_[i].paid)++unclaimed;
  // The collector adopts only personally witnessed ready work, never a remote queue.
  if(!admission_owner){const p10::Group*ready=nullptr;for(const auto&g:w.groups)if(p10::usable(g)&&g.cleared&&g.travel>=0&&(!ready||g.travel<ready->travel))ready=&g;
   if(ready&&w.self.type==p10::Champion::marcella){p10::Directive d;d.task=p10::Task::capture;d.group=ready->id;d.reason="visible ready-stage converter adoption";return d;}
   if(w.self.type==p10::Champion::willow&&w.self.abilities[0].ready)for(const auto&a:w.allies)if(a.visible&&a.alive&&a.channel==p10::Channel::none&&a.max_hp-a.hp>=3*w.self.focus&&p10::distance(a.position,w.self.position)<=4){p10::Directive d;d.task=p10::Task::heal;d.actor=a.id;d.cast_slot=0;return d;}
   // Independent off-cell jobs must not add a second opener in the owned cell.
   for(const auto&g:w.groups)if(p10::usable(g)&&g.safe_solo&&g.travel>=0&&(!c||p10::distance(g.position,c->position)>6)&&(!w.pick(0)||p10::distance(g.position,w.pick(0)->position)>6))return p10::farm(g.id,"independent producer outside admission cell");
   return p10::ordinary(w);
  }
  const p10::Group*g=w.group(w.capture_active?w.productive_group:current_);if(g&&!p10::usable(*g)){g=nullptr;current_=-1;}
  if(g&&g->cleared){stage_=2;
   if(arm==p10::Arm::active&&c&&g->spawns.size()>1&&holder(w.self,*g)&&holder(*c,*g)&&c->id!=w.self.id&&unclaimed<2){
    const p10::Group*next=nullptr;for(const auto&v:w.groups)if(v.id!=g->id&&p10::usable(v)&&v.safe_solo&&v.living&&v.travel>=0&&p10::distance(v.position,g->position)<=16&&(!next||v.travel<next->travel))next=&v;
    if(next){Workcell cell;cell.count=2;cell.unclaimed=unclaimed;cell.collector_present=true;cell.remaining=w.remaining();cell.tribute_multiplier=w.tribute_multiplier;cell.tribute_base=w.tribute_base;cell.tribute_minimum=w.tribute_minimum;cell.second_travel=next->travel;
     for(int i=0;i<2;++i){auto&j=cell.jobs[i];const auto&v=i?*next:*g;j.source=v.id;j.observed=true;j.clear_work=i?std::max(1,v.clear_turns):0;j.capture=w.capture_turns;j.reward=int(v.gems);j.holder_id=c->id;j.clearer_id=w.self.id;j.holder_bag=int(c->bag);j.bank_travel=std::max(0,v.to_bank);j.spawn_count=int(v.spawns.size());j.holder_witnessed=i==0;j.free_successor_spawn=i==0;j.custodyMode=i==0?Custody::witnessed_overlap:Custody::self;j.renewal=v.respawn;}
     // The second source remains the clearer's own capture until a *new* witness.
     auto serial=schedule(cell,Dispatch::serial);auto overlapping=schedule(cell,Dispatch::overlap);
     const int released_clear=next->travel+std::max(1,next->clear_turns);const int owner_service=1+w.capture_turns+std::max(0,g->to_bank)+w.ticks(c->bag+g->gems);
     if((overlapping.complete&&(!serial.complete||overlapping.finish<serial.finish))||released_clear<owner_service){
      // Record admission before returning the action: a later owner turn must
      // still count this exposed job against the same two-job WIP ceiling.
      bool recorded=false;for(int i=0;i<count_;++i)recorded|=jobs_[i].source==next->id;
      if(!recorded){for(int i=0;i<count_;++i)if(jobs_[i].paid){jobs_[i]=jobs_[--count_];--i;}}
      if(!recorded&&count_>=2){p10::Directive d;d.task=p10::Task::capture;d.group=g->id;d.reason="bounded WIP records veto new exposed work";return d;}
      if(!recorded){auto&j=jobs_[count_++];j.source=next->id;j.observed=true;j.clearer_id=w.self.id;j.observed_round=w.round;}
      current_=next->id;stage_=1;p10::Directive d=p10::farm(next->id,"clear second job during witnessed collector custody/service");d.template_id=2;d.expected_min=w.round+released_clear;d.expected_max=w.round+owner_service;return d;}
    }
   }
   p10::Directive d;d.task=p10::Task::capture;d.group=g->id;d.reason="safe current holder finishes; absent collector is not custody";return d;
  }
  if(!g){
   if(unclaimed>=2){for(int i=0;i<count_;++i)if(!jobs_[i].paid){
    // Reinspection owns the outstanding job again. Once personally observed,
    // dispatch its ready stage instead of repeatedly issuing the inspect loop.
    current_=jobs_[i].source;const auto*pending=w.group(current_);
    if(pending&&p10::usable(*pending)){if(pending->cleared){stage_=2;p10::Directive d;d.task=p10::Task::capture;d.group=current_;d.reason="WIP-cap reinspection adopts observed ready custody";return d;}stage_=1;return p10::farm(current_,"WIP-cap reinspection resumes observed productive work");}
    auto d=p10::farm(current_,"WIP cap requires bounded personal reinspection");d.task=p10::Task::inspect;return d;
   }}
   for(const auto&v:w.groups)if(p10::usable(v)&&v.safe_solo&&v.living&&v.travel>=0&&(!g||v.travel<g->travel))g=&v;
   if(g){current_=g->id;if(count_<2){auto&j=jobs_[count_++];j.source=g->id;j.observed=true;j.clearer_id=w.self.id;j.observed_round=w.round;}}
  }
  if(!g)return p10::ordinary(w);stage_=1;return p10::farm(g->id,"bounded owned workcell productive stage");
 }
};
class Policy{
 AlternatePolicy alternate_{};std::array<Job,2> jobs_{};int count_=0,current_=-1,last_payout_=-1;bool income_=false;
 using FuelReader=int(*)();FuelReader fuel_reader_=nullptr;
 static bool occupying(const p10::Actor&a,const p10::Group&g){return a.visible&&a.alive&&a.channel==p10::Channel::none&&std::find(g.spawns.begin(),g.spawns.end(),a.position)!=g.spawns.end();}
 void record(int id,int round){for(int i=0;i<count_;++i)if(jobs_[i].source==id)return;if(count_<2){jobs_[count_]={};jobs_[count_].source=id;jobs_[count_].observed_round=round;++count_;}}
 public:int variant=0; // 0: prospective collector appointment; 1: opportunistic ready-stage cell.
 void set_fuel_reader(FuelReader r){fuel_reader_=r;}
 p10::Directive step(const p10::World&w,p10::Arm arm){
  if(variant==1)return alternate_.step(w,arm);
  if(w.own_payout&&last_payout_!=(w.last_payout_round>=0?w.last_payout_round:w.round)){income_=true;last_payout_=w.last_payout_round>=0?w.last_payout_round:w.round;for(int i=0;i<count_;++i)if(jobs_[i].source==w.last_payout_group)jobs_[i].paid=true;current_=-1;}
  for(int i=0;i<count_;++i){const auto*g=w.group(jobs_[i].source);if(g&&g->observed&&g->respawning)jobs_[i].paid=true;}
  if(w.own_bank_completed)current_=-1;
  if(w.self.channel!=p10::Channel::none)return p10::wait("spindle receipt owner's service");
  if(w.self.bag>0)return p10::bank("spindle actual owner services bag while opener works");
  if(w.own_gold>=w.shard_cost&&(w.self.at_spawn||w.self.type==p10::Champion::tavi)&&w.self.strength_shards<10)return p10::buy(p10::Item::monster_strength,1,"owned spindle work threshold");
  for(int i=0;i<count_;++i)if(jobs_[i].paid){jobs_[i]=jobs_[--count_];--i;}
  const p10::Actor*c=w.pick(1);if(c&&(!c->visible||!c->alive||c->channel!=p10::Channel::none))c=nullptr;
  const bool collector=w.self.type==p10::Champion::marcella;
  const bool opener=w.self.pick==0||(w.picks>=4&&w.self.pick==3);
  if(!collector&&!opener)return p10::ordinary(w);
  const p10::Group*g=w.group(w.capture_active?w.productive_group:current_);
  if(g&&(!g->program_available||g->respawning||g->contested)){current_=-1;g=nullptr;}
  if(!g){double best=-1;
   for(const auto&v:w.groups)if(v.route_known&&v.program_available&&!v.respawning&&!v.contested&&v.travel>=0&&v.monsters.size()<=12){
    bool outstanding=false;for(int i=0;i<count_;++i)outstanding|=jobs_[i].source==v.id;
    if(count_>=2&&!outstanding)continue;
    // Immutable source-cell order agrees in the collector's private frame and
    // the opener's frame even when their current travel distances differ.
    const double value=(v.gems*(income_?8.:5.)+v.gold*.05+1)/(8+p10::distance(w.spawn,v.position));
    if(value>best){best=value;g=&v;}
   }
   if(g){current_=g->id;if(opener)record(g->id,w.round);}
  }
  if(!g)return p10::ordinary(w);
  if(!g->observed||!g->complete){auto d=p10::farm(g->id,"personally verify appointed spindle workcell");d.task=p10::Task::inspect;d.template_id=10;return d;}
  if(g->cleared){
   if(opener&&arm==p10::Arm::active&&c&&c->id!=w.self.id&&g->spawns.size()>1&&occupying(w.self,*g)&&occupying(*c,*g)&&!(w.self.position==c->position)&&count_<2){
    const p10::Group*next=nullptr;double value=-1;
    for(const auto&v:w.groups)if(v.id!=g->id&&p10::usable(v)&&v.living&&v.travel>=0&&v.to_bank>=0){double q=(v.gems*8+v.gold*.05+1)/(v.travel+8+std::max(0,v.clear_turns));if(q>value){value=q;next=&v;}}
    const int fuel=fuel_reader_?fuel_reader_():w.fuel;
    if(next&&fuel>w.action_reserve+1500){auto proof=work_witness(w,*next);if(proof.feasible){
     Workcell cell;cell.count=2;cell.unclaimed=count_;cell.remaining=w.remaining();cell.second_travel=next->travel;cell.tribute_multiplier=w.tribute_multiplier;cell.tribute_base=w.tribute_base;cell.tribute_minimum=w.tribute_minimum;
     for(int i=0;i<2;++i){const auto&v=i?*next:*g;auto&j=cell.jobs[i];j.source=v.id;j.observed=true;j.clear_work=i?proof.clear:0;j.capture=w.capture_turns;j.reward=int(v.gems);j.bank_travel=std::max(0,v.to_bank);j.holder_id=c->id;j.clearer_id=w.self.id;j.holder_bag=int(c->bag);j.spawn_count=int(v.spawns.size());j.custodyMode=i?Custody::self:Custody::witnessed_overlap;j.holder_witnessed=j.free_successor_spawn=i==0;}
     const auto serial=schedule(cell,Dispatch::serial),overlap=schedule(cell,Dispatch::overlap);
     if(overlap.complete&&(!serial.complete||overlap.finish<serial.finish)){record(next->id,w.round);current_=next->id;auto d=proof.stance==w.self.position?proof.first:p10::farm(next->id);if(!(proof.stance==w.self.position)){d.task=p10::Task::move;d.destination=proof.stance;d.reserve_reset=true;}d.reason="appointed witnessed collector releases the next bounded job";d.template_id=12;return d;}
    }}
   }
   p10::Directive d;d.task=p10::Task::capture;d.group=g->id;d.template_id=11;d.reason=collector?"appointed collector personally adopts actual ready custody":"opener finishes until custody is witnessed";return d;
  }
  // The collector travels to a named real appointment before clearance. It
  // contributes combat only under its own observed solo work witness.
  const int fuel=fuel_reader_?fuel_reader_():w.fuel;if(fuel<w.action_reserve+1500)return p10::ordinary(w);
  const auto proof=work_witness(w,*g);
  if(!proof.feasible){if(collector){auto d=p10::farm(g->id,"collector approaches appointed ready-stage observation");d.task=p10::Task::inspect;d.template_id=10;return d;}return p10::ordinary(w);}
  auto d=proof.stance==w.self.position?proof.first:p10::farm(g->id);if(!(proof.stance==w.self.position)){d.task=p10::Task::move;d.destination=proof.stance;d.reserve_reset=true;}d.reason=collector?"productive collector approaches future custody":"owned clearance feeding bounded spindle queue";d.template_id=collector?10:13;
  d.expected_min=w.round+g->travel+proof.clear+w.capture_turns+1;d.expected_max=d.expected_min+std::max(0,g->to_bank)+w.ticks(g->gems);
  return d;
 }
};
} // namespace two_spindle
