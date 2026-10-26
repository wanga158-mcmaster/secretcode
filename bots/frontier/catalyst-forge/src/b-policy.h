#pragma once
#include "../../common/src/physical-appointments.h"
#include "../../common/src/partial-work.h"
#include "model.h"
#include <bit>
namespace p10_b_forge {
struct BPolicy:p10_b::Physical {
 static constexpr bool observe_ally_details=false;
 static constexpr bool observe_open_tiles=false;
 static constexpr bool observe_open_rays=true;
 static constexpr int observation_headroom=18000;
 int source=-1,last_receipt=-2;bool funded=false;int preparation_events=0;
 // Only acknowledged action identities and geometric intentions persist.
 // Every escape is rebuilt from current camps, actors, HP and free tiles.
 bool partial_pending=false,partial_escaping=false,partial_returning=false,partial_acknowledged=false,partial_failed=false;
 bool critical_continuation()const{return partial_pending||partial_escaping||partial_returning;}
 int partial_round=-1,partial_source=-1,partial_target=-1;p10::Point partial_endpoint{};
 void cancel_partial(bool keep_return=false){partial_pending=partial_escaping=false;if(!keep_return)partial_returning=false;partial_acknowledged=false;partial_failed=false;partial_source=partial_target=-1;partial_endpoint={};}
 static p10::Directive return_home(){p10::Directive d;d.task=p10::Task::recover;d.reason="retained forge work returns to actual spawn healing before another prefix";return d;}
 struct Refusal {int source=-1,round=-1;std::uint64_t premise=0;};
 std::array<Refusal,16>refusals{};int refusal_cursor=0,refusal_events=0;
 static std::uint64_t premise(const p10::World&w,const p10::Group&g){
  std::uint64_t h=1469598103934665603ull;auto add=[&](std::uint64_t n){h=(h^n)*1099511628211ull;};
  auto value=[&](double n){add(std::bit_cast<std::uint64_t>(n));};
  add(w.self.position.x);add(w.self.position.y);value(w.self.hp);value(w.self.max_hp);value(w.self.strength);value(w.self.focus);value(w.self.range);value(w.self.shield);add(std::max(0,w.self.shield_until-w.round));add(w.self.monster_emblem);add(w.self.jungle_emblem);
  for(auto cd:w.self.cooldown)add(cd);
  for(const auto&m:g.monsters){add(m.id);value(m.hp);value(m.strength);add(m.stun);add(m.awake);add(m.alive);}
  for(const auto&a:w.allies)if(a.visible&&a.alive){add(a.id);add(a.position.x);add(a.position.y);}
  for(const auto&a:w.enemies)if(a.visible&&a.alive){add(a.id);add(a.position.x);add(a.position.y);}
  return h;
 }
 bool rejected(const p10::World&w,const p10::Group&g)const{if(!g.observed||!g.complete)return false;for(const auto&r:refusals)if(r.source==g.id&&w.round-r.round<=32&&r.premise==premise(w,g))return true;return false;}
 static bool durable(const p10::World&w,const p10::Group&g,p10_mobile::Failure f){return f==p10_mobile::Failure::health||f==p10_mobile::Failure::turn_cap||(f==p10_mobile::Failure::blocked&&std::find(g.approach_pads.begin(),g.approach_pads.end(),w.self.position)!=g.approach_pads.end());}
 void reject(const p10::World&w,const p10::Group&g){refusals[refusal_cursor++%refusals.size()]={g.id,w.round,premise(w,g)};++refusal_events;}
 p10::Directive step(const p10::World&w,p10::Arm arm){
  fresh_traces=0;
  partial_failed=false;bool unacknowledged=false;
  if(partial_pending&&w.round>partial_round){
   partial_acknowledged=false;for(const auto&r:w.receipts)if(r.kind==p10::Receipt::Kind::attack&&r.round==partial_round&&r.group==partial_source&&r.actor==w.self.id&&r.amount>0)partial_acknowledged=true;
   partial_pending=false;
   if(partial_acknowledged)partial_escaping=true;
   else if(p10::possible_phase_damage(w,w.self.position)>0){partial_escaping=true;partial_endpoint={};}
   else unacknowledged=true;
  }
  if(w.own_payout&&last_receipt!=p10_b::receipt_round(w)){last_receipt=p10_b::receipt_round(w);funded=true;source=-1;cancel_partial(true);}
  if(w.own_bank_completed)source=-1;
  if(w.self.channel!=p10::Channel::none)return p10::wait("split forge actual service");
  if(w.self.bag>0)return p10::bank("split-cell actual payout owner banks independently");
  if(w.capture_active){cancel_partial();return p10_b::capture(w.productive_group,"split cell retains real custody",1014);}
  const auto*commit=w.group(partial_source);
  if((partial_pending||partial_escaping||partial_returning||unacknowledged)&&commit&&commit->observed&&commit->complete&&commit->cleared){source=commit->id;cancel_partial();return p10_b::capture(source,"actual cleared partial-work source takes custody before retreat",1014);}
  if(partial_escaping&&w.self.at_spawn){partial_escaping=false;partial_returning=true;}
  if(partial_returning&&!partial_escaping){
   if(!w.self.at_spawn||w.self.hp<w.self.max_hp*.99)return return_home();
   partial_returning=false;
  }
  if(partial_pending)return p10::wait("emitted forge prefix awaits a later actual attack receipt");
  if(partial_escaping){
   auto b=budget(w);auto usage=watch(b);p10_partial::Options o;o.prefix=p10_partial::Prefix::escape_only;o.target=partial_target;o.endpoint=partial_endpoint;
   if(commit){auto escape=p10_partial::partial_work(w,*commit,b,o);
    if(!escape.feasible&&b.spent<2&&b.fuel_ok()){o.endpoint={};escape=p10_partial::partial_work(w,*commit,b,o);}
    if(escape.feasible){partial_endpoint=escape.endpoint;if(escape.moves>0)return escape.first;partial_escaping=false;partial_returning=true;return return_home();}
   }
   partial_failed=true;partial_returning=true;return return_home();
  }
  if(unacknowledged)return p10::wait("unexecuted forge prefix made no retained-damage or escape claim");
  if(w.own_gold>=w.shard_cost&&(w.self.at_spawn||w.self.type==p10::Champion::tavi)&&w.self.strength_shards+w.self.focus_shards<10)return p10::buy(w.self.type==p10::Champion::stellar?p10::Item::monster_focus:p10::Item::monster_strength,1,"own split-cell threshold");
  const auto*ven=p10_b::visible_type(w,p10::Champion::vendetta);const p10::Group*cell=nullptr;double best=-1;
  const bool independent=w.self.type==p10::Champion::stellar||w.self.type==p10::Champion::willow||w.self.type==p10::Champion::marcella;
  const p10::Group*g=w.group(source);if(g&&(!g->route_known||g->respawning||g->contested||rejected(w,*g)))g=nullptr;
  if(!g){
   for(const auto&v:w.groups)if(v.route_known&&v.program_available&&!v.respawning&&!v.contested&&!rejected(w,v)){int clusters=0;for(const auto&m:v.monsters)clusters+=m.alive&&m.strength==0;const double q=(v.gems*(funded?8.:4.)+clusters*10+1)/(8+p10::distance(w.spawn,v.position));if(clusters&&q>best){best=q;cell=&v;}}
   best=-1;for(const auto&v:w.groups)if(v.route_known&&v.program_available&&!v.respawning&&!v.contested&&!rejected(w,v)){if(independent&&cell&&v.id==cell->id)continue;const double q=(v.gems*8+v.gold*.04+1)/(v.travel+8+std::max(0,v.clear_turns));if(q>best){best=q;g=&v;}}
   if(!independent&&cell)g=cell;if(!g&&cell)g=cell;if(g)source=g->id;}
  if(!g)return p10::ordinary(w);if(!g->observed||!g->complete)return p10_b::inspect(g->id,"verify split forge cell privately",1010);
  if(g->cleared)return p10_b::capture(g->id,"sole actual split-forge owner",1014);
  auto b=budget(w);auto usage=watch(b);int preferred=-1;bool prepare=false;
  if(w.self.type==p10::Champion::varg&&ven&&arm==p10::Arm::active){
   for(const auto&primary:g->monsters)if(primary.alive&&p10::distance(w.self.position,primary.position)<=w.self.range)
    for(const auto&cluster:g->monsters)if(cluster.alive&&cluster.strength==0&&p10::distance(primary.position,cluster.position)<=1&&p10::distance(ven->position,cluster.position)<=ven->range&&cluster.hp>ven->strength&&cluster.hp-w.self.strength>0&&cluster.hp-w.self.strength<=ven->strength){preferred=primary.id;prepare=true;}
  }
  if(w.self.type==p10::Champion::vendetta&&arm==p10::Arm::active)for(const auto&m:g->monsters)if(m.alive&&m.strength==0&&m.hp<=w.self.strength&&p10::distance(w.self.position,m.position)<=w.self.range)preferred=m.id;
  const bool partial_role=!funded&&(w.self.type==p10::Champion::varg||w.self.type==p10::Champion::vendetta);
  int current=-1;for(const auto&m:g->monsters)if(m.alive&&m.hp>0&&p10::distance(w.self.position,m.position)<=w.self.range){if(m.id==preferred){current=preferred;break;}if(current<0||m.id<current)current=m.id;}
  if(partial_role&&current<0&&std::find(g->approach_pads.begin(),g->approach_pads.end(),w.self.position)==g->approach_pads.end())return p10_b::inspect(g->id,"approach the retained-work source before spending a full-clear proof",1010);
  if(partial_role&&current>=0&&w.self.combat_details_complete&&w.actor_observation_complete){
   p10_partial::Options o;o.target=current;
   // A live headroom check may choose one basic rather than two fresh cast
   // branches. Per-loop Budget guards remain authoritative during either path.
   if(w.self.can_cast&&(!b.fuel_reader||b.fuel_reader()>=b.action_reserve+20000)){for(int i=0;i<2;++i)if(w.self.abilities[i].ready&&w.self.cooldown[i]==0&&w.self.abilities[i].reset&&w.self.abilities[i].targets_self){o.prefix=p10_partial::Prefix::reset_basic;o.slot=i;break;}
    if(o.slot<0)for(int i=0;i<2;++i)if(w.self.abilities[i].ready&&w.self.cooldown[i]==0&&w.self.abilities[i].shield&&w.self.abilities[i].targets_self){o.prefix=p10_partial::Prefix::shield_basic;o.slot=i;break;}}
   auto partial=p10_partial::partial_work(w,*g,b,o);
   if(partial.feasible){partial_acknowledged=false;partial_pending=true;partial_round=w.round;partial_source=g->id;partial_target=current;partial_endpoint=partial.endpoint;auto d=partial.first;
    if(prepare&&current==preferred)++preparation_events;d.template_id=prepare&&current==preferred?1011:1015;d.reason="current split-forge retained work, receipt-confirmed exit and actual spawn recovery";return d;}
  }
  if(b.spent>=2||!b.fuel_ok())return p10_b::inspect(g->id,"two bounded partial-forge branches refused; retain current source economics",1010);
  auto proof=prove(w,*g,b,preferred,prepare);
  if(proof.feasible){auto d=proof.first;const int actual=d.task==p10::Task::cast?d.second_monster:d.monster;
   const bool actual_preparation=prepare&&actual==preferred&&(d.task==p10::Task::attack||d.task==p10::Task::cast);
   if(actual_preparation)++preparation_events;d.template_id=actual_preparation?1011:independent?1013:1012;d.reason=actual_preparation?"one certified splash manufactures a visible one-hit Vendetta catalyst":independent?"Stellar/support executes its own separate productive circuit":"two-body forge cell executes own complete mobile work";return d;}
  if(durable(w,*g,proof.failure)){
   reject(w,*g);source=-1;const p10::Group*other=nullptr;double value=-1;
   // Preserve this card's split-cell economics; only the failed physical
   // premise is excluded, with a bounded retry lifetime and fresh-state key.
   for(const auto&v:w.groups)if(v.id!=g->id&&v.route_known&&v.program_available&&!v.respawning&&!v.contested&&!rejected(w,v)){
    const double q=(v.gems*8+v.gold*.04+1)/(v.travel+8+std::max(0,v.clear_turns));if(q>value){value=q;other=&v;}}
   if(!other)return p10::wait("split forge rejected current finite programs; await fresh premises");source=other->id;
   if(!other->observed||!other->complete)return p10_b::inspect(other->id,"split forge inspects a different non-refused job",1010);
   if(other->cleared)return p10_b::capture(other->id,"split forge takes actual alternate custody",1014);
   if(b.spent<2){auto alternate=prove(w,*other,b);if(alternate.feasible){auto d=alternate.first;d.template_id=independent?1013:1012;d.reason="split forge replaces a refused long-clear cell with actual productive work";return d;}
    if(durable(w,*other,alternate.failure)){reject(w,*other);source=-1;return p10::wait("two bounded split-forge physical refusals; reselect next fresh turn");}}
   return p10_b::inspect(other->id,"split forge warms its alternate immutable geometry",1010);
  }
  return p10_b::inspect(g->id,"bounded split-forge geometry warmup/appointment",1010);
 }
};
}
