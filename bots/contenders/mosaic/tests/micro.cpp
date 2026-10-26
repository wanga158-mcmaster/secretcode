#include "../src/micro.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace A=mosaic::adaptive;
namespace G=A::game;
static unsigned checks{},groups{};
static void check(bool value,const char* expression,int line){
    ++checks;if(!value){std::fprintf(stderr,"micro check failed at %d: %s\n",line,expression);std::abort();}
}
#define CHECK(...) check(bool(__VA_ARGS__),#__VA_ARGS__,__LINE__)

static G::SelfFact body(A::Epoch epoch={10,1,0}){
    G::SelfFact s{};s.epoch=epoch;s.id=4;s.pose={0,0};s.hp=s.max_hp=100;s.strength=20;s.focus=50;
    s.team=0;s.attack_range=s.move_speed=1;s.alive=s.raw_m=s.raw_a=s.raw_c=true;
    s.body_actual=s.restrictions_actual=s.buffs_actual=s.channel_actual=s.walked_actual=true;
    return s;
}
struct Fixture {
    A::MicroInput in{};std::array<A::MicroCamp,9> camps{};std::array<A::MicroChampion,9> champions{};
    std::array<A::MicroMove,9> moves{};std::array<A::MicroGround,9> ground{};
    unsigned nc{},nu{},nm{},ng{};
    Fixture(){in.self=body();in.context.epoch=in.self.epoch;in.context.group_bound=true;in.context.group_id=0;
        in.context.work_admitted=in.context.cast_admitted=in.context.quiet=true;}
    void date(A::Epoch epoch){
        in.self.epoch=in.context.epoch=epoch;
        for(auto& a:in.abilities)a.epoch=epoch;
        for(auto& c:camps)c.fact.epoch=epoch;
        for(auto& u:champions)u.epoch=epoch;
        for(auto& m:moves)m.epoch=epoch;
        for(auto& g:ground)g.epoch=epoch;
    }
    void next(){auto now=in.self.epoch;++now.attempt;++now.revision;date(now);}
    A::MicroCamp& camp(unsigned index,G::NativeId id,A::Point point,float hp=100,G::NativeId group=0){
        nc=std::max(nc,index+1);auto& c=camps[index];c={};c.fact.epoch=in.self.epoch;c.fact.id=id;c.fact.group_id=group;
        c.fact.pose=point;c.fact.hp=c.fact.max_hp=hp;c.fact.life=G::Life::awake;
        c.fact.life_actual=c.fact.body_actual=c.fact.buffs_actual=c.visible=c.visibility_actual=true;return c;
    }
    A::MicroChampion& unit(unsigned index,G::NativeId id,int team,A::Point point,float hp=100){
        nu=std::max(nu,index+1);auto& u=champions[index];u={};u.epoch=in.self.epoch;u.id=id;u.team=team;u.pose=point;
        u.hp=hp;u.max_hp=100;u.alive=u.body_actual=u.buffs_actual=u.visible=u.visibility_actual=true;return u;
    }
    G::AbilityDescriptor& ability(unsigned slot,G::AbilityKind kind,std::uint32_t targets,float range=4,float modifier=2){
        auto& a=in.abilities[slot];a={};a.epoch=in.self.epoch;a.cooldown_actual=true;
        auto& d=a.descriptor;d.kind=kind;d.target_mask=targets;d.range=range;d.modifier=modifier;d.present=d.actual=true;return d;
    }
    A::MicroMove& move(unsigned index,A::Point point,A::MicroMovePurpose purpose=A::MicroMovePurpose::ingress){
        nm=std::max(nm,index+1);auto& m=moves[index];m={};m.epoch=in.self.epoch;m.point=point;m.purpose=purpose;
        m.observed_legal=m.funded=m.exit_executable=true;return m;
    }
    A::MicroChoice choose(){in.camps={camps.data(),nc};in.champions={champions.data(),nu};in.moves={moves.data(),nm};in.ground={ground.data(),ng};return A::choose_micro(in);}
};

static void actual_basic_survives_optional_unknowns(){
    Fixture f;f.camp(0,0,{1,0});f.in.self.id=0;
    auto chosen=f.choose();CHECK(chosen.valid&&chosen.action.kind==A::MicroKind::basic_camp&&chosen.action.target_id==0&&chosen.action.group_id==0);
    auto& d=f.ability(0,G::AbilityKind::damage,G::target_camps);d.actual=false;
    f.in.context.incoming={std::numeric_limits<float>::quiet_NaN(),0,false};
    f.in.self.strength=std::numeric_limits<float>::quiet_NaN();
    chosen=f.choose();CHECK(chosen.valid&&chosen.action.kind==A::MicroKind::basic_camp&&chosen.benefit.work==0);
    f.in.context.quiet=false;CHECK(!f.choose().valid);
    f.in.context.exit_executable=true;CHECK(f.choose().action.kind==A::MicroKind::basic_camp);
    f.in.context.work_admitted=false;CHECK(!f.choose().valid);
    ++groups;
}
static void actual_target_binding_and_no_vision_landing_requirement(){
    Fixture f;f.camp(0,0,{1,0},100,9);f.camp(1,1,{0,1});
    CHECK(f.choose().action.target_id==1);f.camps[1].visible=false;
    CHECK(!f.choose().valid);f.move(0,{1,1},A::MicroMovePurpose::exit);
    const auto chosen=f.choose();CHECK(chosen.valid&&chosen.action.kind==A::MicroKind::move&&chosen.action.point==A::Point{1,1});
    f.in.context.epoch.attempt=1;CHECK(!f.choose().valid);
    ++groups;
}
static void damage_kill_rebinds_fresh_living_group_member(){
    Fixture f;f.camp(0,0,{1,0},70);f.camp(1,7,{0,1},40);
    f.ability(0,G::AbilityKind::damage,G::target_camps);
    auto chosen=f.choose();CHECK(chosen.action.kind==A::MicroKind::cast_camp&&chosen.action.target_id==0&&chosen.benefit.work==70);
    // Hand-fed actual post-effect facts, not a simulation of the spell engine.
    f.camps[0].fact.life=G::Life::dead;f.camps[0].fact.hp=0;f.in.self.raw_c=false;f.next();
    chosen=f.choose();CHECK(chosen.action.kind==A::MicroKind::basic_camp&&chosen.action.target_id==7&&chosen.benefit.work==20);
    f.camps[1].fact.group_id=9;CHECK(!f.choose().valid);
    ++groups;
}
static void spent_attack_reset_and_actual_mac_reselection(){
    Fixture f;f.camp(0,0,{1,0});f.ability(0,G::AbilityKind::reset,G::target_self,0,0);
    CHECK(f.choose().action.kind==A::MicroKind::basic_camp);
    f.in.self.raw_a=false;f.next();CHECK(f.choose().action.kind==A::MicroKind::cast_self);
    f.in.self.raw_a=true;f.in.self.raw_c=false;f.next();CHECK(f.choose().action.kind==A::MicroKind::basic_camp);
    f.in.self.raw_a=false;f.next();CHECK(!f.choose().valid);
    Fixture ranged;ranged.in.self.ranged=true;ranged.in.self.attack_range=3;ranged.camp(0,0,{2,0});
    ranged.ability(0,G::AbilityKind::reset,G::target_self,0,0);ranged.in.self.raw_a=ranged.in.self.raw_m=false;
    CHECK(ranged.choose().action.kind==A::MicroKind::cast_self);
    ranged.in.self.raw_a=true;ranged.in.self.raw_c=false;ranged.next();CHECK(ranged.choose().action.kind==A::MicroKind::basic_camp);
    ranged.in.self.raw_a=false;ranged.move(0,{1,0}).service_progress=true;ranged.next();CHECK(!ranged.choose().valid);
    ++groups;
}
static void melee_move_basic_and_basic_move_are_effect_dependent(){
    Fixture f;f.camp(0,0,{2,0});f.move(0,{1,0});
    CHECK(f.choose().action.kind==A::MicroKind::move);
    f.in.self.pose={1,0};f.in.self.raw_m=false;f.in.self.ordinary_walked=true;f.next();CHECK(f.choose().action.kind==A::MicroKind::basic_camp);
    Fixture g;g.camp(0,0,{1,0});g.move(0,{0,1},A::MicroMovePurpose::exit);
    CHECK(g.choose().action.kind==A::MicroKind::basic_camp);
    g.in.self.raw_a=false;g.camps[0].fact.life=G::Life::dead;g.next();CHECK(g.choose().action.kind==A::MicroKind::move);
    ++groups;
}
static void exclusive_walk_and_ground_area_use_actual_affected_set(){
    Fixture f;f.camp(0,0,{1,0});f.camp(1,1,{1,1},40);f.camp(2,2,{0,1},100,9);
    f.unit(0,0,1,{1,1},30);f.unit(1,3,0,{1,0},10);
    auto& d=f.ability(0,G::AbilityKind::aoe_damage,G::target_ground|G::target_camps|G::target_enemies);
    d.radius=1;d.exclusive_with_movement=true;
    f.ng=1;f.ground[0]={f.in.self.epoch,{1,0},true,true,true};
    auto chosen=f.choose();CHECK(chosen.action.kind==A::MicroKind::cast_ground&&chosen.benefit.targets==3&&chosen.benefit.work==170);
    f.in.self.ordinary_walked=true;CHECK(f.choose().action.kind==A::MicroKind::basic_camp);
    f.in.self.ordinary_walked=false;f.in.context.preserve_walk=true;CHECK(f.choose().action.kind==A::MicroKind::basic_camp);
    f.in.context.preserve_walk=false;f.ground[0].funded=false;CHECK(f.choose().action.kind==A::MicroKind::basic_camp);
    ++groups;
}
static void useful_heal_and_actual_self_target_mask(){
    Fixture f;f.camp(0,0,{1,0});f.in.self.hp=60;
    f.ability(0,G::AbilityKind::heal,G::target_self,0,1);f.in.self_amount[0]={50,true};
    auto chosen=f.choose();CHECK(chosen.action.kind==A::MicroKind::cast_champion&&chosen.action.target_id==f.in.self.id&&chosen.benefit.healing==40);
    f.in.abilities[0].descriptor.target_mask=G::target_allies;f.in.abilities[0].descriptor.range=4;
    CHECK(f.choose().action.kind==A::MicroKind::basic_camp);
    auto& ally=f.unit(0,0,0,{2,0},70);ally.healing[0]={50,true};
    chosen=f.choose();CHECK(chosen.action.kind==A::MicroKind::cast_champion&&chosen.action.target_id==0&&chosen.benefit.healing==30);
    ally.hp=100;CHECK(f.choose().action.kind==A::MicroKind::basic_camp);
    ally.hp=10;ally.team=1;CHECK(f.choose().action.kind==A::MicroKind::basic_camp);
    ++groups;
}
static void owner_start_shield_expiry_changes_real_usefulness(){
    Fixture f;f.camp(0,0,{1,0});f.in.self.shield=50;f.in.self.shield_time=0;
    auto& d=f.ability(0,G::AbilityKind::shield,G::target_self,0,2);d.duration=2;f.in.self_amount[0]={100,true};
    f.in.context.incoming={40,0,true};CHECK(f.choose().action.kind==A::MicroKind::basic_camp); // timer0 still absorbs before owner START.
    f.in.context.incoming.owner_starts_before=1;auto chosen=f.choose();
    CHECK(chosen.action.kind==A::MicroKind::cast_self&&chosen.benefit.shield==40);
    d.duration=1;CHECK(f.choose().action.kind==A::MicroKind::basic_camp); // newly cast shield expires too.
    d.duration=2;f.in.context.incoming.actual=false;CHECK(f.choose().action.kind==A::MicroKind::basic_camp);
    f.in.context.incoming={70,0,true};chosen=f.choose();CHECK(chosen.action.kind==A::MicroKind::cast_self&&chosen.benefit.shield==20);
    ++groups;
}
static void targeted_cc_and_conditional_dash(){
    Fixture f;f.unit(0,0,1,{1,0});auto& d=f.ability(0,G::AbilityKind::cc,G::target_enemies);d.duration=1;
    f.champions[0].stun_immune=true;CHECK(f.choose().action.kind==A::MicroKind::basic_champion);
    f.champions[0].stun_immune=false;CHECK(f.choose().action.kind==A::MicroKind::cast_champion);
    f.champions[0].stun_time=1;CHECK(f.choose().action.kind==A::MicroKind::basic_champion);
    Fixture dash;dash.camp(0,0,{3,0});dash.in.self.raw_m=false;dash.in.self.raw_a=false;
    auto& dd=dash.ability(1,G::AbilityKind::dash,G::target_ground,2,0);dash.move(0,{2,0}).ability_slot=1;
    auto chosen=dash.choose();CHECK(chosen.action.kind==A::MicroKind::cast_ground&&chosen.action.slot==1&&chosen.action.point==A::Point{2,0});
    dd.exclusive_with_movement=true;dash.in.self.ordinary_walked=true;CHECK(!dash.choose().valid);
    ++groups;
}
static void supported_candidate_bound_is_explicit(){
    Fixture f;for(unsigned i=0;i<9;++i)f.camp(i,i,{1,0},100,i==8?0:9);
    auto chosen=f.choose();CHECK(!chosen.valid&&chosen.truncated);
    f.camps[0].fact.group_id=0;chosen=f.choose();CHECK(chosen.valid&&chosen.truncated&&chosen.action.target_id==0);
    ++groups;
}
static A::MicroReceipt attempted(std::uint64_t event,std::uint64_t serial,A::MicroOutcome outcome,G::NativeId target=0){
    A::MicroReceipt r{};r.event_id=event;r.serial=serial;r.outcome=outcome;r.before=body({10,serial,serial-1});
    r.after=r.before;r.after.epoch={10,serial+1,serial};if(outcome==A::MicroOutcome::applied)r.after.raw_a=false;
    r.action={A::MicroKind::basic_camp,0,target,0,{1,0},r.before.epoch};
    r.acknowledged=true;return r;
}
static A::MicroRankChange delta(const A::MicroReceipt& receipt,G::NativeId before,G::NativeId after,float a,float b,bool dead=false){
    return {{receipt.before.epoch,A::MicroTargetKind::camp,before,0,a,0,true,false},
        {receipt.after.epoch,A::MicroTargetKind::camp,after,0,b,0,true,dead}};
}
static void ledger_separates_prevalidation_false_and_actual_effect(){
    A::MicroLedger ledger;auto refused=attempted(1,1,A::MicroOutcome::refused);
    refused.action.kind=A::MicroKind::move;CHECK(ledger.observe(refused));
    CHECK(ledger.raw_refusals()==1&&ledger.accepted_actions()==0&&!ledger.pending());
    auto pre=refused;pre.event_id=2;pre.serial=0;pre.outcome=A::MicroOutcome::not_attempted;pre.before=pre.after;pre.action.epoch=pre.before.epoch;
    CHECK(ledger.observe(pre));CHECK(ledger.prevalidation_rejections()==1&&ledger.last_serial()==1&&ledger.raw_refusals()==1);
    auto bad=attempted(3,2,A::MicroOutcome::refused);bad.after.raw_m=false;CHECK(!ledger.observe(bad));
    auto effect=attempted(3,2,A::MicroOutcome::applied);auto mismatch=delta(effect,0,7,100,40);effect.ranks={&mismatch,1};
    CHECK(ledger.observe(effect));CHECK(ledger.accepted_actions()==1&&ledger.work_removed()==0&&ledger.deaths()==0);
    auto rebound=attempted(4,3,A::MicroOutcome::applied,7);auto exact=delta(rebound,7,7,40,20);rebound.ranks={&exact,1};
    CHECK(ledger.observe(rebound));CHECK(ledger.work_removed()==20&&ledger.accepted_actions()==2&&ledger.deaths()==0);
    ++groups;
}
static void ledger_pending_resolution_counts_once_and_requires_exact_epoch(){
    A::MicroLedger ledger;auto pending=attempted(1,1,A::MicroOutcome::unknown);pending.acknowledged=false;
    pending.after.body_actual=false;CHECK(ledger.observe(pending));CHECK(ledger.pending()&&ledger.accepted_actions()==0);
    auto forbidden=attempted(2,2,A::MicroOutcome::applied);CHECK(!ledger.observe(forbidden));
    auto accepted=pending;accepted.event_id=2;accepted.outcome=A::MicroOutcome::applied;accepted.after.body_actual=true;accepted.after.raw_a=false;
    CHECK(ledger.observe(accepted));CHECK(ledger.pending()&&ledger.accepted_actions()==1&&ledger.acknowledged_actions()==0);
    accepted.event_id=3;accepted.acknowledged=true;auto killed=delta(accepted,0,0,60,0,true);std::array<A::MicroRankChange,2> duplicates{killed,killed};
    accepted.ranks=duplicates;CHECK(ledger.observe(accepted));
    CHECK(!ledger.pending()&&ledger.work_removed()==60&&ledger.deaths()==1&&ledger.acknowledged_actions()==1);
    accepted.event_id=4;CHECK(ledger.observe(accepted));CHECK(ledger.work_removed()==60&&ledger.deaths()==1&&ledger.accepted_actions()==1);
    CHECK(!ledger.observe(accepted));accepted.event_id=5;++accepted.after.epoch.attempt;CHECK(!ledger.observe(accepted));
    ++groups;
}
static void actual_heal_and_shield_effects_are_distinct_from_estimates(){
    A::MicroLedger ledger;auto restored=attempted(1,1,A::MicroOutcome::applied,0);
    restored.action.kind=A::MicroKind::cast_champion;restored.before.hp=60;restored.after.hp=75;
    restored.before.shield=5;restored.after.shield=30;
    auto ally=delta(restored,0,0,40,70);ally.before.kind=ally.after.kind=A::MicroTargetKind::champion;
    restored.ranks={&ally,1};CHECK(ledger.observe(restored));
    CHECK(ledger.healing_done()==45&&ledger.shield_added()==25&&ledger.work_removed()==0);
    restored.event_id=2;CHECK(ledger.observe(restored));CHECK(ledger.healing_done()==45&&ledger.shield_added()==25);
    ++groups;
}
static void late_known_ack_closes_history_without_cross_input_effects(){
    A::MicroLedger ledger;auto first=attempted(1,1,A::MicroOutcome::applied);
    auto damage=delta(first,0,0,100,80);first.ranks={&damage,1};CHECK(ledger.observe(first));
    auto known=attempted(2,2,A::MicroOutcome::applied);known.before.hp=60;known.after.body_actual=false;known.acknowledged=false;
    CHECK(ledger.observe(known));CHECK(ledger.pending()&&ledger.accepted_actions()==2&&ledger.work_removed()==20);
    auto fresh=body({11,1,0});fresh.hp=100;fresh.shield=80;fresh.raw_m=fresh.raw_a=fresh.raw_c=true;
    auto foreign=fresh;foreign.id=99;CHECK(!ledger.reconcile_ack(3,2,foreign));
    auto stale=fresh;stale.epoch={9,99,99};CHECK(!ledger.reconcile_ack(3,2,stale));
    CHECK(!ledger.reconcile_ack(3,1,fresh));fresh.body_actual=false;CHECK(!ledger.reconcile_ack(3,2,fresh));
    fresh.body_actual=true;CHECK(ledger.reconcile_ack(3,2,fresh));
    CHECK(!ledger.pending()&&ledger.accepted_actions()==2&&ledger.acknowledged_actions()==2);
    CHECK(ledger.work_removed()==20&&ledger.healing_done()==0&&ledger.shield_added()==0&&ledger.deaths()==0);
    CHECK(!ledger.reconcile_ack(4,2,fresh));CHECK(ledger.acknowledged_actions()==2);
    auto cross=known;cross.event_id=4;cross.after=fresh;cross.acknowledged=true;
    auto invented=delta(cross,0,0,100,0,true);cross.ranks={&invented,1};CHECK(!ledger.observe(cross));
    CHECK(ledger.work_removed()==20&&ledger.healing_done()==0&&ledger.shield_added()==0&&ledger.deaths()==0);
    // The next command has its own actual input and exact effect epoch.
    auto next=attempted(4,3,A::MicroOutcome::applied);next.before=fresh;next.after=fresh;
    next.after.epoch={11,2,1};next.after.raw_a=false;next.action.epoch=next.before.epoch;
    auto next_damage=delta(next,0,0,80,50);next.ranks={&next_damage,1};CHECK(ledger.observe(next));
    CHECK(ledger.accepted_actions()==3&&ledger.acknowledged_actions()==3&&ledger.work_removed()==50);
    ++groups;
}
static void late_refusal_ack_preserves_uncertainty_and_cannot_resolve_unknown(){
    A::MicroLedger refused;auto known=attempted(1,1,A::MicroOutcome::refused);known.action.kind=A::MicroKind::move;
    known.before.id=known.after.id=0;known.after.body_actual=false;known.acknowledged=false;
    CHECK(refused.observe(known));CHECK(refused.pending()&&refused.raw_refusals()==1&&refused.accepted_actions()==0);
    auto fresh=body({12,1,0});fresh.id=0;fresh.raw_m=fresh.raw_a=fresh.raw_c=false;
    CHECK(refused.reconcile_ack(2,1,fresh));
    CHECK(!refused.pending()&&refused.raw_refusals()==1&&refused.accepted_actions()==0&&refused.acknowledged_actions()==1);
    CHECK(refused.work_removed()==0&&refused.healing_done()==0&&refused.shield_added()==0);
    A::MicroLedger unknown;auto pending=attempted(1,1,A::MicroOutcome::unknown);pending.after.body_actual=false;pending.acknowledged=false;
    CHECK(unknown.observe(pending));fresh.id=pending.before.id;CHECK(!unknown.reconcile_ack(2,1,fresh));
    CHECK(unknown.pending()&&unknown.accepted_actions()==0&&unknown.raw_refusals()==0&&unknown.acknowledged_actions()==0);
    auto newer=attempted(2,2,A::MicroOutcome::applied);CHECK(!unknown.observe(newer));
    ++groups;
}

int main(){
    actual_basic_survives_optional_unknowns();actual_target_binding_and_no_vision_landing_requirement();
    damage_kill_rebinds_fresh_living_group_member();spent_attack_reset_and_actual_mac_reselection();
    melee_move_basic_and_basic_move_are_effect_dependent();exclusive_walk_and_ground_area_use_actual_affected_set();
    useful_heal_and_actual_self_target_mask();owner_start_shield_expiry_changes_real_usefulness();
    targeted_cc_and_conditional_dash();supported_candidate_bound_is_explicit();
    ledger_separates_prevalidation_false_and_actual_effect();ledger_pending_resolution_counts_once_and_requires_exact_epoch();
    actual_heal_and_shield_effects_are_distinct_from_estimates();
    late_known_ack_closes_history_without_cross_input_effects();late_refusal_ack_preserves_uncertainty_and_cannot_resolve_unknown();
    std::printf("PASS micro: %u checks, %u groups\n",checks,groups);
}
