#pragma once
#include "profile.h"
#include "../../common/src/accounting.h"
#include "../../common/src/execute.h"
#include <cmath>
#include <cstdint>

// One actual Ruin, two distinct live groups, one shield, no purchases or kills.
// Raw basic_modifier EXCLUDES Ruin's passive. No dash/support/respawn model.
namespace p5::collateral {
inline constexpr int step_cap = 6, event_cap = 16;
struct Group {
    int id = -1, gems = 0, gold = 0;
    bool current = false, available = false, no_other_holder = false;
    std::array<CombatMonster,8> monsters{};
    std::size_t count = 0;
};
struct Shield { int slot = -1, duration = 0; double modifier = 0; execution::Spell legal{}; };
struct Observation {
    int round = -1, team_score = 0;
    ActorObservation resources{}; CombatActor actor{};
    bool raw_modifier_excludes_passive = false, naturally_injured = false, must_recover = false;
    double focus = -1;
    Group target{}, renewal{};
    Shield shield{};
    RouteLeg current_to_group{}, current_to_altar{}, safe_exit{}, altar_to_spawn{}, spawn_to_group{},
        group_to_altar{}, spawn_to_renewal{}, renewal_to_altar{}, renewal_return{};
    bool bank_legal = false, safe_initial_bank = false, recall_safe = false;
    int recall_ticks = -1, spawn_rest = 1;
    double spawn_heal_per_tick = -1;
    EconomicSettings settings{}; FinishWindows windows{};
    execution::Observation execution{};
};
enum class StepKind { produce, bank, recover, renew };
struct Step {
    StepKind kind = StepKind::renew;
    int group = -1, waypoint = -1, duration = 0;
    double expected_health = 0;
    bool shield = false;
};
struct Quote {
    bool feasible = false, shielded = false;
    std::array<Step,step_cap> steps{}; int count = 0, occupied = 0, capture_tick = -1, bank_tick = -1;
    double final_health = 0, after_clear_health = 0;
    AccountingSchedule schedule{}; AccountingResult rank{};
};
enum class Job { unavailable, preserve, recover, bootstrap, shield_continue, ordinary, winning_bank };
struct Decision {
    Job job = Job::unavailable; bool feasible = false;
    Observation input{}; Quote selected{}, control{};
};
inline int turns(const RouteLeg& r) { return r.safety_checked ? r.turns() : -1; }
inline bool finite(double n) { return std::isfinite(n); }
inline bool stats_valid(const CombatActor& a) {
    return a.id >= 0 && a.self && a.visible && !a.ranged && a.enemy_cast_bound_known &&
        finite(a.health) && finite(a.max_health) && a.health > 0 && a.max_health >= a.health &&
        finite(a.strength) && a.strength > 0 && finite(a.basic_modifier) && a.basic_modifier > 0 &&
        finite(a.shield) && a.shield >= 0 && a.shield_expiry_tick >= 0 &&
        finite(a.visible_enemy_next_phase) && a.visible_enemy_next_phase >= 0;
}
inline bool reserve(const CombatActor& a) {
    return stats_valid(a) && a.health >= std::max(.2*a.max_health,2*a.visible_enemy_next_phase);
}
inline bool group_valid(const Group& g) {
    if (g.id < 0 || g.gems <= 0 || g.gold < 0 || !g.current || !g.available || !g.no_other_holder || g.count < 1 || g.count > 8) return false;
    for (std::size_t i=0;i<g.count;++i) {
        const auto& m=g.monsters[i];
        if (m.id<0 || !finite(m.public_health) || m.public_health<=0 || !finite(m.observed_health) ||
            !finite(m.effective_strength) || m.effective_strength<0 ||
            (m.visible && (m.observed_health<0 || m.observed_health>m.public_health))) return false;
        for (std::size_t j=0;j<i;++j) if (m.id==g.monsters[j].id) return false;
    }
    return true;
}
inline double bonus(const CombatActor& a) { return 1+.5*(1-a.health/a.max_health); }
// Constant per-phase exposure is segmented at exact shield expiry. It never
// converts unused protection into health and never credits an expired shield.
inline bool expose(CombatActor& a,int ticks,double per_tick) {
    if (!stats_valid(a) || ticks<0 || ticks>100000 || !finite(per_tick) || per_tick<0) return false;
    const int protected_ticks=std::min(ticks,a.shield_expiry_tick);
    const double protected_damage=protected_ticks*per_tick;
    const double absorbed=std::min(a.shield,protected_damage);
    a.health-=protected_damage-absorbed+(ticks-protected_ticks)*per_tick;
    a.shield-=absorbed; a.shield_expiry_tick=std::max(0,a.shield_expiry_tick-ticks);
    if (!a.shield_expiry_tick) a.shield=0;
    return reserve(a);
}
inline bool walk(CombatActor& a,const RouteLeg& r,int& tick,int named=-1) {
    const int n=turns(r);
    if (n<0 || (named>=0 && r.destination!=named) || tick>100000-n || !expose(a,n,a.visible_enemy_next_phase)) return false;
    tick+=n; return true;
}
inline double incoming(const Group& g) {
    double sum=0;
    for (std::size_t m=0;m<g.count;++m)
        if (!g.monsters[m].visible || g.monsters[m].observed_health>0) sum+=g.monsters[m].effective_strength;
    return sum;
}
inline bool escape_reserve(const Observation& o,const Group& g,const CombatActor& a) {
    // The named safe tile must be reachable within the two represented escape
    // phases; longer emergency routes are outside this small certificate.
    if (turns(o.safe_exit)<0 || o.safe_exit.distance>2 || !o.recall_safe) return false;
    auto worst=a;
    // One next camp phase, then two conservative exposed escape phases.
    return expose(worst,3,incoming(g)+a.visible_enemy_next_phase);
}
inline bool clear(const Group& g,CombatActor& a,int& tick,bool public_hp=false) {
    if (!group_valid(g) || !reserve(a)) return false;
    std::array<CombatMonster,8> active{}; std::size_t count=0;
    for (std::size_t m=0;m<g.count;++m) if (!g.monsters[m].visible || g.monsters[m].observed_health>0) active[count++]=g.monsters[m];
    const auto hp=[&](const CombatMonster& m){return public_hp || !m.visible?m.public_health:m.observed_health;};
    std::sort(active.begin(),active.begin()+count,[&](const auto& a0,const auto& b){return hp(a0)==hp(b)?a0.id<b.id:hp(a0)<hp(b);});
    // Damage can only raise the passive. This fixed entering-health multiplier
    // is a lower bound; any actual healing requires a fresh proof.
    const double damage=a.strength*a.basic_modifier*bonus(a);
    for (std::size_t m=0;m<count;++m) {
        const double raw=std::ceil(hp(active[m])/damage);
        if (!finite(raw) || raw<1 || raw>100000) return false;
        const int n=static_cast<int>(raw); double threat=a.visible_enemy_next_phase;
        for (std::size_t j=m;j<count;++j) threat+=active[j].effective_strength;
        if (tick>100000-n || !expose(a,n,threat)) return false;
        tick+=n;
        if (m+1<count) { if (!expose(a,2,threat-active[m].effective_strength)) return false; tick+=2; }
    }
    return true;
}
inline bool recover(const Observation& o,CombatActor& a,int& tick,const RouteLeg& route) {
    if (!o.recall_safe || o.recall_ticks<0 || o.recall_ticks>100000 || o.spawn_rest<1 || o.spawn_rest>100000 ||
        !finite(o.spawn_heal_per_tick) || o.spawn_heal_per_tick<0 || !walk(a,route,tick) ||
        !expose(a,o.recall_ticks,0)) return false;
    tick+=o.recall_ticks;
    if (!expose(a,o.spawn_rest,0)) return false;
    tick+=o.spawn_rest; a.health=std::min(a.max_health,a.health+o.spawn_rest*o.spawn_heal_per_tick);
    return reserve(a) && a.health>=.35*a.max_health;
}
inline bool renewal_certificate(const Observation& o,CombatActor a,int bag,int remaining=-1) {
    int t=0;
    if (!group_valid(o.renewal) || !clear(o.renewal,a,t,true) ||
        !expose(a,o.settings.capture_rounds+1,a.visible_enemy_next_phase) || !walk(a,o.renewal_to_altar,t)) return false;
    t+=o.settings.capture_rounds+1;
    const int channel=o.settings.channel_ticks(bag+o.renewal.gems);
    if(channel<1 || !expose(a,channel,a.visible_enemy_next_phase))return false;
    t+=channel;
    return walk(a,o.renewal_return,t) && (remaining<0 || t<=remaining);
}
inline bool add_step(Quote& q,const Step& s) {
    if(q.count>=step_cap)return false; q.steps[q.count++]=s; return true;
}
inline bool event(Quote& q,AccountingEventKind kind,int tick,int gems=0,int gold=0,int group=-1,int waypoint=-1) {
    if(q.schedule.event_count>=event_cap)return false;
    auto& e=q.schedule.events[q.schedule.event_count]; e.id=q.schedule.event_count++; e.actor=0; e.kind=kind;e.tick=tick;
    e.gems=gems;e.gold=gold;e.group=group;e.waypoint=waypoint;
    if(kind==AccountingEventKind::capture)e.participants=e.holders=1;
    return true;
}
inline bool bank(const Observation& o,Quote& q,CombatActor& a,int& tick,int& bag,const RouteLeg& route) {
    const int channel=o.settings.channel_ticks(bag); const int begin=tick;
    if(!o.bank_legal || channel<1 || !walk(a,route,tick) || !event(q,AccountingEventKind::tribute_start,tick,-1) ||
        !expose(a,channel,a.visible_enemy_next_phase))return false;
    tick+=channel;bag=0;q.bank_tick=tick;
    return event(q,AccountingEventKind::tribute_complete,tick,-1) && add_step(q,{StepKind::bank,-1,route.destination,tick-begin});
}
inline bool tail(const Observation& o,Quote& q,CombatActor& a,int& tick,int& bag,bool cast,bool at_group=false) {
    const int begin=tick;
    if(!at_group && !walk(a,o.current_to_group,tick,o.target.id))return false;
    if(cast) {
        a.shield=o.focus*o.shield.modifier;a.shield_expiry_tick=o.shield.duration;
        if(!finite(a.shield) || !escape_reserve(o,o.target,a) || !expose(a,1,incoming(o.target)+a.visible_enemy_next_phase))return false;
        ++tick;
    }
    if(!escape_reserve(o,o.target,a) || !clear(o.target,a,tick))return false;
    q.after_clear_health=a.health;
    if(!expose(a,o.settings.capture_rounds+1,a.visible_enemy_next_phase))return false;
    tick+=o.settings.capture_rounds+1;q.capture_tick=tick;
    if(!event(q,AccountingEventKind::capture,tick,o.target.gems,o.target.gold,o.target.id) ||
        !add_step(q,{StepKind::produce,o.target.id,o.target.id,tick-begin,0,cast}))return false;
    bag+=o.target.gems;
    if(!bank(o,q,a,tick,bag,o.group_to_altar))return false;
    const int recovery_start=tick;
    if(!recover(o,a,tick,o.altar_to_spawn) || !add_step(q,{StepKind::recover,-1,o.altar_to_spawn.destination,tick-recovery_start,a.health}))return false;
    const int renewal_start=tick;
    if(!walk(a,o.spawn_to_renewal,tick,o.renewal.id) || !renewal_certificate(o,a,bag,o.settings.round_limit-o.round-tick) ||
        !add_step(q,{StepKind::renew,o.renewal.id,o.renewal.id,tick-renewal_start}) ||
        !event(q,AccountingEventKind::return_to_work,tick,0,0,-1,o.renewal.id))return false;
    return true;
}
inline bool valid(const Observation& o) {
    const auto r=private_view(o.resources,o.round);
    return o.round>=0 && r.id==o.actor.id && r.self && r.visible && r.own_team && r.bag.current(o.round) && r.bag.value>=0 &&
        r.gold.current(o.round) && r.gold.value>=0 && stats_valid(o.actor) && o.raw_modifier_excludes_passive &&
        finite(o.focus) && o.focus>=0 && o.settings.valid() && o.settings.capture_rounds<100000 && o.windows.valid &&
        o.windows.official_remaining==o.settings.round_limit-o.round && o.execution.owner_turn==o.round &&
        o.execution.remaining_rounds==o.windows.official_remaining && o.team_score>=0 && group_valid(o.target) &&
        group_valid(o.renewal) && o.target.id!=o.renewal.id;
}
inline Quote quote(const Observation& o,bool cast) {
    Quote q;q.shielded=cast;
    if(!valid(o) || !reserve(o.actor))return q;
    q.schedule.actor_count=1;q.schedule.actors[0]={o.actor.id,true,true,true,o.resources.bag.value,o.resources.gold.value,0,0};
    q.schedule.target_id=o.target.id;q.schedule.starting_team_score=o.team_score;q.schedule.win_score=o.settings.winning_gems;
    q.schedule.tribute_gems_per_tick=o.settings.tribute_gems_per_tick;q.schedule.tribute_min_ticks=o.settings.tribute_min_ticks;
    auto a=o.actor;int tick=0,bag=o.resources.bag.value;
    if(cast) {
        if(!o.naturally_injured || o.must_recover || a.health>=.35*a.max_health || o.shield.slot!=0 || o.shield.duration<1 ||
            o.shield.duration>100000 || !finite(o.shield.modifier) || o.shield.modifier<=0 ||
            o.shield.legal.target!=a.id || o.execution.spells[0].target!=a.id ||
            !execution::spell_legal(o.execution,o.execution.spells[0]) || !execution::spell_legal(o.execution,o.shield.legal))return q;
    } else if(o.must_recover || a.health<.35*a.max_health) {
        if(bag>0 && o.safe_initial_bank && !bank(o,q,a,tick,bag,o.current_to_altar))return q;
        const int start=tick;
        if(!recover(o,a,tick,o.safe_exit) || !add_step(q,{StepKind::recover,-1,o.safe_exit.destination,tick-start,a.health}))return q;
    }
    auto continued=o;
    if(!cast && (o.must_recover || o.actor.health<.35*o.actor.max_health))continued.current_to_group=o.spawn_to_group;
    if(!tail(continued,q,a,tick,bag,cast) || tick>o.windows.official_remaining)return q;
    q.occupied=tick;q.final_health=a.health;q.schedule.actors[0].occupied_turns=tick;
    q.rank=forecast_schedule(q.schedule,o.windows);q.feasible=q.rank.status==AccountingStatus::feasible;return q;
}
inline Decision choose_job(const Observation& o,Profile profile={}) {
    Decision d;d.input=o;
    if(o.execution.channel!=execution::Channel::none && o.execution.channel_valid && o.execution.channel_safe) {d.job=Job::preserve;return d;}
    // A legal own immediate win is independent of remote/route metadata.
    if(o.resources.self && o.resources.bag.current(o.round) && o.resources.bag.value>0 && o.settings.valid() &&
        o.team_score>=0 && o.resources.bag.value>=o.settings.winning_gems-o.team_score && o.bank_legal &&
        o.execution.safe && o.execution.legal_winning_bank && o.execution.adjacent_altar) {d.job=Job::winning_bank;return d;}
    d.control=quote(o,false);d.selected=d.control;
    if(profile.arm==Arm::active) {auto active=quote(o,true);if(active.feasible && (!d.selected.feasible || compare_results(active.rank,d.selected.rank)>0))d.selected=active;}
    d.feasible=d.selected.feasible;
    if(d.feasible)d.job=d.selected.shielded?Job::shield_continue:o.resources.bag.value==0?Job::bootstrap:Job::ordinary;
    else if(stats_valid(o.actor) && (o.must_recover || o.actor.health<.35*o.actor.max_health))d.job=Job::recover;
    return d;
}
struct Facts {
    int round=-1,own_actor=-1,own_bag=-1,own_gold=-1;
    CombatActor own_stats{};double own_focus=-1;bool raw_modifier_excludes_passive=false;
    Group current_target{},current_renewal{};int group_round=-1,route_safety_round=-1;
    std::uint64_t capture_receipt=0,bank_receipt=0,recovery_receipt=0,shield_receipt=0,shield_cast_sequence=0;
    int buff_round=-1,recovery_round=-1;
    bool at_named_productive=false;int productive_group=-1;
    int navigation_goal=-1,navigation_round=-1; // fresh target-specific moves only
};
enum class Stage { running,reobserve,aborted,complete };
struct Project {
    Decision selected{};execution::State executor{};Stage stage=Stage::aborted;
    int step=0,bag=0,gold=0,arrival=-1,cast_turn=-1,bank_turn=-1,bank_ticks=0;
    std::uint64_t capture_base=0,bank_base=0,recovery_base=0,shield_base=0,pending_cast=0,bank_intent=0;
    bool initialized=false,shield_done=false;
};
inline Project start_project(const Decision& d,const Facts& f) {
    Project p;p.selected=d;
    if(d.job==Job::winning_bank || d.job==Job::recover || d.job==Job::preserve){
        if(f.round==d.input.round && f.own_actor==d.input.resources.id && f.own_bag>=0 && f.own_gold>=0)p.stage=Stage::running;
        return p;
    }
    if(!d.feasible || !d.selected.feasible || d.selected.count<1 || d.selected.count>step_cap || f.round!=d.input.round ||
        f.own_actor!=d.input.actor.id || f.own_bag!=d.input.resources.bag.value || f.own_gold!=d.input.resources.gold.value)return p;
    p.stage=Stage::running;return p;
}
inline bool fresh_tail_safe(const Project& p,const Facts& f,bool shield) {
    if(f.group_round!=f.round || f.route_safety_round!=f.round || f.current_target.id!=p.selected.input.target.id ||
        !stats_valid(f.own_stats) || !f.raw_modifier_excludes_passive || !finite(f.own_focus) || f.own_focus<0)return false;
    auto o=p.selected.input;o.round=f.round;o.target=f.current_target;o.focus=f.own_focus;
    o.renewal=f.current_renewal;if(o.renewal.id!=p.selected.input.renewal.id)return false;
    auto a=f.own_stats;Quote q;int t=0,bag=f.own_bag;
    return tail(o,q,a,t,bag,shield,true) && t<=o.settings.round_limit-f.round;
}
inline execution::Decision execute_job(execution::Observation o,const Facts& f,Project& p) {
    namespace e=execution;
    auto stop=[&](Stage s,e::Reason why){p.stage=s;return e::emit(p.executor,e::ActionKind::refresh,why);};
    if(p.stage!=Stage::running)return e::emit(p.executor,e::ActionKind::yield,e::Reason::illegal);
    const auto& d=p.selected;const auto& plan=d.selected;
    if(o.channel!=e::Channel::none)return e::decision(o,p.executor);
    if(d.job==Job::winning_bank || d.job==Job::recover || d.job==Job::preserve){
        if(f.round!=o.owner_turn || f.own_actor!=d.input.resources.id || f.own_bag<0 || f.own_gold<0)return stop(Stage::aborted,e::Reason::illegal);
        if(d.job==Job::preserve){p.stage=Stage::complete;return e::emit(p.executor,e::ActionKind::refresh);}
        if(!p.initialized){p.initialized=true;p.bank_base=f.bank_receipt;o.forecast_turns=8;e::enter(p.executor,d.job==Job::recover?e::Phase::recover:e::Phase::bank,o);}
        if(d.job==Job::recover)return e::decision(o,p.executor);
        if(p.bank_intent && f.bank_receipt>p.bank_base && f.own_bag==0 && o.owner_turn>=p.bank_turn+p.bank_ticks){p.stage=Stage::complete;return e::emit(p.executor,e::ActionKind::yield);}
        if(p.bank_intent){if(o.owner_turn>p.bank_turn+p.bank_ticks)return stop(Stage::reobserve,e::Reason::illegal);return e::emit(p.executor,e::ActionKind::refresh);}
        if(!o.safe || !o.adjacent_altar || !o.legal_winning_bank || o.altar_id!=d.input.execution.altar_id ||
            f.own_bag<d.input.settings.winning_gems-d.input.team_score)return stop(Stage::reobserve,e::Reason::unsafe);
        o.own_bag=f.own_bag;o.tribute_ticks_quote=d.input.settings.channel_ticks(f.own_bag);o.needs_recovery=false;
        auto out=e::decision(o,p.executor);
        if(out.action.kind==e::ActionKind::start_tribute){p.bank_intent=out.action.sequence;p.bank_turn=o.owner_turn;p.bank_ticks=out.action.channel_ticks;}
        return out;
    }
    if(f.round!=o.owner_turn || f.own_actor!=d.input.actor.id || f.own_stats.id!=f.own_actor || f.own_bag<0 || f.own_gold<0 ||
        p.step<0 || p.step>=plan.count || plan.count>step_cap)return stop(Stage::aborted,e::Reason::illegal);
    e::consume_feedback(o,p.executor);
    auto dispatch=[&](e::Decision out,int goal){
        if((out.action.kind==e::ActionKind::move || out.action.kind==e::ActionKind::vacate) &&
            (f.navigation_round!=f.round || f.navigation_goal!=goal))return e::emit(p.executor,e::ActionKind::refresh,e::Reason::unknown_target,goal);
        return out;
    };
    auto enter=[&](){const auto& s=plan.steps[p.step];o.forecast_turns=s.duration;
        e::enter(p.executor,s.kind==StepKind::produce?e::Phase::travel:s.kind==StepKind::bank?e::Phase::bank:
            s.kind==StepKind::recover?e::Phase::recover:e::Phase::renew,o);
        p.executor.target=s.group;p.executor.travel_next=e::Phase::clear;p.arrival=-1;p.bank_intent=0;p.pending_cast=0;p.shield_done=false;
        p.bag=f.own_bag;p.gold=f.own_gold;p.capture_base=f.capture_receipt;p.bank_base=f.bank_receipt;
        p.recovery_base=f.recovery_receipt;p.shield_base=f.shield_receipt;};
    if(!p.initialized){if(f.own_bag!=d.input.resources.bag.value || f.own_gold!=d.input.resources.gold.value)return stop(Stage::reobserve,e::Reason::illegal);p.initialized=true;enter();}
    for(int transition=0;transition<step_cap;++transition){
        const auto& s=plan.steps[p.step];
        if(!o.alive || !o.safe || o.remaining_rounds<=0)return stop(Stage::aborted,e::Reason::unsafe);
        if(o.owner_turn-p.executor.phase_start>=p.executor.phase_budget && p.executor.phase!=e::Phase::hold)return stop(Stage::aborted,e::Reason::timeout);
        bool next=false;
        if(s.kind==StepKind::produce && o.own_payout_observed){
            if((p.executor.phase!=e::Phase::clear && p.executor.phase!=e::Phase::hold) || o.own_payout_group!=s.group ||
                o.payout_receipt<=p.capture_base || f.capture_receipt!=o.payout_receipt ||
                f.own_bag!=p.bag+o.own_gems_received || f.own_gold!=p.gold+o.own_gold_received)return stop(Stage::reobserve,e::Reason::illegal);
            if(o.own_gems_received!=d.input.target.gems || o.own_gold_received!=d.input.target.gold)return stop(Stage::reobserve,e::Reason::payout);
            p.executor.consumed_payout=o.payout_receipt;next=true;
        } else if(s.kind==StepKind::bank && p.bank_intent && f.bank_receipt>p.bank_base && f.own_bag==0 &&
            o.owner_turn>=p.bank_turn+p.bank_ticks && f.own_gold==p.gold)next=true;
        else if(s.kind==StepKind::recover && o.at_spawn){
            if(p.arrival<0)p.arrival=o.owner_turn;
            if(o.owner_turn>p.arrival && o.spawn_recovery_observed && f.recovery_receipt>p.recovery_base && f.recovery_round==f.round){
                if(!reserve(f.own_stats) || f.own_stats.health<s.expected_health || f.own_bag!=p.bag || f.own_gold!=p.gold)return stop(Stage::reobserve,e::Reason::illegal);
                next=true;
            }
        }
        if(next){if(++p.step>=plan.count){p.stage=Stage::complete;return e::emit(p.executor,e::ActionKind::yield);}enter();continue;}
        if(s.kind==StepKind::produce){
            if(f.own_bag!=p.bag || f.own_gold!=p.gold)return stop(Stage::reobserve,e::Reason::illegal);
            if(p.executor.phase==e::Phase::hold && o.group_cleared && o.on_group_spawn && o.group_id==s.group){o.needs_recovery=false;o.helper=false;return e::decision(o,p.executor);}
            if(!o.at_target || o.group_id!=s.group)return dispatch(e::move_intent(o,p.executor),s.waypoint);
            if(p.pending_cast){
                if(o.feedback_sequence!=p.pending_cast || o.feedback_kind!=e::ActionKind::cast){
                    if(o.owner_turn>p.cast_turn)return stop(Stage::reobserve,e::Reason::illegal);
                    return e::emit(p.executor,e::ActionKind::refresh);
                }
                if(!o.feedback_success || o.feedback_target!=f.own_actor || f.shield_receipt<=p.shield_base ||
                    f.shield_cast_sequence!=p.pending_cast || f.buff_round!=f.round || f.own_stats.shield<=0 || f.own_stats.shield_expiry_tick<=0 ||
                    !fresh_tail_safe(p,f,false))return stop(Stage::reobserve,e::Reason::illegal);
                p.pending_cast=0;p.shield_done=true;
            }
            if(s.shield && !p.shield_done){
                const auto& spell=o.spells[0];
                if(spell.target!=f.own_actor || !e::spell_legal(o,spell) || !fresh_tail_safe(p,f,true))return stop(Stage::reobserve,e::Reason::illegal);
                auto action=e::emit(p.executor,e::ActionKind::cast,e::Reason::ordinary,f.own_actor,0);p.pending_cast=action.action.sequence;p.cast_turn=o.owner_turn;return action;
            }
            if(!fresh_tail_safe(p,f,false))return stop(Stage::reobserve,e::Reason::unsafe);
            bool member=false;for(std::size_t m=0;m<f.current_target.count;++m)member|=f.current_target.monsters[m].id==o.attack_target &&
                (!f.current_target.monsters[m].visible || f.current_target.monsters[m].observed_health>0);
            if(!o.group_cleared && !member)return stop(Stage::reobserve,e::Reason::illegal);
            o.needs_recovery=false;o.spells={};o.helper=false;return dispatch(e::decision(o,p.executor),s.waypoint);
        }
        if(s.kind==StepKind::bank){
            if(f.own_gold!=p.gold || f.own_bag!=p.bag)return stop(Stage::reobserve,e::Reason::illegal);
            if(p.bank_intent){if(o.owner_turn>p.bank_turn+p.bank_ticks)return stop(Stage::reobserve,e::Reason::illegal);return e::emit(p.executor,e::ActionKind::refresh);}
            o.bank_requested=true;o.own_bag=f.own_bag;o.tribute_ticks_quote=d.input.settings.channel_ticks(f.own_bag);o.needs_recovery=false;
            if(o.adjacent_altar && o.altar_id!=s.waypoint)return stop(Stage::reobserve,e::Reason::illegal);
            auto out=dispatch(e::decision(o,p.executor),s.waypoint);if(out.action.kind==e::ActionKind::start_tribute){p.bank_intent=out.action.sequence;p.bank_turn=o.owner_turn;p.bank_ticks=out.action.channel_ticks;}return out;
        }
        if(s.kind==StepKind::recover){
            if(o.at_spawn)return e::emit(p.executor,e::ActionKind::yield,e::Reason::arrival_recovery);
            o.needs_recovery=true;return dispatch(e::decision(o,p.executor),s.waypoint);
        }
        if(f.own_bag!=0 || f.own_gold!=p.gold || f.group_round!=f.round || f.route_safety_round!=f.round ||
            f.current_renewal.id!=s.group || !f.raw_modifier_excludes_passive)return stop(Stage::reobserve,e::Reason::illegal);
        if(f.at_named_productive && f.productive_group==s.group){
            auto proof=d.input;proof.renewal=f.current_renewal;
            if(o.on_group_spawn || !renewal_certificate(proof,f.own_stats,0,o.remaining_rounds))return stop(Stage::reobserve,e::Reason::unsafe);
            p.stage=Stage::complete;return e::emit(p.executor,e::ActionKind::yield);
        }
        return dispatch(e::move_intent(o,p.executor),s.waypoint);
    }
    return stop(Stage::aborted,e::Reason::timeout);
}
} // namespace p5::collateral
