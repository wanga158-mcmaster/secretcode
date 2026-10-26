#include "joint_world.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <limits>

using namespace mosaic::joint_world;
unsigned checks=0;
void check(bool value,int line){++checks;if(!value){std::fprintf(stderr,"joint-world failed %d check%u\n",line,checks);std::abort();}}
#define CHECK(x) check(bool(x),__LINE__)
static World world;
static std::array<Candidate,candidate_capacity> menu;

Actor actor(unsigned id,unsigned team,Point position,double hp=1000){
    Actor value{};value.id=id;value.team=team;value.position=value.belief_center=position;
    value.health=value.max_health=hp;value.can_move=value.can_attack=value.can_cast=true;
    value.basic_damage=40;value.attack_range=1;return value;
}
void reset(unsigned actors=2,double hp=1000){
    world=World{};menu={};world.frame=70;world.observation=9;world.effect=2;
    world.owner=0;world.actor_count=actors;world.frontier_count=1;
    world.terrain_complete=world.sources_complete=world.effects_complete=world.schedule_complete=true;
    world.no_exogenous_wake=world.no_respawn=true;world.horizon_intervals=8;world.operation_quota=30000;
    world.camp_orders_known=255;
    for(unsigned round=0;round<8;++round)for(unsigned i=0;i<actors;++i)world.camp_target_ranks[round][i]=i;
    world.observation_intent=901;world.own_spawn={-4,0};
    world.actors[0]=actor(100,0,{0,0},hp);world.actors[0].knowledge=Knowledge::owner;
    for(unsigned i=1;i<actors;++i)world.actors[i]=actor(100+i,1,{int(i),0});
    for(auto& candidate:menu){candidate.frame=world.frame;candidate.observation=world.observation;
        candidate.effect=world.effect;candidate.word_count=1;candidate.purpose=Purpose::flee;}
}
Action& add(Word& word,ActionKind kind,Target target=Target::self,unsigned index=0){
    CHECK(word.count<attempt_capacity);auto& action=word.actions[word.count++];action=Action{};
    action.kind=kind;action.target=target;action.index=index;
    action.target_observed=action.path_legal=action.landing_observed_free=true;return action;
}
Action& move(Word& word,Point point){auto& action=add(word,ActionKind::move,Target::ground);action.point=point;return action;}
Action& basic(Word& word,unsigned target=0){return add(word,ActionKind::basic,Target::actor,target);}
Action& cast(Word& word,unsigned slot,Target target=Target::self,unsigned index=0){
    auto& action=add(word,ActionKind::cast,target,index);action.slot=slot;return action;
}
void wait(Word& word){add(word,ActionKind::wait);}
void event(Frontier& frontier,EventKind kind,unsigned actor_index=0,unsigned word=no_word){
    CHECK(frontier.event_count<event_capacity);frontier.events[frontier.event_count++]={kind,actor_index,word};
}
void camp(Frontier& frontier){event(frontier,EventKind::camp_batch);}
void input(Frontier& frontier,unsigned actor_index,unsigned word=no_word){event(frontier,EventKind::champion_input,actor_index,word);}
void next_owner(Frontier& frontier){camp(frontier);input(frontier,world.owner);}
void damage_camp(unsigned index,Point position,double damage=20){
    world.camp_count=std::max(world.camp_count,index+1);auto& value=world.camps[index];value=Camp{};
    value.position=position;value.id=index;value.group=index;value.health=100;value.health_known=true;
    value.damage=damage;value.range=3;value.knowledge=CampKnowledge::fresh_awake;
}
Ability dash(double damage,int stun=0){
    Ability value{};value.enabled=true;value.ground=true;value.kind=AbilityKind::dash;
    value.range=2;value.radius=1;value.damage=damage;value.stun_ticks=stun;
    value.cooldown_on_cast=1;value.target_mask=target_ground;return value;
}

void supplied_heart_frontier(){
    // This is a supplied symbolic frontier, not a runtime kit/catalog claim.
    // The bad landing takes dash418.006 plus three ordered38 hits =532.006.
    reset(2,500);world.actors[1].position={4,0};world.actors[1].belief_center={4,0};
    world.actors[1].basic_damage=38;world.actors[1].abilities[0]=dash(418.006,1);
    world.reply_count=2;
    auto& leap=cast(world.replies[0],0,Target::ground);leap.point={2,0};
    basic(world.replies[0]).optional_if_unavailable=true;
    basic(world.replies[1]).optional_if_unavailable=true;
    auto& frontier=world.frontiers[0];input(frontier,1,0);camp(frontier);input(frontier,0);
    input(frontier,1,1);camp(frontier);input(frontier,1,1);input(frontier,0);
    move(menu[0].words[0],{1,0});
    auto fatal=evaluate(world,menu[0]);
    CHECK(fatal.valid&&!fatal.alive&&fatal.worst_health<=0);
    CHECK(fatal.hostile_inputs==3&&fatal.lost_owner_inputs>=1&&fatal.fatal_actor==1);
    // A supported dash then ordinary Move leaves the entire supplied reply
    // reach. Unavailable enemy Basics are skipped, not treated as own errors.
    world.actors[0].abilities[0]=dash(0);
    auto& escape=menu[1];auto& flee=cast(escape.words[0],0,Target::ground);flee.point={-2,0};
    move(escape.words[0],{-3,0});
    auto safe=evaluate(world,escape);
    CHECK(safe.valid&&safe.alive&&safe.complete&&safe.next_usable_seen);
    // This bound is conditional on the supplied complete family, not a claim
    // that these replies enumerate a runtime champion catalog or live policy.
    CHECK(safe.worst_loss==0&&safe.assurance==Assurance::conditional_prefix_bound);
    CHECK(choose(world,menu.data(),2).index==1);
}
void ordered_varg_and_opportunities(){
    // C22 witness:80 before the camp seam, then40 before the owner's input.
    reset(2,110);world.actors[0].position={2,0};world.actors[1].position={3,0};
    world.actors[1].belief_center={3,0};world.reply_count=2;
    auto& reset_attack=world.actors[1].abilities[0];reset_attack.enabled=true;
    reset_attack.kind=AbilityKind::reset;reset_attack.range=0;reset_attack.target_mask=target_self;
    move(menu[0].words[0],{1,0});move(world.replies[0],{2,0});basic(world.replies[0]);
    cast(world.replies[0],0);basic(world.replies[0]);basic(world.replies[1]);
    auto& f=world.frontiers[0];input(f,1,0);camp(f);input(f,1,1);input(f,0);
    auto q=evaluate(world,menu[0]);CHECK(q.valid&&!q.alive&&q.hostile_inputs==2);
    CHECK(q.worst_loss>=120&&!q.next_usable_seen);
    // Exact schedules, not a rate multiplied by elapsed rounds, determine how
    // many otherwise identical hostile Basic opportunities actually exist.
    for(unsigned opportunities=0;opportunities<=2;++opportunities){
        reset();world.reply_count=1;basic(world.replies[0]);wait(menu[0].words[0]);
        auto& timeline=world.frontiers[0];if(opportunities)input(timeline,1,0);
        camp(timeline);if(opportunities==2)input(timeline,1,0);input(timeline,0);
        q=evaluate(world,menu[0]);CHECK(q.valid&&q.alive&&q.complete);
        CHECK(q.hostile_inputs==opportunities&&q.worst_loss==40.0*opportunities);
    }
}
void phase_specific_effects(){
    reset(1,100);damage_camp(0,{1,0});world.actors[0].shield={20,1};
    wait(menu[0].words[0]);next_owner(world.frontiers[0]);
    auto q=evaluate(world,menu[0]);CHECK(q.valid&&q.alive&&q.worst_loss==0);
    // The owner's end made shield1→0, but the next camp batch precedes its
    // owner's start/expiry. Camp stun0 instead expires before that camp acts.
    world.actors[0].shield={};world.camps[0].stun_ticks=0;
    q=evaluate(world,menu[0]);CHECK(q.valid&&q.worst_loss==20);
    world.camps[0].stun_ticks=1;q=evaluate(world,menu[0]);CHECK(q.valid&&q.worst_loss==0);

    reset(2,100);damage_camp(0,{-1,0},10);world.actors[1].basic_damage=15;
    world.actors[0].shield={20,1};world.reply_count=1;basic(world.replies[0]);wait(menu[0].words[0]);
    auto& f=world.frontiers[0];camp(f);input(f,1,0);input(f,0);
    q=evaluate(world,menu[0]);CHECK(q.valid&&q.worst_loss==5);
    // One shield pool is depleted by both sources; it is not independently
    // credited once against camps and again against the champion word.
    CHECK(q.camp_batches==1&&q.hostile_inputs==1&&q.worst_health==95);
}
void camp_target_order_not_id(){
    reset(2,100);world.actors[0].id=200;world.actors[1].id=100;
    world.actors[1].position=world.actors[1].belief_center={2,0};
    damage_camp(0,{1,0},20);wait(menu[0].words[0]);next_owner(world.frontiers[0]);
    // Both bodies are one tile from camp. The owner acts first this round,
    // despite its larger entity ID, so it receives the single camp attack.
    auto owner_first=evaluate(world,menu[0]);CHECK(owner_first.valid&&owner_first.worst_loss==20);
    world.camp_target_ranks[0][0]=1;world.camp_target_ranks[0][1]=0;
    auto enemy_first=evaluate(world,menu[0]);CHECK(enemy_first.valid&&enemy_first.worst_loss==0);
    CHECK(owner_first.assurance==Assurance::conditional_prefix_bound&&
        enemy_first.assurance==Assurance::conditional_prefix_bound);
}
void lost_input_and_immunity(){
    reset(2,100);world.reply_count=1;
    auto& control=world.actors[1].abilities[0];control.enabled=true;control.kind=AbilityKind::control;
    control.range=4;control.stun_ticks=1;control.target_mask=target_enemy;
    cast(world.replies[0],0,Target::actor,0);wait(menu[0].words[0]);
    menu[0].word_count=2;move(menu[0].words[1],{-1,0});
    auto& f=world.frontiers[0];input(f,1,0);camp(f);input(f,0,1);camp(f);input(f,0);
    auto stunned=evaluate(world,menu[0]);
    CHECK(stunned.valid&&stunned.alive&&stunned.complete);
    CHECK(stunned.owner_inputs==2&&stunned.lost_owner_inputs==1&&stunned.owner_usable==1);
    world.actors[0].stun_immune=true;
    auto immune=evaluate(world,menu[0]);CHECK(immune.valid&&immune.alive&&immune.complete);
    CHECK(immune.lost_owner_inputs==0&&immune.owner_usable==2);
}
void dynamic_counterhit_and_mac(){
    reset(2,1000);world.actors[1].health=world.actors[1].max_health=100;
    world.actors[1].basic_damage=10;world.actors[1].missing_health_bonus=0.5;
    world.actors[0].basic_damage=50;world.reply_count=1;basic(world.replies[0]);
    wait(menu[0].words[0]);basic(menu[1].words[0],1);menu[1].purpose=Purpose::counteraction;
    auto& f=world.frontiers[0];input(f,1,0);next_owner(f);
    auto baseline=evaluate(world,menu[0]),counter=evaluate(world,menu[1]);
    CHECK(baseline.valid&&counter.valid&&baseline.worst_loss==10);
    CHECK(counter.worst_loss>baseline.worst_loss); // Heart's post-hit output changed

    // NPC damage also mutates a hostile actor before that actor's reply. The
    // camp at3 is nearer Heart at2 than owner0; it never directly hits owner.
    reset(2,1000);world.actors[1].position={2,0};world.actors[1].belief_center={2,0};
    world.actors[1].health=world.actors[1].max_health=100;world.actors[1].attack_range=3;
    world.actors[1].basic_damage=10;world.actors[1].missing_health_bonus=0.5;
    world.reply_count=1;basic(world.replies[0]);wait(menu[0].words[0]);
    auto& camp_then_reply=world.frontiers[0];camp(camp_then_reply);input(camp_then_reply,1,0);input(camp_then_reply,0);
    baseline=evaluate(world,menu[0]);CHECK(baseline.valid&&baseline.worst_loss==10);
    damage_camp(0,{3,0},20);auto npc_changed=evaluate(world,menu[0]);
    CHECK(npc_changed.valid&&npc_changed.worst_loss>10&&npc_changed.worst_loss<20);

    reset(2,100);world.actors[0].ranged=true;world.actors[0].attack_range=4;
    world.actors[1].position={3,0};world.actors[1].health=35;
    auto& reset_a=world.actors[0].abilities[1];reset_a.enabled=true;reset_a.kind=AbilityKind::reset;
    reset_a.range=0;reset_a.target_mask=target_self;
    auto& candidate=menu[0];candidate.purpose=Purpose::work;
    move(candidate.words[0],{-1,0});cast(candidate.words[0],1);basic(candidate.words[0],1);
    next_owner(world.frontiers[0]);auto q=evaluate(world,candidate);
    CHECK(q.valid&&q.alive&&q.complete&&q.successful==3);
    move(candidate.words[0],{-2,0});q=evaluate(world,candidate);
    CHECK(!q.valid&&q.reason==Reason::illegal_own_action); // Reset restores A, never M
}
void actual_work_effect_not_empty_cast(){
    reset(2,100);menu[0].purpose=Purpose::work;
    auto& ability=world.actors[0].abilities[0];ability.enabled=ability.ground=true;
    ability.kind=AbilityKind::damage;ability.range=4;ability.radius=0;ability.damage=40;
    ability.target_mask=target_ground;
    auto& action=cast(menu[0].words[0],0,Target::ground);action.point={3,0};
    next_owner(world.frontiers[0]);
    auto empty=evaluate(world,menu[0]);CHECK(empty.valid&&empty.alive&&!empty.complete);
    action.point=world.actors[1].position;
    auto useful=evaluate(world,menu[0]);CHECK(useful.valid&&useful.alive&&useful.complete);
    CHECK(empty.successful==1&&useful.successful==1); // SDK success alone is not useful work
    ability.damage=0;ability.stun_ticks=1;world.actors[1].stun_immune=true;
    auto immune=evaluate(world,menu[0]);CHECK(immune.valid&&immune.alive&&!immune.complete);
}
void refusal_keeps_effects_and_time(){
    reset(1,10);damage_camp(0,{1,0},20);world.camps[0].health=35;
    world.actors[0].basic_damage=40;world.frontier_count=2;
    auto& candidate=menu[0];candidate.purpose=Purpose::work;
    add(candidate.words[0],ActionKind::basic,Target::camp,0);
    auto& first=move(candidate.words[0],{1,0});first.landing_observed_free=false;
    auto& observation=add(candidate.words[0],ActionKind::observe);
    observation.only_after_refusal=true;observation.relevant_observation=true;
    observation.observation_intent=901;observation.cursor_before=0;observation.cursor_after=1;observation.cursor_total=1;
    auto& alternative=move(candidate.words[0],{0,1});alternative.only_after_refusal=true;
    next_owner(world.frontiers[0]);next_owner(world.frontiers[1]);
    world.frontiers[1].refused_word=0;world.frontiers[1].refused_action=1;
    auto q=evaluate(world,candidate);
    CHECK(q.valid&&q.alive&&q.complete&&q.worst_loss==0);
    CHECK(q.refusals==1&&q.observations==1&&q.camp_batches==1);
    // The earlier lethal Basic persists, M survives false, and observation is
    // finite. No fictional camp phase is inserted between those same-input acts.
    observation.relevant_observation=false;q=evaluate(world,candidate);
    CHECK(!q.valid&&q.reason==Reason::missing_feedback);
    observation.relevant_observation=true;world.blocked_count=1;world.blocked[0]={0,1};
    q=evaluate(world,candidate);CHECK(!q.valid&&q.reason==Reason::illegal_own_action);
}
void retained_hidden_and_envelopes(){
    reset(2,200);world.actors[1].knowledge=Knowledge::retained_hidden;
    world.actors[1].position=world.actors[1].belief_center={4,0};world.actors[1].belief_radius=4;
    world.reply_count=1;basic(world.replies[0]).optional_if_unavailable=true;
    wait(menu[0].words[0]);auto& f=world.frontiers[0];f.position_overrides=std::uint16_t(1u<<1);f.positions[1]={1,0};
    input(f,1,0);next_owner(f);
    auto q=evaluate(world,menu[0]);CHECK(q.valid&&q.worst_loss==40);
    // A partial retained actor uses its whole remaining prefix at each hostile
    // opportunity and expands reachable belief, rather than becoming absent.
    f.position_overrides=0;f.event_count=0;input(f,1,0);camp(f);input(f,1,0);input(f,0);
    auto& hidden=world.actors[1];hidden.frontier_complete=false;
    f.complete=false;
    hidden.remaining_reply.valid=true;hidden.remaining_reply.reach=1;hidden.remaining_reply.travel=1;
    hidden.remaining_reply.hits=1;hidden.remaining_reply.damage[0]=40;
    q=evaluate(world,menu[0]);CHECK(q.valid&&q.worst_loss>=80&&q.assurance==Assurance::approximate_frontier);
    CHECK(choose(world,menu.data(),1).index==std::numeric_limits<std::size_t>::max());
    CHECK(choose(world,menu.data(),1,true).index==0);
    hidden.remaining_reply.valid=false;q=evaluate(world,menu[0]);
    CHECK(!q.valid||q.worst_loss>=80); // missing envelope cannot fabricate zero threat
}
void envelope_motion_loses_npc_protection(){
    reset(2,100);world.actors[1].position=world.actors[1].belief_center={2,0};
    damage_camp(0,{3,0},20);wait(menu[0].words[0]);
    auto& envelope=world.actors[1].remaining_reply;envelope.valid=true;envelope.reach=0;envelope.travel=0;envelope.hits=0;
    auto& f=world.frontiers[0];f.envelope_actor_mask=std::uint16_t(1u<<1);
    input(f,1);next_owner(f);
    auto stationary=evaluate(world,menu[0]);CHECK(stationary.valid&&stationary.worst_loss==0);
    // Unenumerated movement may remove Heart's nearer protective body before
    // the NPC batch. Its original coordinate cannot keep diverting that hit.
    envelope.travel=4;auto moving=evaluate(world,menu[0]);
    CHECK(moving.valid&&moving.alive&&moving.worst_loss==20&&moving.approximate);
}
void service_phase_order(){
    reset(1,10);damage_camp(0,{0,0},0);world.camps[0].alive=false;world.camps[0].health=0;
    damage_camp(1,{1,0},20);world.capture.enabled=world.capture.group_clear=true;
    world.capture.region_count=1;world.capture.region[0]={0,0};world.capture.camp_mask=1;
    world.capture.ticks=0;world.capture.duration=1;world.capture.gems=10;
    menu[0].purpose=Purpose::capture;wait(menu[0].words[0]);next_owner(world.frontiers[0]);
    auto q=evaluate(world,menu[0]);CHECK(q.valid&&q.captured>0&&!q.alive);
    // Capture payout occurs at the batch start, before an unrelated awake
    // source's fatal attack. A capture forecast alone is not surviving service.

    reset(1,100);world.actors[0].gems=10;menu[0].purpose=Purpose::bank;
    auto& bank=add(menu[0].words[0],ActionKind::bank);bank.duration=1;bank.point={0,0};
    next_owner(world.frontiers[0]);q=evaluate(world,menu[0]);
    CHECK(q.valid&&q.alive&&q.complete&&q.banked==10);

    reset(1,100);world.spawn_valid=true;menu[0].purpose=Purpose::recall;
    auto& recall=add(menu[0].words[0],ActionKind::recall);recall.duration=2;recall.point=world.own_spawn;
    auto& f=world.frontiers[0];next_owner(f);next_owner(f);
    q=evaluate(world,menu[0]);CHECK(q.valid&&q.alive&&q.complete&&q.camp_batches==2);
    damage_camp(0,{1,0},1);q=evaluate(world,menu[0]);
    CHECK(q.valid&&q.alive&&!q.complete&&q.worst_loss>0); // real damage cancels recall
}
void ranged_work_requires_separate_escape(){
    reset(1,50);world.actors[0].ranged=true;world.actors[0].attack_range=3;
    damage_camp(0,{3,0},60);world.camps[0].range=3;world.camps[0].health=100;
    menu[0].purpose=Purpose::work;menu[0].useful_service=1;
    add(menu[0].words[0],ActionKind::basic,Target::camp,0);
    menu[1].purpose=Purpose::flee;menu[1].useful_service=1;move(menu[1].words[0],{-1,0});
    next_owner(world.frontiers[0]);
    const auto work=evaluate(world,menu[0]),escape=evaluate(world,menu[1]);
    CHECK(work.valid&&!work.alive&&escape.valid&&escape.alive&&escape.complete);
    CHECK(choose(world,menu.data(),2).index==1);
    // Appending M to this Basic is physically invalid. The escape must be an
    // independent candidate, so the complete menu can choose to move instead.
    move(menu[0].words[0],{-1,0});CHECK(!evaluate(world,menu[0]).valid);
}
void capture_personal_share(){
    const auto prepare=[](unsigned actors){
        reset(actors,100);world.capture.enabled=world.capture.group_clear=world.capture.group_complete=true;
        world.capture.region_count=2;world.capture.region[0]={0,0};world.capture.region[1]={0,1};
        world.capture.ticks=0;world.capture.duration=5;world.capture.gems=7;
        menu[0].purpose=Purpose::capture;wait(menu[0].words[0]);next_owner(world.frontiers[0]);
    };
    prepare(1);auto quote=evaluate(world,menu[0]);
    CHECK(quote.valid&&quote.complete&&quote.captured==7&&quote.team_captured==7&&!quote.owner_share_unknown);
    prepare(2);world.actors[1].team=0;world.actors[1].position={0,1};world.actors[1].id=101;
    quote=evaluate(world,menu[0]);CHECK(quote.valid&&quote.captured==4&&quote.team_captured==7);
    world.actors[1].id=99;quote=evaluate(world,menu[0]);CHECK(quote.valid&&quote.captured==3);
    world.actors[1].position={2,1};quote=evaluate(world,menu[0]);CHECK(quote.valid&&quote.captured==7);
    // The native remainder goes to the lowest ID, not roster index. Personal
    // bag ownership changes later tribute, while team capture stays seven.
    prepare(2);world.actors[1].team=0;world.actors[1].position={0,1};world.actors[1].id=99;
    menu[0].purpose=Purpose::bank;menu[0].word_count=2;
    auto& bank=add(menu[0].words[1],ActionKind::bank);bank.point={0,0};bank.duration=1;
    next_owner(world.frontiers[0]);quote=evaluate(world,menu[0]);
    CHECK(quote.valid&&quote.complete&&quote.banked==3&&quote.team_captured==7);
    prepare(2);auto& hidden=world.actors[1];hidden.team=0;hidden.knowledge=Knowledge::retained_hidden;
    hidden.position=hidden.belief_center={0,1};hidden.belief_radius=0;
    quote=evaluate(world,menu[0]);
    CHECK(quote.valid&&quote.complete&&quote.approximate&&quote.owner_share_unknown&&quote.captured==0&&quote.team_captured==7);
}
void last_kill_enables_capture(){
    reset(1,100);damage_camp(0,{1,0},20);world.camps[0].health=35;
    world.actors[0].basic_damage=40;menu[0].purpose=Purpose::capture;
    add(menu[0].words[0],ActionKind::basic,Target::camp,0);
    move(menu[0].words[0],{1,0}); // the preceding actual model kill frees it
    world.capture.enabled=world.capture.group_complete=true;world.capture.group_clear=false;
    world.capture.camp_mask=1;world.capture.region_count=1;world.capture.region[0]={1,0};
    world.capture.ticks=-1;world.capture.duration=1;world.capture.gems=10;
    next_owner(world.frontiers[0]);next_owner(world.frontiers[0]);
    auto q=evaluate(world,menu[0]);
    CHECK(q.valid&&q.alive&&q.complete&&q.captured==10&&q.worst_loss==0);
    CHECK(q.successful==2&&q.camp_batches==2);
    // Coverage of the whole group, not a predicted timer alone, licenses the
    // post-kill transition from initially uncleared to actually cleared.
    world.capture.group_complete=false;q=evaluate(world,menu[0]);
    CHECK(q.valid&&q.alive&&!q.complete&&q.captured==0);
}
void authority_and_bounded_work(){
    reset(1,100);move(menu[0].words[0],{-1,0});next_owner(world.frontiers[0]);
    auto q=evaluate(world,menu[0]);CHECK(q.valid&&q.alive&&q.complete&&q.requires_actual_confirmation);
    Receipt receipt{};CHECK(!actual_continuation(world,menu[0],q,receipt));
    receipt.frame=world.frame;receipt.observation=world.observation+1;receipt.effect=world.effect+1;
    receipt.successful_prefix=1;receipt.actual_body=receipt.actual_targets=receipt.actual_occupancy=receipt.refusal_closed=true;
    CHECK(actual_continuation(world,menu[0],q,receipt));
    CHECK(!actual_continuation(world,menu[1],q,receipt));
    receipt.actual_occupancy=false;CHECK(!actual_continuation(world,menu[0],q,receipt));
    receipt.actual_occupancy=true;++receipt.frame;CHECK(!actual_continuation(world,menu[0],q,receipt));
    world.operation_quota=1;q=evaluate(world,menu[0]);CHECK(!q.valid&&q.reason==Reason::operation_budget);
}
void invalid_living_health_and_points(){
    reset(1,100);wait(menu[0].words[0]);next_owner(world.frontiers[0]);
    world.actors[0].health=0;auto q=evaluate(world,menu[0]);
    CHECK(!q.valid&&q.reason==Reason::invalid_amount);
    world.actors[0].health=100;world.actors[0].max_health=0;
    CHECK(!evaluate(world,menu[0]).valid);

    reset(1,100);auto& ability=world.actors[0].abilities[0];ability.enabled=ability.ground=true;
    ability.kind=AbilityKind::damage;ability.range=4;ability.damage=1;ability.target_mask=target_ground;
    auto& ground=cast(menu[0].words[0],0,Target::ground);ground.point={std::numeric_limits<int>::min(),0};
    next_owner(world.frontiers[0]);q=evaluate(world,menu[0]);
    CHECK(!q.valid&&q.reason==Reason::illegal_own_action);

    reset(1,100);world.actors[0].gems=1;auto& bank=add(menu[0].words[0],ActionKind::bank);
    bank.duration=1;bank.point={std::numeric_limits<int>::min(),0};next_owner(world.frontiers[0]);
    CHECK(!evaluate(world,menu[0]).valid);
    reset(1,100);world.spawn_valid=true;world.own_spawn={std::numeric_limits<int>::min(),0};
    auto& recall=add(menu[0].words[0],ActionKind::recall);recall.duration=1;next_owner(world.frontiers[0]);
    CHECK(!evaluate(world,menu[0]).valid);
}
// These component oracles deliberately use independent stored float32 state,
// not detail::deplete, a product of hit count and damage, or a tolerance.
float native_hit(float health,float hit){
    volatile float next=health-hit;return next>0?next:0.0f;
}
static detail::State numeric_state;
void numeric_component(unsigned& operations){
    operations=0;numeric_state=detail::State{};
    numeric_state.world=&world;numeric_state.candidate=&menu[0];numeric_state.frontier=&world.frontiers[0];
    numeric_state.actors=world.actors;numeric_state.camps=world.camps;
    numeric_state.capture=world.capture;numeric_state.operations=&operations;
    numeric_state.minimum_health=world.actors[world.owner].health;
    numeric_state.quote.valid=numeric_state.quote.bounded=true;numeric_state.quote.reason=Reason::ok;
}
void float32_actor_and_camp_kill_boundary(){
    constexpr float hit=129.8f;constexpr double remainder=3.0517578125e-5;
    reset(2,1000);world.actors[1].health=world.actors[1].max_health=649;
    world.actors[0].refresh_cooldowns_on_kill=true;world.actors[0].abilities[0].cooldown=7;
    world.actors[1].gems=5;unsigned operations=0;numeric_component(operations);
    float oracle=649.0f;
    for(unsigned hits=1;hits<=5;++hits){
        oracle=native_hit(oracle,hit);
        CHECK(detail::damage_actor(numeric_state,1,double(hit),0));
        CHECK(numeric_state.actors[1].health==double(oracle)&&numeric_state.actors[1].alive);
        CHECK(numeric_state.actors[0].abilities[0].cooldown==7&&numeric_state.actors[0].gems==0);
    }
    CHECK(double(oracle)==remainder&&numeric_state.actors[1].health==remainder);
    bool hidden=false;
    CHECK(detail::occupied(numeric_state,world.actors[1].position,0,hidden)&&!hidden);
    CHECK(!detail::freed(numeric_state,world.actors[1].position));
    oracle=native_hit(oracle,hit);CHECK(oracle==0);
    CHECK(detail::damage_actor(numeric_state,1,double(hit),0));
    CHECK(!numeric_state.actors[1].alive&&numeric_state.actors[1].health==0);
    CHECK(!detail::occupied(numeric_state,world.actors[1].position,0,hidden));
    CHECK(detail::freed(numeric_state,world.actors[1].position));
    CHECK(numeric_state.actors[0].abilities[0].cooldown==0&&numeric_state.actors[0].gems==5);

    reset(1,1000);damage_camp(0,{1,0},0);world.camps[0].health=649;
    world.actors[0].refresh_cooldowns_on_kill=true;world.actors[0].abilities[0].cooldown=7;
    numeric_component(operations);oracle=649.0f;
    for(unsigned hits=1;hits<=5;++hits){
        oracle=native_hit(oracle,hit);CHECK(detail::damage_camp(numeric_state,0,double(hit),0));
        CHECK(numeric_state.camps[0].health==double(oracle)&&numeric_state.camps[0].alive);
        CHECK(numeric_state.actors[0].abilities[0].cooldown==7);
    }
    CHECK(double(oracle)==remainder&&detail::occupied(numeric_state,{1,0},0,hidden));
    CHECK(!detail::freed(numeric_state,{1,0}));
    CHECK(detail::damage_camp(numeric_state,0,double(hit),0));
    CHECK(!numeric_state.camps[0].alive&&numeric_state.camps[0].health==0);
    CHECK(!detail::occupied(numeric_state,{1,0},0,hidden)&&detail::freed(numeric_state,{1,0}));
    CHECK(numeric_state.actors[0].abilities[0].cooldown==0);
}
void float32_retaliation_and_shared_shield(){
    constexpr float hit=129.8f;constexpr double remainder=3.0517578125e-5;
    reset(2,649);world.actors[1].health=world.actors[1].max_health=10000;
    world.actors[1].retaliation=double(hit);unsigned operations=0;numeric_component(operations);
    float oracle=649.0f;
    for(unsigned hits=1;hits<=5;++hits){
        oracle=native_hit(oracle,hit);
        // Six separate actual attacks, not one six-primitive own input. Each
        // tiny outgoing hit triggers exactly one Ren-style stored retaliation.
        CHECK(detail::damage_actor(numeric_state,1,1,0));
        CHECK(numeric_state.actors[0].alive&&numeric_state.actors[0].health==double(oracle));
    }
    CHECK(double(oracle)==remainder);
    CHECK(detail::damage_actor(numeric_state,1,1,0));
    CHECK(!numeric_state.actors[0].alive&&numeric_state.actors[0].health==0);
    CHECK(numeric_state.quote.fatal_actor==1);

    reset(2,100);world.actors[0].shield={649,1};numeric_component(operations);
    float shield_oracle=649.0f,hp_oracle=100.0f;
    for(unsigned hits=1;hits<=5;++hits){
        const float absorbed=std::min(shield_oracle,hit);
        shield_oracle=native_hit(shield_oracle,absorbed);
        hp_oracle=native_hit(hp_oracle,hit-absorbed);
        CHECK(detail::damage_actor(numeric_state,0,double(hit),1));
        CHECK(numeric_state.actors[0].shield.amount==double(shield_oracle));
        CHECK(numeric_state.actors[0].health==double(hp_oracle)&&numeric_state.actors[0].alive);
    }
    CHECK(double(shield_oracle)==remainder&&hp_oracle==100.0f&&numeric_state.loss==0);
    // The tiny residual shield is shared state. The sixth hit spends it once,
    // then its actual float32 remainder penetrates and kills this100HP body.
    const float absorbed=std::min(shield_oracle,hit);
    shield_oracle=native_hit(shield_oracle,absorbed);hp_oracle=native_hit(hp_oracle,hit-absorbed);
    CHECK(detail::damage_actor(numeric_state,0,double(hit),1));
    CHECK(shield_oracle==0&&hp_oracle==0&&!numeric_state.actors[0].alive&&numeric_state.loss>0);
}
void float32_fifth_hit_does_not_delete_reply(){
    constexpr double hit=double(129.8f);
    reset(6,1000);world.reply_count=2;menu[0].purpose=Purpose::work;
    world.actors[0].basic_damage=hit;
    constexpr std::array<Point,4> allies{{{0,1},{1,1},{2,1},{2,0}}};
    for(unsigned i=1;i<=4;++i){
        world.actors[i]=actor(100+i,0,allies[i-1]);world.actors[i].basic_damage=hit;
    }
    world.actors[5]=actor(105,1,{1,0},649);world.actors[5].basic_damage=10;
    world.actors[5].missing_health_bonus=0.5;
    basic(menu[0].words[0],5);basic(world.replies[0],5);basic(world.replies[1],0);
    menu[0].word_count=2;wait(menu[0].words[1]);
    auto& frontier=world.frontiers[0];for(unsigned i=1;i<=4;++i)input(frontier,i,0);
    camp(frontier);input(frontier,0,1);input(frontier,5,1);
    // Owner and four teammates each make ONE legal Basic in separate inputs.
    // Five129.8f hits leave Heart alive; its nearly maximum missing-health
    // bonus must therefore still contribute a real scheduled reply after the
    // next owner's explicit wait. No sixth teammate is invented.
    auto five=evaluate(world,menu[0]);
    CHECK(five.valid&&five.alive&&five.complete&&five.hostile_inputs==1&&five.hostile_usable==1);
    CHECK(five.worst_loss>10&&five.worst_loss<16&&five.approximate);
    // The owner's next actual input has a fresh A. Its sixth total hit really
    // kills Heart before that same reply, retaining the game's five-body team.
    // This changes hostile usability, not just a summed-damage prediction.
    menu[0].words[1].count=0;basic(menu[0].words[1],5);
    auto six=evaluate(world,menu[0]);
    CHECK(six.valid&&six.alive&&six.complete&&six.hostile_inputs==1&&six.hostile_usable==0);
    CHECK(six.worst_loss==0);
}
int main(){
    supplied_heart_frontier();ordered_varg_and_opportunities();phase_specific_effects();camp_target_order_not_id();lost_input_and_immunity();
    dynamic_counterhit_and_mac();actual_work_effect_not_empty_cast();refusal_keeps_effects_and_time();retained_hidden_and_envelopes();
    envelope_motion_loses_npc_protection();service_phase_order();ranged_work_requires_separate_escape();capture_personal_share();last_kill_enables_capture();
    authority_and_bounded_work();invalid_living_health_and_points();
    float32_actor_and_camp_kill_boundary();float32_retaliation_and_shared_shield();float32_fifth_hit_does_not_delete_reply();
    std::printf("%u joint-world checks passed\n",checks);
}
