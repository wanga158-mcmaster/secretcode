// Exact bookkeeping over explicitly supplied legal endpoints; no SDK traces.
#include "../common/phase-tracker.h"
#include "../governor/src/runtime.h"
#include <cassert>
#include <cstdio>

using namespace plan4;
using namespace plan4::runtime;
namespace {
PhaseStart request(ProductionPhase phase = ProductionPhase::travel, int round = 10, unsigned job = 1, unsigned sequence = 1) {
    PhaseStart s;
    s.phase = phase; s.actor = 3; s.job_epoch = job; s.phase_sequence = sequence;
    s.start_round = round; s.nominal_duration = 5; s.mode = 2; s.stat_epoch = 7;
    return s;
}
PhaseClock clock(const PhaseStart& s, int round = -1) {
    return {s.actor, round < 0 ? s.start_round : round, s.mode, s.stat_epoch, true, true, false, true};
}
EndpointKind witness(ProductionPhase phase) {
    switch (phase) {
        case ProductionPhase::travel: return EndpointKind::travel_arrival;
        case ProductionPhase::clear: return EndpointKind::group_clear;
        case ProductionPhase::capture: return EndpointKind::own_capture_receipt;
        case ProductionPhase::bank: return EndpointKind::own_tribute_resolution;
        case ProductionPhase::restore: return EndpointKind::own_restored;
    }
    return EndpointKind::none;
}
PhaseEndpoint endpoint(const PhaseStart& s, int round) {
    return {s, round, witness(s.phase), {round, Source::own_state, Status::exact_now}, false};
}
void progress(PhaseTracker& tracker, const PhaseStart& s, int through_round) {
    for (int round = s.start_round + 1; round <= through_round; ++round) {
        const auto update = tracker.observe_turn(clock(s, round));
        assert(update.result == TrackerResult::waiting && !update.has_start && !update.has_receipt);
    }
}
void incomplete(const PhaseTransition& t, TrackerCause cause, PhaseCensor censor) {
    assert(t.result == TrackerResult::censored && t.has_receipt && t.cause == cause);
    assert(!t.receipt.completed && !t.receipt.endpoint_observed && t.receipt.censor == censor);
}

void five_explicit_endpoints_and_exact_elapsed() {
    PhaseTracker tracker;
    for (unsigned phase = 0; phase < 5; ++phase) {
        const auto s = request(ProductionPhase(phase), 10 + int(phase) * 20, 1, phase + 1);
        const auto start = tracker.begin_phase(s, clock(s));
        assert(start.result == TrackerResult::started && start.has_start && !start.has_receipt);
        assert(tracker.observe_turn(clock(s)).result == TrackerResult::waiting);
        progress(tracker, s, s.start_round + 5);
        const auto done = tracker.finish(endpoint(s, s.start_round + 5), clock(s, s.start_round + 5));
        assert(done.result == TrackerResult::completed && done.has_receipt && !done.has_start);
        const auto& r = done.receipt;
        assert(r.completed && r.endpoint_observed && r.censor == PhaseCensor::none);
        assert(r.actor == 3 && r.phase == s.phase && r.job_epoch == 1 && r.phase_sequence == phase + 1);
        assert(r.start_round == s.start_round && r.end_round - r.start_round == 5 && r.nominal_duration == 5);
        assert(r.stat_epoch == 7 && r.requested_mode == 2 && r.executed_mode == 2 && !r.emergency);
        assert(!tracker.pending());
    }
}

void keyed_mismatches_and_stale_starts_cannot_mutate_pending() {
    PhaseTracker tracker; const auto s = request();
    assert(tracker.begin_phase(s, clock(s)).has_start);
    for (int field = 0; field < 9; ++field) {
        auto bad = endpoint(s, 11);
        switch (field) {
            case 0: bad.key.phase = ProductionPhase::clear; break;
            case 1: ++bad.key.actor; break;
            case 2: ++bad.key.job_epoch; break;
            case 3: ++bad.key.phase_sequence; break;
            case 4: ++bad.key.start_round; break;
            case 5: ++bad.key.nominal_duration; break;
            case 6: ++bad.key.mode; break;
            case 7: ++bad.key.stat_epoch; break;
            case 8: bad.key.emergency = true; break;
        }
        assert(tracker.finish(bad, clock(s, 11)).result == TrackerResult::mismatch);
        assert(tracker.pending() && tracker.requested().phase_sequence == 1 && tracker.last_observed_round() == 10);
    }
    auto next = request(ProductionPhase::clear, 10, 1, 2);
    assert(tracker.begin_phase(next, clock(next)).result == TrackerResult::already_pending);
    assert(tracker.begin_phase(s, clock(s)).result == TrackerResult::duplicate_or_stale);
    auto done = tracker.finish(endpoint(s, 11), clock(s, 11)); assert(done.receipt.completed);
    assert(tracker.finish(endpoint(s, 11), clock(s, 11)).result == TrackerResult::no_pending);
    assert(tracker.begin_phase(s, clock(s)).result == TrackerResult::duplicate_or_stale);
    next.start_round = 9;
    assert(tracker.begin_phase(next, clock(next)).result == TrackerResult::duplicate_or_stale);
    next = request(ProductionPhase::clear, 11, 2, 1); // Sequence restarts only with a new job.
    assert(tracker.begin_phase(next, clock(next)).has_start);
    const auto bad_actor = request(ProductionPhase::bank, 12, 3, 1);
    auto other = clock(bad_actor); ++other.actor;
    assert(tracker.observe_turn(other).result == TrackerResult::mismatch && tracker.pending());
}

void missing_owner_round_is_a_censor_not_a_duration_sample() {
    PhaseTracker tracker; const auto s = request(); tracker.begin_phase(s, clock(s));
    const auto missed = tracker.observe_turn(clock(s, 13));
    incomplete(missed, TrackerCause::missed_owner_round, PhaseCensor::lost_sight);
    assert(missed.receipt.start_round == 10 && missed.receipt.end_round == 13 && !tracker.pending());
    auto past = request(ProductionPhase::clear, 12, 2, 1);
    assert(tracker.begin_phase(past, clock(past)).result == TrackerResult::duplicate_or_stale);
}

void safety_death_stat_mode_visibility_censors() {
    for (int cause = 0; cause < 5; ++cause) {
        PhaseTracker tracker; const auto s = request(ProductionPhase::clear); tracker.begin_phase(s, clock(s));
        auto observed = clock(s, 11);
        switch (cause) {
            case 0: observed.alive = false; observed.mode = -1; break;
            case 1: observed.emergency = true; break;
            case 2: ++observed.stat_epoch; break;
            case 3: ++observed.mode; break;
            case 4: observed.premises_visible = false; break;
        }
        const auto event = tracker.observe_turn(observed);
        const TrackerCause causes[] = {TrackerCause::death, TrackerCause::safety, TrackerCause::stat_change,
            TrackerCause::mode_change, TrackerCause::lost_visibility};
        const PhaseCensor censors[] = {PhaseCensor::interrupted, PhaseCensor::safety, PhaseCensor::stat_change,
            PhaseCensor::mode_change, PhaseCensor::lost_sight};
        incomplete(event, causes[cause], censors[cause]);
        assert(event.receipt.stat_epoch == s.stat_epoch);
        assert(event.receipt.executed_mode == observed.mode);
        assert(!tracker.pending());
    }
    PhaseTracker emergency; auto s = request(); s.emergency = true;
    const auto start = emergency.begin_phase(s, clock(s));
    assert(start.has_start); incomplete(start, TrackerCause::safety, PhaseCensor::safety);
    assert(start.receipt.emergency && start.receipt.end_round == start.receipt.start_round);
    struct Sink {
        int calls = 0;
        bool phase_started(const PhaseStart&) { assert(calls++ == 0); return true; }
        void phase_completed(const PhaseReceipt& receipt) { assert(calls++ == 1 && !receipt.completed); }
    } sink;
    start.forward_to(sink); assert(sink.calls == 2);
}

void endpoint_kind_freshness_and_no_automatic_completion() {
    for (int flaw = 0; flaw < 5; ++flaw) {
        PhaseTracker tracker; const auto s = request(); tracker.begin_phase(s, clock(s));
        auto e = endpoint(s, 11);
        switch (flaw) {
            case 0: e.ambiguous = true; break;
            case 1: e.observation.observed_round = 10; break;
            case 2: e.observation.status = Status::predicted; break;
            case 3: e.observation.status = Status::derived; break;
            case 4: e.observation.source = Source::public_layout; break;
        }
        incomplete(tracker.finish(e, clock(s, 11)), TrackerCause::ambiguous_endpoint, PhaseCensor::ambiguous_endpoint);
    }
    PhaseTracker tracker; const auto s = request(ProductionPhase::bank); tracker.begin_phase(s, clock(s));
    progress(tracker, s, 20);
    auto e = endpoint(s, 20); e.kind = EndpointKind::none;
    assert(tracker.finish(e, clock(s, 20)).result == TrackerResult::waiting && tracker.pending());
    e.kind = EndpointKind::own_capture_receipt; // A capture is not a resolved own tribute.
    assert(tracker.finish(e, clock(s, 20)).result == TrackerResult::mismatch && tracker.pending());
    e = endpoint(s, 20); e.round = 19;
    assert(tracker.finish(e, clock(s, 20)).result == TrackerResult::mismatch && tracker.pending());
    e = endpoint(s, 20); e.observation.source = Source::own_occupancy;
    assert(tracker.finish(e, clock(s, 20)).receipt.completed);
}

void legal_waits_are_explicit_unique_bounded_and_inside_elapsed_time() {
    PhaseTracker tracker; const auto s = request(); tracker.begin_phase(s, clock(s), 4);
    for (int round = 10; round < 14; ++round) {
        assert(tracker.record_wait(s, clock(s, round), true).result == TrackerResult::wait_recorded);
        assert(tracker.record_wait(s, clock(s, round), true).result == TrackerResult::duplicate_or_stale);
    }
    const auto done = tracker.finish(endpoint(s, 14), clock(s, 14));
    assert(done.receipt.completed && done.receipt.legal_wait_rounds == 4 && done.receipt.waiting_bound == 4 && done.receipt.waiting_legal);
    PhaseTracker illegal; illegal.begin_phase(s, clock(s), 4);
    auto event = illegal.record_wait(s, clock(s), false);
    incomplete(event, TrackerCause::illegal_wait, PhaseCensor::interrupted); assert(!event.receipt.waiting_legal);
    PhaseTracker excessive; excessive.begin_phase(s, clock(s), 1);
    excessive.record_wait(s, clock(s), true);
    event = excessive.record_wait(s, clock(s, 11), true);
    incomplete(event, TrackerCause::wait_bound, PhaseCensor::interrupted); assert(!event.receipt.waiting_legal);
    PhaseTracker impossible; impossible.begin_phase(s, clock(s), 1); impossible.record_wait(s, clock(s), true);
    incomplete(impossible.finish(endpoint(s, 10), clock(s)), TrackerCause::illegal_wait, PhaseCensor::interrupted);
}

void stale_or_invalid_clocks_and_explicit_interruptions() {
    PhaseTracker tracker; const auto s = request(); tracker.begin_phase(s, clock(s));
    auto c = clock(s, 11); c.fresh = false;
    assert(tracker.observe_turn(c).result == TrackerResult::invalid && tracker.last_observed_round() == 10);
    c = clock(s, 9);
    assert(tracker.observe_turn(c).result == TrackerResult::duplicate_or_stale && tracker.pending());
    assert(tracker.censor_pending(PhaseCensor::none, clock(s)).result == TrackerResult::invalid);
    assert(tracker.censor_pending(PhaseCensor(99), clock(s)).result == TrackerResult::invalid);
    const auto ended = tracker.censor_pending(PhaseCensor::game_end, clock(s, 11));
    incomplete(ended, TrackerCause::game_end, PhaseCensor::game_end);
    assert(ended.receipt.end_round == 11 && !tracker.pending());
    auto invalid = request(); invalid.nominal_duration = 0;
    assert(tracker.begin_phase(invalid, clock(invalid)).result == TrackerResult::invalid);
    invalid = request(); invalid.phase = ProductionPhase(99);
    assert(tracker.begin_phase(invalid, clock(invalid)).result == TrackerResult::invalid);
}

void sink_governor_only_learns_completed_matching_samples() {
    PhaseTracker tracker; Governor model(true), control(false);
    for (unsigned n = 1; n <= 3; ++n) {
        auto s = request(ProductionPhase::travel, int(n) * 20, n, 1); s.nominal_duration = 10; s.stat_epoch = 0;
        auto start = tracker.begin_phase(s, clock(s), 4);
        assert(start.has_start && tracker.pending_key() && tracker.pending_key()->actor == s.actor);
        start.forward_to(model); start.forward_to(control);
        for (int round = s.start_round; round < s.start_round + 14; ++round) {
            tracker.observe_turn(clock(s, round));
            if (round >= s.start_round + 10) tracker.record_wait(s, clock(s, round), true);
        }
        auto done = tracker.finish(endpoint(s, s.start_round + 14), clock(s, s.start_round + 14));
        assert(done.receipt.completed && !tracker.pending_key()); done.forward_to(model); done.forward_to(control);
    }
    assert(model.state().entry(governor::Phase5::travel).completions == 3);
    assert(control.state().entry(governor::Phase5::travel).completions == 3);
    assert(model.state().quote_phase(governor::Phase5::travel, 10, 10) == 13);
    assert(control.state().quote_phase(governor::Phase5::travel, 10, 10) == 10);
    auto s = request(ProductionPhase::travel, 100, 4, 1); s.stat_epoch = 0;
    assert(tracker.begin_phase(s, clock(s)).has_start); assert(model.phase_started(s));
    auto c = clock(s, 101); c.emergency = true;
    const auto censored = tracker.observe_turn(c); model.phase_completed(censored.receipt);
    assert(model.state().entry(governor::Phase5::travel).completions == 3);
}
}
void physical_group_identity_is_part_of_phase_key() {
    PhaseTracker tracker;auto s=request(ProductionPhase::clear);s.group_id=71;
    assert(tracker.begin_phase(s,clock(s)).has_start);
    auto wrong=endpoint(s,s.start_round);wrong.key.group_id=72;
    assert(tracker.finish(wrong,clock(s)).result==TrackerResult::mismatch && tracker.pending());
    const auto completed=tracker.finish(endpoint(s,s.start_round),clock(s));
    assert(completed.receipt.completed && completed.receipt.group_id==71);
}
void known_death_censors_without_a_current_kit_sample() {
    PhaseTracker tracker; const auto s = request(ProductionPhase::restore);
    assert(tracker.begin_phase(s, clock(s)).has_start);
    auto dead = clock(s, 11); dead.alive = false; dead.fresh = false;
    auto wrong = dead; wrong.actor = 4;
    assert(tracker.observe_death(wrong).result == TrackerResult::mismatch && tracker.pending());
    wrong = dead; wrong.round = 9;
    assert(tracker.observe_death(wrong).result == TrackerResult::invalid && tracker.pending());
    wrong = dead; wrong.alive = true;
    assert(tracker.observe_death(wrong).result == TrackerResult::invalid && tracker.pending());
    const auto result = tracker.observe_death(dead);
    incomplete(result, TrackerCause::death, PhaseCensor::interrupted);
    assert(result.receipt.end_round == 11 && !tracker.pending());
    assert(tracker.observe_death(dead).result == TrackerResult::no_pending);
}

int main() {
    five_explicit_endpoints_and_exact_elapsed();
    keyed_mismatches_and_stale_starts_cannot_mutate_pending();
    missing_owner_round_is_a_censor_not_a_duration_sample();
    safety_death_stat_mode_visibility_censors();
    endpoint_kind_freshness_and_no_automatic_completion();
    legal_waits_are_explicit_unique_bounded_and_inside_elapsed_time();
    stale_or_invalid_clocks_and_explicit_interruptions();
    sink_governor_only_learns_completed_matching_samples();
    physical_group_identity_is_part_of_phase_key();
    known_death_censors_without_a_current_kit_sample();
    std::puts("plan4 phases: 10 pure scenarios passed (no engine witness)");
}
