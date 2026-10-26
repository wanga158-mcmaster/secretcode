#pragma once
#include "../../common/src/combat.h"
#include <array>
#include <algorithm>
namespace two_spindle {
struct WorkWitness {bool feasible=false;int clear=0;double minimum_hp=0,loss=0; p10::Point stance{};p10::Directive first{};};
// A work input, not a source selector: prove one stationary owned combat
// program at a physical contact/firing pad. Scheduling remains card-specific.
inline WorkWitness work_witness(const p10::World&w,const p10::Group&g){
 WorkWitness out;if(!p10::usable(g)||g.monsters.size()>12||!w.enemies.empty())return out;
 if(g.cleared){out.feasible=true;out.stance=w.self.position;return out;}
 p10::Point pad=g.approach;double best=1e30;
 auto consider=[&](p10::Point p){bool all=true;double exposure=0;for(const auto&m:g.monsters)if(m.alive){all&=p10::distance(p,m.position)<=w.self.range;if(p10::distance(p,m.position)<=m.range)exposure+=m.strength;}
  if(all){double cost=exposure*10+p10::distance(w.self.position,p);if(cost<best){best=cost;pad=p;}}};
 consider(w.self.position);for(auto p:g.approach_pads)consider(p);consider(g.approach);
 if(best==1e30)return out;

 // One phase prices a movement-only ranged arrival. No future helper or gold
 // is credited. The actual caller rechecks this witness on every owner turn.
 const double arrival=p10::distance(w.self.position,pad)>0?p10::phase_damage(g,pad):0;
 if(w.self.hp<=arrival+1)return out;
 const auto forecast=p10::stationary_work(w,g,pad,64);
 out.feasible=forecast.feasible;out.clear=forecast.turns+(arrival>0?1:0);out.stance=pad;
 out.minimum_hp=forecast.first.minimum_start_hp;out.loss=std::max(0.,w.self.hp-forecast.hp);out.first=forecast.first;
 return out;
}

enum class Custody { self, witnessed_overlap, conditional_sequence };
struct Job {
 int source=-1,clearFinish=0,capture=5,holder_arrival=0,actualOwner=-1,bank_travel=0,serviceEnd=0,renewal=0;
 int clear_work=0,reward=0,holder_bag=0,holder_id=-1,clearer_id=-1,spawn_count=1,observed_round=-1;
 int reset_cost=0,missed_arrival_cost=0,occupation_delay=0;Custody custodyMode=Custody::self;
 bool observed=false,contested=false,holder_witnessed=false,free_successor_spawn=false,paid=false,renewal_confirmed=false;
};
struct Interval{int actor=-1,start=0,finish=0,stage=0;};
enum class Dispatch{serial,witnessed_takeover,overlap,unavailable};
struct Workcell{
 std::array<Job,2>jobs{};int count=0,unclaimed=0,cap=2,now=0,remaining=128;
 int collector_ready=0,clearer_ready=0,collector_return=0,second_travel=0;
 double tribute_multiplier=.1;int tribute_base=0,tribute_minimum=1;bool collector_present=true;
};
struct Schedule{Dispatch dispatch=Dispatch::unavailable;bool complete=false;int finish=0,second_clear=0,banked=0;std::array<int,2>owners{-1,-1},payout{},banks{},renewal{};std::array<Interval,12>busy{};int intervals=0;};
inline int service(int bag,double multiplier,int base,int minimum){int q=int(bag*multiplier);if(q<bag*multiplier)++q;return std::max(minimum,q+base);}
inline bool can_release(const Job&j){return j.custodyMode==Custody::witnessed_overlap&&j.spawn_count>1&&j.holder_witnessed&&j.free_successor_spawn;}
inline int capture_share(int total,int count,int actor,int lowest){return count>0?total/count+(actor==lowest?total%count:0):0;}
inline Schedule schedule(Workcell c,Dispatch mode){
 Schedule s;s.dispatch=mode;if(c.count<1||c.count>2||c.unclaimed>std::min(2,c.cap)||(!c.collector_present&&mode!=Dispatch::serial))return s;
 int clearer=c.clearer_ready,collector=c.collector_ready;
 for(int i=0;i<c.count;++i){auto&j=c.jobs[i];if(!j.observed||j.contested)return s;
  const int start=clearer+(i?c.second_travel:0);int clear=std::max(start+j.clear_work,j.clearFinish);
  int holder=clear,owner=j.clearer_id;
  const bool transferring=mode!=Dispatch::serial&&j.custodyMode!=Custody::self;
  if(transferring){
   // A planned arrival never establishes witnessed overlap.
   if(!can_release(j)&&j.custodyMode!=Custody::conditional_sequence)return s;
   holder=std::max(clear,std::max(collector,j.holder_arrival));owner=j.holder_id;
   if(j.custodyMode==Custody::conditional_sequence)holder+=j.reset_cost+j.missed_arrival_cost+1;
  }
  const int payout=holder+1+j.capture;
  const int bank=payout+j.bank_travel+service(j.holder_bag+j.reward,c.tribute_multiplier,c.tribute_base,c.tribute_minimum);
  s.owners[i]=j.paid?j.actualOwner:owner;s.payout[i]=payout;s.banks[i]=bank;s.renewal[i]=payout+j.occupation_delay+j.renewal;
  s.busy[s.intervals++]={j.clearer_id,start,clear,0};s.busy[s.intervals++]={owner,holder,payout,1};s.busy[s.intervals++]={owner,payout,bank,2};
  if(i==1)s.second_clear=clear;
  if(mode==Dispatch::serial){clearer=bank+c.collector_return;collector=clearer;}
  else {if(transferring){collector=bank+c.collector_return;clearer=mode==Dispatch::overlap?holder:collector;}else clearer=bank+c.collector_return;}
  s.finish=std::max(s.finish,bank+c.collector_return);s.banked+=j.reward;
 }
 s.complete=s.finish<=std::min(128,c.remaining);return s;
}
inline bool admit(const Workcell&c,int actor,int admission_owner){return actor==admission_owner&&c.collector_present&&c.unclaimed<std::min(2,c.cap);}
inline Schedule choose(const Workcell&c,bool active){
 Schedule best=schedule(c,Dispatch::serial);if(!active)return best;
 for(auto mode:{Dispatch::witnessed_takeover,Dispatch::overlap}){auto s=schedule(c,mode);if(s.complete&&(!best.complete||s.finish<best.finish))best=s;}
 return best;
}
inline bool changed(const Job&j,const Job&n){return !n.observed||n.contested||j.source!=n.source||j.reward!=n.reward||j.holder_witnessed!=n.holder_witnessed||j.free_successor_spawn!=n.free_successor_spawn||j.holder_arrival!=n.holder_arrival||j.actualOwner!=n.actualOwner;}
} // namespace two_spindle
