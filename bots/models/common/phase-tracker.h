#pragma once
#include "model-runtime.h"
#include <algorithm>
#include <cstdint>

namespace plan4::runtime {

struct PhaseClock {
    int actor = -1, round = -1, mode = -1;
    std::uint32_t stat_epoch = 0;
    bool fresh = false, alive = false, emergency = false, premises_visible = false;
};
enum class EndpointKind { none, travel_arrival, group_clear, own_capture_receipt, own_tribute_resolution, own_restored };
struct PhaseEndpoint {
    PhaseStart key{};
    int round = -1;
    EndpointKind kind = EndpointKind::none;
    // Provenance of the fresh endpoint observation. The caller must establish
    // the named endpoint, including own capture attribution/channel resolution.
    // A bag change or a group's capture alone is not such an endpoint.
    Provenance observation{};
    bool ambiguous = false;
};
enum class TrackerResult { started, waiting, wait_recorded, completed, censored, no_pending,
    already_pending, duplicate_or_stale, mismatch, invalid };
enum class TrackerCause { none, death, safety, missed_owner_round, stat_change, mode_change,
    lost_visibility, ambiguous_endpoint, interrupted, game_end, illegal_wait, wait_bound };
struct PhaseTransition {
    TrackerResult result = TrackerResult::invalid;
    bool has_start = false;
    bool has_receipt = false;
    PhaseStart start{};
    PhaseReceipt receipt{};
    TrackerCause cause = TrackerCause::none;
    template<class Sink> void forward_to(Sink& sink) const {
        // An immediately censored request still has start-before-receipt order.
        if (has_start) (void)sink.phase_started(start);
        if (has_receipt) sink.phase_completed(receipt);
    }
};

// Actor-local, SDK-free bookkeeping for one actually requested production
// phase. A new phase never silently completes or overwrites the pending one.
// Callers forward a successful start and each emitted receipt to the model.
class PhaseTracker {
    PhaseStart pending_{};
    bool active_ = false, has_latest_ = false;
    int actor_ = -1, last_round_ = -1, last_wait_round_ = -1;
    int wait_rounds_ = 0, wait_bound_ = 0;
    std::uint64_t latest_job_ = 0, latest_sequence_ = 0;

    static bool valid_phase(ProductionPhase phase) { return unsigned(phase) <= unsigned(ProductionPhase::restore); }
    static EndpointKind expected(ProductionPhase phase) {
        switch (phase) {
            case ProductionPhase::travel: return EndpointKind::travel_arrival;
            case ProductionPhase::clear: return EndpointKind::group_clear;
            case ProductionPhase::capture: return EndpointKind::own_capture_receipt;
            case ProductionPhase::bank: return EndpointKind::own_tribute_resolution;
            case ProductionPhase::restore: return EndpointKind::own_restored;
        }
        return EndpointKind::none;
    }
    static bool valid_clock(const PhaseClock& clock) {
        return clock.actor >= 0 && clock.round >= 0 && clock.fresh;
    }
    bool same_key(const PhaseStart& key) const {
        return key.phase == pending_.phase && key.actor == pending_.actor && key.job_epoch == pending_.job_epoch &&
            key.phase_sequence == pending_.phase_sequence && key.start_round == pending_.start_round &&
            key.nominal_duration == pending_.nominal_duration && key.mode == pending_.mode &&
            key.stat_epoch == pending_.stat_epoch && key.emergency == pending_.emergency && key.group_id == pending_.group_id;
    }
    PhaseTransition close(const PhaseClock& clock, PhaseCensor censor, TrackerCause cause, bool completed = false) {
        PhaseTransition result;
        result.result = completed ? TrackerResult::completed : TrackerResult::censored;
        result.has_receipt = true; result.cause = cause;
        auto& receipt = result.receipt;
        receipt.phase = pending_.phase; receipt.actor = pending_.actor;
        receipt.group_id = pending_.group_id;
        receipt.job_epoch = pending_.job_epoch; receipt.phase_sequence = pending_.phase_sequence;
        receipt.start_round = pending_.start_round; receipt.end_round = clock.round;
        receipt.nominal_duration = pending_.nominal_duration;
        receipt.requested_mode = pending_.mode; receipt.executed_mode = clock.mode;
        receipt.stat_epoch = pending_.stat_epoch;
        receipt.completed = receipt.endpoint_observed = completed;
        receipt.emergency = pending_.emergency || clock.emergency; receipt.censor = censor;
        receipt.legal_wait_rounds = wait_rounds_; receipt.waiting_bound = wait_bound_;
        receipt.waiting_legal = cause != TrackerCause::illegal_wait && cause != TrackerCause::wait_bound;
        last_round_ = clock.round; active_ = false;
        return result;
    }
public:
    bool pending() const { return active_; }
    const PhaseStart* pending_key() const { return active_ ? &pending_ : nullptr; }
    const PhaseStart& requested() const { return pending_; }
    int last_observed_round() const { return last_round_; }
    int legal_wait_rounds() const { return wait_rounds_; }

    PhaseTransition begin_phase(const PhaseStart& start, const PhaseClock& clock, int waiting_bound = 0) {
        if (!valid_phase(start.phase) || start.actor < 0 || start.start_round < 0 || start.nominal_duration <= 0 ||
            start.mode < 0 || waiting_bound < 0 || !valid_clock(clock)) return {TrackerResult::invalid};
        if (start.actor != clock.actor || start.start_round != clock.round || start.mode != clock.mode ||
            start.stat_epoch != clock.stat_epoch || (actor_ >= 0 && start.actor != actor_)) return {TrackerResult::mismatch};
        if (has_latest_ && (start.job_epoch < latest_job_ ||
            (start.job_epoch == latest_job_ && start.phase_sequence <= latest_sequence_) ||
            clock.round < last_round_)) return {TrackerResult::duplicate_or_stale};
        if (active_) return {TrackerResult::already_pending};
        actor_ = start.actor; pending_ = start; active_ = has_latest_ = true;
        latest_job_ = start.job_epoch; latest_sequence_ = start.phase_sequence;
        last_round_ = clock.round; last_wait_round_ = -1; wait_rounds_ = 0; wait_bound_ = waiting_bound;
        // A requested emergency/dead/unobservable phase is still represented by
        // a start plus an incomplete receipt, never an eligible completion.
        auto result = observe_turn(clock); result.has_start = true; result.start = start;
        if (!result.has_receipt) result.result = TrackerResult::started;
        return result;
    }
    PhaseTransition observe_turn(const PhaseClock& clock) {
        if (!active_) return {TrackerResult::no_pending};
        if (!valid_clock(clock)) return {TrackerResult::invalid};
        if (clock.actor != actor_) return {TrackerResult::mismatch};
        if (clock.round < last_round_) return {TrackerResult::duplicate_or_stale};
        if (!clock.alive) return close(clock, PhaseCensor::interrupted, TrackerCause::death);
        if (clock.emergency || pending_.emergency) return close(clock, PhaseCensor::safety, TrackerCause::safety);
        if (clock.stat_epoch != pending_.stat_epoch) return close(clock, PhaseCensor::stat_change, TrackerCause::stat_change);
        if (clock.mode < 0) return close(clock, PhaseCensor::ambiguous_endpoint, TrackerCause::ambiguous_endpoint);
        if (clock.mode != pending_.mode) return close(clock, PhaseCensor::mode_change, TrackerCause::mode_change);
        if (std::int64_t(clock.round) > std::int64_t(last_round_) + 1)
            return close(clock, PhaseCensor::lost_sight, TrackerCause::missed_owner_round);
        if (!clock.premises_visible) return close(clock, PhaseCensor::lost_sight, TrackerCause::lost_visibility);
        last_round_ = clock.round;
        return {TrackerResult::waiting};
    }
    PhaseTransition record_wait(const PhaseStart& key, const PhaseClock& clock, bool legal) {
        if (!active_) return {TrackerResult::no_pending};
        if (!same_key(key)) return {TrackerResult::mismatch};
        const auto observed = observe_turn(clock);
        if (observed.has_receipt || observed.result == TrackerResult::invalid || observed.result == TrackerResult::mismatch ||
            observed.result == TrackerResult::duplicate_or_stale) return observed;
        if (!legal) return close(clock, PhaseCensor::interrupted, TrackerCause::illegal_wait);
        if (last_wait_round_ == clock.round) return {TrackerResult::duplicate_or_stale};
        if (wait_rounds_ >= wait_bound_) return close(clock, PhaseCensor::interrupted, TrackerCause::wait_bound);
        ++wait_rounds_; last_wait_round_ = clock.round;
        return {TrackerResult::wait_recorded};
    }
    PhaseTransition finish(const PhaseEndpoint& endpoint, const PhaseClock& clock) {
        if (!active_) return {TrackerResult::no_pending};
        if (!same_key(endpoint.key)) return {TrackerResult::mismatch};
        if (endpoint.round != clock.round || endpoint.round < pending_.start_round) return {TrackerResult::mismatch};
        const auto observed = observe_turn(clock);
        if (observed.has_receipt || observed.result == TrackerResult::invalid || observed.result == TrackerResult::mismatch ||
            observed.result == TrackerResult::duplicate_or_stale) return observed;
        if (endpoint.ambiguous) return close(clock, PhaseCensor::ambiguous_endpoint, TrackerCause::ambiguous_endpoint);
        if (endpoint.kind == EndpointKind::none) return {TrackerResult::waiting};
        if (endpoint.kind != expected(pending_.phase)) return {TrackerResult::mismatch};
        const auto& proof = endpoint.observation;
        if (proof.observed_round != endpoint.round || proof.status != Status::exact_now ||
            (proof.source != Source::own_state && proof.source != Source::own_occupancy && proof.source != Source::personal_vision))
            return close(clock, PhaseCensor::ambiguous_endpoint, TrackerCause::ambiguous_endpoint);
        if (std::int64_t(wait_rounds_) > std::int64_t(endpoint.round) - pending_.start_round)
            return close(clock, PhaseCensor::interrupted, TrackerCause::illegal_wait);
        return close(clock, PhaseCensor::none, TrackerCause::none, true);
    }
    // Observed own death invalidates the phase even when no current combat-kit
    // sample exists. This can only censor, never manufacture an endpoint.
    PhaseTransition observe_death(const PhaseClock& clock) {
        if (!active_) return {TrackerResult::no_pending};
        if (clock.alive || clock.actor<0 || clock.round<0 || clock.round<last_round_) return {TrackerResult::invalid};
        if (clock.actor!=actor_) return {TrackerResult::mismatch};
        return close(clock,PhaseCensor::interrupted,TrackerCause::death);
    }
    PhaseTransition censor_pending(PhaseCensor censor, const PhaseClock& clock) {
        if (!active_) return {TrackerResult::no_pending};
        if (censor == PhaseCensor::none || unsigned(censor) > unsigned(PhaseCensor::stat_change)) return {TrackerResult::invalid};
        if (!valid_clock(clock) || clock.round < last_round_) return {TrackerResult::invalid};
        if (clock.actor != actor_) return {TrackerResult::mismatch};
        TrackerCause cause = TrackerCause::interrupted;
        switch (censor) {
            case PhaseCensor::none: break;
            case PhaseCensor::interrupted: break;
            case PhaseCensor::lost_sight: cause = TrackerCause::lost_visibility; break;
            case PhaseCensor::ambiguous_endpoint: cause = TrackerCause::ambiguous_endpoint; break;
            case PhaseCensor::game_end: cause = TrackerCause::game_end; break;
            case PhaseCensor::mode_change: cause = TrackerCause::mode_change; break;
            case PhaseCensor::safety: cause = TrackerCause::safety; break;
            case PhaseCensor::stat_change: cause = TrackerCause::stat_change; break;
        }
        return close(clock, censor, cause);
    }
};

} // namespace plan4::runtime
