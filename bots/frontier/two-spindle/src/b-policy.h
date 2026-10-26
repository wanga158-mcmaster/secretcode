#pragma once
#include "../../common/src/physical-appointments.h"
#include "model.h"
namespace p10_b_spindle {
struct BPolicy:p10_b::Physical {
 int current=-1,pending=-1,next_job=-1,vacated=-1,last_receipt=-2,missed_arrivals=0;std::array<int,2>exposed{-1,-1};int exposed_count=0;
 void record(int id){for(int i=0;i<exposed_count;++i)if(exposed[i]==id)return;if(exposed_count<2)exposed[exposed_count++]=id;}
 p10::Directive step(const p10::World&w,p10::Arm arm){
  fresh_traces=0;
  if(w.own_payout&&last_receipt!=p10_b::receipt_round(w)){last_receipt=p10_b::receipt_round(w);for(int i=0;i<exposed_count;++i)if(exposed[i]==w.last_payout_group){exposed[i]=exposed[--exposed_count];--i;}current=-1;}
  for(int i=0;i<exposed_count;++i){const auto*g=w.group(exposed[i]);if(g&&g->observed&&g->respawning){exposed[i]=exposed[--exposed_count];--i;}}
  if(w.self.channel!=p10::Channel::none)return p10::wait("ordered spindle actual service");if(w.self.bag>0)return p10::bank("ordered spindle actual personal owner conversion");
  if(w.own_gold>=w.shard_cost&&(w.self.at_spawn||w.self.type==p10::Champion::tavi)&&w.self.strength_shards<10)return p10::buy(p10::Item::monster_strength,1,"funded ordered-cell threshold");
  const bool collector=w.self.type==p10::Champion::marcella;const auto*c=p10_b::visible_type(w,p10::Champion::marcella);
  if(pending>=0&&!collector){const auto*g=w.group(pending);if(g&&g->observed&&g->respawning){pending=-1;current=next_job;}
   else if(g&&p10::usable(*g)&&g->cleared){if(c&&p10_b::on_spawn(*c,*g)){pending=-1;current=next_job;record(current);}else if(w.round>vacated){++missed_arrivals;current=g->id;pending=-1;next_job=-1;return p10_b::capture(g->id,"missed collector entry pays a fresh full capture countdown",3033);}}
   else return p10_b::inspect(pending,"unknown ordered handoff requires personal reinspection",3030);}
  const p10::Group*g=w.group(w.capture_active?w.productive_group:current);if(g&&(g->respawning||g->contested||!g->route_known))g=nullptr;
  if(!g&&collector){for(const auto&v:w.groups)if(p10::usable(v)&&v.cleared){bool free=false;for(auto spawn:v.spawns)if(!p10_mobile::occupied(w,spawn))free=true;if(free&&(!g||v.travel<g->travel))g=&v;}if(g)current=g->id;}
  if(!g){double best=-1;for(const auto&v:w.groups)if(v.route_known&&v.program_available&&!v.respawning&&!v.contested){if(exposed_count>=2&&std::find(exposed.begin(),exposed.begin()+exposed_count,v.id)==exposed.begin()+exposed_count)continue;double q=(v.gems*8+v.gold*.03+1)/(8+p10::distance(w.spawn,v.position));if(q>best){best=q;g=&v;}}if(g){current=g->id;if(!collector)record(current);}}
  if(!g)return p10::ordinary(w);if(!g->observed||!g->complete)return p10_b::inspect(g->id,"ordered spindle appointment",3030);
  auto b=budget(w);auto usage=watch(b);
  if(g->cleared){
   if(!collector&&arm==p10::Arm::active&&g->spawns.size()==1&&p10_b::on_spawn(w.self,*g)&&c&&c->can_move&&c->bag==0&&c->id!=w.self.id&&p10_mobile::manhattan(c->position,g->spawns[0])==1&&exposed_count<2){
    const bool after=w.self.priority>c->priority||(w.self.priority==c->priority&&w.self.pick<c->pick);
    const p10::Group*next=nullptr;for(const auto&v:w.groups)if(v.id!=g->id&&p10::usable(v)&&v.living&&v.gems>0&&(!next||v.travel<next->travel))next=&v;
    if(after&&next){auto proof=prove(w,*next,b);if(proof.feasible){
     const auto entry=g->spawns[0];double custody_damage=0;bool custody_known=!p10_mobile::visible_enemy_can_intervene(w,entry)&&!p10_mobile::visible_enemy_can_intervene(w,c->position);
     for(const auto&v:w.groups)for(const auto&m:v.monsters)if(m.alive&&m.strength>0&&(p10::distance(entry,m.position)<=m.range||p10::distance(c->position,m.position)<=m.range)){custody_known&=v.observed&&v.complete&&m.observed;custody_damage+=m.strength;}
     if(!custody_known||c->hp<=custody_damage*(proof.clear_turns+w.capture_turns+3)*(c->jungle_emblem?.7:1.)+30)return p10_b::capture(g->id,"collector's whole custody interval is not independently safe",3032);

     two_spindle::Workcell cell;cell.count=2;cell.unclaimed=exposed_count;cell.remaining=w.remaining();cell.second_travel=next->travel;cell.tribute_multiplier=w.tribute_multiplier;cell.tribute_base=w.tribute_base;cell.tribute_minimum=w.tribute_minimum;
     for(int i=0;i<2;++i){const auto&v=i?*next:*g;auto&j=cell.jobs[i];j.source=v.id;j.observed=true;j.clear_work=i?proof.clear_turns:0;j.capture=w.capture_turns;j.reward=int(v.gems);j.bank_travel=std::max(0,v.to_bank);j.clearer_id=w.self.id;j.holder_id=c->id;j.holder_bag=int(i?w.self.bag:c->bag);j.spawn_count=int(v.spawns.size());j.custodyMode=i?two_spindle::Custody::self:two_spindle::Custody::conditional_sequence;j.reset_cost=i?0:w.capture_turns;j.missed_arrival_cost=i?0:1;j.holder_arrival=i?0:1;}
     auto serial=two_spindle::schedule(cell,two_spindle::Dispatch::serial),ordered=two_spindle::schedule(cell,two_spindle::Dispatch::overlap);
     if(ordered.complete&&(!serial.complete||ordered.finish<serial.finish))for(auto p:p10_b::cardinals(w.self.position))if(!(p==c->position)&&std::find(g->spawns.begin(),g->spawns.end(),p)==g->spawns.end()){
      p10::Directive d;if(p10_b::safe_vacate(w,p,d)){pending=g->id;next_job=next->id;vacated=w.round;d.group=g->id;d.template_id=3031;d.reason="ordered single-spawn vacate; capture reset and missed arrival explicitly priced";return d;}}
    }}
   }
   return p10_b::capture(g->id,collector?"collector enters only actually free observed ready spawn":"opener completes custody until a legal ordered transfer",3032);
  }
  auto proof=prove(w,*g,b);if(proof.feasible){auto d=proof.first;d.template_id=3034;d.reason=collector?"collector's independently feasible productive appointment":"bounded mobile opener feeds actual ready-stage queue";return d;}
  return p10_b::inspect(g->id,"ordered spindle local geometry warmup",3030);
 }
};
}
