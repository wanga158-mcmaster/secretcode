#pragma once
#include "../../common/src/physical-appointments.h"
#include "model.h"

namespace p10_b_orchard {
struct BPolicy : p10_b::Physical {
 static constexpr bool observe_ally_details=false;
 int source=-1,bank_after=1,completed=0,birth=-1,last_payout=-2,last_bank=-2;
 int threshold_purchases=0,observed_purchases=0,calendar_choices=0,current_only_choices=0;
 int last_purchase=-2;bool repeating=false,purchase_issued=false;
 p10::Point wait_pad{};
 orchards::Calendar selected_calendar{};
 mutable const p10::World*work_world=nullptr;mutable bool work_aborted=false;mutable int work_checks=0;mutable int work_phase=0;
 bool fuel_ok()const{++work_checks;if(work_aborted)return false;const auto r=reader?reader:p10::work_fuel_reader;const int fuel=r?r():(work_world?work_world->fuel:100000);if(work_world&&fuel<work_world->action_reserve+1500)work_aborted=true;return !work_aborted;}
 p10::Directive fuel_stop()const{return p10::wait("field tour defers unfinished live graph/calendar work before its action reserve");}
 int longest(const p10::Group&g)const{int n=std::max(0,g.respawn);for(const auto&m:g.monsters){if(!fuel_ok())return n;n=std::max(n,m.respawn);}return n;}
 bool harmless(const p10::Group&g)const{if(g.monsters.empty())return false;for(const auto&m:g.monsters)if(!fuel_ok()||!m.observed||m.max_hp<=0||m.strength!=0)return false;return true;}
 bool spawn(const p10::Group&g,p10::Point p)const{for(auto tile:g.spawns){if(!fuel_ok())return false;if(tile==p)return true;}return false;}
 bool open_return(const p10::World&w,const p10::Group&g,p10::Point p)const{
  if(!fuel_ok())return false;
  if(p10_mobile::occupied(w,p)&&!(p==w.self.position))return false;
  return spawn(g,p)||p==w.self.position||std::find(g.approach_pads.begin(),g.approach_pads.end(),p)!=g.approach_pads.end()||std::find(w.visible_open_tiles.begin(),w.visible_open_tiles.end(),p)!=w.visible_open_tiles.end();
 }
 bool hazard_free(const p10::World&w,const p10::Group&g,p10::Point p)const{
  if(!fuel_ok())return false;
  if(p10_mobile::visible_enemy_can_intervene(w,p))return false;
  for(const auto&other:w.groups){if(!fuel_ok())return false;if(other.id==g.id)continue;
   // Other births may occupy these tiles during a future tour, even when the
   // currently observed group is dead and its monsters cause no damage.
   if(spawn(other,p))return false;
   for(const auto&m:other.monsters){
    if(!fuel_ok())return false;
    if(m.position.x<0||m.position.y<0)return false;
    if(m.position==p)return false;
    const bool known=other.observed&&other.complete&&m.observed&&m.range>=0&&
     std::isfinite(m.strength)&&m.strength>=0&&std::isfinite(m.hp)&&m.hp>=0&&std::isfinite(m.max_hp)&&m.max_hp>0;
    // Five is the largest range in the pinned current camp rules. A larger
    // supplied range always wins. Missing current data never proves harmless.
    const int reach=known?m.range:std::max(5,m.range);
    if(p10::distance(p,m.position)<=reach&&(!known||m.strength>0))return false;
   }
  }
  return true;
 }
 int path_from(const p10::World&w,const p10::Group&g,p10::Point origin,p10::Point pad,int axis)const{
  if(!open_return(w,g,origin)||!hazard_free(w,g,origin))return -1;
  auto p=origin;int n=0;
  while(!(p==pad)&&n<32){
   if(!fuel_ok())return -1;
   if((axis==0&&p.x!=pad.x)||(axis==1&&p.y==pad.y))p.x+=(pad.x>p.x)-(pad.x<p.x);
   else p.y+=(pad.y>p.y)-(pad.y<p.y);
   ++n;if(!open_return(w,g,p)||!hazard_free(w,g,p))return -1;
  }
  return p==pad?n:-1;
 }
 int continuation(const p10::World&w,const p10::Group&g,p10::Point origin,p10::Point pad)const{
  if(!fuel_ok())return -1;
  const int x=path_from(w,g,origin,pad,0),y=path_from(w,g,origin,pad,1);
  return x<0?y:y<0?x:std::min(x,y);
 }
 // This path is walked immediately after payout, while this source is dead.
 // Every source spawn has a fully checked cardinal L-route to the firing pad.
 int return_path(const p10::World&w,const p10::Group&g,p10::Point pad)const{
  if(g.spawns.empty())return -1;int longest_path=0;
  for(auto origin:g.spawns){if(!fuel_ok())return -1;const int n=continuation(w,g,origin,pad);if(n<0)return -1;longest_path=std::max(longest_path,n);}
  return longest_path;
 }
 bool neutral(const p10::World&w,const p10::Group&g,p10::Point pad)const{
  if(spawn(g,pad)||!open_return(w,g,pad)||!hazard_free(w,g,pad))return false;
  for(const auto&m:g.monsters)if(!fuel_ok()||p10::distance(pad,m.position)>w.self.range)return false;
  return true;
 }
 int full_basics(const p10::World&w,const p10::Group&g)const{
  const double damage=w.self.strength*(w.self.monster_emblem?1.1:1.);if(damage<=0)return 1000;
  int hits=0;for(const auto&m:g.monsters){if(!fuel_ok())return 1000;hits+=int(std::ceil(m.max_hp/damage));}return hits;
 }
 orchards::Tour make(const p10::World&w,const p10::Group&g,const p10_mobile::Result&proof,int batch,p10::Point&pad)const{
  orchards::Tour t;t.count=1;t.bank_after=batch;t.now=w.round;t.deadline=w.max_rounds;t.actor=w.self.id;
  t.tribute_multiplier=w.tribute_multiplier;t.tribute_base=w.tribute_base;t.tribute_minimum=w.tribute_minimum;
  t.premises=proof.feasible&&harmless(g)&&!g.spawns.empty()&&g.route_known&&g.to_bank>=0&&w.geometry_complete&&w.observation_complete;
  if(!fuel_ok()){t.premises=false;return t;}int leg=-1;auto consider=[&](p10::Point p){if(!fuel_ok()||!neutral(w,g,p))return;const int n=return_path(w,g,p);if(n>=0&&(leg<0||n<leg)){leg=n;pad=p;}};
  consider(w.self.position);for(auto p:g.approach_pads){if(!fuel_ok()){t.premises=false;return t;}consider(p);}consider(g.approach);
  const int basic=full_basics(w,g),entry=return_path(w,g,proof.position);
  if(!fuel_ok()||leg<0||entry<0||basic>64||proof.hp+1e-6<w.self.hp)t.premises=false;
  auto&s=t.sources[0];s.id=g.id;s.observed=g.observed&&g.complete;s.available=!g.respawning;s.contested=g.contested;
  s.owner=w.self.id;s.observed_round=w.round;s.gems=g.gems;s.gold=g.gold;s.capture=w.capture_turns;
  s.longest_cooldown=longest(g);s.last_payout=g.last_payout;s.occupation_delay=g.occupied?1:0;
  // The static pad reaches every full-health monster with ordinary basics.
  // Extra opening work is charged; partial current HP never shrinks a birth.
  // calendar adds one entry tick. Charge every extra pad-to-spawn step too.
  s.clear=basic+std::max(0,leg-1);s.travel=std::max(0,leg);
  s.opening=std::max(0,proof.clear_turns+std::max(1,entry)-basic-2*std::max(0,leg));
  t.altar_travel=std::max(0,g.to_bank);t.return_travel=std::max(0,g.to_bank)+std::max(0,leg);
  return t;
 }
 bool threshold(const p10::World&w,const p10::Group&g,const p10_mobile::Result&proof)const{
  if(!proof.feasible||proof.clear_turns<=1||w.shard_cost<=0||w.own_gold<w.shard_cost||w.self.strength_growth<=0||
     !(w.self.at_spawn||w.self.type==p10::Champion::tavi))return false;
  const double factor=w.self.monster_emblem?1.1:1.,before=w.self.strength*factor,after=(w.self.strength+w.self.strength_growth)*factor;
  if(before<=0||after<=before)return false;int old_hits=0,new_hits=0;
  for(const auto&m:g.monsters){if(!fuel_ok())return false;if(m.alive&&m.hp>0){old_hits+=int(std::ceil(m.hp/before));new_hits+=int(std::ceil(m.hp/after));}}
  return new_hits<old_hits;
 }
 p10::Directive off_source(const p10::World&w,const p10::Group&g)const{
  work_phase=6;if(!fuel_ok())return fuel_stop();
  if(!spawn(g,w.self.position)&&w.self.position==wait_pad&&hazard_free(w,g,wait_pad))return p10::wait("field tour waits on its neutral firing pad");
  if(!w.self.can_move)return p10::wait("field return requires an actual movement allowance");
  const std::array<p10::Point,4>next{{{w.self.position.x+1,w.self.position.y},{w.self.position.x-1,w.self.position.y},{w.self.position.x,w.self.position.y+1},{w.self.position.x,w.self.position.y-1}}};
  for(auto p:next){if(!fuel_ok())return fuel_stop();if(p10_mobile::manhattan(p,wait_pad)<p10_mobile::manhattan(w.self.position,wait_pad)&&continuation(w,g,p,wait_pad)>=0){
   p10::Directive d;if(p10_b::safe_vacate(w,p,d)){d.group=g.id;d.reason="vacate harvested spawns along the verified field return";d.template_id=5052;return d;}}
  }if(!fuel_ok())return fuel_stop();
  return w.self.bag>0?p10::bank("blocked field return ends this regenerative batch"):p10_b::inspect(g.id,"field return needs fresh occupancy",5050);
 }
 p10::Directive step(const p10::World&w,p10::Arm arm){
  fresh_traces=0;work_world=&w;work_aborted=false;work_checks=0;work_phase=1;
  const int receipt=p10_b::receipt_round(w);
  if(w.own_payout&&receipt!=last_payout){last_payout=receipt;if(source>=0&&(w.last_payout_group<0||w.last_payout_group==source)){
    ++completed;const auto*g=w.group(source);birth=receipt+(g?longest(*g):0);if(work_aborted)birth=-1;}}
  const int bank_round=w.last_bank_round>=0?w.last_bank_round:w.round;
  if(w.own_bank_completed&&bank_round!=last_bank){last_bank=bank_round;source=-1;completed=0;birth=-1;repeating=false;purchase_issued=false;}
  if(w.own_purchase&&w.round!=last_purchase){last_purchase=w.round;++observed_purchases;if(completed==0)source=-1;}
  if(w.self.channel!=p10::Channel::none)return p10::wait("field tour preserves its actual economic channel");
  if(w.capture_active)return p10_b::capture(w.productive_group,"finish actual field harvest custody",5053);
  if(!fuel_ok())return fuel_stop();
  if(w.last.kind==p10::Receipt::Kind::blocked){source=-1;repeating=false;}
  if(w.self.bag>0&&(source<0||!repeating||completed>=bank_after||w.remaining()<=w.ticks(w.self.bag)+8))
   return p10::bank("deposit the completed field tour batch before its deadline");
  const auto*g=w.group(source);
  if(g&&(g->contested||!g->route_known)){source=-1;return w.self.bag>0?p10::bank("changed source ends the field tour"):p10_b::inspect(g->id,"field appointment requires reinspection",5050);}
  if(g&&(g->respawning||(birth>=0&&g->cleared&&completed>0))){
   if(g->observed&&g->respawn_remaining>0)birth=std::max(birth,w.round+g->respawn_remaining);
   if(birth<0){source=-1;return w.self.bag>0?p10::bank("unwitnessed birth ends the tour"):p10_b::inspect(g->id,"source birth was not owned",5050);}
   if(!fuel_ok())return fuel_stop();if(w.round<birth||spawn(*g,w.self.position))return off_source(w,*g);
   return p10_b::inspect(g->id,"next generation requires actual private lifecycle evidence",5050);
  }
  auto budget=this->budget(w);auto usage=watch(budget);p10_mobile::Result chosen_proof;bool fresh=false;
  if(source<0){
   std::array<const p10::Group*,2>candidates{};
   for(const auto&v:w.groups){if(!fuel_ok())return fuel_stop();if(p10::usable(v)&&v.gems>0&&v.to_bank>=0&&v.monsters.size()<=12){
    if(!candidates[0]||longest(v)<longest(*candidates[0])||(longest(v)==longest(*candidates[0])&&v.travel<candidates[0]->travel))candidates[0]=&v;
    if(!candidates[1]||v.gems>candidates[1]->gems||(v.gems==candidates[1]->gems&&v.travel<candidates[1]->travel))candidates[1]=&v;
   }}if(!fuel_ok())return fuel_stop();
   if(!candidates[0]){
    const p10::Group*appointment=nullptr;for(const auto&v:w.groups){if(!fuel_ok())return fuel_stop();if(v.route_known&&v.program_available&&!v.contested&&!v.respawning&&v.gems>0){
     const bool better=!appointment||(arm==p10::Arm::active?longest(v)<longest(*appointment):v.gems>appointment->gems);
     if(better||(appointment&&longest(v)==longest(*appointment)&&v.gems==appointment->gems&&v.travel<appointment->travel))appointment=&v;
    }}if(!fuel_ok())return fuel_stop();
    if(appointment)return p10_b::inspect(appointment->id,"public field opening appointment; calendar waits for private physical evidence",5050);
    return w.self.bag>0?p10::bank("liquidate unappointed field inventory"):p10::wait("no reachable field opening");
   }
   if(candidates[1]==candidates[0])candidates[1]=nullptr;
   std::array<p10_mobile::Result,2>proofs{};std::array<orchards::Tour,2>catalogue{};std::array<p10::Point,2>pads{};
   int fallback=-1;double immediate=-1;
   for(int i=0;i<2;++i)if(candidates[i]){if(!fuel_ok())return fuel_stop();const auto&v=*candidates[i];work_phase=2;
    if(v.cleared){proofs[i].feasible=true;proofs[i].hp=w.self.hp;proofs[i].clear_turns=0;proofs[i].position=w.self.position;}else proofs[i]=prove(w,v,budget);
    if(!fuel_ok())return fuel_stop();if(!proofs[i].feasible)continue;work_phase=3;
    const int entry=return_path(w,v,proofs[i].position);
    const int current_finish=proofs[i].clear_turns+std::max(1,entry)+w.capture_turns+v.to_bank+w.ticks(v.gems);
    const double value=double(v.gems)/std::max(1,current_finish);
    if(entry>=0&&current_finish<=w.remaining()&&value>immediate){immediate=value;fallback=i;}
    if(!fuel_ok())return fuel_stop();catalogue[i]=make(w,v,proofs[i],1,pads[i]);
    auto second=make(w,v,proofs[i],2,pads[i]);
    if(!fuel_ok())return fuel_stop();work_phase=4;const auto first_calendar=orchards::calendar(catalogue[i]);if(!fuel_ok())return fuel_stop();const auto second_calendar=orchards::calendar(second);if(!fuel_ok())return fuel_stop();
    if(!first_calendar.complete)catalogue[i].premises=false;
    if(!second_calendar.complete)second.premises=false;
    if(arm==p10::Arm::active&&second_calendar.banked>first_calendar.banked)catalogue[i]=second;
   }
   if(!fuel_ok())return fuel_stop();
   const int picked=orchards::choose(catalogue,arm==p10::Arm::active);
   if(!fuel_ok())return fuel_stop();
   const int which=picked>=0?picked:fallback;
   if(which<0)return p10_b::inspect(candidates[0]->id,"bounded physical field proof is warming or unavailable",5050);
   g=candidates[which];source=g->id;completed=0;birth=-1;chosen_proof=proofs[which];fresh=true;
   repeating=picked>=0;bank_after=repeating?catalogue[which].bank_after:1;wait_pad=pads[which];
   if(repeating){selected_calendar=orchards::calendar(catalogue[which]);if(!fuel_ok()){source=-1;repeating=false;selected_calendar={};return fuel_stop();}++calendar_choices;}else{selected_calendar={};++current_only_choices;}
  }
  if(!g)return p10::wait("field appointment is unavailable");
  if(!g->observed||!g->complete)return p10_b::inspect(g->id,"actual field source state must be complete",5050);
  if(g->cleared)return p10_b::capture(g->id,"hold the actual harvested generation",5053);
  work_phase=2;const auto proof=fresh?chosen_proof:prove(w,*g,budget);if(!fuel_ok())return fuel_stop();
  if(!proof.feasible){source=-1;return w.self.bag>0?p10::bank("changed physical work ends the field tour"):p10_b::inspect(g->id,"field clearance requires a fresh bounded proof",5050);}
  work_phase=5;const bool buy_threshold=!purchase_issued&&threshold(w,*g,proof);if(!fuel_ok())return fuel_stop();if(buy_threshold){purchase_issued=true;++threshold_purchases;
   auto d=p10::buy(p10::Item::monster_strength,1,"personally funded one-shard basic hit threshold; reprove after purchase");d.group=g->id;d.template_id=5054;return d;}
  auto d=proof.first;d.group=g->id;d.template_id=repeating?5051:5055;
  d.reason=repeating?"execute the current field tour birth with actual physical clearance":"execute current work without an unproved future recovery premise";
  return d;
 }
};
} // namespace p10_b_orchard
