#pragma once
#include <iostream>
#include "navigation.h"
#include "combat.h"
#include "mobile-work.h"
#include "runtime-receipt.h"
#if P10_RUNTIME_RECEIPT_ENABLE
#define P10_RUNTIME_EXIT(CODE) runtime_receipt_.finish(RR::CODE)
#else
#define P10_RUNTIME_EXIT(CODE) ((void)0)
#endif

namespace p10 {
template<class Policy> class Runtime {
#if P10_RUNTIME_RECEIPT_ENABLE
 using RR=RuntimeReceipt;RR runtime_receipt_;
#endif
 Profile profile_;Arm arm_;Policy policy_{};Navigation nav_{};World frame_{};bool frame_ready_=false;
 int last_round_=-1,hold_=-1,hold_since_=-1,hold_last_=-1,hold_snapshot_round_=-1,payout_round_=-1,payout_group_=-1,bank_round_=-1;
 Gems hold_bag_=0,previous_bag_=0;int hold_gold_=0;bool previous_alive_=false,bank_pending_=false,recovering_=false,hold_unobserved_=false;
 int pulse_group_=-1,pulse_round_=-1;double pulse_hp_=0;bool pulse_recover_=false;
 struct PulseVeto {int group;double hp;};std::vector<PulseVeto> pulse_vetoes_;double veto_strength_=-1,veto_max_hp_=-1,veto_focus_=-1;
 Receipt last_{};std::vector<Receipt> receipts_;EconomicInbox economic_;Task logged_task_=Task::independent;int logged_group_=-2,logged_template_=-1;
 int reserve_=40000,entry_=100000,last_block_seen_=-1,observation_intent_=-1;bool configured_=false;Gems bank_score_=0;
 std::array<int,4>profile_rounds_{-1000,-1000,-1000,-1000};int observe_cost_round_=-1000;
 int max_action_cost_=0,reported_action_cost_=0,max_action_before_=0,max_action_after_=0,max_action_round_=-1;Task max_action_task_=Task::wait;
 const char* stage_="entry";int stage_fuel_=0;bool critical_turn_=false;
 struct PolicySample {int round=-1,before=0,after=0,task=0,group=-1,tag=0,calls=0,traces=0,failure=-1,source=-1;bool feasible=false;} policy_sample_{};
 int policy_report_round_=-1000,reported_policy_task_=-1,reported_policy_group_=-2,reported_policy_failure_=-2;
 struct NoPartialSample {int round=-1;};
 static auto partial_sample_type(){if constexpr(requires(const Policy&p){p.partial_snapshot();})return std::remove_cvref_t<decltype(std::declval<const Policy&>().partial_snapshot())>{};else return NoPartialSample{};}
 using PartialSample=decltype(partial_sample_type());PartialSample partial_report_{};
 bool partial_report_pending_=false;int partial_seen_sequence_=-1,partial_report_round_=-1000;
 int partial_report_failure_=-1,partial_report_stage_=-1,partial_report_skip_=-1,partial_report_gate_=-1,partial_report_ack_=-2;
 void partial_gate(int round,int gate,int floor=-1){if constexpr(requires{typename Policy::RuntimeGate;})policy_.partial_runtime_note(round,static_cast<typename Policy::RuntimeGate>(gate),int(cambc::fuel_remaining()),floor);}
#if P10_RUNTIME_RECEIPT_ENABLE
 bool execution_handoff(int round,int floor){
  if(!current(round)){runtime_receipt_.set(RR::handoff_result,2);partial_gate(round,7,floor);return false;}
  const auto fuel=cambc::fuel_remaining();runtime_receipt_.set(RR::handoff_fuel,int(fuel));runtime_receipt_.bit(RR::handoff_present);
  if(fuel<std::uint64_t(floor)){runtime_receipt_.set(RR::handoff_result,3);partial_gate(round,4,floor);return false;}
  runtime_receipt_.set(RR::handoff_result,1);return true;
 }
#else
 bool execution_handoff(int round,int floor){if(!current(round)){partial_gate(round,7,floor);return false;}if(cambc::fuel_remaining()<std::uint64_t(floor)){partial_gate(round,4,floor);return false;}return true;}
#endif
 int execution_floor(const World&w,const Directive&d)const{
  const int ordinary=d.task==Task::inspect||d.task==Task::move?std::max(38000,reserve_*93/100):reserve_;
  if constexpr(requires{Policy::basic_prefix_action_floor;}){
   const int proposed=Policy::basic_prefix_action_floor;
   if(proposed>=30000&&proposed<ordinary&&d.finite_basic_prefix&&d.task==Task::attack&&
      w.self.type==Champion::vendetta&&w.self.visible&&w.self.alive&&w.self.can_attack&&w.self.channel==Channel::none&&
      d.group>=0&&d.monster>=0&&d.actor<0&&d.cast_slot<0&&d.second_monster<0&&d.priority==0&&d.destination.x<0&&
      d.strict_tank&&d.reserve_reset&&d.valid_round==w.round&&d.local_safety_verified&&fresh_certificate(w,d)){
    if(const auto*g=w.group(d.group);g&&usable(*g)&&g->living)for(const auto&m:g->monsters)
     if(m.id==d.monster&&m.observed&&m.alive&&m.hp>0&&distance(w.self.position,m.position)<=w.self.range)return proposed;
   }
  }
  return ordinary;
 }
 void partial_report(int round){if constexpr(requires(const Policy&p){p.partial_snapshot();}){
  const auto&s=policy_.partial_snapshot();
  if(s.round>=0&&s.round<round&&s.sequence!=partial_seen_sequence_){partial_seen_sequence_=s.sequence;if(!partial_report_pending_){partial_report_=s;partial_report_pending_=true;}}
  if(!partial_report_pending_||critical_turn_||cambc::fuel_remaining()<90000)return;
  const auto&samp=partial_report_;const auto&proof=samp.proof;
  const bool changed=int(proof.failure)!=partial_report_failure_||int(proof.stage)!=partial_report_stage_||int(samp.skip)!=partial_report_skip_||int(samp.runtime_gate)!=partial_report_gate_||samp.ack!=partial_report_ack_;
  if(round-partial_report_round_<64&&(!changed||round-partial_report_round_<16)){partial_report_pending_=false;return;}
  trace("p10 partial actor=",samp.actor," round=",round," measuredRound=",samp.round," source=",samp.source," target=",samp.target," decision=",int(samp.decision)," skip=",int(samp.skip)," fuel=",samp.entry_fuel,"/",samp.exit_fuel," reserve=",samp.reserve," calls=",samp.calls," proof=",int(proof.completed),"/",int(proof.feasible),"/",int(proof.failure),"/",int(proof.stage)," prefix=",int(proof.prefix)," proofReserve=",proof.action_reserve," proofFuel=",proof.fuel_before,"/",proof.fuel_after," stages=",proof.geometry_fuel,"/",proof.catalogue_fuel,"/",proof.occupancy_fuel,"/",proof.escape_fuel,"/",proof.phase_fuel,"/",proof.phase_end_fuel," catalogue=",proof.groups_visited,"/",proof.groups_skipped,"/",proof.members_copied," slots=",proof.spent_before,"/",proof.spent_after," child=",int(proof.child_completed),"/",int(proof.child_failure),"/",int(proof.child_stage),"/",proof.child_fuel_before,"/",proof.child_fuel_after," ack=",samp.ack_for_round,"/",samp.ack_checked_round,"/",samp.ack,"/",std::int64_t(std::llround(samp.ack_amount*1000.))," gate=",int(samp.runtime_gate),"/",samp.gate_fuel,"/",samp.execute_floor,"/",samp.execute_before,"/",samp.execute_after," status=",int(samp.completed),"/",int(samp.pending),"/",int(samp.escaping),"/",int(samp.returning));
  partial_report_round_=round;partial_report_failure_=int(proof.failure);partial_report_stage_=int(proof.stage);partial_report_skip_=int(samp.skip);partial_report_gate_=int(samp.runtime_gate);partial_report_ack_=samp.ack;partial_report_pending_=false;
 }}
 void record_action_cost(Task task,int round,int before){if(current(round)){const int after=int(cambc::fuel_remaining()),cost=before-after;
  if(cost>max_action_cost_){max_action_cost_=cost;max_action_before_=before;max_action_after_=after;max_action_task_=task;max_action_round_=round;}
 }}
 struct ActionMeter {Runtime&owner;Task task;int round,before;~ActionMeter(){owner.record_action_cost(task,round,before);}};
 void profile(int round,const char*phase,int before,bool complete){
  if(critical_turn_)return;
  const int slot=phase[0]=='p'?0:phase[0]=='f'?1:phase[0]=='o'?2:3;
  if(round<=3||round-profile_rounds_[slot]>=64){
   trace("p10 phase actor=",handle(cambc::self())," round=",round," phase=",phase," fuel=",entry_,"/",before,"/",cambc::fuel_remaining()," complete=",int(complete)," groups=",nav_.sources.size());
   profile_rounds_[slot]=round;
  }
 }
 void note(Receipt r){last_=r;economic_.note(r);receipts_.push_back(r);if(receipts_.size()>8)receipts_.erase(receipts_.begin());
  if(r.kind==Receipt::Kind::payout||r.kind==Receipt::Kind::bank||r.kind==Receipt::Kind::death||r.kind==Receipt::Kind::purchase||r.kind==Receipt::Kind::blocked)
   trace("p10 witnessed actor=",handle(cambc::self())," round=",r.round," kind=",int(r.kind)," group=",r.group," gemsLower=",r.gems," fuel=",cambc::fuel_remaining());}
 bool current(int r)const{return same_round(r);}
 void veto_pulse(const Group&g,int round){
  if(std::none_of(pulse_vetoes_.begin(),pulse_vetoes_.end(),[&](auto v){return v.group==g.id;})){
   pulse_vetoes_.push_back({g.id,g.hp});trace("p10 invalidate pulse r=",round," group=",g.id," reason=no-survivable-retained-program");
  }
 }
 std::optional<cambc::champion> actor(int id)const{if(id<0)return std::nullopt;auto c=cambc::detail::handle_access::make_champion(static_cast<cambc::handle_champion_t>(std::uint32_t(id)));if(c.is_visible()&&c.is_alive())return c;return std::nullopt;}
 std::optional<cambc::camp> monster(int id)const{for(const auto&s:nav_.sources)for(const auto&c:s.group)if(handle(c)==id&&c.is_visible()&&c.is_alive())return c;return std::nullopt;}
 void configure(){
  if(configured_)return;
  if constexpr(requires(Policy p,const std::vector<Champion>&roster){p.fastrestore(roster);}){
   if(!cambc::is_draft()){
    std::vector<Champion> roster;for(const auto pick:cambc::draft::current_draft(cambc::team::ally)){
     for(int i=0;i<int(Champion::other);++i){const auto candidate=static_cast<Champion>(i);if(candidate!=Champion::issac&&draft_type(candidate)==pick){roster.push_back(candidate);break;}}
    }
    profile_=policy_.fastrestore(roster);configured_=true;return;
   }
  }
  if constexpr(requires(Policy p,const DraftContext&d){p.draft(d,Arm::active);}){auto d=nav_.draft_context();profile_=policy_.draft(d,arm_);}
  configured_=true;
 }
 World& observe(int round){
  auto&w=frame_;w.allies.clear();w.enemies.clear();w.altars.clear();w.visible_open_tiles.clear();
  w.capture_active=w.own_payout=w.own_bank_completed=w.own_purchase=false;w.productive_group=-1;w.observation_complete=true;w.actor_observation_complete=false;hold_unobserved_=false;
  w.round=round;w.picks=int(cambc::draft::team_size());w.entry_fuel=entry_;w.action_reserve=reserve_;w.fuel=int(cambc::fuel_remaining());
  for(std::size_t i=0;i<w.groups.size();++i){auto&g=w.groups[i];const auto&base=nav_.sources[i].metadata;
   g.observed=g.complete=g.living=g.cleared=g.respawning=g.contested=g.occupied=g.route_known=g.safe_solo=false;
   g.program_available=base.program_available;g.travel=g.clear_turns=-1;g.respawn_remaining=g.hold_since=-1;g.own_holders=0;g.lowest_holder=-1;g.last_payout=base.last_payout;g.hp=g.incoming=0;
   constexpr bool envelope=[]{if constexpr(requires{Policy::observe_hazard_envelopes;})return bool(Policy::observe_hazard_envelopes);else return false;}();
   if constexpr(envelope){g.hazard_envelope=base.hazard_envelope;g.hazard_envelope.round=round;}
   for(std::size_t m=0;m<g.monsters.size();++m)g.monsters[m]=base.monsters[m];
  }
  w.max_rounds=int(cambc::constants::max_rounds);w.win_gems=int(cambc::constants::winning_gems);w.capture_turns=cambc::constants::capture_time;
  w.recall_turns=cambc::constants::recall_time;w.respawn_turns=cambc::constants::respawn_time;w.lock_hits=int(cambc::constants::altar_lock_hits);
  w.tribute_multiplier=cambc::constants::submit_gem_duration_multiplier;w.tribute_base=cambc::constants::submit_gem_base_duration;w.tribute_minimum=cambc::constants::submit_gem_minimum_duration;
  w.spawn_healing=cambc::constants::spawn_healing;
  w.shard_cost=int(cambc::item_type::monster_shard_of_strength.cost());w.lock_cost=int(cambc::consumable_type::altar_lock.cost());
  w.own_is_team_b=cambc::detail::sdk_my_team()==1;w.our_team_starts=team_starts(w.own_is_team_b,round);
  w.score=cambc::global_submitted_gems(cambc::team::ally);w.enemy_score=cambc::global_submitted_gems(cambc::team::enemy);
  const auto cost_start=cambc::fuel_remaining();
  w.self=actor_view(std::uint32_t(handle(cambc::self())),profile_,true,round);w.own_gold=int(cambc::self().gold_count());w.has_lock=cambc::self().consumables()[cambc::consumable_type::altar_lock].count()>0;
  const auto cost_self=cambc::fuel_remaining();
  if(nav_.last_blocked_round>last_block_seen_){last_block_seen_=nav_.last_blocked_round;note({Receipt::Kind::blocked,last_block_seen_,-1,w.self.id});}
  if(bank_pending_&&w.self.alive&&w.self.bag==0&&w.self.channel!=Channel::tribute&&previous_bag_>0&&w.score>=bank_score_+previous_bag_){bank_round_=round;bank_pending_=false;note({Receipt::Kind::bank,round,-1,w.self.id,0,previous_bag_});}
  if(previous_alive_&&!w.self.alive){note({Receipt::Kind::death,round,-1,w.self.id});bank_pending_=false;hold_=-1;}
  stage_="observe-actors";for(auto id:public_actor_ids())if(int(id)!=w.self.id){
   bool full_ally_details=true;if constexpr(requires{Policy::observe_ally_details;})full_ally_details=Policy::observe_ally_details;
   std::uint64_t ally_detail_types=0;if constexpr(requires{Policy::observe_ally_detail_types;})ally_detail_types=Policy::observe_ally_detail_types;
   // Current P10 consumers need full enemy combat facts but no enemy items.
   // A future equipment consumer can explicitly opt back into the full query.
   bool enemy_equipment=false;if constexpr(requires{Policy::observe_enemy_equipment;})enemy_equipment=Policy::observe_enemy_equipment;
   auto a=actor_view(id,profile_,false,round,full_ally_details,ally_detail_types,enemy_equipment);if(!a.visible)continue;if(a.ally)w.allies.push_back(a);else w.enemies.push_back(a);
   if(!current(round))return w;
  }
  w.opposition_complete=w.enemies.size()==public_actor_ids(cambc::team::enemy).size();
  w.actor_observation_complete=current(round);
  const auto cost_others=cambc::fuel_remaining();
  if(!nav_.own_spawn_pads.empty())w.spawn=nav_.own_spawn_pads.front();w.to_spawn=nav_.origin_round==round?nav_.origin_minimum(nav_.own_spawn_pads):-1;
  for(int i=0;i<int(nav_.banks.size());++i){const auto&b=nav_.banks[i];Altar a;a.id=i;a.position=point(b.position);a.visible=nav_.visible_tile(a.position);a.travel=nav_.origin_round==round?nav_.origin_minimum(b.pads):-1;a.route_known=a.travel>=0;
   if(!b.pads.empty())a.approach=b.pads.front();
   if(a.visible&&cambc::vision::has_altar_lock(b.position)){auto lock=cambc::vision::altar_lock_at(b.position);a.enemy_lock=lock.is_enemy();a.friendly_lock=lock.is_ally();a.lock_hits=int(lock.hits_remaining());}
   w.altars.push_back(a);
  }
  const auto cost_altars=cambc::fuel_remaining();
  w.geometry_complete=nav_.complete;
  bool observe_open_tiles=false;if constexpr(requires{Policy::observe_enemy_exits;})observe_open_tiles=Policy::observe_enemy_exits&&!w.enemies.empty();
  if constexpr(requires{Policy::observe_open_tiles;})observe_open_tiles|=Policy::observe_open_tiles;
  bool observe_open_rays=false;if constexpr(requires{Policy::observe_open_rays;})observe_open_rays=Policy::observe_open_rays;
  if(observe_open_tiles||observe_open_rays){
   // prepare exported packed current visibility before this action-free frame.
   // A stale/unavailable export must use a fresh SDK mask, never its old bytes.
   const auto&layout=cambc::detail::layout();const int width=layout.width,height=layout.height;
   const bool prepared_visible=nav_.origin_round==round&&w.actor_observation_complete&&nav_.visible_values.size()==cambc::bitboard::linear_buffer_size();
   std::optional<cambc::bitboard>visible,camps;
   if(!prepared_visible)visible.emplace(cambc::vision::visible_tiles());
   if(observe_open_rays)camps.emplace(cambc::vision::camp_tiles());
   const auto teleports=cambc::vision::teleport_tiles();
   const auto&walls=observe_open_rays?nav_.terrain:nav_.known_blocked;
   // The pinned SDK exposes engine-chunked uint64 storage and its padded
   // chunk stride. Read only valid candidate tiles, not a whole-board union.
   const auto board_words=layout.board_bytes/sizeof(std::uint64_t);
   const auto*wall_bits=walls.ptr();const auto*teleport_bits=teleports.ptr();
   const auto*camp_bits=observe_open_rays?camps->ptr():nullptr;
   const auto*visible_bits=prepared_visible?nullptr:visible->ptr();
   auto free=[&](Point p){
    if(p.x<0||p.y<0||p.x>=width||p.y>=height)return false;
    const auto chunk=std::size_t(p.y/8)*layout.chunk_stride+std::size_t(p.x/8);
    if(chunk>=board_words)throw std::runtime_error("invalid observation mask layout");
    const auto bit=std::uint64_t{1}<<((p.y%8)*8+p.x%8);
    const auto n=std::size_t(p.y)*width+p.x;
    const bool seen=prepared_visible?((nav_.visible_values[n/8]>>(n%8))&1u)!=0:(visible_bits[chunk]&bit)!=0;
    if(!seen)return false;
    const auto occupied=wall_bits[chunk]|teleport_bits[chunk]|(camp_bits?camp_bits[chunk]:std::uint64_t{0});
    return (occupied&bit)==0;
   };
   auto add=[&](Point p){if(w.visible_open_tiles.size()<64&&std::find(w.visible_open_tiles.begin(),w.visible_open_tiles.end(),p)==w.visible_open_tiles.end())w.visible_open_tiles.push_back(p);};
   auto add_exits=[&](Point p){for(int y=std::max(0,p.y-1);y<=std::min(height-1,p.y+1);++y)for(int x=std::max(0,p.x-1);x<=std::min(width-1,p.x+1);++x)if(free({x,y}))add({x,y});};
   if(observe_open_tiles){add_exits(w.self.position);for(const auto&e:w.enemies)if(e.alive)add_exits(e.position);}
   if(observe_open_rays&&w.self.position.x>=0&&w.self.position.y>=0&&w.self.position.x<width&&w.self.position.y<height){
    if(free(w.self.position))add(w.self.position);
    for(Point d:std::array<Point,4>{{{1,0},{-1,0},{0,1},{0,-1}}})for(int step=1;step<=4;++step){Point p{w.self.position.x+d.x*step,w.self.position.y+d.y*step};if(!free(p))break;add(p);}
   }
   std::erase_if(w.visible_open_tiles,[&](Point p){return p10_mobile::occupied(w,p);});
  }
  const auto cost_open=cambc::fuel_remaining();
  // Route metadata is cheap and complete even when live observation is partial.
  // Custody, nearby hazards and the previous policy's source get fresh reads
  // before distant catalogue entries. Only source identity survives this turn.
  const int route_width=cambc::map::width(),route_height=cambc::map::height();
  for(std::size_t group_index=0;group_index<nav_.sources.size();++group_index){auto&s=nav_.sources[group_index];auto&g=w.groups[group_index];g.observed=nav_.visible_source(s);
   g.travel=-1;
   // One witnessed reachable approach gives a conservative route-length bound.
   // Exact minimum over every pad is unnecessary for legality and can consume
   // the whole owner turn before the live-observation guard is reached.
   if(nav_.origin_round==round)for(auto pad:g.approach_pads){int steps=linear_distance(nav_.origin_values,route_width,route_height,pad);if(steps>=0){g.travel=steps;g.approach=pad;break;}}
   g.route_known=g.travel>=0;
  }
  const auto cost_routes=cambc::fuel_remaining();
  int observation_headroom=6000;if constexpr(requires{Policy::observation_headroom;})observation_headroom=std::clamp(int(Policy::observation_headroom),6000,30000);
  const auto live_order=observation_order(w,hold_,observation_intent_,true);const auto cost_order=cambc::fuel_remaining();
  stage_="observe-groups";for(auto group_index:live_order){auto&s=nav_.sources[group_index];auto&g=w.groups[group_index];
   // Unseen members remain explicit unknown hazards in the complete catalogue.
   // Static route work cannot invalidate a fully collected local observation.
   if(!g.observed)continue;
   const auto live_fuel=cambc::fuel_remaining();
   if(live_fuel<std::uint64_t(reserve_+6000)){w.observation_complete=false;break;}
   if(observation_headroom>6000&&live_fuel<std::uint64_t(reserve_+observation_headroom)&&!observation_required(w,g,hold_,observation_intent_)){w.observation_complete=false;continue;}
   if(g.observed&&cambc::fuel_remaining()>std::uint64_t(reserve_+6000)){
    // The current owner-visible spawn mask above supplies the same legality
    // boundary as camp_group::state, without allocating/querying it a second time.
    std::uint32_t state=s.state;if(s.state_round!=round)cambc::detail::check(cambc::detail::sdk_camp_group_state(std::uint32_t(g.id),&state));
    g.living=state==1;g.cleared=state==2;g.respawning=state==3;
    if(state<1||state>3)throw std::runtime_error("invalid visible group lifecycle");
    if(g.respawning)cambc::detail::check(cambc::detail::sdk_camp_group_cooldown(std::uint32_t(g.id),&g.respawn_remaining));
    g.complete=true;g.hp=0;g.incoming=0;
    for(std::size_t i=0;i<g.monsters.size();++i){auto&m=g.monsters[i];
     if(cambc::fuel_remaining()<std::uint64_t(reserve_+5000)){g.complete=false;g.program_available=false;w.observation_complete=false;break;}
     m.observed=nav_.visible_tile(m.position);if(g.cleared||g.respawning){m.alive=false;m.hp=0;continue;}
     if(m.observed){cambc::detail::camp_state raw{};
      if(fresh_camp_snapshot(m,raw,int(cambc::error_code::api_not_in_vision),
          [](auto id,auto*out){return cambc::detail::sdk_camp_state(id,out);},
          [](auto id,auto*out){return cambc::detail::sdk_camp(id,out);},[](int code){cambc::detail::check(code);})&&m.alive){
       std::array<cambc::detail::camp_buff_data,static_cast<std::size_t>(cambc::buff_types::count)> buffs{};std::uint32_t count=0;
       cambc::detail::check(cambc::detail::sdk_camp_buffs(std::uint32_t(m.id),buffs.data(),std::uint32_t(buffs.size()),&count));
       if(count>buffs.size())throw std::runtime_error("invalid engine camp buff count");
       for(std::uint32_t j=0;j<count;++j){if(buffs[j].type>=std::uint32_t(cambc::buff_types::count))throw std::runtime_error("unknown camp buff");if(buffs[j].type==std::uint32_t(cambc::buff_types::stun))m.stun=buffs[j].timer;}
      }
     }
     if(!m.observed)g.complete=false;
     if(m.alive){g.hp+=m.hp;g.incoming+=m.strength;}
    }
    auto occupant=[&](const Actor&a){return a.visible&&a.alive&&std::find(g.spawns.begin(),g.spawns.end(),a.position)!=g.spawns.end();};
    if(occupant(w.self)){++g.own_holders;g.lowest_holder=w.self.id;g.occupied=true;}
    for(const auto&a:w.allies)if(occupant(a)){++g.own_holders;g.occupied=true;g.lowest_holder=g.lowest_holder<0?a.id:std::min(g.lowest_holder,a.id);}
    for(const auto&e:w.enemies)if(occupant(e))g.contested=true;
   }
   g.clear_turns=0;bool all_reachable=true;
   const double damage=w.self.strength*(w.self.monster_emblem?1.1:1.);
   for(const auto&m:g.monsters)if(m.alive&&m.hp>0){g.clear_turns+=damage>0?int(std::ceil(m.hp/damage)):w.max_rounds;all_reachable&=distance(g.approach,m.position)<=w.self.range;}
   // Algebraic ordinary-only certificate: sequential basics, individually
   // ranged incoming, no promised helpers or free spells. It is not a freshly
   // instantiated trajectory and leaves both live script slots to the card.
   g.safe_solo=g.cleared||(g.complete&&g.program_available&&all_reachable&&
    w.self.hp>phase_damage(g,g.approach)*(w.self.jungle_emblem?.7:1.)*(g.clear_turns+2)+1);
   if(!current(round))return w;
  }
  // Position continuity is fresh even when this turn could not hydrate the
  // held group. Preserve the pending ownership claim without inventing clear
  // state, countdown progress or a payout; only a fresh lifecycle confirms it.
  if(hold_>=0)if(auto*s=nav_.source(hold_)){auto*g=const_cast<Group*>(w.group(hold_));
   const int passive=int(cambc::constants::passive_gold)*std::max(0,round-hold_snapshot_round_);
   const auto status=observe_hold(w,*g,hold_last_,hold_snapshot_round_,hold_bag_,hold_gold_,int(cambc::constants::passive_gold));
   if(status==HoldObservation::payout){payout_round_=round;payout_group_=g->id;s->metadata.last_payout=round;note({Receipt::Kind::payout,round,g->id,w.self.id,0,std::max<Gems>(0,w.self.bag-hold_bag_),std::max(0,w.own_gold-hold_gold_-passive)});hold_=-1;}
   else if(status==HoldObservation::lost){trace("p10 invalidate capture actor=",w.self.id," r=",round," group=",hold_);hold_=-1;}
   else {hold_last_=round;hold_unobserved_=status==HoldObservation::unknown;if(status==HoldObservation::capturing){g->hold_since=hold_since_;w.capture_active=true;w.productive_group=g->id;}}
  }
  if(!critical_turn_&&!w.observation_complete&&round-observe_cost_round_>=64){
   trace("p10 observe-cost actor=",w.self.id," round=",round," fuel=",cost_start,"/",cost_self,"/",cost_others,"/",cost_altars,"/",cost_open,"/",cost_routes,"/",cost_order,"/",cambc::fuel_remaining()," processed=",w.groups.size());observe_cost_round_=round;
  }
  double incoming=possible_phase_damage(w,w.self.position);
  for(const auto&e:w.enemies)if(e.alive&&distance(e.position,w.self.position)<=e.range+1)incoming+=e.strength;
  w.safe_now=w.self.alive&&w.self.hp>incoming*2+1;w.own_payout=economic_.has(Receipt::Kind::payout);w.last_payout_group=payout_group_;w.last_payout_round=payout_round_;w.last_bank_round=bank_round_;w.own_bank_completed=economic_.has(Receipt::Kind::bank);
  w.last=last_;w.receipts=receipts_;economic_.append(w.receipts);w.own_purchase=economic_.has(Receipt::Kind::purchase);w.fuel=int(cambc::fuel_remaining());
#if P10_RUNTIME_RECEIPT_ENABLE
  runtime_receipt_.observe_fuel(w.fuel);
#endif
  return w;
 }
 bool shop(Item item,int count,int round){
  auto&me=cambc::self();if(item==Item::none||!current(round)||channel_active())return false;
  if(item==Item::lock){if(me.can_buy(cambc::consumable_type::altar_lock)&&current(round)){me.buy(cambc::consumable_type::altar_lock);note({Receipt::Kind::purchase,round,-1,handle(me),int(item)});return true;}return false;}
  const auto type=item_type(item);bool bought=false;
  for(int i=0;i<std::min(5,std::max(1,count))&&cambc::fuel_remaining()>12000;++i){
   if(!guarded_action(round,[]{return int(cambc::round_number());},[&]{return me.can_buy(type);},[&]{me.buy(type);}))break;
   bought=true;note({Receipt::Kind::purchase,round,-1,handle(me),int(item)});
  }return bought;
 }
 void recover(const World&w){
  stage_="recover";stage_fuel_=int(cambc::fuel_remaining());
  ActionMeter meter{*this,Task::recover,w.round,int(cambc::fuel_remaining())};
  auto&me=cambc::self();recovering_=true;hold_=-1;
  if(!w.self.alive)return;
  const auto state=me.state();if(!state.contains(cambc::champion_state::can_move)&&!state.contains(cambc::champion_state::can_attack)&&!state.contains(cambc::champion_state::can_cast))return;
  if(w.self.at_spawn){if(w.self.hp>=w.self.max_hp*.99){recovering_=false;pulse_recover_=false;}return;}
  if(cambc::fuel_remaining()<std::uint64_t(reserve_))return;
  auto goals=cambc::map::spawn_region(cambc::team::ally);
  bool camp_threat=possible_phase_damage(w,w.self.position)>0;for(const auto&g:w.groups)for(const auto&m:g.monsters){if(cambc::fuel_remaining()<22000)return;if(m.alive&&g.id==pulse_group_&&distance(m.position,w.self.position)<=m.range)camp_threat=true;}
  if(!camp_threat&&w.safe_now&&current(w.round)){me.recall();return;}
  nav_.walk(goals,w.round);
 }
 bool safe_hitset(const World&w,Point center,double radius,int source,Point exposure={})const{
  if(exposure.x<0)exposure=point(cambc::self().position());
  return collateral_survives(w,center,radius,source,exposure,cambc::self().health());
 }
 bool attack_target(const World&w,const cambc::camp&t,const Directive&d,int round,bool walked){
  stage_="basic-hit";stage_fuel_=int(cambc::fuel_remaining());
  if(cambc::fuel_remaining()<14000)return false;
  auto&me=cambc::self();if(!current(round)||!t.is_visible()||!t.is_alive()||channel_active())return false;
  if(w.self.type==Champion::varg&&!safe_hitset(w,point(t.position()),1,d.group))return false;
  if(me.can_attack()&&me.attack_tiles().is_set(t.position())&&current(round)){
   const double before=t.health();
   if(!guarded_action(round,[]{return int(cambc::round_number());},[&]{return me.can_attack()&&t.is_visible()&&t.is_alive()&&me.attack_tiles().is_set(t.position());},[&]{me.attack(t);}))return false;
   if(!current(round))return false;note({Receipt::Kind::attack,round,d.group,handle(me),0,0,0,before-(t.is_visible()&&t.is_alive()?t.health():0)});
  }
  if(!current(round)||d.reserve_reset||cambc::fuel_remaining()<20000)return true;
  if(me.can_cast())for(int i=0;i<2;++i){const auto a=me.abilities()[i];if(a.is_attack_reset()&&a.cooldown()==0&&cast_self(i,round,walked)){
   std::optional<cambc::camp> next;if(t.is_visible()&&t.is_alive())next=t;
   else if(auto*s=nav_.source(d.group))for(const auto&c:s->group)if(c.is_visible()&&c.is_alive()&&me.attack_tiles().is_set(c.position())){next=c;break;}
   if(next&&me.can_attack()&&me.attack_tiles().is_set(next->position())&&(w.self.type!=Champion::varg||safe_hitset(w,point(next->position()),1,d.group))&&current(round)){double before=next->health();if(!current(round))return true;me.attack(*next);note({Receipt::Kind::attack,round,d.group,handle(me),0,0,0,before-(next->is_visible()&&next->is_alive()?next->health():0)});}break;
  }}
  return true;
 }
 bool dash_to(const World&w,Point destination,int slot,int round,bool walked){
  auto&me=cambc::self();if(destination.x<0||slot<0||slot>1||!current(round)||!me.can_cast())return false;
  auto a=me.abilities()[slot];if(!a.is_dash()||a.cooldown()!=0||!compatible(a,walked))return false;
  const auto info=ability_view(a,true);auto walls=nav_.blocked();
  auto possible=a.valid_tiles()&walls.reachable_tiles(me.position(),std::uint16_t(std::floor(info.range)))&~walls&cambc::vision::visible_tiles();auto candidates=possible.positions();
  std::sort(candidates.begin(),candidates.end(),[&](auto x,auto y){return distance(point(x),destination)<distance(point(y),destination);});
  for(auto p:candidates)if(distance(point(p),destination)<distance(point(me.position()),destination)&&
   (info.modifier<=0||safe_hitset(w,point(p),info.radius,-1,point(p)))&&current(round))
    return guarded_action(round,[]{return int(cambc::round_number());},[&]{return me.can_cast()&&a.cooldown()==0;},[&]{return a.use(p);});
  return false;
 }
 bool cast_directive(const World&w,const Directive&d,bool walked=false){
  stage_="cast-directive";stage_fuel_=int(cambc::fuel_remaining());
  if(cambc::fuel_remaining()<26000)return false;
  auto&me=cambc::self();if(d.cast_slot<0||d.cast_slot>1||!current(w.round))return false;auto a=me.abilities()[d.cast_slot];
  if(a.is_dash())return dash_to(w,d.destination,d.cast_slot,w.round,walked);
  if(a.is_attack_reset()||a.is_shield()){const bool done=cast_self(d.cast_slot,w.round,walked);if(done&&current(w.round))note({Receipt::Kind::cast,w.round,d.group,handle(me)});return done;}
  if(d.actor>=0){if(auto target=actor(d.actor)){double before=target->health();auto info=ability_view(a,true);
   if((a.is_heal()||safe_hitset(w,point(target->position()),info.radius,d.group))&&cast_on(*target,d.cast_slot,w.round,walked)){if(current(w.round))note({a.is_heal()?Receipt::Kind::heal:Receipt::Kind::cast,w.round,d.group,d.actor,0,0,0,a.is_heal()?target->health()-before:0});return true;}}return false;}
  if(d.destination.x>=0&&a.can_target_ground()&&a.cooldown()==0&&me.can_cast()&&compatible(a,walked)&&a.valid_tiles().is_set(point(d.destination))&&cambc::vision::in_vision(point(d.destination))&&current(w.round)){
   const auto info=ability_view(a,true);if(!safe_hitset(w,d.destination,info.radius,d.group))return false;
   if(guarded_action(w.round,[]{return int(cambc::round_number());},[&]{return me.can_cast()&&a.cooldown()==0;},[&]{return a.use(point(d.destination));})){if(current(w.round))note({Receipt::Kind::cast,w.round,d.group,handle(me)});return true;}return false;
  }
  if(d.monster>=0){if(auto t=monster(d.monster)){auto info=ability_view(a,true);if(safe_hitset(w,point(t->position()),info.radius,d.group)&&cast_on(*t,d.cast_slot,w.round,walked)){if(current(w.round))note({Receipt::Kind::cast,w.round,d.group,handle(me)});return true;}}return false;}
  return false;
 }
 void bank_now(const World&w,const Directive&d){
  if(w.self.bag<=0){
#if P10_RUNTIME_RECEIPT_ENABLE
   runtime_receipt_.set(RR::bank_status,1);
#endif
   return;
  }const Altar*best=nullptr;
  for(const auto&a:w.altars)if(a.route_known&&(d.altar<0||a.id==d.altar)&&(!best||(!a.enemy_lock&&best->enemy_lock)||(a.enemy_lock==best->enemy_lock&&a.travel<best->travel)))best=&a;
#if P10_RUNTIME_RECEIPT_ENABLE
  runtime_receipt_.bank(best,d.altar);
#endif
  if(!best)return;auto&me=cambc::self();
#if P10_RUNTIME_RECEIPT_ENABLE
  const int bank_distance=distance(w.self.position,best->position),submit_range=cambc::constants::submit_gems_range;
  runtime_receipt_.set(RR::bank_distance,bank_distance);runtime_receipt_.set(RR::submit_range,submit_range);
  if(bank_distance<=submit_range){
#else
  if(distance(w.self.position,best->position)<=cambc::constants::submit_gems_range){
#endif
   if(best->visible&&best->enemy_lock){auto lock=cambc::vision::altar_lock_at(point(best->position));
#if P10_RUNTIME_RECEIPT_ENABLE
    runtime_receipt_.set(RR::bank_branch,1);
#endif
    guarded_action(w.round,[]{return int(cambc::round_number());},[&]{return me.can_attack()&&me.attack_tiles().is_set(lock.position());},[&]{me.attack(lock);});return;}
   if(best->visible&&current(w.round)){
#if P10_RUNTIME_RECEIPT_ENABLE
    runtime_receipt_.set(RR::bank_branch,2);
#endif
    me.submit_gems(point(best->position));
    if(current(w.round)&&me.is_submitting_gems()){
#if P10_RUNTIME_RECEIPT_ENABLE
     runtime_receipt_.set(RR::bank_branch,5);
#endif
     bank_pending_=true;bank_score_=w.score;
     trace("p10 prediction actor=",w.self.id," round=",w.round," source=-1 predictionEvent=bank predicted=[",w.round+w.ticks(w.self.bag),",",w.round+w.ticks(w.self.bag),"] condition=safe-channel startBag=",w.self.bag," altar=",best->id);
    }
   }
#if P10_RUNTIME_RECEIPT_ENABLE
   else runtime_receipt_.set(RR::bank_branch,best->visible?4:3);
#endif
  }else {
#if P10_RUNTIME_RECEIPT_ENABLE
   runtime_receipt_.set(RR::bank_branch,6);
#endif
   nav_.walk(nav_.banks[best->id].goals,w.round);
  }
 }
 void capture(const World&w,int group){
  ActionMeter meter{*this,Task::capture,w.round,int(cambc::fuel_remaining())};
  auto*s=nav_.source(group);const auto*g=w.group(group);if(!s||!g||!g->observed||!g->cleared||g->contested)return;
  if(!s->spawns.is_set(point(w.self.position))){
   if(!nav_.walk(s->spawns,w.round)||!current(w.round)||!s->spawns.is_set(cambc::self().position()))return;
  }
  if(hold_!=group){hold_=group;hold_since_=w.round;
   // The API exposes no timer. A joining holder may inherit progress from an
   // ally, including a handoff earlier this round. Own entry bounds the latest
   // payout but cannot establish that this is the first capture phase.
   trace("p10 prediction actor=",w.self.id," round=",w.round," source=",group," predictionEvent=payout predicted=[",w.round+1,",",w.round+w.capture_turns+1,"] condition=continuous-uncontested-progress-unknown");
  }hold_last_=hold_snapshot_round_=w.round;hold_bag_=w.self.bag;hold_gold_=w.own_gold;
 }
 void farm_group(const World&w,Directive d){
  stage_="farm-select";stage_fuel_=int(cambc::fuel_remaining());
  auto*s=nav_.source(d.group);const auto*g=w.group(d.group);if(!s||!g||!g->program_available)return;
  if(!g->observed||!g->complete){nav_.walk(s->approaches,w.round);return;}
  if(g->cleared){capture(w,g->id);return;}if(g->respawning)return;
  auto&me=cambc::self();std::optional<cambc::camp> target;
  // No action occurs between observation and this selection. Rank its fresh
  // complete records in guest memory; refresh the chosen live target after any
  // movement and again at the final action boundary below.
  const Monster*selected=nullptr;
  for(const auto&m:g->monsters)if(m.observed&&m.alive&&m.hp>0){if(m.id==d.monster){selected=&m;break;}if(!selected||m.hp<selected->hp||(m.hp==selected->hp&&m.id<selected->id))selected=&m;}
  if(selected)for(const auto&c:s->group)if(handle(c)==selected->id){target=c;break;}
  if(!target)return;
  stage_="farm-move";stage_fuel_=int(cambc::fuel_remaining());if(!me.attack_tiles().is_set(point(selected->position))){
   if(d.strict_tank)return;
   if(d.destination.x>=0)nav_.walk(around(point(d.destination),0),w.round);
   else nav_.walk(around(target->position(),me.attack_range())&~nav_.blocked(),w.round);
  }
  if(cambc::fuel_remaining()<14000||!current(w.round)||!target->is_visible()||!target->is_alive())return;const bool walked=point(me.position())!=w.self.position;
  if(d.pulse){
   if(cambc::fuel_remaining()<20000)return;
   stage_="pulse-proof";stage_fuel_=int(cambc::fuel_remaining());
   // An approach turn cannot spend the departure-proof budget for a shot it
   // cannot make. In particular ranged movement consumes the attack allowance.
   if(!me.can_attack()||!me.attack_tiles().is_set(target->position())||!current(w.round))return;
   const Point target_position=point(target->position());
   // Include the attack point's next camp phase, and two departure phases,
   // independently for every member. A ranged shot cannot also walk out.
   auto pulse_phase=[&](Point position){return pulse_phase_damage(w,g->id,position);};
   // Varg's pulse may wake diagonally adjacent groups. Keep persistent pulses
   // single-group when that collateral cannot fit the retained-work program.
   if(w.self.type==Champion::varg){for(const auto&other:w.groups)if(other.id!=g->id&&!other.cleared&&!other.respawning)for(const auto&m:other.monsters)if(m.alive&&distance(m.position,target_position)<=1){veto_pulse(*g,w.round);return;}}
   double damage=pulse_phase(point(me.position()));
   // Construct a finite departure to a real spawn route. Price the phase
   // before the next owner turn and every in-range phase on that departure.
   const auto&values=nav_.spawn_values;const int width=cambc::map::width(),height=cambc::map::height();
   auto steps=[&](cambc::vec2 p){return linear_distance(values,width,height,point(p));};
   auto pos=me.position();bool escaped=damage==0;double exposure=damage;
   if(!escaped){
    // Conservative immutable terrain route, with freshly legal successors.
    // Newly cleared sources are never assumed passable to make this proof fit.
    auto walls=nav_.blocked();
    for(int step=0;step<12&&!escaped&&cambc::fuel_remaining()>18000;++step){const int before=steps(pos);if(before<=0)break;auto choices=walls.reachable_tiles(pos,1).positions();
     auto next=pos;int best=before;for(auto p:choices){int n=steps(p);if(n>=0&&n<best){best=n;next=p;}}
     if(next==pos)break;pos=next;double phase=pulse_phase(point(pos));exposure+=phase;escaped=phase==0;
    }
   }
   if(!current(w.round))return;
   if(!escaped||exposure+std::max(1.,w.self.max_hp*.1)>=me.health()){veto_pulse(*g,w.round);return;}
   if(!me.can_attack()||!me.attack_tiles().is_set(target->position()))return;
   pulse_group_=g->id;pulse_round_=w.round;pulse_hp_=g->hp;pulse_recover_=true;
   d.reserve_reset=true;attack_target(w,*target,d,w.round,walked);return;
  }
  stage_="farm-cast";stage_fuel_=int(cambc::fuel_remaining());if(d.cast_slot>=0){if(!cast_directive(w,d,walked)&&d.safety_verified)return;}
  else if(!d.reserve_reset&&cambc::fuel_remaining()>26000&&me.can_cast()){
   for(int slot=0;slot<2;++slot){auto a=me.abilities()[slot];auto info=ability_view(a,true);if((a.is_damage()||a.is_cc()||a.is_aoe_damage())&&safe_hitset(w,point(target->position()),info.radius,d.group)&&cast_on(*target,slot,w.round,walked))break;}
  }
  if(current(w.round)&&target->is_visible()&&target->is_alive())attack_target(w,*target,d,w.round,walked);
 }
 Directive fallback(const World&w){
  if(pulse_recover_||recovering_)return {Task::recover};
  // These ordinary/pulse programs have not proved an incomplete local frame.
  if(!w.observation_complete)return wait("ordinary fallback awaits its full observation premise");
  auto d=ordinary(w);if(d.task!=Task::independent)return d;
  const Group*best=nullptr;
  for(const auto&g:w.groups)if(g.route_known&&g.program_available&&!g.respawning&&std::none_of(pulse_vetoes_.begin(),pulse_vetoes_.end(),[&](auto v){return v.group==g.id;})){
   if(g.observed&&g.complete&&g.living&&phase_damage(g,g.approach)*3>=w.self.hp)continue;
   if(!best||g.travel<best->travel||(g.travel==best->travel&&g.id<best->id))best=&g;
  }
  if(best){auto next=farm(best->id,"bounded retained-work cold start");next.pulse=best->observed&&best->complete;next.reserve_reset=false;return next;}
  return wait("opportunity-blocked: no survivable first-income program");
 }
 void execute(const World&w,Directive d){
  auto&me=cambc::self();
#if P10_RUNTIME_RECEIPT_ENABLE
  if(runtime_receipt_.active)++runtime_receipt_.active->f[RR::execute_calls];
  const auto guard_fuel=cambc::fuel_remaining();runtime_receipt_.set(RR::execute_guard_fuel,int(guard_fuel));
  if(guard_fuel<8000){runtime_receipt_.set(RR::execute_guard,2);return;}
  if(!current(w.round)){runtime_receipt_.set(RR::execute_guard,3);return;}
  if(d.valid_round>=0&&d.valid_round!=w.round){runtime_receipt_.set(RR::execute_guard,4);return;}
  runtime_receipt_.set(RR::execute_guard,1);
#else
  if(cambc::fuel_remaining()<8000||!current(w.round)||(d.valid_round>=0&&d.valid_round!=w.round))return;
#endif
  if(d.priority!=0&&d.task!=Task::priority&&current(w.round))me.set_priority(d.priority);
  switch(d.task){
  case Task::independent:d=fallback(w);if(d.task!=Task::independent)execute(w,d);break;
  case Task::farm:farm_group(w,d);break;case Task::capture:capture(w,d.group);break;
  case Task::bank:bank_now(w,d);break;case Task::recover:recover(w);break;
  case Task::inspect:if(auto*s=nav_.source(d.group))nav_.walk(s->approaches,w.round);break;
  case Task::release:{auto goals=around(point(w.self.position),2)&~nav_.blocked();for(const auto&s:nav_.sources)goals&=~s.spawns;nav_.walk(goals,w.round);hold_=-1;break;}
  case Task::move:if(d.destination.x>=0)nav_.walk(around(point(d.destination),0),w.round);break;
  case Task::attack:if(d.monster>=0){if(auto t=monster(d.monster)){
    if(!d.strict_tank&&!me.attack_tiles().is_set(t->position()))nav_.walk(around(t->position(),me.attack_range())&~nav_.blocked(),w.round);
    if(!current(w.round))return;bool walked=point(me.position())!=w.self.position;if(d.cast_slot>=0)d.reserve_reset=true;attack_target(w,*t,d,w.round,walked);
    if(d.cast_slot>=0&&current(w.round)){cast_directive(w,d,walked);if(d.second_monster>=0&&current(w.round))if(auto next=monster(d.second_monster))attack_target(w,*next,d,w.round,walked);}}}
   else if(auto enemy=actor(d.actor);enemy&&enemy->is_enemy()){
    if(!d.strict_tank&&!me.attack_tiles().is_set(enemy->position()))nav_.walk(around(enemy->position(),me.attack_range())&~nav_.blocked(),w.round);
    const bool walked=point(me.position())!=w.self.position;
    const bool reset=d.cast_slot>=0&&d.cast_slot<2&&me.abilities()[d.cast_slot].is_attack_reset();
    if(reset){
     if(d.safety_verified&&!fresh_certificate(w,d))break;
     auto target_legal=[&](){return enemy->is_visible()&&enemy->is_alive()&&me.attack_tiles().is_set(enemy->position())&&
      (w.self.type!=Champion::varg||safe_hitset(w,point(enemy->position()),1,d.group));};
     auto hit=[&](){if(cambc::fuel_remaining()<14000||channel_active())return false;
      return guarded_action(w.round,[]{return int(cambc::round_number());},[&]{return me.can_attack()&&target_legal();},[&]{me.attack(*enemy);});};
     if(me.can_attack()){if(!hit())break;}
     else if(!current(w.round)||!target_legal()||!current(w.round))break;
     if(!current(w.round)||!enemy->is_visible()||!enemy->is_alive())break;
     if(!cast_directive(w,d,walked))break;
     hit();
    }else{
     if(d.cast_slot>=0)cast_directive(w,d,walked);if(me.can_attack()&&enemy->is_visible()&&enemy->is_alive()&&me.attack_tiles().is_set(enemy->position())&&
      (w.self.type!=Champion::varg||safe_hitset(w,point(enemy->position()),1,d.group))&&current(w.round))me.attack(*enemy);
    }
   }break;
  case Task::cast:if(!cast_directive(w,d)&&d.safety_verified)break;if(d.group>=0&&(d.second_monster>=0||d.monster>=0)&&current(w.round))if(auto t=monster(d.second_monster>=0?d.second_monster:d.monster)){d.reserve_reset=true;attack_target(w,*t,d,w.round,false);}break;
  case Task::heal:case Task::shield:{bool cast=false;if(d.cast_slot>=0)cast=cast_directive(w,d);else for(int slot=0;slot<2;++slot){auto a=me.abilities()[slot];if((d.task==Task::heal&&a.is_heal())||(d.task==Task::shield&&a.is_shield())){d.cast_slot=slot;cast=cast_directive(w,d);break;}}
   if(!cast&&d.safety_verified)break;
   if(d.group>=0&&d.monster>=0&&current(w.round))if(auto t=monster(d.monster)){d.reserve_reset=true;attack_target(w,*t,d,w.round,false);}break;
  }
  case Task::buy:if(!shop(d.item,d.buy_count,w.round)){if(!w.self.at_spawn&&w.self.type!=Champion::tavi)recover(w);}break;
  case Task::lock:if(d.altar>=0&&d.altar<int(nav_.banks.size())&&w.has_lock){auto p=nav_.banks[d.altar].position;if(distance(w.self.position,point(p))<=2&&cambc::vision::in_vision(p)&&current(w.round))me.consumables()[cambc::consumable_type::altar_lock].use(p);else nav_.walk(nav_.banks[d.altar].goals,w.round);}break;
  case Task::priority:if(current(w.round))me.set_priority(d.priority);break;
  case Task::wait:break;
  }
 }
public:
 Runtime(const Profile&p,Arm a):profile_(p),arm_(a){set_work_fuel_reader(+[]{return int(cambc::fuel_remaining());});if constexpr(requires(Policy x){x.set_fuel_reader(nullptr);})policy_.set_fuel_reader(+[]{return int(cambc::fuel_remaining());});}
 const char* stage()const{return stage_;}
 void trace_suspension()const{if(last_round_>=0&&!current(last_round_))trace("p10 suspended actor=",handle(cambc::self())," round=",last_round_," stage=",stage_," startFuel=",stage_fuel_," walkPhase=",nav_.walk_phase," walkFuel=",nav_.walk_fuel);}
 void draft(){configure();const auto selected=cambc::draft::current_draft(cambc::team::ally);for(auto c:profile_.draft){auto p=draft_type(c);if(std::find(selected.begin(),selected.end(),p)==selected.end()){cambc::draft::pick(p);return;}}}
 void turn(){
  nav_.partial_walk_frame=nullptr;const int r=int(cambc::round_number());entry_=int(cambc::fuel_remaining());
  // Ordinary floor, not a completed-action bound: C04 measured a42099 farm.
  // Live inner-loop guards remain required; one-basic calibration is separate.
  reserve_=std::max(41000,action_reserve(entry_));
  critical_turn_=false;if constexpr(requires(const Policy&p){p.critical_continuation();})critical_turn_=policy_.critical_continuation();
#if P10_RUNTIME_RECEIPT_ENABLE
  const int receipt_actor=handle(cambc::self());runtime_receipt_.begin(r,receipt_actor,entry_,reserve_);
  // No I/O until the complete collection window is over. This intentionally
  // exposed later owner turn skips gameplay; economic/channel observation is
  // delayed. The ordinary last-round stamp prevents a false suspension gap,
  // but is not a claim of post-emission bookkeeping or policy preservation.
  if(!critical_turn_&&runtime_receipt_.ready(r,receipt_actor,entry_)){
   const int old_last_round=last_round_;last_round_=r;stage_="runtime-receipt-emission";stage_fuel_=entry_;
   runtime_receipt_.emit(r,receipt_actor,entry_,int(bank_pending_),hold_,previous_bag_,old_last_round);return;
  }
#endif
  partial_report(r);
  constexpr bool has_partial_report=requires(const Policy&p){p.partial_snapshot();};
  if(!has_partial_report&&!critical_turn_&&policy_sample_.round>=0&&policy_sample_.round<r&&(r-policy_report_round_>=64||policy_sample_.task!=reported_policy_task_||policy_sample_.group!=reported_policy_group_||policy_sample_.failure!=reported_policy_failure_)){
   const auto&p=policy_sample_;trace("p10 policy-cost actor=",handle(cambc::self())," round=",r," measuredRound=",p.round," task=",p.task," group=",p.group," template=",p.tag," fuel=",p.before,"/",p.after," mobile=",p.calls,"/",p.traces," failure=",p.failure," source=",p.source," feasible=",int(p.feasible));
   policy_report_round_=r;reported_policy_task_=p.task;reported_policy_group_=p.group;reported_policy_failure_=p.failure;
  }
  // Defer reporting a new maximum until the next owner turn has a fresh fuel
  // allowance. Every completed action is measured, including low-tail turns.
  if(!critical_turn_&&max_action_cost_>reported_action_cost_&&entry_>80000){trace("p10 action-cost actor=",handle(cambc::self())," round=",r," measuredRound=",max_action_round_," task=",int(max_action_task_)," cost=",max_action_cost_," fuel=",max_action_before_,"/",max_action_after_);reported_action_cost_=max_action_cost_;}
  stage_="entry";if(last_round_>=0&&r-last_round_>1){hold_=-1;bank_pending_=false;trace("p10 invalidate suspended actor=",handle(cambc::self())," r=",r);}last_round_=r;
  if(channel_active()){P10_RUNTIME_EXIT(pre_channel);return;}
  if(!cambc::self().is_alive()){
   if(previous_alive_)note({Receipt::Kind::death,r,-1,handle(cambc::self())});
   previous_alive_=false;previous_bag_=0;bank_pending_=false;hold_=-1;recovering_=true;P10_RUNTIME_EXIT(pre_dead);return;
  }
  configure();const int before_prepare=int(cambc::fuel_remaining());nav_.prepare(r,reserve_);
  if(r<=3||!nav_.complete||cambc::fuel_remaining()<std::uint64_t(reserve_+8000))profile(r,"prepare",before_prepare,nav_.complete);
  if(!current(r)||!nav_.complete||!nav_.routes_complete){P10_RUNTIME_EXIT(prepare_unavailable);return;}
  if constexpr(requires{Policy::observe_hazard_envelopes;})if constexpr(Policy::observe_hazard_envelopes)if(!nav_.prepare_envelopes(r,reserve_)){P10_RUNTIME_EXIT(envelope_unavailable);return;}
  if(!frame_ready_){
   while(frame_.groups.size()<nav_.sources.size()&&cambc::fuel_remaining()>std::uint64_t(reserve_+20000))frame_.groups.push_back(nav_.sources[frame_.groups.size()].metadata);
   frame_ready_=frame_.groups.size()==nav_.sources.size();profile(r,"frame-warmup",before_prepare,frame_ready_);P10_RUNTIME_EXIT(frame_warmup);return;
  }
  if(cambc::fuel_remaining()<std::uint64_t(reserve_+8000)){if(cambc::self().is_alive()&&!channel_active()){Directive d;d.task=Task::recover;World w;w.round=r;w.self=actor_view(cambc::self(),profile_,true);w.safe_now=false;recover(w);}P10_RUNTIME_EXIT(pre_observe_low);return;}
  const int before_observe=int(cambc::fuel_remaining());auto&w=observe(r);
#if P10_RUNTIME_RECEIPT_ENABLE
  runtime_receipt_.world(w,int(recovering_)|(int(pulse_recover_)<<1)|(int(hold_unobserved_)<<2));
#endif
  if(r<=3||!w.observation_complete)profile(r,"observe",before_observe,w.observation_complete);
  if(!current(r)){P10_RUNTIME_EXIT(observe_round_changed);return;}
  previous_bag_=w.self.bag;previous_alive_=w.self.alive;
  if(channel_active()){P10_RUNTIME_EXIT(post_channel);return;}
  if(!w.self.alive){P10_RUNTIME_EXIT(post_dead);return;}
  if(!w.self.can_move&&!w.self.can_attack&&!w.self.can_cast){P10_RUNTIME_EXIT(no_actions);return;}
  if(!w.actor_observation_complete){P10_RUNTIME_EXIT(actors_incomplete);return;}
  nav_.partial_walk_frame=w.observation_complete?nullptr:&w;
  // Keep a continuously occupied pending capture in place until its current
  // lifecycle can be read. A policy must not inspect away its own unknown hold.
  if(hold_unobserved_){if(!w.safe_now)recover(w);P10_RUNTIME_EXIT(hold_unknown);return;}
  nav_.open_observed_corpses(w);
  // Vetoes are part of this actor's bounded fallback intent. Changed personal
  // capital or a witnessed change to a source invalidates its refusal premise.
  if(w.self.strength!=veto_strength_||w.self.max_hp!=veto_max_hp_||w.self.focus!=veto_focus_){pulse_vetoes_.clear();veto_strength_=w.self.strength;veto_max_hp_=w.self.max_hp;veto_focus_=w.self.focus;}
  std::erase_if(pulse_vetoes_,[&](auto v){const auto*g=w.group(v.group);return g&&g->observed&&g->complete&&g->hp!=v.hp;});
  if(pulse_group_>=0&&w.round>pulse_round_){if(const auto*g=w.group(pulse_group_);g&&g->observed&&g->complete&&!pulse_recover_&&!recovering_){if(g->hp>=pulse_hp_)veto_pulse(*g,w.round);pulse_group_=-1;}}
  const auto*held=w.capture_active?w.group(w.productive_group):nullptr;
  const bool successor_present=held&&held->own_holders>1;
#if P10_RUNTIME_RECEIPT_ENABLE
  runtime_receipt_.set(RR::successor,successor_present);
#endif
  if(w.capture_active&&w.safe_now&&!successor_present){capture(w,w.productive_group);P10_RUNTIME_EXIT(capture_before_policy);return;}
  Directive d;
#if P10_RUNTIME_RECEIPT_ENABLE
  const bool policy_admitted=cambc::fuel_remaining()>std::uint64_t(reserve_);runtime_receipt_.set(RR::policy_admission,policy_admitted?1:2);
  if(policy_admitted){stage_="policy";stage_fuel_=int(cambc::fuel_remaining());runtime_receipt_.set(RR::policy_before,stage_fuel_);runtime_receipt_.bit(RR::policy_before_present);
   d=policy_.step(w,arm_);runtime_receipt_.directive(d,false);
#else
  if(cambc::fuel_remaining()>std::uint64_t(reserve_)){stage_="policy";stage_fuel_=int(cambc::fuel_remaining());d=policy_.step(w,arm_);
#endif
   if(!current(r)){partial_gate(r,7);P10_RUNTIME_EXIT(policy_round_changed);return;}economic_.acknowledge();
   if(d.group>=0)observation_intent_=d.group;
   policy_sample_={r,stage_fuel_,int(cambc::fuel_remaining()),int(d.task),d.group,d.template_id};const auto&m=p10_mobile::call_diagnostics;
#if P10_RUNTIME_RECEIPT_ENABLE
   runtime_receipt_.set(RR::policy_checkpoint,policy_sample_.after);runtime_receipt_.bit(RR::policy_checkpoint_present);
#endif
   if(m.actor==w.self.id&&m.round==r){policy_sample_.calls=m.calls;policy_sample_.traces=m.traces;policy_sample_.failure=int(m.failure);policy_sample_.source=m.source;policy_sample_.feasible=m.feasible;}
  }
  if(!w.safe_now||recovering_||pulse_recover_){if(!fresh_certificate(w,d)){partial_gate(r,1);recover(w);P10_RUNTIME_EXIT(recovery_veto);return;}recovering_=pulse_recover_=false;}
  // A policy that overspent cannot afford an additional fallback plus its
  // action. Yield this owner turn instead of suspending that stack into the next.
  const int execute_reserve=execution_floor(w,d);
#if P10_RUNTIME_RECEIPT_ENABLE
  const auto floor_fuel=cambc::fuel_remaining();runtime_receipt_.set(RR::execute_floor,execute_reserve);runtime_receipt_.set(RR::floor_fuel,int(floor_fuel));
  if(floor_fuel<std::uint64_t(execute_reserve)){partial_gate(r,2,execute_reserve);P10_RUNTIME_EXIT(execution_floor_veto);return;}
#else
  if(cambc::fuel_remaining()<std::uint64_t(execute_reserve)){partial_gate(r,2,execute_reserve);return;}
#endif
  if(d.task==Task::farm)if(const auto*g=w.group(d.group);g&&g->observed&&g->complete&&g->cleared&&!g->contested)d.task=Task::capture;
  if(!w.observation_complete&&(d.task==Task::farm||d.task==Task::attack||d.task==Task::cast||d.task==Task::heal||d.task==Task::shield)&&!fresh_certificate(w,d)){partial_gate(r,3,execute_reserve);P10_RUNTIME_EXIT(observation_veto);return;}
  if(d.task==Task::independent){
   constexpr bool owns_capital=[]{if constexpr(requires{Policy::owns_capital;})return bool(Policy::owns_capital);else return false;}();
   if(!owns_capital&&w.own_gold>=w.shard_cost&&(w.self.at_spawn||w.self.type==Champion::tavi)&&shop(Item::monster_strength,1,r)){P10_RUNTIME_EXIT(fallback_shop);return;}
   d=fallback(w);
  }
  if(w.capture_active&&w.safe_now){
   const bool leaves=d.task==Task::release||d.task==Task::move||d.task==Task::bank||(d.task==Task::farm&&d.group!=w.productive_group);
   if(!successor_present||!leaves){capture(w,w.productive_group);P10_RUNTIME_EXIT(capture_after_policy);return;}
  }
  if(!critical_turn_&&(d.task!=logged_task_||d.group!=logged_group_||d.template_id!=logged_template_)&&cambc::fuel_remaining()>std::uint64_t(execute_reserve+12000)){
   trace("p10 decision actor=",w.self.id," round=",r," source=",d.group," task=",int(d.task)," template=",d.template_id," owner=",w.self.id," predicted=[",d.expected_min,",",d.expected_max,"] reason=",d.reason," fuel=",entry_,"/",cambc::fuel_remaining());logged_task_=d.task;logged_group_=d.group;logged_template_=d.template_id;
  }
  // Logging and an independent fallback also spend fuel. Enforce the action
  // floor again at the actual handoff, after those costs, on this owner turn.
#if P10_RUNTIME_RECEIPT_ENABLE
  runtime_receipt_.directive(d,true);
#endif
  if(!execution_handoff(r,execute_reserve)){P10_RUNTIME_EXIT(handoff_veto);return;}
  stage_="execute";
  const int before_execute=int(cambc::fuel_remaining());
#if P10_RUNTIME_RECEIPT_ENABLE
  runtime_receipt_.set(RR::before_execute,before_execute);runtime_receipt_.bit(RR::before_execute_present);
#endif
  partial_gate(r,5,execute_reserve);execute(w,d);
  if(current(r)){partial_gate(r,6,execute_reserve);record_action_cost(d.task,r,before_execute);
   if(cambc::fuel_remaining()>12000)profile(r,"action",before_execute,true);
#if P10_RUNTIME_RECEIPT_ENABLE
   runtime_receipt_.set(RR::execute_returned,1);runtime_receipt_.bit(RR::execute_return_present);
#endif
   P10_RUNTIME_EXIT(execution_complete);
  }else {partial_gate(r,7,execute_reserve);P10_RUNTIME_EXIT(execution_round_changed);}
 }
};
template<class Policy> int run(const Profile&p,Arm a){Runtime<Policy> bot(p,a);while(true){
 try{if(cambc::is_draft())bot.draft();else {bot.turn();bot.trace_suspension();}}
 catch(const std::exception&e){trace("p10 error [",p.slug,"]: ",e.what()," stage=",bot.stage());}
 catch(...){trace("p10 error [",p.slug,"]: unknown exception");}
 cambc::end_turn();
}}
} // namespace p10
#undef P10_RUNTIME_EXIT
