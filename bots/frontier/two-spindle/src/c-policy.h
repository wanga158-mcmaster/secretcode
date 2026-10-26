#pragma once
#include "../../common/src/c-capital-01-05.h"
#include "model.h"
namespace p10_c_spindle {
struct CPolicy:p10_c::Physical {
 static constexpr bool observe_ally_details=false;
 static constexpr std::uint64_t observe_ally_detail_types=(std::uint64_t{1}<<unsigned(p10::Champion::marcella));
 p10_c::Wallet wallet;int capital_target=-1,capital_packet=0,capital_events=0;
 int current=-1,pending=-1,next_job=-1,vacated=-1,last_receipt=-2,missed_arrivals=0,declined=-1,declined_round=-2;std::array<int,2>exposed{-1,-1};int exposed_count=0;
 void record(int id){for(int i=0;i<exposed_count;++i)if(exposed[i]==id)return;if(exposed_count<2)exposed[exposed_count++]=id;}
 bool contains(int id)const{return std::find(exposed.begin(),exposed.begin()+exposed_count,id)!=exposed.begin()+exposed_count;}
 struct Admission {int work=0,finish=1000;};
 // This is a finite128-turn service proposal screen, not a48-basic proxy for
 // physical turns. Physical::prove retains its actual fresh48-turn limit.
 // Public birth work is used when private HP is unknown, and for renewal
 // capital; a current observed wounded/ready source keeps its actual work.
 bool admit(const p10::World&w,const p10::Group&g,p10_mobile::Budget&b,bool&fuel_ready,Admission&out,bool renewal=false)const{
  if(!b.fuel_ok()){fuel_ready=false;return false;}
  if(w.groups.size()>128||!w.geometry_complete||!w.actor_observation_complete||!w.self.combat_details_complete||!w.self.equipment_details_complete||!g.route_known||!g.program_available||g.respawning||g.contested||g.travel<0||g.to_bank<0||g.monsters.empty()||g.monsters.size()>12)return false;
  if(!renewal&&g.id==declined&&w.round<=declined_round+1)return false;
  const double damage=w.self.strength*(w.self.monster_emblem?1.1:1.);if(!(damage>0)||!std::isfinite(damage))return false;
  const bool ready=!renewal&&p10::usable(g)&&g.cleared,live=!renewal&&g.observed&&g.complete;
  const int limit=128;int work=0;double wave=0;
  if(!ready)for(const auto&m:g.monsters){
   if(!b.fuel_ok()){fuel_ready=false;return false;}if(live&&(!m.alive||m.hp<=0))continue;
   const double hp=live?m.hp:m.max_hp;if(!(hp>0)||!std::isfinite(hp)||!std::isfinite(m.strength)||m.strength<0)return false;
   const double n=std::ceil(hp/damage);if(n>limit-work)return false;work+=int(n);wave+=m.strength;
  }
  const std::int64_t finish=std::int64_t(g.travel)+work+w.capture_turns+1+g.to_bank+w.ticks(g.gems);
  if(finish>std::min(128,w.remaining())||(!renewal&&!ready&&w.self.hp<=wave*(work+2)*(w.self.jungle_emblem?.7:1.)+std::max(30.,w.self.max_hp*.1)))return false;
  out={work,int(finish)};return true;
 }
 static int full(const p10::Group&g,double damage){int work=0;for(const auto&m:g.monsters)work+=int(std::ceil(m.max_hp/std::max(1.,damage)));return work;}
 int queue_finish(const p10::World&w,const p10::Group&a,const p10::Group&b,double strength,const p10::Actor*collector,bool overlap)const{
  two_spindle::Workcell cell;cell.count=a.id==b.id?1:2;cell.unclaimed=0;cell.remaining=128;cell.second_travel=std::max(0,a.to_spawn)+std::max(0,b.to_spawn);cell.tribute_multiplier=w.tribute_multiplier;cell.tribute_base=w.tribute_base;cell.tribute_minimum=w.tribute_minimum;
  for(int i=0;i<cell.count;++i){const auto&g=i?b:a;auto&j=cell.jobs[i];j.source=g.id;j.observed=true;j.clear_work=full(g,strength*(w.self.monster_emblem?1.1:1.));j.capture=w.capture_turns;j.reward=int(g.gems);j.bank_travel=g.to_bank;j.clearer_id=w.self.id;j.holder_id=collector?collector->id:w.self.id;j.holder_bag=int(overlap&&collector?collector->bag:w.self.bag);j.spawn_count=int(g.spawns.size());j.custodyMode=collector?two_spindle::Custody::conditional_sequence:two_spindle::Custody::self;j.holder_arrival=collector?1:0;j.reset_cost=collector?w.capture_turns:0;j.missed_arrival_cost=collector?1:0;}
  const auto result=two_spindle::schedule(cell,overlap&&collector?two_spindle::Dispatch::overlap:two_spindle::Dispatch::serial);return result.complete?result.finish:1000;
 }
 // A cold opener/collector has not yet delivered its own productive receipt.
 // Admit one short public full-birth service stage before using the established
 // queue value objective. This is a proposal screen, never a work certificate.
 bool cold_queue()const{return wallet.last_payout<0&&wallet.last_bank<0;}
 const p10::Group*cold_stage(const p10::World&w,bool&fuel_ready)const{
  auto b=budget(w);const p10::Group*best=nullptr;double value=-1;
  const int window=std::min(12,std::max(1,w.capture_turns)+3); // capture plus vacate/entry/missed-entry service
  const double damage=w.self.strength*(w.self.monster_emblem?1.1:1.);
  if(!(damage>0)||!std::isfinite(damage))return nullptr;
  for(const auto&g:w.groups){
   if(!b.fuel_ok()){fuel_ready=false;return nullptr;}
   if(g.id==declined&&w.round<=declined_round+1)continue;
   if(!g.route_known||!g.program_available||g.respawning||g.contested||g.travel<0||g.to_bank<0)continue;
   if(exposed_count>=2&&std::find(exposed.begin(),exposed.begin()+exposed_count,g.id)==exposed.begin()+exposed_count)continue;
   int work=0;bool known=!g.monsters.empty()&&g.monsters.size()<=12;
   if(!(p10::usable(g)&&g.cleared))for(std::size_t i=0;known&&i<g.monsters.size();++i){
    if(!b.fuel_ok()){fuel_ready=false;return nullptr;}const double hp=g.monsters[i].max_hp;
    if(!(hp>0)||!std::isfinite(hp)){known=false;break;}
    const double basics=std::ceil(hp/damage);if(basics>window-work){known=false;break;}work+=int(basics);
   }
   if(!known||std::int64_t(g.travel)+work+w.capture_turns+1+g.to_bank+w.ticks(g.gems)>w.remaining())continue;
   const double q=(g.gems*8+g.gold*.03+1)/(8+double(g.travel)+work);
   if(q>value){value=q;best=&g;}
  }
  return best;
 }
 p10::Directive capital(const p10::World&w,p10::Arm arm){
  if(reader&&reader()<w.action_reserve+1200)return {};
  if(!wallet.available(w)||pending>=0||exposed_count>0)return {};
  auto b=budget(w);bool fuel_ready=true;Admission admitted;
  const auto*c=p10_c::visible_type(w,p10::Champion::marcella);if(c&&(c->id==w.self.id||c->bag>0||!c->can_move))c=nullptr;
  auto finite=[&](const p10::Group&a,const p10::Group&v){const int finish=queue_finish(w,a,v,w.self.strength,c,arm==p10::Arm::active);return finish<1000&&std::int64_t(a.travel)+finish<=std::min(128,w.remaining());};
  const p10::Group*a=w.group(current),*next=nullptr;if(a&&(!admit(w,*a,b,fuel_ready,admitted,true)||!finite(*a,*a)))a=nullptr;if(!fuel_ready)return {};
  if(!a)for(const auto&g:w.groups){if(!b.fuel_ok())return {};if(g.gems>0&&admit(w,g,b,fuel_ready,admitted,true)&&finite(g,g)&&(!a||g.gems>a->gems))a=&g;if(!fuel_ready)return {};}
  if(!a)return {};
  // Retain the original nearest second station among complete finite pairs.
  // An independently complete single station is preferable to a fake pair.
  for(const auto&g:w.groups){if(!b.fuel_ok())return {};if(g.id!=a->id&&g.gems>0&&admit(w,g,b,fuel_ready,admitted,true)&&finite(*a,g)&&(!next||g.travel<next->travel))next=&g;if(!fuel_ready)return {};}
  if(!next)next=a;
  const bool collector=w.self.type==p10::Champion::marcella;const int trip=p10_c::Wallet::detour(w,*a),old=queue_finish(w,*a,*next,w.self.strength,c,arm==p10::Arm::active);
  const int period=old+std::max(a->respawn,next->respawn);int best=0,packet=0;auto item=p10::Item::none;
  for(int n=1;n<=std::min(12,w.own_gold/w.shard_cost);++n){if(reader&&reader()<w.action_reserve+1000)return {};if(old>=1000||w.remaining()<old+n+trip)continue;const int cycles=arm==p10::Arm::active?std::min(8,(w.remaining()-n-trip)/std::max(1,period)):1;
   if(w.self.strength_growth>0){const int finish=queue_finish(w,*a,*next,w.self.strength+n*w.self.strength_growth,c,arm==p10::Arm::active);const int value=(old-finish)*cycles-n-trip;if(value>best){best=value;packet=n;item=p10::Item::monster_strength;}}
   if(collector&&w.self.health_growth>0){double custody=0;for(const auto&g:w.groups){if(!b.fuel_ok()||g.monsters.size()>32)return {};if(g.id!=a->id)for(const auto&m:g.monsters){if(!b.fuel_ok())return {};if(m.strength>0&&p10::distance(a->position,m.position)<=m.range)custody+=m.strength;}}
    const int interval=arm==p10::Arm::active?full(*next,w.self.strength)+w.capture_turns+3:w.capture_turns+2;const double target=custody*interval*(w.self.jungle_emblem?.7:1.)+30;
    if(w.self.max_hp<target&&w.self.max_hp+n*w.self.health_growth>=target){const int value=interval*cycles-n-trip;if(value>best){best=value;packet=n;item=p10::Item::jungle_health;}}}
  }
  if(item==p10::Item::none)return {};capital_target=a->id;capital_packet=packet;++capital_events;
  return wallet.issue(w,item,a->id,6301,"own queue-station reinvestment with full custody/clearance service price");
 }
 p10::Directive step(const p10::World&w,p10::Arm arm){
  fresh_traces=0;wallet.observe(w);
  if(w.own_payout&&last_receipt!=p10_c::receipt_round(w)){last_receipt=p10_c::receipt_round(w);for(int i=0;i<exposed_count;++i)if(exposed[i]==w.last_payout_group){exposed[i]=exposed[--exposed_count];--i;}current=-1;}
  for(int i=0;i<exposed_count;++i){const auto*g=w.group(exposed[i]);if(g&&g->observed&&g->respawning){exposed[i]=exposed[--exposed_count];--i;}}
  if(w.self.channel!=p10::Channel::none)return p10::wait("ordered spindle actual service");if(w.self.bag>0)return p10::bank("ordered spindle actual personal owner conversion");
  auto investment=capital(w,arm);if(investment.task!=p10::Task::independent)return investment;
  const bool collector=w.self.type==p10::Champion::marcella;const auto*c=p10_c::visible_type(w,p10::Champion::marcella);
  if(pending>=0&&!collector){const auto*g=w.group(pending);if(g&&g->observed&&g->respawning){pending=-1;current=next_job;}
   else if(g&&p10::usable(*g)&&g->cleared){if(c&&p10_c::on_spawn(*c,*g)){pending=-1;current=next_job;}else if(w.round>vacated){++missed_arrivals;current=g->id;pending=-1;next_job=-1;return p10_c::capture(g->id,"missed collector entry pays a fresh full capture countdown",3033);}}
   else return p10_c::inspect(pending,"unknown ordered handoff requires personal reinspection",3030);}
  auto selection=budget(w);bool fuel_ready=true;Admission admitted;
  const p10::Group*g=w.group(w.capture_active?w.productive_group:current);if(g&&(g->respawning||g->contested||!g->route_known))g=nullptr;
  if(g&&!w.capture_active&&!contains(g->id)&&!admit(w,*g,selection,fuel_ready,admitted))g=nullptr;
  if(!fuel_ready)return p10::wait("finite queue admission preserves the action reserve");
  if(!g&&collector){for(const auto&v:w.groups){if(!selection.fuel_ok())return p10::wait("collector finite queue preserves the action reserve");if(p10::usable(v)&&v.cleared&&admit(w,v,selection,fuel_ready,admitted)){bool free=false;for(auto spawn:v.spawns)if(!p10_mobile::occupied(w,spawn))free=true;if(free&&(!g||v.travel<g->travel))g=&v;}if(!fuel_ready)return p10::wait("collector finite queue preserves the action reserve");}if(g)current=g->id;}
  if(!g){
   if(!cold_queue()){double best=-1;for(const auto&v:w.groups){if(!selection.fuel_ok())return p10::wait("finite queue admission preserves the action reserve");if(v.gems<=0||(exposed_count>=2&&!contains(v.id)))continue;if(!admit(w,v,selection,fuel_ready,admitted)){if(!fuel_ready)return p10::wait("finite queue admission preserves the action reserve");continue;}const double q=(v.gems*8+v.gold*.03+1)/(8+p10::distance(w.spawn,v.position));if(q>best){best=q;g=&v;}}}
   // Personal income alone never makes an impossible renewal the queue head.
   if(!g){g=cold_stage(w,fuel_ready);if(!fuel_ready)return p10::wait("cold queue admission preserves the action reserve");}
   if(g)current=g->id;else current=-1;
  }
  if(!g)return w.safe_now?p10::wait("queue retains short funding until a finite current-body head exists"):p10::Directive{p10::Task::recover};if(!g->observed||!g->complete)return p10_c::inspect(g->id,"ordered spindle appointment",3030);
  auto b=budget(w);auto usage=watch(b);
  if(g->cleared){
   if(!collector)record(g->id);
   if(!collector&&arm==p10::Arm::active&&g->spawns.size()==1&&p10_c::on_spawn(w.self,*g)&&c&&c->can_move&&c->bag==0&&c->id!=w.self.id&&p10_mobile::manhattan(c->position,g->spawns[0])==1&&exposed_count<2){
    const bool after=w.self.priority>c->priority||(w.self.priority==c->priority&&w.self.pick<c->pick);
    const p10::Group*next=nullptr;for(const auto&v:w.groups){if(!b.fuel_ok())return p10_c::capture(g->id,"ordered custody remains while next-stage admission preserves fuel",3032);if(v.id!=g->id&&p10::usable(v)&&v.living&&v.gems>0&&admit(w,v,b,fuel_ready,admitted)&&(!next||v.travel<next->travel))next=&v;if(!fuel_ready)return p10_c::capture(g->id,"ordered custody remains while next-stage admission preserves fuel",3032);}
    if(after&&next){auto proof=prove(w,*next,b);if(proof.feasible){
     const auto entry=g->spawns[0];double custody_damage=0;bool custody_known=!p10_mobile::visible_enemy_can_intervene(w,entry)&&!p10_mobile::visible_enemy_can_intervene(w,c->position);
     for(const auto&v:w.groups)for(const auto&m:v.monsters)if(m.alive&&m.strength>0&&(p10::distance(entry,m.position)<=m.range||p10::distance(c->position,m.position)<=m.range)){custody_known&=v.observed&&v.complete&&m.observed;custody_damage+=m.strength;}
     if(!custody_known||c->hp<=custody_damage*(proof.clear_turns+w.capture_turns+3)*(c->jungle_emblem?.7:1.)+30)return p10_c::capture(g->id,"collector's whole custody interval is not independently safe",3032);

     two_spindle::Workcell cell;cell.count=2;cell.unclaimed=exposed_count;cell.remaining=w.remaining();cell.second_travel=next->travel;cell.tribute_multiplier=w.tribute_multiplier;cell.tribute_base=w.tribute_base;cell.tribute_minimum=w.tribute_minimum;
     for(int i=0;i<2;++i){const auto&v=i?*next:*g;auto&j=cell.jobs[i];j.source=v.id;j.observed=true;j.clear_work=i?proof.clear_turns:0;j.capture=w.capture_turns;j.reward=int(v.gems);j.bank_travel=std::max(0,v.to_bank);j.clearer_id=w.self.id;j.holder_id=c->id;j.holder_bag=int(i?w.self.bag:c->bag);j.spawn_count=int(v.spawns.size());j.custodyMode=i?two_spindle::Custody::self:two_spindle::Custody::conditional_sequence;j.reset_cost=i?0:w.capture_turns;j.missed_arrival_cost=i?0:1;j.holder_arrival=i?0:1;}
     auto serial=two_spindle::schedule(cell,two_spindle::Dispatch::serial),ordered=two_spindle::schedule(cell,two_spindle::Dispatch::overlap);
     if(ordered.complete&&(!serial.complete||ordered.finish<serial.finish))for(auto p:p10_c::cardinals(w.self.position))if(!(p==c->position)&&std::find(g->spawns.begin(),g->spawns.end(),p)==g->spawns.end()){
      p10::Directive d;if(p10_c::safe_vacate(w,p,d)){pending=g->id;next_job=next->id;vacated=w.round;d.group=g->id;d.template_id=3031;d.reason="ordered single-spawn vacate; capture reset and missed arrival explicitly priced";return d;}}
    }}
   }
   return p10_c::capture(g->id,collector?"collector enters only actually free observed ready spawn":"opener completes custody until a legal ordered transfer",3032);
  }
  if(!contains(g->id)){bool positioned=true,on_pad=false;for(const auto&m:g->monsters){if(!b.fuel_ok())return p10::wait("queue arrival preserves the action reserve");if(m.alive&&m.hp>0)positioned&=p10::distance(w.self.position,m.position)<=w.self.range;}if(g->approach_pads.size()<=p10_mobile::max_raw_geometry)for(std::size_t i=0;i<g->approach_pads.size();++i){if((i&7)==0&&!b.fuel_ok())return p10::wait("queue arrival preserves the action reserve");on_pad|=w.self.position==g->approach_pads[i];}if(!positioned&&!on_pad)return p10_c::inspect(g->id,"approach finite queue proposal before spending its own fresh work proof",3030);}
  auto proof=prove(w,*g,b);if(proof.feasible){if(!collector)record(g->id);auto d=proof.first;d.template_id=3034;d.reason=collector?"collector's independently feasible productive appointment":"bounded mobile opener feeds actual ready-stage queue";return d;}
  if(!contains(g->id)&&(proof.failure==p10_mobile::Failure::turn_cap||proof.failure==p10_mobile::Failure::health||proof.failure==p10_mobile::Failure::blocked)){declined=g->id;declined_round=w.round;current=-1;return proof.failure==p10_mobile::Failure::health&&w.self.hp<w.self.max_hp*.9?p10::Directive{p10::Task::recover}:p10::wait("unproved queue head declined; reselect a different finite stage next owner");}
  return p10_c::inspect(g->id,"ordered spindle local geometry warmup",3030);
 }
};
}
