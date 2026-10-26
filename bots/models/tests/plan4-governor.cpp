// Pure residual/milestone/forecast checks. No production-phase engine evidence.
#include "../governor/src/governor.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>

using namespace plan4;
using namespace plan4::governor;

namespace {
PhaseStart start(Phase5 phase, unsigned sequence, int nominal = 10, unsigned epoch = 0) {
    PhaseStart s;
    s.phase = phase; s.actor = 3; s.job_epoch = 1; s.phase_sequence = sequence;
    s.start_round = int(sequence) * 30; s.nominal_duration = nominal; s.mode = int(phase); s.stat_epoch = epoch;
    return s;
}
PhaseReceipt end(const PhaseStart& s, int duration) {
    PhaseReceipt r;
    r.phase = s.phase; r.actor = s.actor; r.job_epoch = s.job_epoch; r.phase_sequence = s.phase_sequence;
    r.start_round = s.start_round; r.end_round = s.start_round + duration;
    r.nominal_duration = s.nominal_duration; r.requested_mode = r.executed_mode = s.mode;
    r.stat_epoch = s.stat_epoch; r.completed = r.endpoint_observed = true;
    return r;
}
void complete(Governor& governor, Phase5 phase, unsigned seq, int nominal, int actual, unsigned epoch = 0) {
    const auto s = start(phase, seq, nominal, epoch);
    assert(governor.begin_phase(s));
    assert(governor.observe_phase_receipt(end(s, actual)) == ReceiptResult::learned);
}
bool close(double a, double b) { return std::abs(a - b) < 1e-10; }

void warmup_legal_waiting_and_disabled_collection() {
    Governor active, disabled(false);
    for (unsigned seq = 1; seq <= 3; ++seq) {
        const auto s = start(Phase5::travel, seq);
        auto r = end(s, 14);
        r.legal_wait_rounds = r.waiting_bound = 4;
        for (auto* g : {&active, &disabled}) {
            assert(g->begin_phase(s));
            assert(g->observe_phase_receipt(r) == ReceiptResult::learned);
        }
        assert(close(active.entry(Phase5::travel).residual, disabled.entry(Phase5::travel).residual));
        assert(disabled.quote_phase(Phase5::travel, 10, 1) == 10);
        if (seq < 3) assert(active.quote_phase(Phase5::travel, 10, 1) == 10);
    }
    assert(close(active.entry(Phase5::travel).residual, 2.3125));
    assert(active.quote_phase(Phase5::travel, 10, 1) == 13);
    assert(active.entry(Phase5::travel).completions == 3);
    assert(active.quote_phase(Phase5::travel, 10, 15) == 15);
    assert(active.quote_phase(Phase5::travel, 0, 0) == 0);
}

void disturbance_removal_decays_and_clips() {
    Governor model;
    for (unsigned seq = 1; seq <= 3; ++seq) complete(model, Phase5::clear, seq, 10, 100);
    assert(close(model.entry(Phase5::clear).residual, 5));
    assert(model.quote_phase(Phase5::clear, 4, 1) == 6); // Reclip to candidate d/2.
    double previous = model.entry(Phase5::clear).residual;
    for (unsigned seq = 4; seq <= 12; ++seq) {
        complete(model, Phase5::clear, seq, 10, 10);
        const double current = model.entry(Phase5::clear).residual;
        assert(close(current, previous * 0.75));
        assert(current >= 0 && current < previous);
        previous = current;
    }
    Governor faster;
    for (unsigned seq = 1; seq <= 10; ++seq) complete(faster, Phase5::clear, seq, 10, 0);
    assert(close(faster.entry(Phase5::clear).residual, -5));
    assert(faster.quote_phase(Phase5::clear, 2, 1) == 1);
    assert(faster.quote_phase(Phase5::clear, 10, 10) == 10);
}

void censors_and_mode_changes_never_train() {
    for (Censor reason : {Censor::interrupted, Censor::lost_sight, Censor::ambiguous_endpoint, Censor::game_end,
                         Censor::mode_change, Censor::safety, Censor::stat_change}) {
        Governor g;
        auto s = start(Phase5::capture, 1);
        auto r = end(s, 20); r.censor = reason;
        assert(g.begin_phase(s));
        assert(g.observe_phase_receipt(r) == ReceiptResult::censored);
        assert(g.entry(Phase5::capture).completions == 0 && g.entry(Phase5::capture).residual == 0);
    }
    for (int mutation = 0; mutation < 5; ++mutation) {
        Governor g;
        auto s = start(Phase5::travel, 1);
        auto r = end(s, 20);
        if (mutation == 0) r.completed = false;
        if (mutation == 1) r.endpoint_observed = false;
        if (mutation == 2) ++r.executed_mode;
        if (mutation == 3) { r.legal_wait_rounds = 5; r.waiting_bound = 4; }
        if (mutation == 4) { r.legal_wait_rounds = r.waiting_bound = 5; r.waiting_legal = false; }
        assert(g.begin_phase(s));
        assert(g.observe_phase_receipt(r) == ReceiptResult::censored);
        assert(g.entry(Phase5::travel).completions == 0);
    }
}

void duplicate_and_mismatched_endpoints() {
    Governor g;
    const auto s = start(Phase5::bank, 1);
    assert(g.begin_phase(s));
    assert(!g.begin_phase(s));
    for (int mutation = 0; mutation < 6; ++mutation) {
        auto r = end(s, 12);
        if (mutation == 0) ++r.actor;
        if (mutation == 1) ++r.job_epoch;
        if (mutation == 2) ++r.phase_sequence;
        if (mutation == 3) ++r.start_round;
        if (mutation == 4) ++r.nominal_duration;
        if (mutation == 5) ++r.stat_epoch;
        assert(g.observe_phase_receipt(r) == ReceiptResult::mismatch);
        assert(g.entry(Phase5::bank).pending && g.entry(Phase5::bank).completions == 0);
    }
    assert(g.observe_phase_receipt(end(s, 12)) == ReceiptResult::learned);
    assert(g.observe_phase_receipt(end(s, 12)) == ReceiptResult::duplicate_or_stale);
    assert(!g.begin_phase(s));
    auto next_job = start(Phase5::bank, 0); next_job.job_epoch = 2;
    assert(g.begin_phase(next_job)); // Sequence numbers may restart in a new job.
    assert(g.observe_phase_receipt(end(s, 12)) == ReceiptResult::mismatch);
    assert(g.observe_phase_receipt(end(next_job, 10)) == ReceiptResult::learned);
    assert(g.entry(Phase5::bank).completions == 2);
}

void safety_freezes_and_stats_reset_affected_evidence() {
    Governor g;
    auto s = start(Phase5::restore, 1);
    assert(g.begin_phase(s));
    g.set_emergency(true);
    g.set_emergency(false);
    assert(g.observe_phase_receipt(end(s, 20)) == ReceiptResult::censored);
    assert(g.entry(Phase5::restore).completions == 0);
    for (Phase5 phase : {Phase5::travel, Phase5::clear, Phase5::capture, Phase5::bank, Phase5::restore})
        for (unsigned seq = 2; seq <= 4; ++seq) complete(g, phase, seq, 10, 14);
    const double travel_bias = g.entry(Phase5::travel).residual;
    const auto in_flight = start(Phase5::clear, 5);
    assert(g.begin_phase(in_flight));
    g.on_stat_change(1, phase_mask(Phase5::clear));
    assert(g.entry(Phase5::clear).completions == 0 && g.entry(Phase5::clear).residual == 0);
    assert(g.entry(Phase5::travel).completions == 3 && close(g.entry(Phase5::travel).residual, travel_bias));
    assert(g.observe_phase_receipt(end(in_flight, 14)) == ReceiptResult::duplicate_or_stale);
    assert(!g.begin_phase(start(Phase5::clear, 6, 10, 0)));
    complete(g, Phase5::clear, 6, 10, 14, 1);
    g.on_stat_change(1);
    assert(g.entry(Phase5::clear).completions == 1); // Duplicate stat epoch is inert.
}

int grid_distance(Point a, Point b, const void*) { return std::max(std::abs(a.x-b.x), std::abs(a.y-b.y)); }
void base(Information& i) {
    i.round = 10; i.own.id = 3; i.own.position = {0, 0}; i.own_spawn = {0, 0};
    i.own.health = i.own.maximum_health = 1000; i.own.recall_safe = true;
    i.travel.distance = grid_distance; i.group_count = i.altar_count = 1;
    auto& group = i.groups[0];
    group.id = 101; group.position = {2, 0}; group.health = group.full_health = 100;
    group.clear_rounds = group.full_clear_rounds = 2; group.incoming_damage = group.full_incoming_damage = 10;
    group.gems = 20; group.respawn_rounds = 30; group.lifecycle = Lifecycle::living;
    group.visible = group.all_spawns_visible = group.reward_available = group.occupancy_known = true;
    group.provenance = group.health_provenance = {i.round, Source::personal_vision, Status::exact_now};
    i.altars[0].id = 1; i.altars[0].position = {5, 0}; i.altars[0].altar_position = {6, 0};
    i.altars[0].lock_known = true;
}
void common_forecast_hooks_preserve_control_and_couple_health() {
    auto input = std::make_unique<Information>(); base(*input);
    Job job; job.kind = JobKind::farm; job.group = 0; job.altar = 0;
    Menu menu;
    Governor active, disabled(false), warmup;
    auto ordinary = std::make_unique<Forecast>(), control = std::make_unique<Forecast>(), changed = std::make_unique<Forecast>();
    predict_into(*input, job, menu, *ordinary);
    predict_into(*input, job, menu, *control, warmup.hooks());
    assert(control->event_count == ordinary->event_count && control->final_health == ordinary->final_health);
    assert(control->first_conversion_round == ordinary->first_conversion_round && control->our_secured == ordinary->our_secured);
    for (unsigned seq = 1; seq <= 3; ++seq) {
        complete(active, Phase5::clear, seq, 2, 4);
        complete(disabled, Phase5::clear, seq, 2, 4);
    }
    predict_into(*input, job, menu, *control, disabled.hooks());
    predict_into(*input, job, menu, *changed, active.hooks());
    assert(control->first_conversion_round == ordinary->first_conversion_round && control->final_health == ordinary->final_health);
    assert(changed->first_conversion_round == ordinary->first_conversion_round + 1);
    assert(changed->final_health == ordinary->final_health - 10);
    assert(changed->our_secured == ordinary->our_secured && changed->final_bag == ordinary->final_bag);
    assert(close(active.entry(Phase5::clear).residual, disabled.entry(Phase5::clear).residual));
    assert(!active.hooks().supply_wait && !active.hooks().captured);
}
} // namespace

int main() {
    warmup_legal_waiting_and_disabled_collection();
    disturbance_removal_decays_and_clips();
    censors_and_mode_changes_never_train();
    duplicate_and_mismatched_endpoints();
    safety_freezes_and_stats_reset_affected_evidence();
    common_forecast_hooks_preserve_control_and_couple_health();
    std::puts("PLAN4_GOVERNOR {\"passed\":true,\"scenarios\":6,\"engineLaunches\":0,\"enginePhaseCalibration\":false}");
}
