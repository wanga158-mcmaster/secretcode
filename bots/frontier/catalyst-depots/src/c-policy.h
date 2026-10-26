#pragma once
#include "b-policy.h"
#include "../../common/src/c-capital-06-10.h"
#if P10_DEPOT_CALLER_TRACE
#include <bit>
#include <unistd.h>
#endif
namespace p10_depots {
struct CPolicy:BPolicy {
 static constexpr bool owns_capital=true;
 static constexpr bool observe_ally_details=false;
 static constexpr std::uint64_t observe_ally_detail_types=(std::uint64_t{1} << static_cast<unsigned>(p10::Champion::vendetta));
 p10_c_capital::Income capitalIncome;int returnCycle=0;
#if P10_DEPOT_CALLER_TRACE
 int caller_loaded=0,caller_seen=0,caller_skipped=0,caller_c_path=0;
 // Diagnostic only: no SDK queries, at most three loaded Vendetta packets.
 // Fixed-width integer/bit-pattern serialization avoids formatted floating
 // conversion. The conservative World stamp is not actual post-policy fuel.
 void caller_packet(const p10::World&w,const p10::Directive&d,p10::Arm arm){
  if(w.self.type!=p10::Champion::vendetta||w.self.bag<=0)return;
  ++caller_seen;if(caller_loaded>=3)return;
  if(w.fuel<65000){++caller_skipped;return;}
  const auto&a=w.self;const auto written=std::min<std::size_t>(4,w.altars.size());
  std::array<char,1024> buffer;std::size_t size=0;
  for(char c:std::array<char,11>{{'p','1','0','_','c','a','l','l','e','r','2'}})buffer[size++]=c;
  const char digits[]="0123456789abcdef";
  auto word=[&](std::uint32_t value){buffer[size++]=' ';for(int shift=28;shift>=0;shift-=4)buffer[size++]=digits[(value>>shift)&15];};
  auto wide=[&](std::uint64_t value){word(std::uint32_t(value));word(std::uint32_t(value>>32));};
  word(2);word(w.round);word(a.id);word(int(arm));word(a.position.x);word(a.position.y);wide(std::bit_cast<std::uint64_t>(a.hp));
  word(w.own_gold);wide(std::uint64_t(a.bag));word(int(a.channel));word(w.capture_active);word(caller_c_path);word(caller_b_entered);word(caller_b_bank);
  word(a.cooldown[1]);word(a.can_cast);word(a.can_attack);word(a.can_move);word(a.abilities[1].dash);wide(std::bit_cast<std::uint64_t>(a.abilities[1].range));
  word(w.fuel);word(w.action_reserve);word(w.entry_fuel);word(w.safe_now);word(w.geometry_complete);word(w.observation_complete);word(w.actor_observation_complete);
  word(int(d.task));word(d.template_id);word(d.altar);word(d.destination.x);word(d.destination.y);word(d.group);word(d.monster);
  word(caller_seen);word(caller_loaded+1);word(caller_skipped);word(std::uint32_t(w.altars.size()));word(std::uint32_t(written));
  for(std::size_t i=0;i<written;++i){const auto&b=w.altars[i];for(int value:{b.id,b.position.x,b.position.y,b.approach.x,b.approach.y,int(b.route_known),b.travel,int(b.visible),int(b.enemy_lock),int(b.friendly_lock)})word(value);}
  buffer[size++]='\n';if(::write(1,buffer.data(),size)==static_cast<ssize_t>(size))++caller_loaded;
 }
 p10::Directive step(const p10::World&w,p10::Arm arm){
  caller_c_path=4;caller_b_entered=0;caller_b_bank=-1;
  const auto d=caller_step(w,arm);caller_packet(w,d,arm);return d;
 }
#endif
 p10::Item reinvest(const p10::World&w)const{
  using namespace p10;using p10_c_capital::stock;
  // The supplier's production finances its own stock. The consumer gets the
  // same physical menu: completed camp passive, one survival family, raw work.
  if(stock(w,Item::monster_strength)<5)return Item::monster_strength;
  if(stock(w,Item::jungle_health)<5)return Item::jungle_health;
  return Item::strength;
 }
 p10::Directive independent(const p10::World&w){
  using namespace p10;p10_b_local::Turn turn(w);const Group*job=nullptr;
  for(const auto&g:w.groups)if(g.program_available&&g.route_known&&!g.respawning&&!g.contested&&g.travel>=0&&g.to_bank>=0&&
   (!job||g.travel<job->travel||(g.travel==job->travel&&g.id<job->id)))job=&g;
  if(job){auto ingress=appointment(w,*job,turn.budget);if(ingress.task!=Task::independent)return ingress;}
  auto d=job?turn.work(*job,"matched depot control liquidates every source instead of manufacturing return stock",60):ordinary(w);fresh_trajectories=turn.budget.spent;return d;
 }
 // A full local source program owns no global ingress. Current contact must
 // precede its trace, including after the supplier has bought real capital.
 p10::Directive appointment(const p10::World&w,const p10::Group&g,p10_mobile::Budget&budget)const{
  using namespace p10;
  if(!g.observed||!g.complete||!g.living||g.cleared||g.respawning||g.contested||!g.route_known||g.travel<0||!g.program_available)return {};
  if(!w.safe_now||!w.self.can_move||!w.geometry_complete||!w.actor_observation_complete)return {};
  bool living=false,positioned=w.self.can_attack;
  for(const auto&m:g.monsters){if(!budget.fuel_ok())return wait("depot appointment preserves the action reserve");if(m.alive&&m.hp>0){living=true;positioned&=distance(w.self.position,m.position)<=w.self.range;}}
  if(living&&positioned)return {};
  for(const auto*points:{&g.spawns,&g.approach_pads})for(auto p:*points){if(!budget.fuel_ok())return wait("depot appointment preserves the action reserve");if(p10_mobile::manhattan(w.self.position,p)<=1)return {};}
  Directive d;d.task=Task::inspect;d.group=g.id;d.destination=g.approach;d.valid_round=w.round;d.reserve_reset=true;
  d.reason="depot supplier retains its named job and approaches the actual local graph before work";return d;
 }
 p10::Directive active_appointment(const p10::World&w)const{
  using namespace p10;if(!w.safe_now)return {};auto budget=p10_mobile::budget_for(w);int excludedDepot=depot;
  // Mirror B's existing work recipient and precedence. In particular a banked
  // consumed depot and a supplier's recorded independent job are not replaced.
  if(bankReceipt>=0&&depot>=0){const auto*g=w.group(depot);if(g&&usable(*g)&&!g->respawning)return appointment(w,*g,budget);excludedDepot=-1;}
  const Altar*bankPoint=nullptr;for(const auto&a:w.altars){if(!budget.fuel_ok())return wait("depot appointment preserves the action reserve");if(a.route_known&&!a.enemy_lock&&a.travel>=0&&(!bankPoint||a.travel<bankPoint->travel))bankPoint=&a;}
  const Actor*customer=nullptr;for(const auto&a:w.allies){if(!budget.fuel_ok())return wait("depot appointment preserves the action reserve");if(a.visible&&a.alive&&a.type==Champion::vendetta&&a.bag>0&&a.cooldown[1]>1)customer=&a;}
  const Group*stock=nullptr,*job=nullptr;const Monster*prepare=nullptr;
  if(customer&&ownIncome&&bankPoint&&quiet(w,-1))for(const auto&g:w.groups){if(!budget.fuel_ok())return wait("depot appointment preserves the action reserve");if(usable(g)&&harmless(g)&&g.travel<=1&&distance(customer->position,g.position)+distance(g.position,bankPoint->approach)<=distance(customer->position,bankPoint->approach)+1){
   const double damage=w.self.strength*(w.self.monster_emblem?1.1:1.);
   for(const auto&m:g.monsters){if(!budget.fuel_ok())return wait("depot appointment preserves the action reserve");if(m.alive&&m.hp>damage&&m.hp-damage<=customer->strength*(customer->monster_emblem?1.1:1.)&&distance(w.self.position,m.position)<=w.self.range){stock=&g;prepare=&m;break;}}
   if(stock)break;
  }}
  for(const auto&g:w.groups){if(!budget.fuel_ok())return wait("depot appointment preserves the action reserve");if((!stock||g.id!=stock->id)&&g.id!=excludedDepot&&g.route_known&&g.program_available&&!g.respawning&&!g.contested&&g.travel>=0&&g.to_bank>=0&&(!job||g.travel<job->travel||(g.travel==job->travel&&g.id<job->id)))job=&g;}
  if(stock&&prepare&&job&&w.self.can_attack&&distance(w.self.position,job->position)<=w.self.range+2)return appointment(w,*job,budget);
  if(producerJob>=0){const auto*g=w.group(producerJob);if(g&&usable(*g))return appointment(w,*g,budget);}
  return job?appointment(w,*job,budget):Directive{};
 }
#if P10_DEPOT_CALLER_TRACE
 p10::Directive caller_step(const p10::World&w,p10::Arm arm){
#else
 p10::Directive step(const p10::World&w,p10::Arm arm){
#endif
  using namespace p10;using namespace p10_c_capital;capitalIncome.read(w);receipts(w);fresh_trajectories=0;
  if(protected_turn(w)){
#if P10_DEPOT_CALLER_TRACE
   caller_c_path=1;
#endif
   return wait("depot capital respects an actual capture/channel");
  }
  if(w.self.bag>0){
#if P10_DEPOT_CALLER_TRACE
   caller_c_path=arm==Arm::active?2:3;
#endif
   return arm==Arm::active?BPolicy::step(w,arm):bank("matched depot control banks its personal output");
  }
  invested=true; // Retire B's one-shard terminal; receipts never cap C growth.
  if(capitalIncome.funded&&affordable(w)>0&&legal_shop(w)){
   const auto item=reinvest(w);const int missing=item==Item::strength?5:5-p10_c_capital::stock(w,item);
   return purchase(w,item,missing,"personally funded depot production replenishes recurring work capital");
  }
  if(capitalIncome.funded&&w.own_gold>=5*w.shard_cost&&!legal_shop(w)&&w.safe_now){++returnCycle;Directive d;d.task=Task::recover;d.reason="depot supplier closes a real funded batch at spawn before renewed service";return d;}
  if(arm==Arm::active){auto ingress=active_appointment(w);if(ingress.task!=Task::independent)return ingress;}
  return arm==Arm::active?BPolicy::step(w,arm):independent(w);
 }
};
}
