#pragma once
#include "../../common/src/physical-appointments.h"
#include "model.h"
namespace p10_b_metered {
enum class Kind { two_sources,two_generations };
struct Candidate {bool valid=false;Kind kind=Kind::two_sources;metered_works::Program party=metered_works::Program::sole;std::array<int,2>sources{-1,-1};metered_works::ServiceProgram service{};p10::Gems secured=0;int participant=-1;};
struct BPolicy:p10_b::Physical {
 static constexpr bool observe_ally_details=false;
 Candidate selected{};int index=0,payouts=0,start=-1,last_receipt=-2;bool chosen=false;int actual_parallel=0;
 mutable const p10::World*work_world=nullptr;mutable bool work_aborted=false;mutable int work_checks=0,work_phase=0;
 bool fuel_ok()const{++work_checks;if(work_aborted)return false;const auto r=reader?reader:p10::work_fuel_reader;const int fuel=r?r():(work_world?work_world->fuel:100000);if(work_world&&fuel<work_world->action_reserve+1500)work_aborted=true;return !work_aborted;}
 p10::Directive fuel_stop()const{return p10::wait("joint batch defers unfinished live work before its action reserve");}
 int appointment(const p10::World&w,const p10::Actor&a,const p10::Group&g)const{
  // Two literal cardinal L-routes through the current immutable/free view.
  // Source spawns may open after this actor's own proven clearance; no other
  // champion's occupied square or unknown terrain is silently vacated.
  int best=1000;for(auto target:g.spawns)for(int xy=0;xy<2;++xy){if(!fuel_ok())return 1000;auto p=a.position;int steps=0;bool legal=true;
   for(int axis=0;axis<2&&legal;++axis)while((axis==xy?p.x:p.y)!=(axis==xy?target.x:target.y)){
    if(++steps>16||!fuel_ok()){legal=false;break;}if(axis==xy)p.x+=p.x<target.x?1:-1;else p.y+=p.y<target.y?1:-1;
    bool terrain=std::find(w.visible_open_tiles.begin(),w.visible_open_tiles.end(),p)!=w.visible_open_tiles.end()||std::find(g.spawns.begin(),g.spawns.end(),p)!=g.spawns.end()||std::find(g.approach_pads.begin(),g.approach_pads.end(),p)!=g.approach_pads.end();
    for(const auto&v:w.groups)if(v.id!=g.id)for(const auto&m:v.monsters){if(!fuel_ok())return 1000;if(m.alive&&m.hp>0&&m.position==p)terrain=false;}
    for(const auto&v:w.allies){if(!fuel_ok())return 1000;if(v.id!=a.id&&v.visible&&v.alive&&v.position==p)terrain=false;}if(w.self.id!=a.id&&w.self.position==p)terrain=false;
    if(!terrain||p10_mobile::visible_enemy_can_intervene(w,p))legal=false;
   }
   if(legal)best=std::min(best,steps);
  }
  return best;
 }
 Candidate candidate(const p10::World&w,const p10::Group&a,const p10::Group&b,int clear_a,int clear_b,const p10::Actor*other,Kind kind,p10::Arm arm)const{
  Candidate c;c.kind=kind;c.sources={a.id,b.id};if(!fuel_ok()||a.to_bank<0||b.to_bank<0||(a.id!=b.id&&(a.to_spawn<0||b.to_spawn<0)))return c;
  const int arrival=other?appointment(w,*other,a):0;if(arrival>=1000)other=nullptr;
  if(other){double exposure=0;bool known=true;for(const auto&g:w.groups)for(const auto&m:g.monsters){if(!fuel_ok())return c;if(m.alive&&m.strength>0&&p10::distance(other->position,m.position)<=m.range+arrival){known&=g.observed&&g.complete&&m.observed;exposure+=m.strength;}}if(!known||other->hp<=exposure*(arrival+w.capture_turns+3)+30)other=nullptr;}
  c.participant=other?other->id:-1;metered_works::Batch batch;batch.settings={w.tribute_multiplier,w.tribute_base,w.tribute_minimum};batch.remaining=std::min(192,w.remaining());batch.workers_count=other?2:1;batch.prospective=other&&a.spawns.size()>1&&b.spawns.size()>1;batch.party_verified=other&&p10_b::on_spawn(w.self,a)&&p10_b::on_spawn(*other,a);batch.source_travel=a.id==b.id?0:std::max(0,a.to_spawn)+std::max(0,b.to_spawn);
  auto worker=[&](const p10::Actor&v){metered_works::Worker r;r.id=v.id;r.arrival=v.id==w.self.id?0:arrival;r.altar_travel=std::max(a.to_bank,b.to_bank);r.return_travel=r.altar_travel;r.bag=v.bag;r.marcella=v.type==p10::Champion::marcella;r.healer=v.type==p10::Champion::willow;return r;};batch.workers[0]=worker(w.self);if(other)batch.workers[1]=worker(*other);
  for(int i=0;i<2;++i){const auto&g=i?b:a;auto&s=batch.sources[i];s.id=g.id;s.observed=g.observed&&g.complete;s.contested=g.contested;s.clear=i?clear_b:clear_a;s.capture=w.capture_turns;s.gems=g.gems;s.gold=g.gold;}
  if(kind==Kind::two_generations){int work=0;for(const auto&m:a.monsters){if(!fuel_ok()||m.strength>0)return c;work+=int(std::ceil(std::max(m.hp,m.max_hp)/std::max(1.,w.self.strength)));}batch.sources[1].clear=clear_a+w.capture_turns+1+a.respawn+work;}
  work_phase=4;if(!fuel_ok())return c;c.service=metered_works::choose(batch,arm==p10::Arm::active);if(!fuel_ok())return Candidate{};c.party=c.service.kind;c.valid=c.service.complete;for(auto gems:c.service.banked)c.secured+=gems;return c;
 }
 static bool better(const Candidate&a,const Candidate&b){return a.valid&&(!b.valid||a.secured>b.secured||(a.secured==b.secured&&a.service.finish<b.service.finish));}
 p10::Directive step(const p10::World&w,p10::Arm arm){
  fresh_traces=0;work_world=&w;work_aborted=false;work_checks=0;work_phase=1;
  if(w.own_payout&&last_receipt!=p10_b::receipt_round(w)){last_receipt=p10_b::receipt_round(w);++payouts;if(selected.kind==Kind::two_sources)index=1;}
  if(w.own_bank_completed){chosen=false;index=payouts=0;start=-1;}
  if(w.self.channel!=p10::Channel::none)return p10::wait("joint batch actual personal channel");
  if(!fuel_ok())return fuel_stop();
  if(w.self.bag>0&&(payouts>=2||w.remaining()<192||(start>=0&&w.round-start>=192)))return p10::bank("all actual recipients finish the declared two-capture service batch");
  if(w.own_gold>=w.shard_cost&&(w.self.at_spawn||w.self.type==p10::Champion::tavi)&&w.self.strength_shards<10)return p10::buy(p10::Item::monster_strength,1,"personal actual service capacity");
  auto budget_=budget(w);auto usage=watch(budget_);p10_mobile::Result first_proof,second_proof;const p10::Group*a=nullptr,*b=nullptr;
  if(!chosen){double best=-1;for(const auto&g:w.groups){if(!fuel_ok())return fuel_stop();if(g.route_known&&g.program_available&&!g.contested&&g.gems>0&&!g.respawning){double q=(g.gems*8+1)/(8+p10::distance(w.spawn,g.position));if(q>best){best=q;a=&g;}}}
   if(!a)return p10::ordinary(w);if(!a->observed||!a->complete)return p10_b::inspect(a->id,"joint batch public opening appointment needs private work evidence",4040);best=-1;for(const auto&g:w.groups){if(!fuel_ok())return fuel_stop();if(g.id!=a->id&&p10::usable(g)&&g.gems>0&&!g.respawning){double q=(g.gems+1)/(8+g.travel);if(q>best){best=q;b=&g;}}}
   work_phase=2;
   if(a->cleared){first_proof.feasible=true;first_proof.clear_turns=0;}else first_proof=prove(w,*a,budget_);
   if(!fuel_ok())return fuel_stop();if(!first_proof.feasible)return p10_b::inspect(a->id,"joint batch first physical appointment/warmup",4040);
   if(b){if(b->cleared){second_proof.feasible=true;second_proof.clear_turns=0;}else second_proof=prove(w,*b,budget_);}
   if(!fuel_ok())return fuel_stop();work_phase=3;const p10::Actor*other=nullptr;for(const auto&v:w.allies){if(!fuel_ok())return fuel_stop();if(v.visible&&v.alive&&v.channel==p10::Channel::none&&v.id!=w.self.id&&(v.type==p10::Champion::marcella||v.type==p10::Champion::willow)&&p10::distance(v.position,a->position)<=6)other=&v;}
   selected=candidate(w,*a,*a,first_proof.clear_turns,first_proof.clear_turns,other,Kind::two_generations,arm);
   if(b&&second_proof.feasible){auto distinct=candidate(w,*a,*b,first_proof.clear_turns,second_proof.clear_turns,other,Kind::two_sources,arm);if(better(distinct,selected))selected=distinct;}
   if(!fuel_ok()){selected={};return fuel_stop();}
   if(!selected.valid){if(a->cleared)return p10_b::capture(a->id,"unavailable common batch still secures real current custody",4044);auto d=first_proof.first;d.template_id=4040;return d;}
   chosen=true;index=payouts=0;start=w.round;
  }
  if(!fuel_ok())return fuel_stop();const auto*g=w.group(selected.sources[index]);if(w.capture_active)g=w.group(w.productive_group);if(!g)return w.self.bag>0?p10::bank():p10::ordinary(w);
  if(g->contested){chosen=false;return w.self.bag>0?p10::bank("interrupted declared batch liquidates actual stock"):p10::ordinary(w);}
  if(g->respawning){if(selected.kind==Kind::two_sources&&index==0){index=1;return p10_b::inspect(selected.sources[1],"second declared source after observed first payout",4040);}if(w.self.bag>0&&g->respawn_remaining+w.ticks(w.self.bag)+g->to_bank>w.remaining())return p10::bank();
   if(p10_b::on_spawn(w.self,*g)){p10::Directive d;d.task=p10::Task::release;d.group=g->id;d.template_id=4042;return d;}return p10::wait("two-generation batch waits off all renewal spawns");}
  if(!g->observed||!g->complete)return p10_b::inspect(g->id,"declared next capture requires personal lifecycle observation",4040);
  if(g->cleared){bool party=selected.participant>=0;int holders=0;for(const auto&v:w.allies){if(!fuel_ok())return fuel_stop();if(p10_b::on_spawn(v,*g)&&!(v.position==w.self.position)){++holders;if(v.id!=selected.participant)party=false;}}party&=holders==1&&p10_b::on_spawn(w.self,*g);if(selected.party==metered_works::Program::parallel&&party)++actual_parallel;return p10_b::capture(g->id,"future party becomes actual only at observed payout occupation",selected.party==metered_works::Program::parallel?(party?4043:4041):4044);}
  p10_mobile::Result proof;if(a&&a->id==g->id&&first_proof.feasible)proof=first_proof;else if(b&&b->id==g->id&&second_proof.feasible)proof=second_proof;else if(budget_.spent<2)proof=prove(w,*g,budget_);
  if(!fuel_ok())return fuel_stop();if(proof.feasible){auto d=proof.first;d.template_id=selected.kind==Kind::two_generations?4042:4041;d.reason="jointly chosen batch's exact mobile physical prefix";return d;}return p10_b::inspect(g->id,"declared mobile work warmup",4040);
 }
};
}
