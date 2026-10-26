#pragma once
#include "../../common/src/physical-appointments.h"
#include "../../common/src/partial-work.h"
#include "model.h"
#include "../../common/src/c-capital-01-05.h"
#include <bit>
namespace p10_c_forge {
struct CPolicy:p10_b::Physical {
 // Native calibration is deliberately limited to one already-selected basic.
 // Full work, resets, shields, escape and recovery retain their ordinary budget.
 static constexpr int basic_prefix_action_floor=30000;
 static constexpr bool observe_ally_details=false;
 static constexpr std::uint64_t observe_ally_detail_types=0;
 static constexpr bool owns_capital=true;
 p10_c::Wallet wallet;int capital_target=-1,capital_packet=0,capital_events=0;
 static constexpr bool observe_open_tiles=false;
 static constexpr bool observe_open_rays=true;
 static constexpr int observation_headroom=18000;
 static constexpr bool observe_hazard_envelopes=true;
 enum class PartialDecision {entry=0,service=1,pending=2,escape=3,recovery=4,no_ack=5,capital=6,source=7,approach=8,preconditions=9,prefix=10,emitted=11,full_clear=12};
 enum class PartialSkip {none=0,funded=1,role=2,no_source=3,unobserved=4,incomplete=5,no_target=6,self_details=7,actor_frame=8};
 enum class RuntimeGate {unseen=0,recovery=1,execute_floor=2,certificate=3,handoff_floor=4,entered=5,returned=6,suspended=7};
 struct PartialSnapshot {
  int sequence=0,actor=-1,round=-1,entry_fuel=-1,exit_fuel=-1,reserve=0,source=-1,target=-1;
  PartialDecision decision=PartialDecision::entry;PartialSkip skip=PartialSkip::none;
  p10_partial::ProofDiagnostic proof{};int calls=0,task=-1,tag=-1;
  int ack_for_round=-1,ack_checked_round=-1,ack_source=-1,ack_target=-1;int ack=-1;double ack_amount=0;
  RuntimeGate runtime_gate=RuntimeGate::unseen;int gate_fuel=-1,execute_floor=-1,execute_before=-1,execute_after=-1;
  bool completed=false,pending=false,escaping=false,returning=false;
 } partial_diagnostic_;
 const PartialSnapshot&partial_snapshot()const{return partial_diagnostic_;}
 void partial_runtime_note(int round,RuntimeGate gate,int fuel,int floor=-1){
  if(partial_diagnostic_.round!=round)return;auto&d=partial_diagnostic_;d.runtime_gate=gate;d.gate_fuel=fuel;d.execute_floor=floor;
  if(gate==RuntimeGate::entered)d.execute_before=fuel;if(gate==RuntimeGate::returned)d.execute_after=fuel;
  if(gate==RuntimeGate::suspended)d.completed=false; // Cross-owner fuel pairs are censored.
 }
 void record_partial(const p10::World&w,p10_partial::Options&o){o.diagnostic=&partial_diagnostic_.proof;++partial_diagnostic_.calls;partial_diagnostic_.source=partial_source>=0?partial_source:source;partial_diagnostic_.target=o.target;(void)w;}
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
 static int work(const p10::World&w,const p10::Group&g,double strength,double focus){
  auto fuel_ok=[&]{return !p10::work_fuel_reader||p10::work_fuel_reader()>=w.action_reserve+700;};if(!fuel_ok())return 100000;
  const double factor=w.self.monster_emblem?1.1:1.;int best=0;for(const auto&m:g.monsters)best+=int(std::ceil(m.max_hp/std::max(1.,strength*factor)));
  for(const auto&a:w.self.abilities)if((a.damage||a.aoe||a.cc)&&(a.targets_camps||a.targets_ground)&&a.modifier>0)
   for(const auto&center:g.monsters){if(!fuel_ok())return 100000;int basics=0;int seen=0;for(const auto&m:g.monsters){if((++seen&3)==0&&!fuel_ok())return 100000;const bool struck=m.id==center.id||(a.aoe&&p10::distance(m.position,center.position)<=a.radius);const double hp=std::max(0.,m.max_hp-(struck?a.modifier*focus*factor:0));basics+=int(std::ceil(hp/std::max(1.,strength*factor)));}best=std::min(best,std::max(1,basics));}
  return best;
 }
 p10::Directive capital(const p10::World&w,p10::Arm arm){
  if(reader&&reader()<w.action_reserve+1200)return {};
  if(!wallet.available(w))return {};const bool caster=w.self.type==p10::Champion::stellar;
  const p10::Group*g=w.group(source);if(!g){double best=-1;for(const auto&v:w.groups)if(v.route_known&&v.program_available&&!v.contested&&v.gems>0&&v.to_bank>=0){int clusters=0;for(const auto&m:v.monsters)clusters+=m.strength==0;const double q=(v.gems+1.)/(8+v.to_bank+int(v.monsters.size()));if((!caster||clusters<3)&&q>best){best=q;g=&v;}}}
  if(!g||g->monsters.empty())return {};for(const auto&m:g->monsters)if(m.max_hp<=0)return {};
  if(!p10_c::Wallet::shop(w)&&g->observed&&g->living)for(const auto&m:g->monsters)if(m.alive&&m.hp<m.max_hp)return {};
  const int old=work(w,*g,w.self.strength,w.self.focus),trip=p10_c::Wallet::detour(w,*g);
  if(old>=100000)return {};
  const int period=old+w.capture_turns+1+2*g->to_bank+w.ticks(g->gems)+std::max(0,g->respawn);
  const auto*ven=p10_c::visible_type(w,p10::Champion::vendetta);
  auto prepared=[&](double strength){int count=0;if(!ven||ven->bag>0||!g->observed||!g->complete)return count;for(const auto&m:g->monsters)if(m.alive&&m.strength==0&&m.hp>strength&&m.hp-strength<=ven->strength&&p10::distance(ven->position,m.position)<=ven->range)++count;return count;};
  const int prep=prepared(w.self.strength);int best_gain=0,best_n=0;auto item=p10::Item::none;
  for(int n=1;n<=std::min(12,w.own_gold/w.shard_cost);++n){if(reader&&reader()<w.action_reserve+1000)break;const int cycles=arm==p10::Arm::active?std::min(8,std::max(0,(w.remaining()-n-trip)/std::max(1,period))):1;
   if(w.remaining()<period+n+trip)continue;
   const double strength=w.self.strength+n*w.self.strength_growth;int gain=0;
   if(w.self.strength_growth>0){gain=old-work(w,*g,strength,w.self.focus);
    if(arm==p10::Arm::active&&w.self.type==p10::Champion::varg){const int next=prepared(strength);if(next<prep)continue;gain+=next-prep;}
    if(arm==p10::Arm::active&&w.self.type==p10::Champion::vendetta)for(const auto&m:g->monsters)if(m.strength==0&&m.max_hp>w.self.strength&&m.max_hp<=strength)gain+=1;
    const int value=gain*cycles-n-trip;if(value>best_gain){best_gain=value;best_n=n;item=p10::Item::monster_strength;}
   }
   if(caster&&w.self.focus_growth>0){gain=old-work(w,*g,w.self.strength,w.self.focus+n*w.self.focus_growth);const int value=gain*cycles-n-trip;if(value>best_gain){best_gain=value;best_n=n;item=p10::Item::monster_focus;}}
  }
  if(item==p10::Item::none)return {};capital_target=g->id;capital_packet=best_n;++capital_events;
  return wallet.issue(w,item,g->id,6101,"owned split-cell threshold package; one real shard then fresh work/stat observation");
 }
 p10::Directive work_step(const p10::World&w,p10::Arm arm){
  fresh_traces=0;wallet.observe(w);partial_diagnostic_.decision=PartialDecision::service;
  partial_failed=false;bool unacknowledged=false;
  if(partial_pending&&w.round>partial_round){
   partial_diagnostic_.ack_for_round=partial_round;partial_diagnostic_.ack_checked_round=w.round;partial_diagnostic_.ack_source=partial_source;partial_diagnostic_.ack_target=partial_target;partial_diagnostic_.ack=0;partial_diagnostic_.ack_amount=0;
   partial_acknowledged=false;for(const auto&r:w.receipts)if(r.kind==p10::Receipt::Kind::attack&&r.round==partial_round&&r.group==partial_source&&r.actor==w.self.id&&r.amount>0){partial_acknowledged=true;partial_diagnostic_.ack=1;partial_diagnostic_.ack_amount+=r.amount;}
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
   partial_diagnostic_.decision=PartialDecision::recovery;
   if(!w.self.at_spawn||w.self.hp<w.self.max_hp*.99)return return_home();
   partial_returning=false;
  }
  if(partial_pending){partial_diagnostic_.decision=PartialDecision::pending;return p10::wait("emitted forge prefix awaits a later actual attack receipt");}
  if(partial_escaping){
   partial_diagnostic_.decision=PartialDecision::escape;
   auto b=budget(w);auto usage=watch(b);p10_partial::Options o;o.prefix=p10_partial::Prefix::escape_only;o.target=partial_target;o.endpoint=partial_endpoint;
   if(commit){record_partial(w,o);auto escape=p10_partial::partial_work(w,*commit,b,o);
    if(!escape.feasible&&b.spent<2&&b.fuel_ok()){o.endpoint={};record_partial(w,o);escape=p10_partial::partial_work(w,*commit,b,o);}
    if(escape.feasible){partial_endpoint=escape.endpoint;if(escape.moves>0)return escape.first;partial_escaping=false;partial_returning=true;return return_home();}
   }
   partial_failed=true;partial_returning=true;return return_home();
  }
  if(unacknowledged){partial_diagnostic_.decision=PartialDecision::no_ack;return p10::wait("unexecuted forge prefix made no retained-damage or escape claim");}
  partial_diagnostic_.decision=PartialDecision::capital;
  auto investment=capital(w,arm);if(investment.task!=p10::Task::independent)return investment;
  partial_diagnostic_.decision=PartialDecision::source;
  const auto*ven=p10_b::visible_type(w,p10::Champion::vendetta);const p10::Group*cell=nullptr;double best=-1;
  const bool independent=w.self.type==p10::Champion::stellar||w.self.type==p10::Champion::willow||w.self.type==p10::Champion::marcella;
  const p10::Group*g=w.group(source);if(g&&(!g->route_known||g->respawning||g->contested||rejected(w,*g)))g=nullptr;
  if(!g){
   for(const auto&v:w.groups)if(v.route_known&&v.program_available&&!v.respawning&&!v.contested&&!rejected(w,v)){int clusters=0;for(const auto&m:v.monsters)clusters+=m.alive&&m.strength==0;const double q=(v.gems*(funded?8.:4.)+clusters*10+1)/(8+p10::distance(w.spawn,v.position));if(clusters&&q>best){best=q;cell=&v;}}
   best=-1;for(const auto&v:w.groups)if(v.route_known&&v.program_available&&!v.respawning&&!v.contested&&!rejected(w,v)){if(independent&&cell&&v.id==cell->id)continue;const double q=(v.gems*8+v.gold*.04+1)/(v.travel+8+std::max(0,v.clear_turns));if(q>best){best=q;g=&v;}}
   if(!independent&&cell)g=cell;if(!g&&cell)g=cell;if(g)source=g->id;}
  if(!g){partial_diagnostic_.skip=PartialSkip::no_source;return p10::ordinary(w);}partial_diagnostic_.source=g->id;
  if(!g->observed||!g->complete){partial_diagnostic_.skip=!g->observed?PartialSkip::unobserved:PartialSkip::incomplete;return p10_b::inspect(g->id,"verify split forge cell privately",1010);}
  if(g->cleared)return p10_b::capture(g->id,"sole actual split-forge owner",1014);
  auto b=budget(w);auto usage=watch(b);int preferred=-1;bool prepare=false;
  if(w.self.type==p10::Champion::varg&&ven&&arm==p10::Arm::active){
   for(const auto&primary:g->monsters)if(primary.alive&&p10::distance(w.self.position,primary.position)<=w.self.range)
    for(const auto&cluster:g->monsters)if(cluster.alive&&cluster.strength==0&&p10::distance(primary.position,cluster.position)<=1&&p10::distance(ven->position,cluster.position)<=ven->range&&cluster.hp>ven->strength&&cluster.hp-w.self.strength>0&&cluster.hp-w.self.strength<=ven->strength){preferred=primary.id;prepare=true;}
  }
  if(w.self.type==p10::Champion::vendetta&&arm==p10::Arm::active)for(const auto&m:g->monsters)if(m.alive&&m.strength==0&&m.hp<=w.self.strength&&p10::distance(w.self.position,m.position)<=w.self.range)preferred=m.id;
  const bool partial_role=!funded&&(w.self.type==p10::Champion::varg||w.self.type==p10::Champion::vendetta);
  int current=-1;for(const auto&m:g->monsters)if(m.alive&&m.hp>0&&p10::distance(w.self.position,m.position)<=w.self.range){if(m.id==preferred){current=preferred;break;}if(current<0||m.id<current)current=m.id;}
  partial_diagnostic_.target=current;partial_diagnostic_.decision=PartialDecision::preconditions;
  partial_diagnostic_.skip=funded?PartialSkip::funded:!partial_role?PartialSkip::role:current<0?PartialSkip::no_target:!w.self.combat_details_complete?PartialSkip::self_details:!w.actor_observation_complete?PartialSkip::actor_frame:PartialSkip::none;
  if(partial_role&&current<0&&std::find(g->approach_pads.begin(),g->approach_pads.end(),w.self.position)==g->approach_pads.end()){partial_diagnostic_.decision=PartialDecision::approach;return p10_b::inspect(g->id,"approach the retained-work source before spending a full-clear proof",1010);}
  if(partial_role&&current>=0&&w.self.combat_details_complete&&w.actor_observation_complete){
   p10_partial::Options o;o.target=current;
   // A live headroom check may choose one basic rather than two fresh cast
   // branches. Per-loop Budget guards remain authoritative during either path.
   if(w.self.can_cast&&(!b.fuel_reader||b.fuel_reader()>=b.action_reserve+20000)){for(int i=0;i<2;++i)if(w.self.abilities[i].ready&&w.self.cooldown[i]==0&&w.self.abilities[i].reset&&w.self.abilities[i].targets_self){o.prefix=p10_partial::Prefix::reset_basic;o.slot=i;break;}
    if(o.slot<0)for(int i=0;i<2;++i)if(w.self.abilities[i].ready&&w.self.cooldown[i]==0&&w.self.abilities[i].shield&&w.self.abilities[i].targets_self){o.prefix=p10_partial::Prefix::shield_basic;o.slot=i;break;}}
   partial_diagnostic_.decision=PartialDecision::prefix;record_partial(w,o);
   const int ordinary_reserve=b.action_reserve;
   const bool narrow_basic=w.self.type==p10::Champion::vendetta&&w.self.can_attack&&o.prefix==p10_partial::Prefix::basic;
   if(narrow_basic)b.action_reserve=basic_prefix_action_floor;
   auto partial=p10_partial::partial_work(w,*g,b,o);
   b.action_reserve=ordinary_reserve; // No failure or full-clear fallback inherits the experiment.
   if(narrow_basic&&!partial.feasible&&!b.fuel_ok())return p10::wait("one-basic calibration preserves the ordinary fallback reserve");
   if(narrow_basic&&partial.feasible&&partial.moves>0&&!b.fuel_ok())return p10::wait("reduced-budget work cannot commit an unserviceable next-owner escape");
   if(partial.feasible){partial_diagnostic_.decision=PartialDecision::emitted;partial_diagnostic_.ack_for_round=w.round;partial_diagnostic_.ack_checked_round=-1;partial_diagnostic_.ack_source=g->id;partial_diagnostic_.ack_target=current;partial_diagnostic_.ack=-1;partial_diagnostic_.ack_amount=0;partial_acknowledged=false;partial_pending=true;partial_round=w.round;partial_source=g->id;partial_target=current;partial_endpoint=partial.endpoint;auto d=partial.first;
    d.finite_basic_prefix=narrow_basic&&partial.moves==0&&partial.basics==1&&partial.casts==0&&d.task==p10::Task::attack&&d.cast_slot<0&&d.second_monster<0;
    if(prepare&&current==preferred)++preparation_events;d.template_id=prepare&&current==preferred?1011:1015;d.reason="current split-forge retained work, receipt-confirmed exit and actual spawn recovery";return d;}
  }
  if(b.spent>=2||!b.fuel_ok())return p10_b::inspect(g->id,"two bounded partial-forge branches refused; retain current source economics",1010);
  partial_diagnostic_.decision=PartialDecision::full_clear;
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
 p10::Directive step(const p10::World&w,p10::Arm arm){
  const auto old=partial_diagnostic_;
  partial_diagnostic_={};auto&diag=partial_diagnostic_;diag.sequence=old.sequence+1;diag.actor=w.self.id;diag.round=w.round;diag.reserve=w.action_reserve;diag.entry_fuel=reader?reader():-1;
  // ACK evidence stays until a new emitted prefix replaces it. It cannot be
  // reconstructed from a directive, gate marker, or a disappeared HP bar.
  diag.ack_for_round=old.ack_for_round;diag.ack_checked_round=old.ack_checked_round;diag.ack_source=old.ack_source;diag.ack_target=old.ack_target;diag.ack=old.ack;diag.ack_amount=old.ack_amount;
  auto d=work_step(w,arm);
  diag.task=int(d.task==p10::Task::independent?p10::Task::wait:d.task);diag.tag=d.template_id;diag.pending=partial_pending;diag.escaping=partial_escaping;diag.returning=partial_returning;diag.exit_fuel=reader?reader():-1;diag.completed=true;
  // Current Runtime auto-shops on Task::independent. C owns the capital menu:
  // no silent one-shard fallback may bypass its paid service admission.
  return d.task==p10::Task::independent?p10::wait("owned forge awaits productive work or a funded admitted package"):d;
 }

};
}
