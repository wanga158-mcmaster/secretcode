// Pure attempt lifecycle bookkeeping: explicit observations, no SDK or engine.
#include "../common/attempts.h"
#include <cassert>
#include <cstdio>
#include <type_traits>

using namespace plan4::attempts;

namespace {
Observation own(int round, int bag = 20) {
    Observation sample;
    sample.actor = 7; sample.round = round; sample.own = sample.fresh = true;
    sample.alive_known = sample.alive = true;
    sample.position_known = sample.bag_known = sample.hp_known = sample.max_hp_known = true;
    sample.x = 4; sample.y = 8; sample.bag = bag;
    sample.hp_bits = 0x42700000u; sample.max_hp_bits = 0x42c80000u; // 60.0f, 100.0f
    return sample;
}
Identity bank(std::uint64_t token, Origin origin = Origin::bootstrap) {
    Identity key;
    key.kind = Kind::bank; key.origin = origin; key.job_token = token; key.altar_id = 2;
    return key;
}
Identity camp(std::uint64_t token) {
    Identity key;
    key.kind = Kind::one_camp; key.origin = Origin::planned; key.job_token = token;
    key.group_id = 13; key.altar_id = 2;
    return key;
}
Event take(Recorder& recorder) {
    assert(recorder.front());
    const Event event = *recorder.front(); recorder.pop(); return event;
}
void assert_end(const Event& event, std::uint64_t attempt, Outcome outcome, bool has_end) {
    assert(event.type == EventType::end && event.attempt_id == attempt);
    assert(event.outcome == outcome && event.has_end == has_end);
    if (!has_end) assert(event.end.actor == -1 && event.end.round == -1 && !event.end.alive_known);
}
PreactionEvidence evidence(int round) {
    PreactionEvidence value;
    value.actor = 7; value.round = round;
    value.champion = 5; value.ability0 = 1; value.ability1 = 4; value.attack_range = 3;
    value.strength_bits = 0x42718000u; value.focus_bits = 0x41c90000u; // 60.375f, 25.125f
    value.item_count = 2; value.items[0] = {20, 3}; value.items[1] = {4, 1};
    value.composition_count = 3; value.composition[0] = 4; value.composition[1] = 2; value.composition[2] = 4;
    value.context = 0; value.travel = 8; value.nominal_duration = 27;
    value.kit_known = value.composition_known = value.context_known = true;
    value.travel_known = value.nominal_duration_known = true;
    return value;
}
void assert_unknown(const PreactionEvidence& value) {
    assert(!value.kit_known && !value.composition_known && !value.context_known);
    assert(!value.travel_known && !value.nominal_duration_known);
    assert(value.champion == -1 && value.ability0 == -1 && value.ability1 == -1 && value.attack_range == -1);
    assert(value.strength_bits == 0 && value.focus_bits == 0 && value.item_count == 0);
    assert(value.composition_count == 0 && value.context == -1 && value.travel == -1 && value.nominal_duration == 0);
}
void assert_original(const PreactionEvidence& value, int round) {
    assert(value.actor == 7 && value.round == round);
    assert(value.kit_known && value.composition_known && value.context_known);
    assert(value.travel_known && value.nominal_duration_known);
    assert(value.champion == 5 && value.ability0 == 1 && value.ability1 == 4 && value.attack_range == 3);
    assert(value.strength_bits == 0x42718000u && value.focus_bits == 0x41c90000u);
    assert(value.item_count == 2 && value.items[0].kind == 4 && value.items[0].count == 1);
    assert(value.items[1].kind == 20 && value.items[1].count == 3);
    assert(value.composition_count == 3 && value.composition[0] == 2 && value.composition[1] == 4 && value.composition[2] == 4);
    assert(value.context == 0 && value.travel == 8 && value.nominal_duration == 27);
}

void first_preaction_is_immutable_and_terminal_retries_do_not_reopen() {
    Recorder recorder;
    const auto key = camp(801);
    assert(!recorder.same_attempt(key));
    auto source = evidence(130);
    assert(recorder.begin(key, own(130), source) && recorder.same_attempt(key));
    source.actor = 9; source.round = 131; source.items[0].count = 12; source.composition[0] = 30;
    source.strength_bits = 0; source.context = 1; source.travel = 100; source.nominal_duration = 200;
    assert(!recorder.begin(key, own(131), source));
    const Event begin = take(recorder);
    assert_original(begin.preaction, 130);
    assert(recorder.finish(Outcome::completed, own(131)));
    const Event end = take(recorder);
    assert_original(end.preaction, 130);
    assert(recorder.same_attempt(key) && !recorder.active());
    assert(!recorder.begin(key, own(132), evidence(132)) && recorder.pending() == 0);
    assert(!recorder.same_attempt(camp(802)));
    assert(recorder.begin(camp(802), own(132), evidence(132)));
    assert_original(take(recorder).preaction, 132);
    assert(!recorder.same_attempt(key) && recorder.same_attempt(camp(802)));

    Recorder missing;
    assert(missing.begin(bank(803), own(140)));
    assert(!missing.begin(bank(803), own(141), evidence(141)));
    assert_unknown(take(missing).preaction);
    assert(missing.finish(Outcome::aborted, own(141)));
    assert_unknown(take(missing).preaction);
    assert(!missing.begin(bank(803), own(142), evidence(142)));
    assert(missing.same_attempt(bank(803)) && !missing.front());
}

void mismatched_preaction_is_unknown_and_raw_bits_are_exact() {
    for (int variant = 0; variant < 5; ++variant) {
        Recorder recorder;
        auto source = evidence(150);
        if (variant == 0) source.actor = 8;
        if (variant == 1) source.round = 149;
        if (variant == 2) source.round = 151;
        if (variant == 3) source.actor = -1;
        if (variant == 4) source.round = -1;
        assert(recorder.begin(camp(810 + variant), own(150), source));
        const auto start = take(recorder);
        assert_unknown(start.preaction);
        assert(start.preaction.actor == -1 && start.preaction.round == -1);
        assert(recorder.finish(Outcome::completed, own(151)));
        assert_unknown(take(recorder).preaction);
    }
    Recorder exact;
    auto raw = evidence(160);
    raw.strength_bits = 0x42718001u; raw.focus_bits = 0x80000000u; // Last mantissa bit and signed zero remain exact.
    assert(exact.begin(camp(820), own(160), raw));
    auto start = take(exact);
    assert(start.preaction.kit_known && start.preaction.strength_bits == raw.strength_bits && start.preaction.focus_bits == raw.focus_bits);
    assert(exact.finish(Outcome::aborted, own(161)));
    auto end = take(exact);
    assert(end.preaction.kit_known && end.preaction.strength_bits == raw.strength_bits && end.preaction.focus_bits == raw.focus_bits);
    raw = evidence(162); raw.travel = 0; raw.nominal_duration = 1; raw.context = 1;
    raw.item_count = raw.composition_count = 0; // Known empty inventory/bank composition is legal.
    assert(exact.begin(bank(821), own(162), raw));
    start = take(exact);
    assert(start.preaction.kit_known && start.preaction.item_count == 0);
    assert(start.preaction.composition_known && start.preaction.composition_count == 0);
    assert(start.preaction.context_known && start.preaction.context == 1);
    assert(start.preaction.travel_known && start.preaction.travel == 0);
    assert(start.preaction.nominal_duration_known && start.preaction.nominal_duration == 1);
}

void sdk3_champion_range_keeps_raw_kit_and_independent_unknowns() {
    for (int champion = 0; champion <= 12; ++champion) {
        auto source = evidence(165);
        source.champion = champion; // SDK3 Heart of Ruin is the physical kind 12.
        source.strength_bits = 0x42718001u; source.focus_bits = 0x80000000u;
        source.composition_known = source.context_known = false;
        source.travel_known = source.nominal_duration_known = false;
        const auto checked = freeze_preaction(source, 7, 165);
        assert(checked.kit_known && checked.champion == champion);
        assert(checked.ability0 == 1 && checked.ability1 == 4 && checked.attack_range == 3);
        assert(checked.strength_bits == source.strength_bits && checked.focus_bits == source.focus_bits);
        assert(checked.item_count == 2 && checked.items[0].kind == 4 && checked.items[0].count == 1);
        assert(checked.items[1].kind == 20 && checked.items[1].count == 3);
        assert(!checked.composition_known && checked.composition_count == 0);
        assert(!checked.context_known && checked.context == -1);
        assert(!checked.travel_known && checked.travel == -1);
        assert(!checked.nominal_duration_known && checked.nominal_duration == 0);
    }
    const int out_of_range[] = {13, 14, 0x7fffffff};
    for (int champion : out_of_range) {
        auto source = evidence(166);
        source.champion = champion;
        const auto checked = freeze_preaction(source, 7, 166);
        assert(!checked.kit_known && checked.champion == -1 && checked.item_count == 0);
        assert(checked.strength_bits == 0 && checked.focus_bits == 0);
        assert(checked.composition_known && checked.context_known);
        assert(checked.travel_known && checked.nominal_duration_known);
    }
}

void preaction_capacity_and_category_validation_are_bounded() {
    Recorder full;
    auto source = evidence(170);
    source.item_count = PreactionEvidence::item_capacity;
    source.composition_count = PreactionEvidence::composition_capacity;
    for (int n = 0; n < source.item_count; ++n) {
        source.items[n] = {source.item_count - n, ~0u};
        source.composition[n] = (source.composition_count - 1 - n) / 7;
    }
    assert(full.begin(camp(830), own(170), source));
    const auto start = take(full);
    assert(start.preaction.kit_known && start.preaction.item_count == 32);
    assert(start.preaction.composition_known && start.preaction.composition_count == 32);
    for (int n = 0; n < 32; ++n) {
        assert(start.preaction.items[n].kind == n + 1 && start.preaction.items[n].count == ~0u);
        assert(start.preaction.composition[n] == n / 7);
    }
    for (int variant = 0; variant < 22; ++variant) {
        Recorder invalid;
        auto value = evidence(180);
        if (variant == 0) value.item_count = 33;
        if (variant == 1) value.item_count = -1;
        if (variant == 2) value.items[1].kind = value.items[0].kind;
        if (variant == 3) value.items[0].count = 0;
        if (variant == 4) value.items[0].kind = -1;
        if (variant == 5) value.champion = -1;
        if (variant == 6) value.ability0 = -1;
        if (variant == 7) value.ability1 = -1;
        if (variant == 8) value.attack_range = -1;
        if (variant == 9) value.kit_known = false;
        if (variant == 10) value.composition_count = 33;
        if (variant == 11) value.composition_count = -1;
        if (variant == 12) value.composition[0] = -1;
        if (variant == 13) value.composition_known = false;
        if (variant == 14) value.champion = 13;
        if (variant == 15) value.ability0 = 8;
        if (variant == 16) value.ability1 = 8;
        if (variant == 17) value.items[0].kind = 42;
        if (variant == 18) value.strength_bits = 0x7fc12345u;
        if (variant == 19) value.focus_bits = 0x7f800000u;
        if (variant == 20) value.strength_bits = 0xbf800000u;
        if (variant == 21) value.composition[0] = 5;
        assert(invalid.begin(camp(840 + variant), own(180), value));
        const auto checked = take(invalid).preaction;
        if (variant < 10 || (variant >= 14 && variant <= 20)) {
            assert(!checked.kit_known && checked.item_count == 0 && checked.champion == -1);
            assert(checked.strength_bits == 0 && checked.focus_bits == 0 && checked.composition_known);
        } else assert(checked.kit_known && !checked.composition_known && checked.composition_count == 0);
        assert(checked.context_known && checked.travel_known && checked.nominal_duration_known);
    }
    auto invalid_scalars = evidence(190);
    invalid_scalars.context = 2; invalid_scalars.travel = -1; invalid_scalars.nominal_duration = 0;
    auto checked = freeze_preaction(invalid_scalars, 7, 190);
    assert(checked.kit_known && checked.composition_known);
    assert(!checked.context_known && checked.context == -1);
    assert(!checked.travel_known && checked.travel == -1);
    assert(!checked.nominal_duration_known && checked.nominal_duration == 0);
    invalid_scalars = evidence(190);
    invalid_scalars.kit_known = invalid_scalars.composition_known = invalid_scalars.context_known = false;
    invalid_scalars.travel_known = invalid_scalars.nominal_duration_known = false;
    checked = freeze_preaction(invalid_scalars, 7, 190);
    assert_unknown(checked);
    assert(checked.items[0].kind == -1 && checked.items[0].count == 0 && checked.composition[0] == 0);
}

void bootstrap_bank_is_one_actual_attempt() {
    Recorder recorder;
    const auto key = bank(101);
    assert(recorder.begin(key, own(10)));
    assert(recorder.active() && recorder.active_attempt_id() == 1);
    assert(recorder.active_identity() && *recorder.active_identity() == key);
    assert(!recorder.begin(key, own(11)) && !recorder.begin(key, own(12)));
    assert(recorder.pending() == 1);
    const Event start = take(recorder);
    assert(start.type == EventType::begin && start.event_id == 1 && start.attempt_id == 1);
    assert(start.identity.origin == Origin::bootstrap && start.identity.kind == Kind::bank);
    assert(start.identity.group_id == -1 && start.identity.altar_id == 2);
    assert(start.start.actor == 7 && start.start.round == 10 && start.start.bag == 20);
    // A lower bag alone is not evidence that tribute completed.
    assert(recorder.observe(own(13, 0)) && recorder.active() && !recorder.front());
    assert(recorder.finish(Outcome::completed, own(13, 0)));
    const Event end = take(recorder);
    assert_end(end, 1, Outcome::completed, true);
    assert(end.event_id == 2 && end.start.round == 10 && end.end.round == 13);
    assert(end.end.bag == 0 && end.terminal_round == 13);
    assert(!recorder.active() && !recorder.active_identity() && recorder.active_attempt_id() == 0);
    assert(!recorder.finish(Outcome::completed, own(13, 0)));
    assert(!recorder.begin(key, own(14)) && recorder.pending() == 0);
}

void replacement_orders_end_before_start_and_abort_once() {
    Recorder recorder;
    const auto first = camp(201); auto replacement = first; replacement.group_id = 14;
    assert(recorder.begin(first, own(20)));
    assert(recorder.begin(replacement, own(21)));
    const Event begin1 = take(recorder), end1 = take(recorder), begin2 = take(recorder);
    assert(begin1.event_id == 1 && begin1.attempt_id == 1);
    assert_end(end1, 1, Outcome::replaced, true);
    assert(end1.event_id == 2 && end1.identity.group_id == 13 && end1.end.round == 21);
    assert(begin2.event_id == 3 && begin2.attempt_id == 2 && begin2.identity.group_id == 14);
    assert(recorder.finish(Outcome::aborted, own(21)));
    assert_end(take(recorder), 2, Outcome::aborted, true);
    assert(!recorder.finish(Outcome::aborted, own(22)) && !recorder.front());
    // Physical target, origin and kind all belong to the identity.
    auto legacy = replacement; legacy.origin = Origin::legacy;
    assert(recorder.begin(legacy, own(22)));
    auto other_altar = legacy; other_altar.altar_id = 3;
    assert(recorder.begin(other_altar, own(22)));
    auto other_kind = other_altar; other_kind.kind = Kind::bank;
    assert(recorder.begin(other_kind, own(22)));
    assert(recorder.active_attempt_id() == 5);
}

void death_is_observed_and_gaps_censor_success() {
    Recorder death;
    assert(death.begin(camp(301), own(30)));
    auto dead = own(31); dead.alive = false; dead.hp_bits = 0;
    assert(death.observe(dead));
    (void)take(death);
    const Event end = take(death);
    assert_end(end, 1, Outcome::death, true);
    assert(end.end.alive_known && !end.end.alive && end.end.hp_known && end.end.hp_bits == 0);
    assert(!death.finish(Outcome::completed, own(31)) && !death.front());
    assert(!death.begin(camp(302), dead));

    Recorder dead_dispatch;
    assert(dead_dispatch.begin(bank(306), own(30)));
    assert(!dead_dispatch.begin(bank(306), dead));
    (void)take(dead_dispatch);
    assert_end(take(dead_dispatch), 1, Outcome::death, true);

    Recorder precedence;
    assert(precedence.begin(bank(307), own(28)));
    assert(precedence.observe(dead)); // A gap makes the intervening path unknown.
    (void)take(precedence);
    assert_end(take(precedence), 1, Outcome::observation_gap, false);

    Recorder gap;
    assert(gap.begin(camp(303), own(40)));
    assert(!gap.finish(Outcome::completed, own(42, 0)));
    (void)take(gap);
    const Event censored = take(gap);
    assert_end(censored, 1, Outcome::observation_gap, false);
    assert(censored.terminal_round == 42 && !gap.active());
    assert(!gap.begin(camp(303), own(43)) && !gap.front());
    // Replacement after a gap preserves the censor instead of calling the old
    // macro replaced/completed; the new invocation still enters the cohort.
    assert(gap.begin(bank(304), own(45)));
    assert(gap.begin(camp(305), own(47)));
    (void)take(gap);
    assert_end(take(gap), 2, Outcome::observation_gap, false);
    const Event new_start = take(gap);
    assert(new_start.type == EventType::begin && new_start.attempt_id == 3);
}

void stale_foreign_and_invalid_samples_do_not_become_endpoints() {
    Recorder recorder;
    assert(recorder.begin(camp(401), own(50)));
    assert(recorder.observe(own(51)));
    auto sample = own(50); sample.alive = false;
    assert(!recorder.observe(sample) && !recorder.finish(Outcome::completed, sample));
    sample = own(52); sample.fresh = false;
    assert(!recorder.observe(sample) && !recorder.finish(Outcome::completed, sample));
    sample = own(52); sample.own = false;
    assert(!recorder.observe(sample) && !recorder.finish(Outcome::completed, sample));
    sample = own(52); sample.actor = 8;
    assert(!recorder.observe(sample) && !recorder.finish(Outcome::completed, sample));
    sample = own(52); sample.round = -1;
    assert(!recorder.observe(sample) && !recorder.finish(Outcome::completed, sample));
    assert(recorder.active() && recorder.pending() == 1);
    assert(recorder.finish(Outcome::completed, own(52)));
    (void)take(recorder);
    const Event end = take(recorder);
    assert_end(end, 1, Outcome::completed, true);
    assert(end.end.actor == 7 && end.end.round == 52);

    Recorder missing;
    assert(missing.begin(bank(402), own(60)));
    sample = own(61); sample.fresh = false;
    assert(!missing.observe(sample));
    assert(!missing.finish(Outcome::completed, own(62)));
    (void)take(missing);
    assert_end(take(missing), 1, Outcome::observation_gap, false);
    Recorder invalid;
    auto key = bank(403); key.group_id = -2;
    assert(!invalid.begin(key, own(70)));
    key = bank(403); key.kind = Kind(99);
    assert(!invalid.begin(key, own(70)));
    key = bank(403); key.origin = Origin(99);
    assert(!invalid.begin(key, own(70)) && !invalid.front());
    assert(invalid.begin(bank(403), own(70)));
    assert(!invalid.finish(Outcome(99), own(70)) && invalid.active());
}

void unknown_fields_and_missing_alive_are_explicit() {
    Recorder recorder;
    auto unknown = own(70);
    unknown.alive_known = unknown.position_known = unknown.bag_known = false;
    unknown.hp_known = unknown.max_hp_known = false;
    assert(recorder.begin(bank(501, Origin::legacy), unknown));
    const Event start = take(recorder);
    assert(!start.start.alive_known && !start.start.alive);
    assert(!start.start.position_known && start.start.x == -1 && start.start.y == -1);
    assert(!start.start.bag_known && start.start.bag == -1);
    assert(!start.start.hp_known && start.start.hp_bits == 0);
    assert(!start.start.max_hp_known && start.start.max_hp_bits == 0);
    assert(recorder.observe(unknown) && !recorder.active()); // Unknown is a censor, never death.
    assert(!recorder.finish(Outcome::completed, unknown));
    assert_end(take(recorder), 1, Outcome::unknown, false);

    // Unknown economic/body fields do not invent data or prevent recording an
    // explicitly established completion with a known-alive own endpoint.
    unknown = own(71); unknown.position_known = unknown.bag_known = false;
    unknown.hp_known = unknown.max_hp_known = false;
    assert(recorder.begin(bank(502), unknown));
    assert(recorder.finish(Outcome::completed, unknown));
    (void)take(recorder);
    const Event end = take(recorder);
    assert_end(end, 2, Outcome::completed, true);
    assert(!end.end.bag_known && end.end.bag == -1 && !end.end.hp_known && end.end.hp_bits == 0);

    Recorder invalid_values;
    auto partial = own(80); partial.x = -3; partial.bag = -1;
    partial.hp_bits = 0xc0800000u; partial.max_hp_bits = 0; // Retain explicitly known anomalous health.
    assert(invalid_values.begin(bank(503), partial));
    const Event sanitized = take(invalid_values);
    assert(!sanitized.start.position_known && !sanitized.start.bag_known);
    assert(sanitized.start.hp_known && sanitized.start.hp_bits == 0xc0800000u);
    assert(sanitized.start.max_hp_known && sanitized.start.max_hp_bits == 0);
    // A caller's abort cause remains known when its body endpoint is absent.
    partial.alive_known = false;
    assert(invalid_values.finish(Outcome::aborted, partial));
    assert_end(take(invalid_values), 1, Outcome::aborted, false);

    Recorder fractions;
    auto fractional = own(81);
    fractional.hp_bits = 0x42718000u; fractional.max_hp_bits = 0x42c88000u; // 60.375f, 100.25f
    assert(fractions.begin(bank(504), fractional));
    assert(take(fractions).start.hp_bits == 0x42718000u);
    assert(fractions.finish(Outcome::completed, fractional));
    const Event exact = take(fractions);
    assert(exact.end.hp_known && exact.end.hp_bits == 0x42718000u);
    assert(exact.end.max_hp_known && exact.end.max_hp_bits == 0x42c88000u);
    assert(fractions.begin(bank(505), own(82)));
    assert(!fractions.finish(Outcome::death, own(82)) && fractions.active());
    assert(fractions.finish(Outcome::unknown, own(82)));
    (void)take(fractions);
    assert_end(take(fractions), 2, Outcome::unknown, false);

    Recorder zero_health;
    auto zero = own(83); zero.hp_bits = 0;
    assert(zero_health.begin(bank(506), zero));
    assert(zero_health.observe(zero) && zero_health.active()); // Health cannot infer death.
    assert(zero_health.finish(Outcome::completed, zero));
    (void)take(zero_health);
    assert_end(take(zero_health), 1, Outcome::completed, true);

    Recorder unknown_start;
    unknown = own(84); unknown.alive_known = false;
    assert(unknown_start.begin(bank(507), unknown));
    assert(!unknown_start.finish(Outcome::completed, own(85)));
    (void)take(unknown_start);
    assert_end(take(unknown_start), 1, Outcome::unknown, false);

    Recorder bridged_death;
    assert(bridged_death.begin(bank(508), own(90)));
    for (int round = 91; round <= 150; ++round) {
        auto unseen_alive = own(round); unseen_alive.alive_known = false;
        assert(bridged_death.observe(unseen_alive));
    }
    assert(!bridged_death.finish(Outcome::completed, own(151)));
    (void)take(bridged_death);
    const Event censored_path = take(bridged_death);
    assert_end(censored_path, 1, Outcome::unknown, false);
    assert(censored_path.terminal_round == 91 && !bridged_death.front());
}

void bounded_overflow_is_visible_and_counters_do_not_reset() {
    Recorder recorder;
    static_assert(Recorder::capacity == 8);
    for (unsigned i = 0; i < 5; ++i) {
        assert(recorder.begin(bank(600 + i), own(90 + int(i))));
        assert(recorder.finish(Outcome::completed, own(90 + int(i), 0)));
    }
    assert(recorder.pending() == 8 && recorder.dropped_events() == 2);
    for (unsigned i = 1; i <= 8; ++i) {
        const Event event = take(recorder);
        assert(event.event_id == i && event.attempt_id == (i + 1) / 2);
        assert(event.dropped_events == 2);
    }
    recorder.pop(); // Empty drain is harmless.
    assert(!recorder.front() && recorder.pending() == 0 && recorder.dropped_events() == 2);
    assert(recorder.begin(bank(605), own(95)));
    const Event after_loss = take(recorder);
    assert(after_loss.event_id == 11 && after_loss.attempt_id == 6 && after_loss.dropped_events == 2);
    assert(recorder.finish(Outcome::aborted, own(95)));
    const Event end = take(recorder);
    assert(end.event_id == 12 && end.dropped_events == 2);
    // Exercise ring wrap with immediate drains, without resetting the IDs.
    for (unsigned i = 0; i < 24; ++i) {
        assert(recorder.begin(bank(606 + i), own(96 + int(i))));
        const Event next = take(recorder);
        assert(next.event_id == 13 + 2 * i && next.attempt_id == 7 + i && next.dropped_events == 2);
        assert(recorder.finish(Outcome::completed, own(96 + int(i), 0)));
        assert(take(recorder).event_id == 14 + 2 * i);
    }
}

void game_stop_does_not_fabricate_an_end() {
    Recorder recorder;
    assert(recorder.begin(camp(701), own(120)));
    assert(recorder.observe(own(121)));
    const Event begin = take(recorder);
    assert(begin.type == EventType::begin && !begin.has_end && begin.terminal_round == -1);
    assert(recorder.active() && recorder.pending() == 0 && !recorder.front());
    static_assert(std::is_trivially_destructible_v<Recorder>);
    static_assert(std::is_trivially_copyable_v<Recorder>);
    static_assert(std::is_trivially_copyable_v<PreactionEvidence>);
    static_assert(sizeof(PreactionEvidence) < 512);
    static_assert(sizeof(Recorder) < 8192); // Eight events and frozen evidence; no heap ownership.
}
} // namespace

int main() {
    first_preaction_is_immutable_and_terminal_retries_do_not_reopen();
    mismatched_preaction_is_unknown_and_raw_bits_are_exact();
    sdk3_champion_range_keeps_raw_kit_and_independent_unknowns();
    preaction_capacity_and_category_validation_are_bounded();
    bootstrap_bank_is_one_actual_attempt();
    replacement_orders_end_before_start_and_abort_once();
    death_is_observed_and_gaps_censor_success();
    stale_foreign_and_invalid_samples_do_not_become_endpoints();
    unknown_fields_and_missing_alive_are_explicit();
    bounded_overflow_is_visible_and_counters_do_not_reset();
    game_stop_does_not_fabricate_an_end();
    std::puts("plan4 attempts: immutable preaction, exact bits, bounded evidence, lifecycle and overflow passed");
}
