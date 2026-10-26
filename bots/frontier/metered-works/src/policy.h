#pragma once
#include "../../common/src/contract.h"
#include "model.h"
namespace metered_works {
inline constexpr p10::Profile profile{"p10-metered-works",{p10::Champion::marcella,p10::Champion::varg,p10::Champion::willow,p10::Champion::tavi,p10::Champion::vesper}};
class AlternatePolicy{
 std::array<int,2>sources_{-1,-1};int source_count_=0,index_=0,stage_=0,payouts_=0;int batch_start_=-1,next_visit_=-1;Program organization_=Program::sole;bool program_chosen_=false;
 static bool present(const p10::Actor&a,const p10::Group&g){if(!a.visible||!a.alive||a.channel!=p10::Channel::none)return false;for(auto p:g.spawns)if(a.position==p)return true;return false;}
 Batch batch(const p10::World&w,const p10::Group&first,const p10::Group&second,const p10::Actor*second_holder)const{
  Batch b;b.settings={w.tribute_multiplier,w.tribute_base,w.tribute_minimum};b.remaining=std::min(192,w.remaining());b.count=2;b.workers_count=second_holder?2:1;b.party_verified=second_holder&&present(w.self,first)&&present(*second_holder,first);b.source_travel=p10::distance(first.position,second.position);
  auto worker=[&](const p10::Actor&a){Worker v;v.id=a.id;v.arrival=p10::distance(a.position,first.position);v.altar_travel=std::max(0,first.to_bank);v.return_travel=v.altar_travel;v.bag=a.bag;v.marcella=a.type==p10::Champion::marcella;v.healer=a.type==p10::Champion::willow;return v;};
  b.workers[0]=worker(w.self);if(second_holder)b.workers[1]=worker(*second_holder);
  for(int i=0;i<2;++i){const auto&g=i?second:first;auto&s=b.sources[i];s.id=g.id;s.clear=std::max(0,g.clear_turns);s.capture=w.capture_turns;s.gems=g.gems;s.gold=g.gold;s.observed=g.observed&&g.complete;s.contested=g.contested;}
  if(first.id==second.id){int future_work=0;for(const auto&m:first.monsters)future_work+=int(std::ceil(m.max_hp/std::max(1.,w.self.strength)));b.sources[1].clear=b.sources[0].clear+w.capture_turns+1+first.respawn+future_work;}
  for(const auto&a:w.altars)if(a.route_known&&a.enemy_lock)b.locked=true;
  // Crossdock remains represented but unavailable until official geometry AND
  // owner-start/capture phase calibration is supplied; proximity alone is not it.
  return b;
 }
 public:p10::Directive step(const p10::World&w,p10::Arm arm){
  if(w.self.channel!=p10::Channel::none)return p10::wait("preserve actual exhaustive personal tribute");
  if(w.own_bank_completed){if(payouts_>=2){stage_=4;source_count_=0;index_=0;payouts_=0;batch_start_=-1;program_chosen_=false;next_visit_=-1;}else stage_=1;}
  if(w.own_payout){++payouts_;stage_=payouts_>=2?3:1;if(source_count_>1)index_=(index_+1)%source_count_;else if(source_count_==1){const auto*paid=w.group(sources_[0]);next_visit_=w.round+(paid?paid->respawn:0);}}
  if(w.self.bag>0&&(stage_==3||w.remaining()<192))return p10::bank("all actual payout owners finish personal service and return");
  if(w.own_gold>=w.shard_cost&&(w.self.at_spawn||w.self.type==p10::Champion::tavi)){
   if(w.self.type==p10::Champion::willow&&w.self.focus_shards<5)return p10::buy(p10::Item::jungle_focus,1,"actual Willow capture share funds future heal");
   if(w.self.strength_shards<10)return p10::buy(p10::Item::monster_strength,1,"post-receipt individual metered capital");
  }
  if(batch_start_<0){batch_start_=w.round;source_count_=0;for(const auto&g:w.groups)if(p10::usable(g)&&g.safe_solo&&g.travel>=0&&source_count_<2)sources_[source_count_++]=g.id;}
  const p10::Group*g=index_<source_count_?w.group(sources_[index_]):nullptr;
  if(g&&g->respawning){
   // One-source batches require two real generations. Leave every source spawn
   // and retain the declared second capture while its witnessed clock runs.
   if(next_visit_<0)next_visit_=w.round+std::max(0,g->respawn_remaining);
   if(w.round<next_visit_){if(w.self.at_spawn)return p10::wait("declared second capture waits off-source for witnessed renewal");p10::Directive d;d.task=p10::Task::move;d.destination=w.spawn;d.reason="vacate source during declared batch renewal wait";return d;}
   p10::Directive d;d.task=p10::Task::inspect;d.group=g->id;d.reason="personally reobserve second batch generation";return d;
  }
  if(g&&(!g->observed||!g->complete)&&g->route_known){p10::Directive d;d.task=p10::Task::inspect;d.group=g->id;d.reason="retain declared source while personally verifying next capture";return d;}
  if(g&&g->contested)return w.self.bag>0?p10::bank("interrupted common batch preserves actual receipt accounting"):p10::Directive{p10::Task::recover};
  if(!g)return p10::ordinary(w);
  if(w.round-batch_start_>=192){source_count_=0;batch_start_=-1;return w.self.bag>0?p10::bank("cap-censored batch liquidation"):p10::ordinary(w);}
  if(g->cleared){stage_=2;
   const p10::Actor*other=nullptr;for(const auto&a:w.allies)if(present(a,*g)&&a.id!=w.self.id&&(a.type==p10::Champion::marcella||a.type==p10::Champion::willow))other=&a;
   const p10::Group*next=source_count_>1?w.group(sources_[1-index_]):g;if(!next)next=g;
   if(!program_chosen_){auto b=batch(w,*g,*next,other);auto selected=choose(b,arm==p10::Arm::active);organization_=selected.kind;program_chosen_=true;}
   // Joining another actual holder is permitted only when the entire common
   // two-capture batch repays the extra party's travel, service and return.
   if(w.self.type==p10::Champion::willow&&other&&organization_!=Program::parallel){
    for(const auto&v:w.groups)if(v.id!=g->id&&p10::usable(v)&&v.safe_solo&&v.living)return p10::farm(v.id,"second holder fails full service-batch repayment");
   }
   if(w.self.type==p10::Champion::varg&&other&&g->spawns.size()>1&&present(w.self,*g)){
    for(const auto&v:w.groups)if(v.id!=g->id&&p10::usable(v)&&v.safe_solo&&v.living)return p10::farm(v.id,"visible converter releases Varg to named production");
   }
   p10::Directive d;d.task=p10::Task::capture;d.group=g->id;d.template_id=organization_==Program::parallel?2:1;d.reason=organization_==Program::parallel?"verified two-party payout and parallel individual service":"sole-holder complete-cycle circulation";d.expected_min=w.round+w.capture_turns+1;d.expected_max=d.expected_min+std::max(0,g->to_bank)+w.ticks(w.self.bag+g->gems);return d;
  }
  if(w.self.type==p10::Champion::willow&&w.self.can_cast&&w.self.abilities[0].ready)for(const auto&a:w.allies)if(a.visible&&a.alive&&a.channel==p10::Channel::none&&p10::distance(w.self.position,a.position)<=4&&a.max_hp-a.hp>=3*w.self.focus){p10::Directive d;d.task=p10::Task::heal;d.actor=a.id;d.cast_slot=0;d.reason="productive heal outside personal channel";return d;}
  stage_=1;return p10::farm(g->id,"declared two-capture batch productive opening");
 }
};
class Policy{
 AlternatePolicy alternate_{};std::array<int,2>sources_{-1,-1};int count_=0,index_=0,payouts_=0,start_=-1,last_payout_=-2;Program program_=Program::sole;bool planned_=false,income_=false;
 using FuelReader=int(*)();FuelReader fuel_reader_=nullptr;
 static bool present(const p10::Actor&a,const p10::Group&g){return a.visible&&a.alive&&a.channel==p10::Channel::none&&std::find(g.spawns.begin(),g.spawns.end(),a.position)!=g.spawns.end();}
 Batch future_batch(const p10::World&w,const p10::Group&a,const p10::Group&b,const p10::Actor*other,int clear)const{
  Batch out;out.remaining=std::min(192,w.remaining());out.settings={w.tribute_multiplier,w.tribute_base,w.tribute_minimum};out.source_travel=p10::distance(a.position,b.position);
  out.workers_count=other?2:1;out.prospective=other!=nullptr;out.party_verified=other&&present(w.self,a)&&present(*other,a);
  auto worker=[&](const p10::Actor&v){Worker r;r.id=v.id;r.arrival=p10::distance(v.position,a.position);r.altar_travel=std::max(a.to_bank,b.to_bank);r.return_travel=r.altar_travel;r.bag=v.bag;r.marcella=v.type==p10::Champion::marcella;r.healer=v.type==p10::Champion::willow;return r;};
  out.workers[0]=worker(w.self);if(other)out.workers[1]=worker(*other);
  for(int i=0;i<2;++i){const auto&g=i?b:a;auto&v=out.sources[i];v.id=g.id;v.observed=g.observed&&g.complete;v.contested=g.contested;v.capture=w.capture_turns;v.clear=i?std::max(clear,g.clear_turns):clear;v.gems=g.gems;v.gold=g.gold;}
  if(a.id==b.id)out.sources[1].clear+=w.capture_turns+1+a.respawn;
  // The recipient menu is prospective; payout credit remains conditional until
  // personally observed participants actually occupy source spawns.
  return out;
 }
 public:int variant=0; // 0: prospective service batch; 1: already-present party.
 void set_fuel_reader(FuelReader r){fuel_reader_=r;}
 p10::Directive step(const p10::World&w,p10::Arm arm){
  if(variant==1)return alternate_.step(w,arm);
  const int receipt=w.last_payout_round>=0?w.last_payout_round:w.round;
  if(w.own_payout&&receipt!=last_payout_){last_payout_=receipt;income_=true;++payouts_;if(count_>1)index_=(index_+1)%count_;}
  if(w.own_bank_completed){count_=0;index_=payouts_=0;planned_=false;start_=-1;}
  if(w.self.channel!=p10::Channel::none)return p10::wait("metered actual owner's personal service");
  if(w.self.bag>0&&(payouts_>=2||w.remaining()<192||(start_>=0&&w.round-start_>=192)))return p10::bank("complete actual two-capture owner service batch");
  if(w.own_gold>=w.shard_cost&&(w.self.at_spawn||w.self.type==p10::Champion::tavi)){
   if(w.self.type==p10::Champion::willow&&w.self.focus_shards<5)return p10::buy(p10::Item::jungle_focus,1,"recipient's actual own share funds support");
   if(w.self.strength_shards<10)return p10::buy(p10::Item::monster_strength,1,"recipient owned service capacity");
  }
  if(count_==0){std::array<const p10::Group*,2>best{};std::array<double,2>value{-1,-1};
   for(const auto&g:w.groups)if(g.route_known&&g.program_available&&!g.respawning&&!g.contested&&g.travel>=0&&g.to_bank>=0&&g.monsters.size()<=12){
    // Same static appointment order for prospective party members; real combat
    // and their individually owned receipts are verified in private later.
    const double q=(g.gems*(income_?10.:6.)+g.gold*.05+1)/(8+p10::distance(w.spawn,g.position));
    if(q>value[0]){best[1]=best[0];value[1]=value[0];best[0]=&g;value[0]=q;}else if(q>value[1]){best[1]=&g;value[1]=q;}}
   if(best[0]){sources_[0]=best[0]->id;count_=1;if(best[1]){sources_[1]=best[1]->id;count_=2;}start_=w.round;}
  }
  const p10::Group*g=index_<count_?w.group(sources_[index_]):nullptr;
  if(w.capture_active)g=w.group(w.productive_group);
  if(!g)return w.self.bag>0?p10::bank():p10::ordinary(w);
  if(g->contested||(start_>=0&&w.round-start_>=192)){count_=0;planned_=false;return w.self.bag>0?p10::bank("interrupted common batch liquidates actual bag"):p10::ordinary(w);}
  if(g->respawning){
   if(count_>1){index_=(index_+1)%count_;const auto*next=w.group(sources_[index_]);if(next&&!next->respawning){g=next;}else return w.self.bag>0?p10::bank("all declared sources currently unavailable"):p10::ordinary(w);}
   else {if(w.self.bag>0)return p10::bank("one-source service safely precedes second generation");auto d=p10::farm(g->id,"reinspect conditional second batch generation");d.task=p10::Task::inspect;return d;}
  }
  if(!g->observed||!g->complete){auto d=p10::farm(g->id,"personally verify prospective service appointment");d.task=p10::Task::inspect;d.template_id=20;return d;}
  const p10::Actor*other=nullptr;
  for(const auto&a:w.allies)if(a.visible&&a.alive&&a.channel==p10::Channel::none&&a.id!=w.self.id&&(a.type==p10::Champion::marcella||a.type==p10::Champion::willow)&&p10::distance(a.position,g->position)<=4)other=&a;
  const auto*next=count_>1?w.group(sources_[1-index_]):g;if(!next)next=g;
  const int fuel=fuel_reader_?fuel_reader_():w.fuel;
  WorkWitness proof;if(!g->cleared&&fuel>w.action_reserve+1500)proof=work_witness(w,*g);
  if(!planned_&&(g->cleared||proof.feasible)){
   const auto choice=choose(future_batch(w,*g,*next,other,g->cleared?0:proof.clear),arm==p10::Arm::active);program_=choice.kind;planned_=true;
  }
  if(g->cleared){
   const bool verified=other&&present(w.self,*g)&&present(*other,*g)&&!(w.self.position==other->position);
   if(w.self.type==p10::Champion::varg&&other&&present(*other,*g)&&g->spawns.size()>1&&present(w.self,*g)){
    if(next->id!=g->id&&p10::usable(*next)&&next->living&&fuel>w.action_reserve+1500){auto next_proof=work_witness(w,*next);if(next_proof.feasible){index_=1-index_;auto d=next_proof.stance==w.self.position?next_proof.first:p10::farm(next->id);if(!(next_proof.stance==w.self.position)){d.task=p10::Task::move;d.destination=next_proof.stance;d.reserve_reset=true;}d.reason="actual converter custody releases Varg to the second source";d.template_id=23;return d;}}
   }
   if(w.self.type==p10::Champion::willow&&program_!=Program::parallel){
    if(next->id!=g->id&&p10::usable(*next)&&next->living){index_=1-index_;planned_=false;auto d=p10::farm(next->id,"future second party fails complete service repayment");d.task=p10::Task::inspect;return d;}
   }
   // A prospective choice is not a receipt. Until actual overlap, each current
   // holder safely continues; no faster capture or already split bag is claimed.
   p10::Directive d;d.task=p10::Task::capture;d.group=g->id;d.template_id=program_==Program::parallel?(verified?22:21):1;
   d.reason=program_==Program::parallel?"future party rendezvous; verify real recipients at payout":"competent sole-recipient complete service batch";return d;
  }
  if(w.self.type==p10::Champion::willow&&w.self.can_cast&&w.self.abilities[0].ready)
   for(const auto&a:w.allies)if(a.visible&&a.alive&&a.channel==p10::Channel::none&&p10::distance(a.position,w.self.position)<=4&&a.max_hp-a.hp>=3*w.self.focus){p10::Directive d;d.task=p10::Task::heal;d.actor=a.id;d.cast_slot=0;return d;}
  if(!proof.feasible){auto d=p10::farm(g->id,"prospective recipient approaches useful private appointment");d.task=p10::Task::inspect;d.template_id=20;return d;}
  auto d=proof.stance==w.self.position?proof.first:p10::farm(g->id);if(!(proof.stance==w.self.position)){d.task=p10::Task::move;d.destination=proof.stance;d.reserve_reset=true;}d.reason="two-capture prospective party productive opening";d.template_id=program_==Program::parallel?21:1;
  return d;
 }
};
} // namespace metered_works
