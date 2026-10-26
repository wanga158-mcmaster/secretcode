// Synthetic law fixtures only. No frozen production table or engine witnesses.
#include "../kernel/src/kernel.h"
#include <cassert>
#include <cstdio>
#include <memory>

using namespace plan4;
using namespace plan4::kernel;
namespace {
BinKey key(unsigned bag = 0, float health = 100, float maximum = 100) {
    ExactKit kit; kit.known = true; kit.champion = 1; kit.first_ability = 2; kit.second_ability = 3;
    kit.strength = 100; kit.attack_range = 1;
    Composition composition; composition.known = true; composition.count = 1; composition.kinds[0] = 2;
    return preaction_bin(Macro::one_camp, kit, composition, Context::solo_visible_context, bag, health, maximum, 4);
}
StartedRow row(const BinKey& bin, unsigned index, bool successful) {
    StartedRow r; r.bin = bin; r.key = {0, 0, index, 0}; r.origin = Origin::synthetic_test;
    r.nominal_duration = 160; r.duration = successful ? 160 : 80;
    r.right_censored = false; r.required_receipts = r.solo_receipts = true;
    if (!successful) {
        r.phase_count = 1; r.phases[0] = {PhaseKind::approach, {0, 1}, {1, 1}};
        r.endpoint = Symbol::failed_approach; return r; // An observed abort, not a censor.
    }
    r.phase_count = 5;
    r.phases[0] = {PhaseKind::approach, {0, 1}, {1, 10}};
    r.phases[1] = {PhaseKind::clear, {1, 10}, {2, 10}};
    r.phases[2] = {PhaseKind::capture, {2, 10}, {3, 10}};
    r.phases[3] = {PhaseKind::conversion_approach, {3, 10}, {4, 10}};
    r.phases[4] = {PhaseKind::conversion, {4, 10}, {1, 1}};
    r.event_count = 3;
    r.tape[0] = {EconomicKind::group_capture, 2, {1, 1}, Phase::capture, false};
    r.tape[1] = {EconomicKind::solo_reward, 2, {1, 1}, Phase::capture, false};
    r.tape[2] = {EconomicKind::whole_bag_conversion, 4, {1, 1}, Phase::owner_start, false};
    r.endpoint = Symbol::altar; return r;
}
struct Fixture {
    std::unique_ptr<Information> info = std::make_unique<Information>();
    Candidate candidate{};
    std::array<StartedRow, 12> rows;
    Fixture() {
        auto& i = *info; i.own.id = 0; i.own.health = i.own.maximum_health = 100; i.own.gold = 7;
        i.group_count = i.altar_count = 1; i.groups[0].id = 40; i.groups[0].position = {2, 0};
        i.groups[0].gems = 48; i.groups[0].gold = 4; i.groups[0].health = 100;
        i.altars[0].id = 20; i.altars[0].position = {4, 0};
        candidate.information = info.get(); candidate.bin = key(); candidate.nominal_duration = 160;
        candidate.job.kind = JobKind::farm; candidate.job.group = candidate.job.altar = 0;
        candidate.minimum_phase = {{2, 1, 6, 2, 1, -1}};
        candidate.endpoints[unsigned(Symbol::target)] = {{2, 0}, true};
        candidate.endpoints[unsigned(Symbol::altar)] = {{4, 0}, true};
        candidate.endpoints[unsigned(Symbol::spawn)] = {{0, 0}, true};
        candidate.endpoints[unsigned(Symbol::failed_approach)] = {{0, 0}, true};
        for (unsigned n = 0; n < rows.size(); ++n) rows[n] = row(candidate.bin, n, n % 2 != 0);
    }
    std::unique_ptr<FrozenTable> freeze(int count = 8) {
        auto table = std::make_unique<FrozenTable>();
        const std::array<BinKey, 1> bins{{candidate.bin}};
        assert(table->freeze(bins, std::span<const StartedRow>(rows.data(), count), FreezeMode::synthetic_tests_allowed));
        return table;
    }
};
PairPrediction predict(Kernel& model, const FrozenTable& table, const Candidate& c) {
    return model.predict_pair(table, c, nullptr, nullptr, Use::synthetic_tests_allowed);
}

void typed_preaction_bins_and_exact_kit() {
    const auto a = key(1), b = key(24), c = key(25);
    assert(a.valid && a == b && !(a == c));
    auto k = a.kit; k.items[0] = {9, 1}; k.items[1] = {4, 2}; k.item_count = 2;
    auto reverse = k; std::swap(reverse.items[0], reverse.items[1]);
    auto first = preaction_bin(Macro::one_camp, k, a.composition, a.context, 1, 25, 100, 8);
    auto second = preaction_bin(Macro::one_camp, reverse, a.composition, a.context, 24, 25, 100, 8);
    assert(first.valid && first == second && first.health_quarter == 1);
    second = preaction_bin(Macro::one_camp, reverse, a.composition, a.context, 24, 26, 100, 9);
    assert(!(first == second) && second.health_quarter == 2 && second.travel == TravelBin::over_eight);
    reverse.items[1] = reverse.items[0];
    assert(!preaction_bin(Macro::one_camp, reverse, a.composition, a.context, 1, 25, 100, 8).valid);
    auto bank_composition = a.composition; bank_composition.count = 0;
    assert(preaction_bin(Macro::bank, a.kit, bank_composition, a.context, 1, 100, 100, 1).valid);
    assert(!preaction_bin(Macro::bank, a.kit, a.composition, a.context, 1, 100, 100, 1).valid);
}

void first_eight_started_never_replace_censors_with_survivors() {
    auto f = std::make_unique<Fixture>(); f->rows[0].right_censored = true;
    // Deliberately unsorted source: chronology, not input order or success, wins.
    std::swap(f->rows[0], f->rows[10]);
    auto table = f->freeze(12); const auto* bin = table->find(f->candidate.bin);
    assert(bin && bin->started_coverage == 12 && bin->selected == 8 && bin->rows[0]->key.round == 0);
    assert(bin->support == Support::censored && !bin->production_qualified);
    auto model = std::make_unique<Kernel>(); assert(predict(*model, *table, f->candidate).use_m0_for_both);
    f = std::make_unique<Fixture>(); table = f->freeze(7);
    assert(table->find(f->candidate.bin)->support == Support::fewer_than_eight);
    f = std::make_unique<Fixture>(); f->rows[3].required_receipts = false; table = f->freeze();
    assert(table->find(f->candidate.bin)->support == Support::missing_receipt);
    f = std::make_unique<Fixture>(); f->rows[3].solo_receipts = false; table = f->freeze();
    assert(table->find(f->candidate.bin)->support == Support::nonsolo);
    f = std::make_unique<Fixture>(); f->rows[3].key = f->rows[2].key; table = f->freeze();
    assert(table->find(f->candidate.bin)->support != Support::supported);
}

void fit_partition_and_synthetic_gate_are_explicit() {
    auto f = std::make_unique<Fixture>(); auto table = f->freeze(); auto model = std::make_unique<Kernel>();
    assert(table->find(f->candidate.bin)->support == Support::supported && !table->find(f->candidate.bin)->production_qualified);
    const auto denied = model->predict_pair(*table, f->candidate);
    assert(denied.use_m0_for_both && denied.failure == Failure::test_only_table);
    auto production = std::make_unique<FrozenTable>(); const std::array<BinKey, 1> bins{{f->candidate.bin}};
    assert(production->freeze(bins, std::span<const StartedRow>(f->rows.data(), 8)));
    assert(production->find(f->candidate.bin)->support == Support::fewer_than_eight);
    for (int n = 0; n < 8; ++n) f->rows[n].origin = Origin::reference_held_out;
    auto excluded = f->freeze(); assert(excluded->find(f->candidate.bin)->started_coverage == 0);
}

void joint_cutoff_then_expectation_not_mean_duration() {
    auto f = std::make_unique<Fixture>(); auto table = f->freeze(); auto model = std::make_unique<Kernel>();
    const auto p = predict(*model, *table, f->candidate);
    assert(!p.use_m0_for_both && !p.production_qualified);
    assert(p.expectation[0].paths == 8 && p.expectation[1].paths == 8);
    assert(p.expectation[0].our_sum == 0 && p.expectation[1].our_sum == 192); // Exact means: 0 versus 24.
    assert(p.expectation[0].first_conversion_sum == 8 * 129 && p.expectation[1].first_conversion_sum == 4 * 80 + 4 * 129);
    f->info->settings.winning_gems = 48;
    const auto terminal = predict(*model, *table, f->candidate);
    assert(terminal.expectation[0].z_sum == 0 && terminal.expectation[1].z_sum == 4); // No threshold on mean gems.
    for (int n = 0; n < 8; ++n) f->rows[n].duration = 100;
    table = f->freeze(); const auto tight = predict(*model, *table, f->candidate);
    assert(tight.expectation[0].our_sum == tight.expectation[1].our_sum && tight.expectation[0].z_sum == tight.expectation[1].z_sum);
}

void instantiate_candidate_rewards_bag_health_and_forward_rounding() {
    auto f = std::make_unique<Fixture>(); f->info->own.bag = 10; f->info->own.health = 150; f->info->own.maximum_health = 200;
    f->info->groups[0].gems = 24; f->candidate.bin = key(10, 150, 200); f->candidate.nominal_duration = 161;
    for (int n = 0; n < 8; ++n) {
        f->rows[n] = row(f->candidate.bin, n, true); f->rows[n].duration = 81; f->rows[n].health_loss_fraction = 0.1;
    }
    auto table = f->freeze(); auto model = std::make_unique<Kernel>(); const auto p = predict(*model, *table, f->candidate);
    assert(!p.use_m0_for_both && p.expectation[0].our_sum == 8 * 34);
    const auto& path = model->path(0, 0);
    assert(path.duration == 82 && path.captured_round == 25 && path.endpoint_bag == 0 && path.endpoint_gold == 11);
    assert(path.endpoint_health == 130 && path.endpoint == Point({4, 0}));
    assert(path.first.events[1].kind == EventKind::bank && path.first.events[1].secured == 34);
}

void any_invalid_mapping_in_either_pairing_falls_back_for_both() {
    auto f = std::make_unique<Fixture>();
    for (int n = 1; n < 8; n += 2) {
        // Duration 160 gives 8 capture rounds; re-paired duration 80 gives 4,
        // below the legal six. Do not shift the event later or drop that row.
        f->rows[n].phases[2].end = {1, 4}; f->rows[n].phases[3].begin = {1, 4};
    }
    auto table = f->freeze(); auto model = std::make_unique<Kernel>(); auto p = predict(*model, *table, f->candidate);
    assert(p.use_m0_for_both && p.failure == Failure::invalid_mapping && p.expectation[0].paths == 0 && p.expectation[1].paths == 0);
    f = std::make_unique<Fixture>(); f->candidate.endpoints[unsigned(Symbol::altar)].feasible = false; table = f->freeze();
    p = predict(*model, *table, f->candidate); assert(p.use_m0_for_both && p.failure == Failure::invalid_mapping);
    f = std::make_unique<Fixture>(); f->candidate.minimum_total_duration = 81; table = f->freeze();
    assert(predict(*model, *table, f->candidate).use_m0_for_both);
    f = std::make_unique<Fixture>(); f->rows[1].tape[2].within = {1, 2}; table = f->freeze();
    assert(predict(*model, *table, f->candidate).use_m0_for_both); // No conversion before its declared minimum-bearing endpoint.
    f = std::make_unique<Fixture>(); f->rows[1].phases[1].end = {3, 10}; table = f->freeze();
    assert(table->find(f->candidate.bin)->support == Support::malformed); // Cannot occupy/capture before this clear completes.
}

void death_is_discrete_and_loses_the_actual_candidate_bag() {
    auto f = std::make_unique<Fixture>(); f->info->own.bag = 10; f->info->own.health = 40;
    f->candidate.bin = key(10, 40, 100);
    for (int n = 0; n < 8; ++n) {
        f->rows[n] = row(f->candidate.bin, n, false); f->rows[n].dead = true; f->rows[n].health_loss_fraction = 0.4;
        f->rows[n].event_count = 1; f->rows[n].tape[0] = {EconomicKind::whole_bag_loss, 0, {1, 1}, Phase::camp_action, true};
    }
    auto table = f->freeze(); auto model = std::make_unique<Kernel>(); auto p = predict(*model, *table, f->candidate);
    assert(!p.use_m0_for_both && p.expectation[0].our_sum == 0);
    const auto& dead = model->path(0, 0); assert(!dead.alive && dead.endpoint_bag == 0 && dead.endpoint_health == 0);
    assert(dead.first.events[0].bag_before == 10 && dead.first.events[0].bag_after == 0 && dead.first.final_health == 0);
    f->info->settings.max_rounds = 50; p = predict(*model, *table, f->candidate);
    assert(!p.use_m0_for_both && model->path(0, 0).first.final_bag == 10 && model->path(0, 0).first.final_health == 40);
    f->info->own.health = 49; // Same quarter, impossible unchanged death-loss fraction: never clamp it into a death.
    assert(predict(*model, *table, f->candidate).use_m0_for_both);
}

bool refuse_tail(const Candidate&, const Mapping&, Forecast&, const void*) { return false; }
bool unchanged_tail(const Candidate&, const Mapping& mapping, Forecast& out, const void*) { out = mapping.first; return true; }
bool duplicate_capture_tail(const Candidate& candidate, const Mapping& mapping, Forecast& out, const void*) {
    out = mapping.first;
    const auto& group = candidate.information->groups[candidate.job.group];
    Event event; event.kind = EventKind::capture;
    event.start = {mapping.endpoint_round, Phase::owner_action};
    event.end = {mapping.endpoint_round + 6, Phase::capture};
    event.group_id = group.id; event.bag_before = mapping.endpoint_bag;
    event.bag_after = mapping.endpoint_bag + group.gems; event.gold_after = mapping.endpoint_gold + group.gold;
    event.secured = 0; event.health_after = mapping.endpoint_health; event.status = Status::predicted;
    out.events[out.event_count++] = event; out.final_round = event.end.round;
    return true;
}
void continuation_cannot_reuse_captured_stock_before_respawn() {
    auto f = std::make_unique<Fixture>(); f->candidate.continuation_required = true;
    for (unsigned n = 0; n < 8; ++n) { f->rows[n] = row(f->candidate.bin, n, true); f->rows[n].duration = 80; }
    f->info->groups[0].respawn_rounds = 100;
    auto table = f->freeze(); auto model = std::make_unique<Kernel>();
    auto p = model->predict_pair(*table, f->candidate, duplicate_capture_tail, nullptr, Use::synthetic_tests_allowed);
    assert(p.use_m0_for_both && p.failure == Failure::invalid_continuation);
    f->info->groups[0].respawn_rounds = 0;
    p = model->predict_pair(*table, f->candidate, duplicate_capture_tail, nullptr, Use::synthetic_tests_allowed);
    assert(!p.use_m0_for_both); // Explicit test callback; real checkpoint still has to prove the intervening clear/route.
}
struct Gate { bool open = false; };
bool allowed(const void* context) { return static_cast<const Gate*>(context)->open; }
void explicit_continuation_binding_and_work_gate() {
    auto f = std::make_unique<Fixture>(); f->candidate.continuation_required = true;
    auto table = f->freeze(); auto model = std::make_unique<Kernel>();
    auto p = predict(*model, *table, f->candidate);
    assert(p.use_m0_for_both && p.failure == Failure::missing_continuation);
    p = model->predict_pair(*table, f->candidate, refuse_tail, nullptr, Use::synthetic_tests_allowed);
    assert(p.use_m0_for_both && p.failure == Failure::invalid_continuation);
    p = model->predict_pair(*table, f->candidate, unchanged_tail, nullptr, Use::synthetic_tests_allowed);
    assert(!p.use_m0_for_both); // Explicit empty deterministic continuation fixture, not a production checkpoint binding.
    Gate gate; f->info->work_budget = {allowed, &gate};
    p = predict(*model, *table, f->candidate); assert(p.use_m0_for_both && p.failure == Failure::budget);
}
bool wrong_marker_start(const Candidate& candidate,const Mapping& mapping,Forecast& out,const void*) {
    out=mapping.first;const auto& i=*candidate.information;const auto& g=i.groups[candidate.job.group];
    Event marker;marker.kind=EventKind::capture_start;marker.start={mapping.endpoint_round,Phase::owner_action};
    marker.end={mapping.endpoint_round+1,Phase::capture};marker.group_id=g.id;marker.bag_before=marker.bag_after=mapping.endpoint_bag;
    marker.secured=0;marker.gold_after=mapping.endpoint_gold;marker.health_after=mapping.endpoint_health;marker.status=Status::predicted;
    out.events[out.event_count++]=marker;auto capture=marker;capture.kind=EventKind::capture;
    --capture.start.round; // Adjacent completion may only share its EXACT marker start.
    capture.end.round=mapping.endpoint_round+6;capture.bag_after+=g.gems;capture.gold_after+=g.gold;
    out.events[out.event_count++]=capture;out.final_round=capture.end.round;out.final_bag=capture.bag_after;out.final_gold=capture.gold_after;return true;
}
void marker_overlap_is_not_a_general_backwards_clock_exception() {
    auto f=std::make_unique<Fixture>();f->candidate.continuation_required=true;f->info->groups[0].respawn_rounds=0;
    for(unsigned n=0;n<8;++n){f->rows[n]=row(f->candidate.bin,n,true);f->rows[n].duration=80;}
    auto table=f->freeze();auto model=std::make_unique<Kernel>();
    const auto p=model->predict_pair(*table,f->candidate,wrong_marker_start,nullptr,Use::synthetic_tests_allowed);
    assert(p.use_m0_for_both&&p.failure==Failure::invalid_continuation);
}
}
int main() {
    typed_preaction_bins_and_exact_kit();
    first_eight_started_never_replace_censors_with_survivors();
    fit_partition_and_synthetic_gate_are_explicit();
    joint_cutoff_then_expectation_not_mean_duration();
    instantiate_candidate_rewards_bag_health_and_forward_rounding();
    any_invalid_mapping_in_either_pairing_falls_back_for_both();
    death_is_discrete_and_loses_the_actual_candidate_bag();
    explicit_continuation_binding_and_work_gate();
    continuation_cannot_reuse_captured_stock_before_respawn();
    marker_overlap_is_not_a_general_backwards_clock_exception();
    std::puts("plan4 kernel: 10 synthetic scenarios passed (production table/checkpoint binding missing)");
}
