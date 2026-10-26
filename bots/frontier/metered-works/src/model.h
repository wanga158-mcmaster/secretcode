#pragma once
#include "../../common/src/combat.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace metered_works {
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

using Gems=std::int64_t;
struct Settings{double multiplier=.1;int base=0,minimum=1;};
inline int tributeTicks(Gems bag,Settings q={}){return bag>0?std::max(q.minimum,int(std::ceil(bag*q.multiplier))+q.base):0;}
inline Gems share(Gems total,const std::array<int,5>&participants,int count,int actor){
 if(count<1||count>5)return 0;int lowest=participants[0];bool member=false;
 for(int i=0;i<count;++i){lowest=std::min(lowest,participants[i]);member|=participants[i]==actor;for(int j=0;j<i;++j)if(participants[i]==participants[j])return 0;}
 return member?total/count+(actor==lowest?total%count:0):0;
}
struct Worker{int id=-1,arrival=0,altar_travel=0,return_travel=0;Gems bag=0;bool marcella=false,healer=false;};
struct Source{int id=-1,clear=0,capture=5;Gems gems=0,gold=0;bool observed=false,contested=false,adjacent_verified=false,phase_calibrated=false;int payout_earliest=-1,payout_latest=-1;};
enum class Program{sole,parallel,crossdock};
struct Batch{
 std::array<Source,2>sources{};int count=2;std::array<Worker,3>workers{};int workers_count=1;
 Settings settings{};int now=0,remaining=192,source_travel=0,lost_worker_actions=0,lock_hits=5;
 bool party_verified=false,locked=false,hidden_contest=false,prospective=false;Gems real_seed=0;int seed_owner=-1;
};
struct ServiceProgram{
 Program kind=Program::sole;bool complete=false,conditional=false;int finish=0;
 std::array<Gems,3>banked{},capture_gold{},tribute_gold{};std::array<int,3>service_start{},service_end{},return_end{};
 std::array<std::array<Gems,3>,2>shares{};int occupied_until=0;
};
inline ServiceProgram plan(const Batch&b,Program kind){
 ServiceProgram p;p.kind=kind;if(b.count!=2||b.workers_count<1||b.workers_count>3||b.locked||b.hidden_contest)return p;
 const int party=kind==Program::parallel?b.workers_count:1;if(party>1&&!b.party_verified&&!b.prospective)return p;
 if(party>1&&!b.party_verified)p.conditional=true;
 std::array<int,5>ids{};for(int i=0;i<party;++i)ids[i]=b.workers[i].id;
 std::array<int,3>ready{};std::array<Gems,3>bags{};for(int a=0;a<party;++a){ready[a]=b.workers[a].arrival;bags[a]=b.workers[a].bag;}
 int previous=0;
 for(int j=0;j<2;++j){const auto&s=b.sources[j];if(!s.observed||s.contested)return p;
  int capture_start=std::max(s.clear,previous+(j?b.source_travel:0));for(int a=0;a<party;++a)capture_start=std::max(capture_start,ready[a]);
  const int payout=capture_start+1+s.capture;previous=payout;
  for(int a=0;a<party;++a){const auto&w=b.workers[a];const Gems amount=share(s.gems,ids,party,w.id);p.shares[j][a]=amount;p.capture_gold[a]+=share(s.gold,ids,party,w.id);bags[a]+=amount;
   ready[a]=payout;
   // Each service has its own bag and actor resource; no pooled bag arithmetic.
   if(j==1){p.service_start[a]=payout+w.altar_travel;p.service_end[a]=p.service_start[a]+tributeTicks(bags[a],b.settings);p.banked[a]=bags[a];p.tribute_gold[a]=w.marcella?2*bags[a]:0;p.return_end[a]=p.service_end[a]+w.return_travel;p.finish=std::max(p.finish,p.return_end[a]);}
  }
  p.occupied_until=payout+1;
 }
 if(kind==Program::crossdock){
  const auto&s=b.sources[1];const auto&w=b.workers[0];
  if(!s.adjacent_verified||!s.phase_calibrated||b.real_seed<=0||b.seed_owner!=w.id||b.real_seed>w.bag||tributeTicks(b.real_seed,b.settings)!=1||s.payout_earliest<0||s.payout_latest!=s.payout_earliest)return ServiceProgram{};
  // Starts with a real seed before the capture phase. Whole-bag completion is
  // conditional on the privately witnessed occupation interval surviving.
  const int start=s.payout_earliest-1;if(start<0)return ServiceProgram{};
  p.conditional=true;p.service_start[0]=start;p.service_end[0]=start+1;
  p.return_end[0]=p.service_end[0]+w.return_travel;p.finish=p.return_end[0];
 }
 // Nonrecipient workers keep their actual initial bags in both arms. Their
 // independently executable source-approach/altar/return route is still priced.
 for(int a=party;a<b.workers_count;++a){const auto&w=b.workers[a];p.banked[a]=w.bag;
  if(w.bag>0){p.service_start[a]=w.arrival+w.altar_travel;p.service_end[a]=p.service_start[a]+tributeTicks(w.bag,b.settings);p.return_end[a]=p.service_end[a]+w.return_travel;p.finish=std::max(p.finish,p.return_end[a]);p.tribute_gold[a]=w.marcella?2*w.bag:0;}}
 if(kind!=Program::sole)p.finish+=b.lost_worker_actions;p.complete=p.finish<=std::min(192,b.remaining);return p;
}
inline ServiceProgram choose(const Batch&b,bool active){
 auto best=plan(b,Program::sole);if(!active)return best;
 auto candidate=plan(b,Program::parallel);
 if(candidate.complete&&(!best.complete||candidate.finish<best.finish))best=candidate;
 auto crossdock=plan(b,Program::crossdock);
 if(crossdock.complete&&(!best.complete||crossdock.finish<best.finish))best=crossdock;
 return best;
}
inline bool invalidated(const Batch&a,const Batch&b){
 if(b.locked||b.hidden_contest||a.party_verified!=b.party_verified||a.workers_count!=b.workers_count)return true;
 for(int i=0;i<a.workers_count;++i)if(a.workers[i].id!=b.workers[i].id||a.workers[i].bag!=b.workers[i].bag)return true;
 for(int i=0;i<2;++i)if(a.sources[i].gems!=b.sources[i].gems||!b.sources[i].observed||b.sources[i].contested)return true;
 return false;
}
} // namespace metered_works
