#pragma once
#include "../../common/src/combat.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace orchards {
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

struct Source{
 int id=-1,opening=0,clear=0,capture=5,longest_cooldown=0,occupation_delay=0,travel=0,last_payout=-1,observed_round=-1,owner=-1;
 std::int64_t gems=0,gold=0;bool observed=false,available=true,contested=false;int service_travel=0,heal_ticks=0;
};
struct Gear{int shards=0,cost=30,own_gold=0;int old_clear=0,new_clear=0,shop_detour=0;bool field=false;};
struct Tour{
 std::array<Source,4>sources{};int count=0,bank_after=1,altar_travel=0,return_travel=0,recovery=0,neutral_wait_travel=0;
 int now=0,deadline=2000,health_loss=0,restored_health=0;double tribute_multiplier=.1;int tribute_base=0,tribute_minimum=1;
 Gear gear{};bool buy=false,premises=true;int actor=-1;
};
struct Calendar{
 bool regenerative=false,complete=false,purchased=false;int first_bank=-1,last_bank=-1,period=0,finished_tours=0,generations=0,opening_end=0;
 std::int64_t banked=0,stranded=0;std::array<int,4>next_birth{},last_payout{};int purchase_round=-1;
};
inline int tribute(std::int64_t bag,const Tour&t){return bag>0?std::max(t.tribute_minimum,int(std::ceil(bag*t.tribute_multiplier))+t.tribute_base):0;}
inline Calendar calendar(const Tour&t){
 Calendar c;if(!t.premises||t.count<1||t.count>4||t.bank_after<1||t.bank_after>2||t.health_loss>t.restored_health)return c;
 for(int i=0;i<t.count;++i)if(!t.sources[i].observed||t.sources[i].contested||!t.sources[i].available)return c;
 if(t.buy&&(t.gear.shards<1||t.gear.own_gold<t.gear.shards*t.gear.cost))return c;
 int time=t.now;if(t.buy){time+=t.gear.field?0:t.gear.shop_detour;c.purchased=true;c.purchase_round=time;}
 std::int64_t bag=0;int prior_bank=-1;int cycle=0;
 for(int i=0;i<t.count;++i){c.next_birth[i]=t.sources[i].last_payout<0?time:t.sources[i].last_payout+t.sources[i].longest_cooldown+t.sources[i].occupation_delay;c.last_payout[i]=t.sources[i].last_payout;}
 // Explicit opening plus two tours establish the same recovery/bag state.
 // Once that state repeats, the event pattern is repeated arithmetically.
 for(;cycle<4;++cycle){
  const int begin=time;int births=0;
  for(int i=0;i<t.count;++i){const auto&s=t.sources[i];time+=s.travel+(cycle==0?s.opening:0);
   // All waiting occurs off spawns. Occupation delay remains in the birth bound.
   time=std::max(time,c.next_birth[i]+t.neutral_wait_travel);
   int clear=s.clear;if(t.buy&&t.gear.old_clear==s.clear)clear=t.gear.new_clear;
   time+=clear+1+s.capture;
   if(time>t.deadline){c.stranded=bag;c.regenerative=true;c.complete=c.banked>0;return c;}
   c.last_payout[i]=time;c.next_birth[i]=time+s.longest_cooldown+s.occupation_delay;bag+=s.gems;++births;
   // Every source restores its required starting state before another source
   // or bank batch. Recovery is actual travel plus owner-end healing service.
   time+=s.service_travel+s.heal_ticks;
  }
  c.generations+=births;++c.finished_tours;if(cycle==0)c.opening_end=time;
  if((cycle+1)%t.bank_after==0){time+=t.altar_travel+tribute(bag,t);if(time>t.deadline){c.stranded=bag;c.regenerative=true;c.complete=c.banked>0;return c;}
   if(c.first_bank<0)c.first_bank=time;c.last_bank=time;c.banked+=bag;bag=0;
   time+=t.return_travel+t.recovery;
   const int block_period=prior_bank<0?0:c.last_bank-prior_bank;
   if(cycle>=2&&block_period>0){c.period=block_period;const int repeat=std::max(0,(t.deadline-time)/block_period);
    std::int64_t block_reward=0;for(int i=0;i<t.count;++i)block_reward+=t.sources[i].gems*t.bank_after;
    c.banked+=repeat*block_reward;c.generations+=repeat*t.count*t.bank_after;c.finished_tours+=repeat*t.bank_after;c.last_bank+=repeat*block_period;time+=repeat*block_period;
    // Explicit tail below never credits a bag that cannot finish tribute.
    break;
   }prior_bank=c.last_bank;
  }
  if(time<=begin)return c;
 }
 c.regenerative=true;c.complete=c.banked>0;c.stranded=bag;return c;
}
inline int choose(const std::array<Tour,2>&catalogue,bool active){
 const auto a=calendar(catalogue[0]),b=calendar(catalogue[1]);
 if(!a.regenerative)return b.regenerative?1:-1;if(!b.regenerative)return 0;
 if(active){if(a.banked!=b.banked)return b.banked>a.banked?1:0;if(a.last_bank!=b.last_bank)return b.last_bank<a.last_bank?1:0;}
 // Strong principal control optimizes the immediate *complete* cycle.
 auto immediate=[](const Tour&t){double reward=0;int ticks=t.altar_travel+t.return_travel+t.recovery;for(int i=0;i<t.count;++i){reward+=t.sources[i].gems;ticks+=t.sources[i].travel+t.sources[i].opening+t.sources[i].clear+1+t.sources[i].capture+t.sources[i].service_travel+t.sources[i].heal_ticks;}ticks+=tribute(std::int64_t(reward),t);return ticks>0?reward/ticks:0.;};
 return immediate(catalogue[1])>immediate(catalogue[0])?1:0;
}
inline bool invalidated(const Tour&a,const Tour&b){
 if(!b.premises||a.count!=b.count||a.gear.own_gold!=b.gear.own_gold||a.health_loss!=b.health_loss)return true;
 for(int i=0;i<a.count;++i)if(!b.sources[i].available||!b.sources[i].observed||b.sources[i].contested||a.sources[i].occupation_delay!=b.sources[i].occupation_delay||a.sources[i].last_payout!=b.sources[i].last_payout)return true;
 return false;
}
} // namespace orchards
