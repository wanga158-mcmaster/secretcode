#pragma once
#include "organization.h"
#include <cmath>
#include <cstdint>
#include <memory>

// A conditional owned-income forecast, never a receipt that the enemy died or
// that its bag has transferred. Actual actions must reacquire their legal target.
namespace p6::runtime_intercept {
struct Result {
    ScoreEvaluation rank{};
    int target=-1;
};
namespace detail {
inline bool nonnegative(double value) { return std::isfinite(value) && value>=0; }
inline std::int64_t distance(Point a,Point b) {
    return std::max(std::abs(std::int64_t(a.x)-b.x),std::abs(std::int64_t(a.y)-b.y));
}
inline int stress_index(const organization::Input& in,int id) {
    int match=-1;
    for (int i=0; i<in.enemy_stress_count; ++i)
        if (in.enemy_stress[i].current(in.frame.clock,id)) {
            if (match>=0) return -1;
            match=i;
        }
    return match;
}
inline int cast_index(const forecast::Request& request,ViewClock clock,int id) {
    int match=-1;
    for (int i=0; i<request.enemy_cast_count; ++i)
        if (request.enemy_cast_stress[i].current(clock,id)) {
            if (match>=0) return -1;
            match=i;
        }
    return match;
}
}
inline Result quote(const organization::Input& in,const forecast::Request& bank_request,
                    Observation<double> target_effective_health,Fact<Gems> kill_gold) {
    Result unavailable;
    const auto& actual=in.frame;
    const auto& own=actual.own.actor;
    if (!actual.valid() || bank_request.shape!=forecast::Shape::bank_only ||
        bank_request.group_count!=0 || in.legal_enemy_target<0 ||
        !own.alive || own.channel!=Channel::none || !own.can_attack || own.attack_range<1 ||
        !detail::nonnegative(own.strength) || own.strength<=0 ||
        !detail::nonnegative(own.health) || !detail::nonnegative(own.max_health) ||
        kill_gold.source!=Source::queried_setting || kill_gold.value<0 ||
        in.enemy_stress_count<0 || in.enemy_stress_count>max_actors ||
        bank_request.enemy_cast_count<0 || bank_request.enemy_cast_count>max_actors ||
        !(in.bank_approach==bank_request.bank_position)) return unavailable;
    const auto* enemy=actual.visible(in.legal_enemy_target);
    if (!enemy || enemy->team!=Team::enemy || !enemy->alive || enemy->bag<=0 ||
        !detail::nonnegative(enemy->health) || enemy->health<=0 ||
        !target_effective_health.current(actual.clock,enemy->id) ||
        !detail::nonnegative(target_effective_health.value) ||
        target_effective_health.value<enemy->health ||
        detail::distance(own.position,enemy->position)>own.attack_range ||
        detail::distance(enemy->position,in.bank_approach)>6 ||
        !bank_request.ambient_incoming.current(actual.clock,own.id) ||
        !detail::nonnegative(bank_request.ambient_incoming.value)) return unavailable;
    const double rate=own.strength*(own.ranged?.5:1.);
    const double turns=std::ceil(target_effective_health.value/rate);
    if (!std::isfinite(turns) || turns<1 || turns>8) return unavailable;
    const int kill_turns=static_cast<int>(turns);

    // The target's entire visible stress is charged during every kill turn.
    // Match every represented enemy in both lists; missing entries never mean
    // zero, and capped-out bodies retain their caller-supplied ambient stress.
    std::array<bool,max_actors> used_stress{},used_cast{};
    double full=bank_request.ambient_incoming.value,removed=0;
    int enemies=0;
    for (int a=0; a<actual.actor_count; ++a) {
        const auto& observed=actual.actors[a];
        if (!observed.current(actual.clock,observed.subject) ||
            observed.value.team!=Team::enemy || !observed.value.alive) continue;
        const auto& value=observed.value;
        const int stress=detail::stress_index(in,value.id);
        const int cast=detail::cast_index(bank_request,actual.clock,value.id);
        if (stress<0 || cast<0 || !detail::nonnegative(value.strength) ||
            !detail::nonnegative(in.enemy_stress[stress].value) ||
            !detail::nonnegative(bank_request.enemy_cast_stress[cast].value) ||
            in.enemy_stress[stress].value<value.strength+bank_request.enemy_cast_stress[cast].value)
            return unavailable;
        used_stress[stress]=used_cast[cast]=true;
        full+=in.enemy_stress[stress].value;
        if (value.id==enemy->id) removed=in.enemy_stress[stress].value;
        ++enemies;
    }
    if (enemies>in.enemy_stress_count || enemies!=bank_request.enemy_cast_count ||
        !detail::nonnegative(full) || !detail::nonnegative(full-removed)) return unavailable;
    double omitted=0;
    for (int i=0; i<in.enemy_stress_count; ++i) if (!used_stress[i]) {
        const auto& stress=in.enemy_stress[i];
        if (stress.subject<0 || stress.subject==own.id ||
            !stress.current(actual.clock,stress.subject) || !detail::nonnegative(stress.value) ||
            actual.visible(stress.subject)) return unavailable;
        for (int j=0; j<i; ++j) if (in.enemy_stress[j].subject==stress.subject) return unavailable;
        omitted+=stress.value;
    }
    // The bounded Frame can omit personally visible enemies. The caller's
    // ambient bound already includes their full stress, retained after the kill.
    if (!detail::nonnegative(omitted) || omitted>bank_request.ambient_incoming.value) return unavailable;
    for (int i=0; i<bank_request.enemy_cast_count; ++i) if (!used_cast[i]) return unavailable;
    const double after=full-removed;
    const double injury=kill_turns*full;
    if (!detail::nonnegative(injury)) return unavailable;
    for (int tick=1; tick<=kill_turns; ++tick) {
        auto exposure=organization::local_exposure(own,full);
        exposure.incoming_total=tick*full;
        exposure.next_phase_incoming=tick==kill_turns?after:full;
        if (!survives(exposure)) return unavailable;
    }

    // Heap fixtures keep the bounded forecast builder within the default WASI
    // stack. Clock equality records current premises, not hypothetical receipts.
    auto frame=std::make_unique<Frame>(actual);
    auto request=std::make_unique<forecast::Request>(bank_request);
    frame->own.actor.health=own.health-injury;
    if (!add(own.bag,enemy->bag,frame->own.actor.bag) ||
        !add(actual.own.gold,kill_gold.value,frame->own.gold)) return unavailable;
    frame->actor_count=0;
    for (int a=0; a<actual.actor_count; ++a)
        if (actual.actors[a].subject!=enemy->id)
            frame->actors[frame->actor_count++]=actual.actors[a];
    request->enemy_cast_count=0;
    for (int a=0; a<frame->actor_count; ++a) {
        const auto& observed=frame->actors[a];
        if (!observed.current(actual.clock,observed.subject) ||
            observed.value.team!=Team::enemy || !observed.value.alive) continue;
        const int stress=detail::stress_index(in,observed.subject);
        const double cast=in.enemy_stress[stress].value-observed.value.strength;
        request->enemy_cast_stress[request->enemy_cast_count++]={cast,actual.clock,
            observed.subject,Knowledge::exact};
    }
    // No fresh heal, refreshed shield or future CC protection is manufactured.
    request->shield.enabled=false;
    auto model=std::make_unique<forecast::Result>(forecast::build(*frame,*request));
    if (model->status!=Status::feasible || model->schedule.actor_count!=1 ||
        model->schedule.production_count!=0) return unavailable;
    auto& certificate=model->certificate;
    for (auto* exposure:{&certificate.clear,&certificate.hold,&certificate.bank,&certificate.recovery}) {
        exposure->health=own.health;
        exposure->incoming_total+=injury;
        exposure->own_timed_shield=exposure->assigned_timed_heal=0;
    }
    if (!certificate.valid(actual,own.id,bank_request.project_group)) return unavailable;

    auto& schedule=model->schedule;
    auto& actor=schedule.actors[0];
    if (actor.actor_id!=own.id || actor.event_count>=max_actor_events) return unavailable;
    Gems occupied=0;
    if (!add(actor.occupied_turns.value,kill_turns,occupied)) return unavailable;
    actor.occupied_turns.value=occupied;
    actor.initial_bag={own.bag,Source::self_observed};
    actor.initial_gold={actual.own.gold,Source::self_observed};
    for (int e=actor.event_count; e>0; --e) {
        actor.events[e]=actor.events[e-1];
        actor.events[e].tick+=kill_turns;
        actor.events[e].event_id+=32;
    }
    ++actor.event_count;
    actor.events[0]={EventKind::production,1,kill_turns,0,{},-1,Source::scheduled};
    auto& payout=schedule.productions[0];
    payout.event_id=1; payout.tick=kill_turns;
    payout.gems={enemy->bag,Source::scheduled}; payout.gold={kill_gold.value,Source::scheduled};
    payout.holders[0]=own.id; payout.holder_count=1; payout.source=Source::scheduled;
    schedule.production_count=1; schedule.target_id=enemy->id;
    schedule.immediate_win={}; schedule.terminal_tiebreak={};
    const auto rank=evaluate_schedule(schedule,in.windows,in.settings,
        {actual.clock.round,own.id},actual.own_team_score);
    if (!rank.eligible()) return unavailable;
    for (auto denial:rank.denial) if (denial!=0) return unavailable;
    return {rank,enemy->id};
}
}
