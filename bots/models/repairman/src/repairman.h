#pragma once
#include "../../common/safe-segment.h"

namespace plan4::repairman {
inline constexpr const char* version = "plan4-repairman/3";
using safe_segment::max_escape_phases;
using safe_segment::Failure;
using safe_segment::Exposure;
using safe_segment::EscapeProof;
using safe_segment::AttackContext;
using safe_segment::SegmentQuote;
using safe_segment::valid_loss;
using safe_segment::work_available;
using safe_segment::retreat_boundary;
using safe_segment::recall_ticks;
using safe_segment::validate_escape;
using safe_segment::quote_segment;
using safe_segment::safe_segment_quote;
using safe_segment::SegmentToken;
using safe_segment::begin_segment;
using safe_segment::segment_still_valid;
using safe_segment::consumed_attack;
struct GroupObservation {
    int group_id = -1, round = -1;
    unsigned lifecycle_version = 0;
    Lifecycle lifecycle = Lifecycle::unknown;
    float health = 0;
    Provenance provenance{};
    bool visible = false, all_remaining_work_observed = false;
    bool new_visible_rival = false, emergency = false, escape_premise_valid = true;
};
struct WorkRecord {
    int group_id = -1, observed_round = -1;
    unsigned lifecycle_version = 0;
    float observed_health = 0, predicted_remaining = 0;
    Provenance observed_provenance{};
    bool valid = false, absent = false, return_verified = false, observation_current = false;
    float return_prediction_error = 0;
    Failure invalidation = Failure::none;
};
inline bool observe(WorkRecord& work, const GroupObservation& observation, int current_round) {
    if (observation.new_visible_rival || observation.emergency || !observation.escape_premise_valid) {
        work.valid = work.return_verified = work.observation_current = false;
        work.invalidation = observation.new_visible_rival ? Failure::rival : Failure::emergency; return false;
    }
    const auto& provenance = observation.provenance;
    if (!observation.visible || !observation.all_remaining_work_observed || observation.round != current_round ||
        provenance.observed_round != current_round || provenance.status != Status::exact_now ||
        provenance.source != Source::personal_vision || !valid_loss(observation.health)) {
        work.return_verified = work.observation_current = false; return false;
    }
    const bool new_lifecycle = work.group_id >= 0 && (work.group_id != observation.group_id ||
        work.lifecycle_version != observation.lifecycle_version || observation.lifecycle != Lifecycle::living);
    if (new_lifecycle) {
        work.valid = work.return_verified = work.observation_current = false; work.invalidation = Failure::new_lifecycle;
        work.predicted_remaining = 0; return false;
    }
    if (observation.lifecycle != Lifecycle::living) {
        work.valid = work.return_verified = work.observation_current = false; work.invalidation = Failure::new_lifecycle; return false;
    }
    work.group_id = observation.group_id; work.observed_round = current_round;
    work.lifecycle_version = observation.lifecycle_version;
    work.return_prediction_error = work.valid && work.absent ? observation.health - work.predicted_remaining : 0;
    work.observed_health = work.predicted_remaining = observation.health;
    work.observed_provenance = provenance;
    work.valid = work.return_verified = work.observation_current = true; work.absent = false; work.invalidation = Failure::none;
    return true;
}
inline void processed(WorkRecord& work, int attacks, float damage_per_attack) {
    if (!work.valid || attacks < 0 || attacks > 4 || !valid_loss(damage_per_attack)) return;
    work.predicted_remaining = std::max(0.0f, work.predicted_remaining - attacks * damage_per_attack);
    work.return_verified = work.observation_current = false;
}
inline void departed(WorkRecord& work) { work.absent = true; work.return_verified = work.observation_current = false; }
inline bool start_new_work(WorkRecord& work, const GroupObservation& observation, int current_round) {
    work = WorkRecord{};
    return observe(work, observation, current_round);
}
inline Provenance health_provenance(const WorkRecord& work, int current_round) {
    auto result = work.observed_provenance;
    if (!work.valid) result.status = Status::unknown;
    else if (work.absent || !work.observation_current || work.observed_round != current_round) result.status = Status::derived;
    return result;
}
inline bool return_attack_authorized(const WorkRecord& work, int current_round) {
    return work.valid && work.return_verified && work.observation_current && !work.absent && work.observed_round == current_round;
}

struct CycleInput {
    AttackContext actor{};
    EscapeProof escape{};
    Settings settings{};
    int round = 0, group_id = -1, remaining_targets = 1;
    float current_group_health = 0, damage_per_attack = 0;
    bool fresh_group_observation = false, group_work_is_attackable_target = false;
    int return_rounds = 0, return_phase_count = 0, capture_entry_rounds = 0, bank_rounds = 0;
    std::array<Exposure, max_horizon> returning{};
    bool return_route_supported = false, return_attackable = false, bank_route_supported = false;
    unsigned group_gems = 0, starting_bag = 0;
};
struct Excursion {
    int attacks = 0, segments = 0, start_round = 0, end_round = 0;
    float starting_health = 0, ending_health = 0, remaining_work = 0;
};
struct CycleForecast {
    std::array<Excursion, 2> excursions{};
    int excursion_count = 0, completion_round = -1, final_round = 0, renewal_rounds = 0;
    unsigned secured = 0, final_bag = 0;
    float remaining_work = 0, final_health = 0;
    bool completed_capture = false, completed_conversion = false;
    bool return_scheduled = false, requires_return_reobservation = false, return_attack_authorized = false;
    // A partial excursion ends at the same restored spawn in both arms.
    // Runtime continuation may use this endpoint, never the old attack tile.
    bool restored_at_spawn = false;
    Status work_status = Status::predicted;
    Failure failure = Failure::none;
};
// One remaining, already attackable target is the bounded v1 template. A
// target switch needs a new legal observation/admission, so it uses M0 instead.
inline CycleForecast predict_two_excursions(const CycleInput& input, bool coupling_enabled) {
    CycleForecast result;
    result.final_bag = input.starting_bag; result.remaining_work = input.current_group_health;
    result.final_health = input.actor.health; result.final_round = input.round;
    if (input.group_id < 0 || !input.fresh_group_observation || !input.group_work_is_attackable_target || input.remaining_targets != 1 ||
        input.actor.observed_round != input.round || !valid_loss(input.current_group_health) || input.current_group_health <= 0 ||
        !std::isfinite(input.damage_per_attack) || input.damage_per_attack <= 0 ||
        input.capture_entry_rounds < 0 || input.bank_rounds < 0 || !input.bank_route_supported) {
        result.failure = Failure::unsupported; return result;
    }
    const int deadline = input.round + std::max(0, std::min(max_horizon, input.settings.max_rounds - input.round));
    int now = input.round;
    float remaining = input.current_group_health, health = input.actor.health;
    auto advance = [&](int rounds) {
        if (!work_available(input.actor)) { result.failure = Failure::budget; return false; }
        if (rounds < 0 || rounds > deadline - now) { now = deadline; result.failure = Failure::horizon; return false; }
        now += rounds; return true;
    };
    for (int excursion = 0; excursion < 2; ++excursion) {
        result.restored_at_spawn = false;
        auto& trace = result.excursions[excursion];
        ++result.excursion_count; trace.start_round = now; trace.starting_health = health;
        for (int segment = 0; segment < max_horizon && remaining > 0; ++segment) {
            auto context = input.actor; context.health = health;
            const auto safe = safe_segment_quote(context, input.escape, input.settings);
            if (safe.failure != Failure::none) {
                if (!trace.attacks || safe.failure == Failure::budget) result.failure = safe.failure;
                break;
            }
            const int needed = std::max(1, int(std::min(4.0, std::ceil(double(remaining) / input.damage_per_attack))));
            const int attacks = std::min(safe.attacks, needed);
            if (!advance(attacks)) break;
            ++trace.segments; trace.attacks += attacks;
            remaining = std::max(0.0f, remaining - attacks * input.damage_per_attack);
            // Reassessment itself is not an extra attack phase. The extra
            // exposure reserved by every quote is charged only on escape.
            health -= attacks * context.predicted_incoming;
        }
        trace.end_round = now; trace.ending_health = health; trace.remaining_work = remaining;
        if (result.failure == Failure::horizon || result.failure == Failure::budget) break;
        if (remaining <= 0) {
            if (!advance(input.capture_entry_rounds)) break;
            const int capture_end = capture_completion_round(now, input.settings.capture_rounds);
            if (!advance(capture_end - now)) break;
            result.completed_capture = true;
            result.final_bag += input.group_gems;
            if (!advance(input.bank_rounds)) break;
            const int ticks = tribute_ticks(result.final_bag, input.settings);
            if (ticks && !advance(ticks)) break;
            if (ticks) {
                result.secured = result.final_bag; result.final_bag = 0;
                result.completed_conversion = true; result.completion_round = now;
            }
            result.failure = Failure::none; break;
        }
        if (!trace.attacks) break;
        auto context = input.actor; context.health = health;
        const auto escape = quote_segment(context, input.escape, input.settings, 0, false);
        if (escape.failure != Failure::none) { result.failure = escape.failure; break; }
        const int restoration_start = now;
        if (!advance(escape.elapsed_rounds)) break;
        health = escape.health_at_recall_arrival;
        const float healing = context.maximum_health * input.settings.spawn_heal_fraction;
        if (!std::isfinite(healing) || healing <= 0) { result.failure = Failure::unsupported; break; }
        const int heals = std::max(0, int(std::min(double(max_horizon + 1),
            std::ceil(double(context.maximum_health - health) / healing))));
        if (!advance(heals)) break;
        health = context.maximum_health;
        result.restored_at_spawn = true;
        result.renewal_rounds += now - restoration_start;
        // Ending the retained-work schedule changes only the return choice.
        // Its first safe escape/recall/heal is not free or omitted in control.
        if (!coupling_enabled || excursion == 1) { result.failure = Failure::unfinished; break; }
        if (!input.return_route_supported || !input.return_attackable || input.return_rounds < 0 ||
            input.return_rounds > max_horizon || input.return_phase_count != input.return_rounds) {
            result.failure = Failure::unsupported; break;
        }
        result.return_scheduled = result.requires_return_reobservation = true;
        const int return_start = now;
        result.restored_at_spawn = false;
        for (int phase = 0; phase < input.return_rounds; ++phase) {
            const auto& exposure = input.returning[phase];
            if (!exposure.known || !exposure.ordinary_action || !exposure.legal || !valid_loss(exposure.incoming)) {
                result.failure = Failure::unsupported; break;
            }
            if (!advance(1)) break;
            health -= exposure.incoming;
            if (health <= 0) { result.failure = Failure::unsafe_escape; break; }
        }
        if (result.failure != Failure::none) break;
        result.renewal_rounds += now - return_start;
        // The forecast predicts persistence; only observe() can authorize the
        // actual return attack. No hypothetical future sight is installed here.
    }
    result.final_round = now; result.final_health = health; result.remaining_work = remaining;
    return result;
}
} // namespace plan4::repairman
