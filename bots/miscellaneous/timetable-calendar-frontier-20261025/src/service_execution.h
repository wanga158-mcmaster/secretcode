#pragma once
// Bounded first-command refinement of the conditional service model. No SDK calls,
// epoch authority, dynamic occupancy, or fuel sufficiency are asserted here.
#include "service_model.h"

namespace mb::service {

inline constexpr std::size_t max_first_command_options = first_basic_limit + spell_slots;
static_assert(max_first_command_options <= 8);

enum class ExecutionStatus { Ready, NeedObservation, NoCurrentCommand, NoSafePrefix, NoWork, NeedPosition };
enum class ExecutionDisposition { Execute, Yield, Observe, Recover, WaitReward, Reposition };
// This is the shared caller decision. A spent allowance does not abandon residual
// work. Only rejection of every productive candidate's survival prefix recovers.
inline ExecutionDisposition execution_disposition(ExecutionStatus status)
{
    switch (status)
    {
        case ExecutionStatus::Ready: return ExecutionDisposition::Execute;
        case ExecutionStatus::NeedObservation: return ExecutionDisposition::Observe;
        case ExecutionStatus::NoCurrentCommand: return ExecutionDisposition::Yield;
        case ExecutionStatus::NoSafePrefix: return ExecutionDisposition::Recover;
        case ExecutionStatus::NoWork: return ExecutionDisposition::WaitReward;
        case ExecutionStatus::NeedPosition: return ExecutionDisposition::Reposition;
    }
    return ExecutionDisposition::Observe;
}
struct ExecutionWork
{
    std::size_t state_copies{}, basic_candidates{}, cast_candidates{};
    std::size_t transition_checks{}, volley_checks{}, productive_options{};
};
struct ExecutionChoice
{
    ExecutionStatus status{ExecutionStatus::NoCurrentCommand};
    Primitive primitive;
    // Utility-first choices include one simulated productive basic in this score.
    // next_volley always describes the returned first command's immediate prefix.
    float effective_damage{}, stopped_strength{};
    Volley next_volley;
    ExecutionWork work;
};

namespace execution_detail {
struct Score
{
    detail::Effect effect;
    float hp_damage{};
    bool immediate_damage{};
};

inline bool better(Score a, Score b)
{
    if (a.effect.stopped != b.effect.stopped) return a.effect.stopped > b.effect.stopped;
    if (a.effect.damage != b.effect.damage) return a.effect.damage > b.effect.damage;
    if (a.effect.kills != b.effect.kills) return a.effect.kills > b.effect.kills;
    // Preserve the sole cast when a direct productive command offers the same work.
    if (a.immediate_damage != b.immediate_damage) return a.immediate_damage;
    return a.hp_damage < b.hp_damage;
}

inline detail::Effect residual_effect(const State& before, const State& after)
{
    detail::Effect effect;
    for (std::size_t i = 0; i < before.member_count; ++i)
    {
        const auto& old = before.members[i];
        const auto& now = after.members[i];
        effect.damage += std::max(0.0f,old.hp-now.hp);
        effect.kills += old.hp > 0.0f && now.hp <= 0.0f;
        if (old.hp > 0.0f && old.stun_remaining <= 0 && (now.hp <= 0.0f || now.stun_remaining > 0) &&
            distance(before.body.pos,old.pos) <= old.range) effect.stopped += old.strength;
    }
    return effect;
}
} // namespace execution_detail

// Representative family, not an optimal sequence search: top four basic targets,
// then the best pointwise cast target/center for each ready slot. At most six State
// copies are made, one per first-command option; no Fragment vector is constructed.
// Shield/reset (or zero-damage control) first requires a productive legal basic in
// its resulting state. BOTH prefixes must retain strictly more HP than the reserve.
// The caller reobserves after this one command; simulated continuation is no authority
// to issue a second command. Candidate/center loops retain service_model's bounds.
// NoSafePrefix refers only to this representative family, not every possible plan.
// NoWork means no modeled living residual; it does not prove reward availability.
inline ExecutionChoice choose_first_command(const State& state, float escape_reserve)
{
    ExecutionChoice choice;
    if (state_status(state) != ModelStatus::ready || !nonnegative(escape_reserve))
    {
        choice.status = ExecutionStatus::NeedObservation;
        return choice;
    }
    bool has_work=false;
    for (std::size_t i=0;i<state.member_count;++i) has_work=has_work || state.members[i].hp>0.0f;
    if (!has_work)
    {
        choice.status=ExecutionStatus::NoWork;
        return choice;
    }
    execution_detail::Score best_score;
    bool incomplete_projection=false;
    const auto evaluate = [&](const Primitive& primitive, bool utility_first) {
        State option = state;
        ++choice.work.state_copies;
        ++choice.work.transition_checks;
        // One input validation above discharges the unchecked transition precondition.
        const bool applied = primitive.kind == PrimitiveKind::basic
            ? detail::apply_basic_unchecked(option,primitive.member_id)
            : detail::apply_cast_unchecked(option,primitive.slot,primitive.aim,primitive.member_id);
        if (!applied) return;
        ++choice.work.volley_checks;
        const auto first_volley = detail::volley_unchecked(option,true);
        bool safe_prefix=first_volley.complete && option.body.hp-first_volley.hp_damage>escape_reserve;
        const auto immediate = execution_detail::residual_effect(state,option);
        auto final_volley = first_volley;
        if (utility_first || immediate.damage <= 0.0f)
        {
            const auto continuation = detail::basic_targets(option,1);
            if (continuation.empty()) return;
            ++choice.work.transition_checks;
            if (!detail::apply_basic_unchecked(option,continuation.front())) return;
            ++choice.work.volley_checks;
            final_volley = detail::volley_unchecked(option,true);
            safe_prefix=safe_prefix && final_volley.complete &&
                option.body.hp-final_volley.hp_damage>escape_reserve;
        }
        const auto effect = execution_detail::residual_effect(state,option);
        if (!(effect.damage > 0.0f)) return;
        // Legality/productivity is established BEFORE HP/reserve rejection, even
        // when the returned utility command needs a following basic to do work.
        ++choice.work.productive_options;
        if (!std::isfinite(effect.damage) || !std::isfinite(effect.stopped) ||
            !first_volley.complete || !final_volley.complete)
        { incomplete_projection=true; return; }
        if (!safe_prefix) return;
        const execution_detail::Score score{effect,final_volley.hp_damage,immediate.damage > 0.0f};
        if (choice.status != ExecutionStatus::Ready || execution_detail::better(score,best_score))
        {
            choice.status = ExecutionStatus::Ready;
            choice.primitive = primitive;
            choice.effective_damage = effect.damage;
            choice.stopped_strength = effect.stopped;
            choice.next_volley = first_volley;
            best_score = score;
        }
    };
    const auto basics = detail::basic_targets(state,first_basic_limit);
    for (const int id : basics)
    {
        ++choice.work.basic_candidates;
        const int index = member_index(state,id);
        evaluate({PrimitiveKind::basic,id,-1,state.members[static_cast<std::size_t>(index)].pos},false);
    }
    for (std::size_t slot = 0; slot < spell_slots; ++slot)
    {
        const auto cast = detail::best_cast(state,static_cast<int>(slot));
        if (!cast) continue;
        ++choice.work.cast_candidates;
        const auto kind = state.spells[slot].kind;
        evaluate(*cast,kind == SpellKind::shield || kind == SpellKind::reset);
    }
    if (choice.status!=ExecutionStatus::Ready)
        choice.status=incomplete_projection ? ExecutionStatus::NeedObservation :
            choice.work.productive_options ? ExecutionStatus::NoSafePrefix : ExecutionStatus::NoCurrentCommand;
    if (choice.status==ExecutionStatus::NoCurrentCommand && state.body.can_attack &&
        !(state.body.ranged && state.body.moved_this_turn))
    {
        bool reachable_basic=false;
        for (std::size_t i=0;i<state.member_count;++i)
            reachable_basic|=state.members[i].hp>0 && distance(state.body.pos,state.members[i].pos)<=state.body.attack_range;
        if (!reachable_basic) choice.status=ExecutionStatus::NeedPosition;
    }
    return choice;
}

} // namespace mb::service
