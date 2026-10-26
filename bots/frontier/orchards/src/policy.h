#pragma once
#include "../../common/src/contract.h"
#include "model.h"
namespace orchards {
inline constexpr p10::Profile profile{"p10-orchards",{p10::Champion::tavi,p10::Champion::vesper,p10::Champion::marcella,p10::Champion::vendetta,p10::Champion::varg}};
class AlternatePolicy{
 std::array<int,4>tour_{-1,-1,-1,-1},birth_{-1,-1,-1,-1};int count_=0,cursor_=0,completed_tours_=0,bank_after_=1,stage_=0;bool need_purchase_=false,service_recovery_=false;int purchase_shards_=0,last_round_=-1;
 static Tour make(const p10::World&w,const p10::Group&a,const p10::Group*b,bool reverse,int bank_after){
  Tour t;t.count=b?2:1;t.bank_after=bank_after;t.now=0;t.deadline=w.remaining();t.actor=w.self.id;t.tribute_multiplier=w.tribute_multiplier;t.tribute_base=w.tribute_base;t.tribute_minimum=w.tribute_minimum;
  // A verified conservative spawn→source→altar→source→spawn itinerary bounds
  // bank travel after the explicitly priced per-source recovery service.
  t.altar_travel=std::max(0,a.to_bank)+std::max(0,a.to_spawn);t.return_travel=t.altar_travel;t.recovery=0;
  for(int i=0;i<t.count;++i){const auto&g=b?(reverse?(i?a:*b):(i?*b:a)):a;auto&s=t.sources[i];s.id=g.id;s.clear=std::max(0,g.clear_turns);s.capture=w.capture_turns;s.longest_cooldown=g.respawn;for(const auto&m:g.monsters)s.longest_cooldown=std::max(s.longest_cooldown,m.respawn);s.travel=std::max(0,g.to_spawn);s.opening=i==0?g.travel-s.travel:0;s.last_payout=g.last_payout<0?-1:g.last_payout-w.round;s.observed_round=w.round;s.owner=w.self.id;s.observed=g.observed&&g.complete;s.available=!g.respawning;s.contested=g.contested;s.gems=g.gems;s.gold=g.gold;s.occupation_delay=g.occupied?1:0;
   const int loss=int(std::ceil(g.incoming*std::max(0,g.clear_turns)));s.service_travel=std::max(0,g.to_spawn);
   if(loss>0&&w.spawn_healing<=0)t.premises=false;
   s.heal_ticks=loss>0?int(std::ceil(loss/std::max(.001,w.self.max_hp*w.spawn_healing))):0;
   t.health_loss+=loss;t.restored_health+=int(s.heal_ticks*w.self.max_hp*w.spawn_healing);
  }
  // This conservative catalogue physically returns by the verified spawn leg;
  // field purchases remove only the shopping detour, never fictitious healing.
  t.gear.own_gold=w.own_gold;t.gear.cost=w.shard_cost;t.gear.field=w.self.type==p10::Champion::tavi;t.gear.shop_detour=w.to_spawn<0?999:w.to_spawn+w.recall_turns;
  if(w.self.strength_growth>0&&w.own_gold>=w.shard_cost){int before=0;for(const auto&m:a.monsters)if(m.alive)before+=int(std::ceil(m.hp/std::max(1.,w.self.strength)));
   // Cheap hit-count screening finds the smallest personally affordable build;
   // it does not instantiate a separate live trajectory for every shard count.
   for(int n=1;n<=std::min(5,w.own_gold/w.shard_cost);++n){int after=0;for(const auto&m:a.monsters)if(m.alive)after+=int(std::ceil(m.hp/(w.self.strength+n*w.self.strength_growth)));
    if(after<before){t.gear.shards=n;t.gear.old_clear=std::max(0,a.clear_turns);t.gear.new_clear=std::max(1,t.gear.old_clear-(before-after));t.buy=true;break;}
   }
  }
  return t;
 }
 public:p10::Directive step(const p10::World&w,p10::Arm arm){
  if(w.self.channel!=p10::Channel::none)return p10::wait("orchard liquidation/recovery owner interval");
  if(w.own_bank_completed){stage_=4;completed_tours_=0;count_=0;cursor_=0;birth_={-1,-1,-1,-1};}
  if(w.own_purchase){need_purchase_=false;count_=0;}
  if(w.own_payout){stage_=2;service_recovery_=true;if(count_>0){const auto*paid=w.group(tour_[cursor_]);birth_[cursor_]=w.round+(paid?paid->respawn:0);cursor_=(cursor_+1)%count_;if(cursor_==0)++completed_tours_;}}
  if(w.self.bag>0&&count_==0)return p10::bank("calendar invalidation liquidates actual stranded inventory");
  if(w.self.bag>0&&w.remaining()<std::max(8,w.ticks(w.self.bag)+(w.to_spawn<0?0:w.to_spawn)+4)){stage_=3;return p10::bank("final liquidation stops the repeating orchard premise");}
  if(service_recovery_){if(!w.self.at_spawn){p10::Directive d;d.task=p10::Task::move;d.destination=w.spawn;d.reason="priced source-to-spawn walking recovery before next orchard stage";return d;}if(w.self.hp<w.self.max_hp)return p10::wait("restore measured regenerative HP start state");service_recovery_=false;}
  if(w.self.bag>0&&completed_tours_>=bank_after_){stage_=3;return p10::bank("bank complete tour batch after its modeled source recovery");}
  if(count_>0){const auto*g=w.group(tour_[cursor_]);
   if(!g||g->contested||(!g->observed&&!g->route_known)||w.last.kind==p10::Receipt::Kind::blocked){count_=0;cursor_=0;need_purchase_=false;}
   else if(g->respawning||(!g->observed&&birth_[cursor_]>=0)){
    // A planned next generation is not a missed visit. Retain the two-tour
    // bag, vacate every source spawn, then reobserve at the conditional birth.
    if(birth_[cursor_]<0){count_=0;return w.self.bag>0?p10::bank("unwitnessed source change invalidates orchard calendar"):p10::ordinary(w);}
    if(g->observed&&g->respawn_remaining>0)birth_[cursor_]=std::max(birth_[cursor_],w.round+g->respawn_remaining);
    if(w.round<birth_[cursor_]){if(w.self.at_spawn)return p10::wait("bank-after-two retains bag while waiting off-source for next birth");p10::Directive d;d.task=p10::Task::move;d.destination=w.spawn;d.reason="neutral waiting pad cannot suppress source renewal";return d;}
    p10::Directive d;d.task=p10::Task::inspect;d.group=g->id;d.reason="scheduled birth requires personal lifecycle reinspection";return d;
   }
  }
  if(count_==0){const p10::Group*immediate=nullptr,*renewing=nullptr;
   for(const auto&g:w.groups)if(p10::usable(g)&&g.safe_solo&&g.living&&g.travel>=0&&g.to_bank>=0&&g.to_spawn>=0){
    // Cheap screening creates at most two full calendars regardless of map size.
    const int ticks=std::max(1,p10::cycle_ticks(w,g));
    if(!immediate||double(g.gems)/ticks>double(immediate->gems)/std::max(1,p10::cycle_ticks(w,*immediate)))immediate=&g;
    if(!renewing||g.respawn<renewing->respawn||(g.respawn==renewing->respawn&&g.travel<renewing->travel))renewing=&g;
   }
   if(!immediate)return p10::ordinary(w);
   if(renewing==immediate)renewing=nullptr;
   std::array<Tour,2>catalogue{make(w,*immediate,renewing,false,1),make(w,*immediate,renewing,true,2)};
   // Both arms use the same complete periodic evaluator; the principal control
   // orders openings by immediate complete-cycle output.
   int which=choose(catalogue,arm==p10::Arm::active);if(which<0)return p10::ordinary(w);const auto&t=catalogue[which];
   count_=t.count;cursor_=0;bank_after_=t.bank_after;birth_={-1,-1,-1,-1};for(int i=0;i<count_;++i)tour_[i]=t.sources[i].id;
   need_purchase_=t.buy;purchase_shards_=t.gear.shards;last_round_=w.round;
  }
  if(need_purchase_&&w.own_gold>=purchase_shards_*w.shard_cost){if(w.self.at_spawn||w.self.type==p10::Champion::tavi)return p10::buy(p10::Item::monster_strength,purchase_shards_,"attainable personal tour-period gear threshold");p10::Directive d;d.task=p10::Task::recover;d.reason="funded regime transition includes real shop journey";return d;}
  if(w.self.at_spawn&&w.self.hp<w.self.max_hp)return p10::wait("regenerative start state requires actual spawn healing");
  const auto*g=w.group(tour_[cursor_]);if(!g)return p10::ordinary(w);
  if(g->cleared){p10::Directive d;d.task=p10::Task::capture;d.group=g->id;d.reason="actual generation harvest before source vacation";return d;}
  stage_=1;auto d=p10::farm(g->id,"opening/current tour/named liquidation orchard program");d.template_id=bank_after_;d.expected_min=w.round+std::max(0,g->clear_turns)+w.capture_turns+1;d.expected_max=d.expected_min+std::max(0,g->to_bank)+w.ticks(w.self.bag+g->gems);return d;
 }
};
class Policy{
 AlternatePolicy alternate_{};std::array<int,2>tour_{-1,-1},birth_{-1,-1};int count_=0,cursor_=0,tours_=0,bank_after_=1,last_payout_=-2;bool recovery_=false,needs_recovery_=false,income_=false;
 using FuelReader=int(*)();FuelReader fuel_reader_=nullptr;
 static int leg(const p10::World&w,const p10::Group&a,const p10::Group&b){
  if(a.id==b.id)return 0;
  // A direct local field leg is used only when every intermediate walk tile is
  // personally visible/free. Otherwise the known spawn route is an upper bound.
  auto p=a.approach;bool known=true;int n=0;
  while(!(p==b.approach)&&n<32){p.x+=(b.approach.x>p.x)-(b.approach.x<p.x);p.y+=(b.approach.y>p.y)-(b.approach.y<p.y);++n;
   if(std::find(w.visible_open_tiles.begin(),w.visible_open_tiles.end(),p)==w.visible_open_tiles.end())known=false;}
  return known&&p==b.approach?n:std::max(0,a.to_spawn)+std::max(0,b.to_spawn);
 }
 static Tour field_tour(const p10::World&w,const p10::Group&a,const WorkWitness&pa,const p10::Group*b,const WorkWitness&pb,int batch){
  Tour t;t.count=b?2:1;t.bank_after=batch;t.deadline=w.remaining();t.actor=w.self.id;t.tribute_multiplier=w.tribute_multiplier;t.tribute_base=w.tribute_base;t.tribute_minimum=w.tribute_minimum;
  const auto&last=b?*b:a;const int loss=int(std::ceil(pa.loss+(b?pb.loss:0)));
  t.health_loss=loss;t.restored_health=loss;t.premises=w.self.hp>loss+std::max(30.,w.self.max_hp*.08);
  for(int i=0;i<t.count;++i){const auto&g=i?*b:a;const auto&proof=i?pb:pa;auto&s=t.sources[i];s.id=g.id;s.clear=proof.clear;s.capture=w.capture_turns;s.longest_cooldown=g.respawn;for(const auto&m:g.monsters)s.longest_cooldown=std::max(s.longest_cooldown,m.respawn);
   s.travel=i?leg(w,a,*b):leg(w,last,a);s.opening=i?0:g.travel-s.travel;s.last_payout=g.last_payout<0?-1:g.last_payout-w.round;s.observed_round=w.round;s.owner=w.self.id;s.observed=g.observed&&g.complete;s.available=!g.respawning;s.contested=g.contested;s.gems=g.gems;s.gold=g.gold;
   // Restore a full tour's required start state once, after its last source.
   // Harmless tours need no recovery journey at all, including their payout.
   if(i==t.count-1&&loss>0){s.service_travel=std::max(0,g.to_spawn);if(w.spawn_healing<=0)t.premises=false;s.heal_ticks=int(std::ceil(loss/std::max(.001,w.self.max_hp*w.spawn_healing)));}
  }
  t.altar_travel=std::max(0,last.to_bank)+(loss>0?std::max(0,last.to_spawn):0);t.return_travel=std::max(0,last.to_bank);
  return t;
 }
 public:int variant=0; // 0: regenerative field tour; 1: per-source spawn service.
 static constexpr bool observe_enemy_exits=true;
 void set_fuel_reader(FuelReader r){fuel_reader_=r;}
 p10::Directive step(const p10::World&w,p10::Arm arm){
  if(variant==1)return alternate_.step(w,arm);
  const int receipt=w.last_payout_round>=0?w.last_payout_round:w.round;
  if(w.own_payout&&receipt!=last_payout_){last_payout_=receipt;income_=true;if(count_>0){const auto*paid=w.group(tour_[cursor_]);birth_[cursor_]=w.round+(paid?paid->respawn:0);cursor_=(cursor_+1)%count_;if(cursor_==0){++tours_;recovery_=needs_recovery_;}}}
  if(w.own_bank_completed){count_=0;cursor_=tours_=0;birth_={-1,-1};}
  if(w.own_purchase){count_=0;cursor_=0;}
  if(w.self.channel!=p10::Channel::none)return p10::wait("orchard actual tour service interval");
  if(w.capture_active){p10::Directive d;d.task=p10::Task::capture;d.group=w.productive_group;return d;}
  if(w.self.bag>0&&(count_==0||w.remaining()<std::max(8,w.ticks(w.self.bag)+8)))return p10::bank("orchard named final liquidation");
  if(recovery_){if(!w.self.at_spawn){p10::Directive d;d.task=p10::Task::recover;d.reason="one priced recovery restores the complete tour state";return d;}if(w.self.hp<w.self.max_hp)return p10::wait("restore repeated-tour HP state");recovery_=false;}
  if(w.self.bag>0&&tours_>=bank_after_)return p10::bank("field tour batch deposits actual harvested generations");
  if(w.own_gold>=w.shard_cost&&(w.self.at_spawn||w.self.type==p10::Champion::tavi)&&w.self.strength_shards<10){
   // Only actual own income can finance the next regime. Field shopping never
   // restores HP and is not charged as a fictitious healing visit.
   return p10::buy(p10::Item::monster_strength,1,"owned field-tour hit-count investment");
  }
  const p10::Group*g=count_>0?w.group(tour_[cursor_]):nullptr;
  if(g&&g->contested){count_=0;return w.self.bag>0?p10::bank("lost source ends repeat premise"):p10::ordinary(w);}
  if(g&&(g->respawning||(!g->observed&&birth_[cursor_]>=0))){
   if(g->observed&&g->respawn_remaining>0)birth_[cursor_]=std::max(birth_[cursor_],w.round+g->respawn_remaining);
   if(birth_[cursor_]<0){count_=0;return w.self.bag>0?p10::bank():p10::ordinary(w);}
   if(w.round<birth_[cursor_]){
    if(std::find(g->spawns.begin(),g->spawns.end(),w.self.position)!=g->spawns.end()){p10::Directive d;d.task=p10::Task::release;d.group=g->id;d.reason="neutral field wait vacates actual renewal spawns";return d;}
    return p10::wait("regenerative field wait preserves batch off source");
   }
   auto d=p10::farm(g->id,"actual next birth requires private lifecycle reinspection");d.task=p10::Task::inspect;return d;
  }
  WorkWitness selected_proof;bool selected_fresh=false;const int fuel=fuel_reader_?fuel_reader_():w.fuel;
  if(count_==0){std::array<const p10::Group*,2>best{};std::array<double,2>value{-1,-1};
   for(const auto&v:w.groups)if(p10::usable(v)&&v.living&&v.travel>=0&&v.to_bank>=0&&v.to_spawn>=0&&v.monsters.size()<=12){
    const double q=(v.gems*(income_?10.:6.)+v.gold*.04+1)/(v.travel+6+std::max(0,v.clear_turns));
    if(q>value[0]){value[1]=value[0];best[1]=best[0];value[0]=q;best[0]=&v;}else if(q>value[1]){value[1]=q;best[1]=&v;}}
   if(!best[0]||fuel<w.action_reserve+2000)return p10::ordinary(w);
   auto a=work_witness(w,*best[0]);WorkWitness b;if(best[1]){b=work_witness(w,*best[1]);}
   if(!a.feasible){if(b.feasible){best[0]=best[1];a=b;best[1]=nullptr;}else return p10::ordinary(w);}
   if(!b.feasible)best[1]=nullptr;
   std::array<Tour,2>catalogue{field_tour(w,*best[0],a,best[1],b,1),field_tour(w,*best[0],a,best[1],b,2)};
   if(arm==p10::Arm::active&&best[1]&&best[1]->respawn<best[0]->respawn)catalogue[1]=field_tour(w,*best[1],b,best[0],a,2);
   const int chosen=choose(catalogue,arm==p10::Arm::active);if(chosen<0)return p10::ordinary(w);const auto&t=catalogue[chosen];
   count_=t.count;cursor_=tours_=0;bank_after_=t.bank_after;needs_recovery_=t.health_loss>0;birth_={-1,-1};for(int i=0;i<count_;++i)tour_[i]=t.sources[i].id;
   g=w.group(tour_[0]);selected_proof=g&&g->id==best[0]->id?a:b;selected_fresh=true;
  }
  if(!g)return p10::ordinary(w);
  if(!g->observed||!g->complete){auto d=p10::farm(g->id,"private tour stage verification");d.task=p10::Task::inspect;return d;}
  if(g->cleared){p10::Directive d;d.task=p10::Task::capture;d.group=g->id;d.template_id=30+bank_after_;return d;}
  const auto proof=selected_fresh?selected_proof:work_witness(w,*g);if(!proof.feasible){count_=0;return w.self.bag>0?p10::bank("changed work breaks regenerative tour"):p10::ordinary(w);}
  auto d=proof.stance==w.self.position?proof.first:p10::farm(g->id);if(!(proof.stance==w.self.position)){d.task=p10::Task::move;d.destination=proof.stance;d.reserve_reset=true;}d.reason="execute regenerative source tour without per-payout spawn detour";d.template_id=30+bank_after_;
  return d;
 }
};
} // namespace orchards
