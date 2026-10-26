// Synthetic contract/physical-state fixtures only; no runtime guard witnesses.
#include "../covenant/src/covenant.h"
#include <cassert>
#include <cstdio>
#include <memory>

using namespace plan4;
using namespace plan4::covenant;
namespace {
Observation observation(int round = 10, unsigned bag = 0, float hp = 100) {
    Observation o; o.round = round; o.own.id = 3; o.own.health = hp; o.own.maximum_health = 100; o.own.bag = bag;
    o.physical_provenance = {round, Source::own_state, Status::exact_now}; o.revision = 1; return o;
}
void at(Observation& o, int round) {
    o.round = round; o.physical_provenance.observed_round = round;
    if (o.live_capture) o.capture_provenance.observed_round = round;
    for (int n = 0; n < o.premise_count; ++n) o.premises[n].provenance.observed_round = round;
}
JobKey job() { return {JobKind::farm, 10, 20, 30}; }
RepairQuotes quotes() {
    RepairQuotes q; q.bank_count = 2; q.banks[0] = {30, 5, true}; q.banks[1] = {40, 7, true};
    q.alternate_path_duration = 3; q.renew_duration = 8; q.retreat_duration = 10; return q;
}
Premise premise(PremiseKind kind, int subject, Truth truth, const Observation& o, std::uint64_t revision = 1) {
    return {kind, subject, truth, {o.round, Source::personal_vision, Status::exact_now}, revision};
}
Receipt receipt(const Covenant& model, Milestone milestone, const Observation& o) {
    const auto& contract = model.contract();
    return {contract.key, contract.epoch, contract.phase_sequence, o.own.id, o.round, milestone,
        {o.round, Source::own_state, Status::exact_now}, true, false, -1, 0};
}

void frozen_epoch_deadline_and_failed_suffix_cooldown() {
    Covenant model; auto o = observation(); const auto key = job();
    assert(promise_deadline(10, 3) == 17 && promise_deadline(10, 4) == 18);
    assert(model.accept(key, Stage::approach, 3, 99, o) == Admit::accepted);
    const auto epoch = model.contract().epoch;
    at(o, 11); assert(model.accept(key, Stage::approach, 100, 99, o) == Admit::retained);
    assert(model.contract().epoch == epoch && model.contract().deadline == 17 && model.contract().phase_start == 10);
    at(o, 17); assert(!model.observe(o, quotes()).activate);
    at(o, 18); auto failed = model.observe(o, quotes());
    assert(failed.activate && failed.cause == Cause::deadline && failed.blocked_until == 26 && failed.repair.kind == RepairKind::replan);
    at(o, 19); assert(model.accept(key, Stage::approach, 1, 99, o) == Admit::failed_suffix);
    auto different_altar = key; different_altar.altar = 40;
    assert(model.accept(different_altar, Stage::approach, 1, 99, o) == Admit::failed_suffix); // Same physical failed suffix.
    at(o, 25); failed = model.observe(o, quotes()); assert(failed.blocked_until == 26);
    assert(model.contract().deadline == 17); // Repeated repair cannot extend the promise.
    at(o, 26); assert(!model.observe(o, quotes()).activate);
    assert(model.accept(key, Stage::approach, 1, 101, o) == Admit::accepted);
    assert(model.contract().epoch > epoch && model.contract().deadline == 30 && model.contract().route == 101);
}

void only_new_exact_resolution_can_reopen_early() {
    Covenant model; auto o = observation(); const auto key = job(); model.accept(key, Stage::approach, 1, 99, o);
    at(o, 15); model.observe(o, quotes()); assert(model.contract().failed);
    o.premise_count = 1; o.premises[0] = premise(PremiseKind::connector_open, 99, Truth::satisfied, o, 1);
    assert(model.accept(key, Stage::approach, 1, 99, o) == Admit::failed_suffix); // Same old revision is not progress.
    o.premises[0].revision = 2; o.premises[0].provenance.status = Status::predicted;
    assert(model.accept(key, Stage::approach, 1, 99, o) == Admit::failed_suffix);
    o.premises[0].provenance.status = Status::exact_now; o.revision = 2;
    assert(!model.observe(o, quotes()).activate); // Do not immediately re-latch the old expired deadline.
    assert(model.accept(key, Stage::approach, 2, 99, o) == Admit::accepted);
    assert(model.contract().phase_start == 15 && model.contract().deadline == 20);
}

void alternate_route_needs_new_exact_evidence() {
    Covenant model; auto o = observation(); const auto key = job(); model.accept(key, Stage::approach, 3, 99, o);
    o.premise_count = 1; o.premises[0] = premise(PremiseKind::connector_open, 99, Truth::contradicted, o);
    const auto failed = model.observe(o, quotes());
    assert(failed.activate && failed.repair.kind == RepairKind::alternate_path);
    at(o, 11);
    assert(model.accept(key, Stage::approach, 3, 100, o) == Admit::failed_suffix);
    auto other_key = key; other_key.altar = 40;
    assert(model.accept(other_key, Stage::approach, 3, 100, o) == Admit::failed_suffix);
    o.premises[0] = premise(PremiseKind::connector_open, 100, Truth::satisfied, o, 2);
    o.premises[0].provenance.status = Status::predicted;
    assert(model.accept(key, Stage::approach, 3, 100, o) == Admit::failed_suffix);
    o.premises[0].provenance.status = Status::exact_now;
    assert(model.accept(key, Stage::approach, 3, 100, o) == Admit::accepted);
    assert(model.contract().route == 100 && model.contract().phase_start == 11);
    // A new route does not repair missing physical stock.
    Covenant unavailable; o = observation(); unavailable.accept(key, Stage::approach, 3, 99, o);
    o.premise_count = 1; o.premises[0] = premise(PremiseKind::target_available, 10, Truth::contradicted, o);
    unavailable.observe(o, quotes()); at(o, 11);
    o.premises[0] = premise(PremiseKind::connector_open, 100, Truth::satisfied, o, 2);
    assert(unavailable.accept(key, Stage::approach, 3, 100, o) == Admit::failed_suffix);
}

void three_valued_premises_never_turn_occlusion_into_failure() {
    Covenant active(true), disabled(false); auto o = observation(10, 24);
    for (auto* model : {&active, &disabled}) assert(model->accept(job(), Stage::approach, 10, 99, o) == Admit::accepted);
    o.premise_count = 1; o.premises[0] = premise(PremiseKind::target_available, 10, Truth::unknown, o);
    auto guard = active.forecast_guard(job(), active.remaining(), 15, o, quotes());
    assert(!guard.activate && !guard.supported && guard.future_unknown);
    o.premises[0].truth = Truth::contradicted; o.premises[0].provenance.status = Status::predicted;
    assert(!active.observe(o, quotes()).activate);
    o.premises[0].provenance.status = Status::exact_now; o.premises[0].provenance.observed_round = 9;
    assert(!active.observe(o, quotes()).activate);
    o.premises[0].provenance.observed_round = 10;
    const auto a = active.observe(o, quotes()), d = disabled.observe(o, quotes());
    assert(a.activate && d.activate && a.cause == Cause::unavailable_camp && d.cause == a.cause);
    assert(a.repair.kind == RepairKind::convert_retained && d.repair.kind == a.repair.kind);
    assert(a.repair.altar_id == 30 && a.repair.retained_bag == 24 && o.own.bag == 24);
}

void common_minimal_repair_table_and_no_empty_tribute() {
    auto o = observation(10, 24); auto q = quotes();
    auto r = minimal_repair(Cause::approach_obstruction, o, q); assert(r.kind == RepairKind::alternate_path && r.quoted_duration == 3);
    r = minimal_repair(Cause::unavailable_camp, o, q); assert(r.kind == RepairKind::convert_retained && r.altar_id == 30);
    r = minimal_repair(Cause::blocked_conversion, o, q, 30); assert(r.kind == RepairKind::convert_retained && r.altar_id == 40);
    q.banks[1].accessible = false; assert(minimal_repair(Cause::blocked_conversion, o, q, 30).kind == RepairKind::safe_retreat_renew);
    q = quotes(); q.banks[1].altar_id = 30; assert(minimal_repair(Cause::blocked_conversion, o, q, 30).kind == RepairKind::safe_retreat_renew);
    o.own.bag = 0; o.own.health = 40; assert(minimal_repair(Cause::unavailable_camp, o, q).kind == RepairKind::renew);
    assert(minimal_repair(Cause::blocked_conversion, o, quotes(), 30).kind == RepairKind::safe_retreat_renew);
    o.own.health = 100; assert(minimal_repair(Cause::deadline, o, q).kind == RepairKind::replan);
    o.own.bag = 24; q.banks[0].accessible = q.banks[1].accessible = false;
    assert(minimal_repair(Cause::deadline, o, q).kind == RepairKind::safe_retreat_renew);
}

void protected_capture_channels_and_no_same_key_extension() {
    Covenant model; auto o = observation(10, 24); model.accept(job(), Stage::approach, 1, 99, o);
    const auto epoch = model.contract().epoch; at(o, 30); o.own.channel = Channel::tribute; o.own.channel_ticks = 2;
    auto observed = model.observe(o, quotes()); assert(observed.protected_progress && !observed.activate);
    auto predicted = model.forecast_guard(job(), model.remaining(), 100, o, quotes()); assert(predicted.protected_progress && !predicted.activate);
    assert(model.accept(job(), Stage::approach, 100, 99, o) == Admit::retained);
    auto other = job(); other.first_group = 11;
    assert(model.accept(other, Stage::approach, 2, 99, o) == Admit::protected_progress);
    assert(model.contract().epoch == epoch && model.contract().deadline == 14);
    o.own.channel = Channel::none; o.live_capture = true; o.capture_group = 20;
    o.capture_provenance = {30, Source::own_occupancy, Status::exact_now};
    assert(model.observe(o, quotes()).protected_progress);
    o.capture_provenance.status = Status::unknown;
    assert(model.observe(o, quotes()).activate); // Unknown progress cannot silently suppress an actual expired promise.
}

void first_capture_receipt_and_compatible_milestones_are_retained() {
    Covenant model; auto o = observation(); model.accept(job(), Stage::approach, 2, 99, o);
    assert(model.complete(receipt(model, Milestone::arrival, o), 2, -1, o) == ReceiptResult::advanced);
    at(o, 12); assert(model.complete(receipt(model, Milestone::clear, o), 6, -1, o) == ReceiptResult::advanced);
    at(o, 18); o.own.bag = 24;
    auto capture = receipt(model, Milestone::capture_receipt, o);
    capture.own_attribution = true; capture.group_id = 10; capture.stock_epoch = 77;
    capture.observation.source = Source::own_occupancy;
    assert(model.complete(capture, 4, 199, o) == ReceiptResult::advanced);
    assert(model.contract().completed_mask == 1 && model.contract().remaining_group == 1 && model.remaining().group == 20);
    const auto epoch = model.contract().epoch; const int deadline = model.contract().deadline;
    at(o, 19); assert(model.accept(job(), Stage::approach, 100, 199, o) == Admit::retained);
    assert(model.contract().epoch == epoch && model.contract().deadline == deadline && model.contract().completed[0].stock_epoch == 77);
    o.premise_count = 1; o.premises[0] = premise(PremiseKind::target_available, 10, Truth::contradicted, o);
    assert(!model.observe(o, quotes()).activate); // First camp is finished, not a failed remaining suffix.
    o.acquisition_count = 1; o.acquisitions[0] = {10, 77, {19, Source::own_occupancy, Status::exact_now}};
    auto compatible = job(); compatible.second_group = 50;
    assert(model.accept(compatible, Stage::approach, 3, 299, o) == Admit::accepted);
    assert(model.contract().completed_mask == 1 && model.remaining().group == 50 && o.own.bag == 24);
}

void keyed_exact_progress_and_no_replayed_capture_receipt() {
    Covenant model; auto o = observation(); model.accept(job(), Stage::approach, 1, 99, o);
    auto wrong = receipt(model, Milestone::arrival, o); ++wrong.epoch;
    assert(model.complete(wrong, 2, -1, o) == ReceiptResult::mismatch);
    at(o, 15); model.observe(o, quotes()); assert(model.contract().failed);
    const auto arrival = receipt(model, Milestone::arrival, o);
    assert(model.complete(arrival, 2, -1, o) == ReceiptResult::advanced && !model.contract().failed);
    assert(model.complete(arrival, 2, -1, o) == ReceiptResult::mismatch);
    at(o, 17); model.complete(receipt(model, Milestone::clear, o), 6, -1, o);
    at(o, 23); auto capture = receipt(model, Milestone::capture_receipt, o); capture.group_id = 10; capture.stock_epoch = 77;
    capture.observation = {23, Source::personal_vision, Status::derived};
    assert(model.complete(capture, 4, 199, o) == ReceiptResult::unsupported);
    capture.own_attribution = true; capture.observation = {23, Source::own_occupancy, Status::exact_now};
    assert(model.complete(capture, 4, 199, o) == ReceiptResult::advanced);
    assert(model.complete(capture, 4, 199, o) == ReceiptResult::mismatch && model.contract().completed_mask == 1);
}

bool repair_bank(const ProductState& state, Forecast& out, const void*) {
    assert(state.skip_failed_suffix && state.contract.completed_mask == 1 && state.remaining.group == 20);
    assert(state.physical.own.bag == 24 && state.guard.repair.kind == RepairKind::convert_retained);
    out.reset(); out.macros_started = 1; out.our_secured = state.physical.own.bag; out.final_bag = 0;
    out.final_gold = state.physical.own.gold; out.final_health = state.physical.own.health;
    out.final_round = out.first_conversion_round = state.guard.repair_start_round + state.guard.repair.quoted_duration;
    out.completed_conversion = true;
    out.events[out.event_count++] = {EventKind::bank, {state.guard.repair_start_round, Phase::owner_action},
        {out.final_round, Phase::owner_start}, -1, state.physical.own.bag, 0, state.physical.own.bag,
        state.physical.own.gold, state.physical.own.health, Status::predicted};
    return true;
}
void deterministic_guard_product_differs_only_in_prediction() {
    Covenant active(true), disabled(false); auto o = observation();
    for (auto* model : {&active, &disabled}) {
        model->accept(job(), Stage::approach, 2, 99, o);
        model->complete(receipt(*model, Milestone::arrival, o), 2, -1, o);
        at(o, 12); model->complete(receipt(*model, Milestone::clear, o), 6, -1, o);
        at(o, 18); auto capture = receipt(*model, Milestone::capture_receipt, o);
        capture.own_attribution = true; capture.group_id = 10; capture.stock_epoch = 77; capture.observation.source = Source::own_occupancy;
        model->complete(capture, 4, 199, o); at(o, 10);
    }
    at(o, 20); o.own.bag = 24;
    Forecast baseline; baseline.reset(); baseline.our_secured = 72; baseline.completed_conversion = true; baseline.first_conversion_round = baseline.final_round = 31;
    Forecast composed, flat;
    const auto a = active.predict_product(job(), active.remaining(), 30, o, quotes(), baseline, composed, repair_bank);
    const auto d = disabled.predict_product(job(), disabled.remaining(), 30, o, quotes(), baseline, flat, repair_bank);
    assert(a.composed && !a.use_m0 && a.evidence.cause == Cause::deadline && a.evidence.at.round == 26);
    assert(composed.our_secured == 24 && composed.first_conversion_round == 32);
    assert(!d.composed && d.use_m0 && d.evidence.activate && flat.our_secured == 72);
    const auto missing = active.predict_product(job(), active.remaining(), 30, o, quotes(), baseline, composed);
    assert(missing.use_m0 && missing.binding_missing && composed.our_secured == 72);
    at(o, 27); const auto actual_a = active.observe(o, quotes()), actual_d = disabled.observe(o, quotes());
    assert(actual_a.cause == actual_d.cause && actual_a.repair.kind == actual_d.repair.kind && actual_a.repair.retained_bag == 24);
}

void unknown_future_no_script_and_completed_jobs_can_restart() {
    Covenant model; auto o = observation(); auto key = job(); key.second_group = -1;
    model.accept(key, Stage::convert, 1, -1, o);
    auto other = key; other.first_group = 100;
    assert(!model.forecast_guard(other, {Stage::approach, 100, -1, 99}, 1000, o, quotes()).supported);
    assert(model.complete(receipt(model, Milestone::conversion, o), 0, -1, o) == ReceiptResult::advanced);
    assert(model.contract().stage == Stage::done && !model.forecast_guard(key, model.remaining(), 1000, o, quotes()).activate);
    const auto epoch = model.contract().epoch; at(o, 11);
    assert(model.accept(key, Stage::approach, 3, 99, o) == Admit::accepted && model.contract().epoch > epoch);
    assert(model.contract().completed_mask == 0);
    auto stale = o; at(stale, 10); stale.premise_count = 1;
    stale.premises[0] = premise(PremiseKind::target_available, 10, Truth::contradicted, stale);
    assert(!model.observe(stale, quotes()).activate);
}
void actual_repair_completion_does_not_erase_failed_suffix_or_invent_capture() {
    Covenant model;auto o=observation(10,24);assert(model.accept(job(),Stage::approach,3,99,o)==Admit::accepted);
    at(o,11);o.premise_count=1;o.premises[0]=premise(PremiseKind::target_available,10,Truth::contradicted,o);
    assert(model.observe(o,quotes()).activate);auto completed=receipt(model,Milestone::conversion,o);completed.own_attribution=true;
    assert(model.complete_repair(completed,o)==ReceiptResult::unsupported); // Retained bag not yet converted.
    o.own.bag=0;completed.own_attribution=false;assert(model.complete_repair(completed,o)==ReceiptResult::unsupported);
    completed.own_attribution=true;assert(model.complete_repair(completed,o)==ReceiptResult::advanced);
    assert(model.contract().converted && model.contract().completed_mask==0 && model.contract().stage==Stage::done);
    const auto epoch=model.contract().epoch;at(o,12);
    assert(model.accept(job(),Stage::approach,100,99,o)==Admit::failed_suffix && model.contract().epoch==epoch);
    o.premises[0].truth=Truth::satisfied;o.premises[0].revision=2;
    assert(model.accept(job(),Stage::approach,3,99,o)==Admit::accepted && model.contract().completed_mask==0);
}
void exact_late_progress_resolves_the_completed_suffix() {
    Covenant model;auto o=observation();const auto original=job();
    assert(model.accept(original,Stage::approach,1,99,o)==Admit::accepted);
    at(o,15);assert(model.observe(o,quotes()).activate);
    assert(model.complete(receipt(model,Milestone::arrival,o),2,-1,o)==ReceiptResult::advanced);
    auto other=original;other.first_group=50;
    assert(model.accept(other,Stage::approach,1,199,o)==Admit::accepted);
    assert(model.accept(original,Stage::approach,1,99,o)==Admit::accepted);
}
void alternate_step_preserves_key_acquisitions_and_only_resolves_its_suffix() {
    for(bool enabled:{false,true}) {
        Covenant model(enabled);auto o=observation();auto unrelated=job();unrelated.first_group=50;unrelated.second_group=-1;
        assert(model.accept(unrelated,Stage::approach,3,88,o)==Admit::accepted);
        o.premise_count=1;o.premises[0]=premise(PremiseKind::target_available,50,Truth::contradicted,o);
        assert(model.observe(o,quotes()).cause==Cause::unavailable_camp);o.premise_count=0;
        o.acquisition_count=1;o.acquisitions[0]={10,77,{o.round,Source::own_occupancy,Status::exact_now}};
        assert(model.accept(job(),Stage::approach,3,99,o)==Admit::accepted);
        const auto epoch=model.contract().epoch,sequence=model.contract().phase_sequence;const int deadline=model.contract().deadline;
        const int blocked=connector_id({1,0}),next=connector_id({0,1});
        assert(model.bind_approach_connector(blocked,o));
        assert(model.contract().epoch==epoch && model.contract().phase_sequence==sequence && model.contract().deadline==deadline);
        o.premise_count=1;o.premises[0]=premise(PremiseKind::connector_open,blocked,Truth::contradicted,o);
        assert(model.observe(o,quotes()).cause==Cause::approach_obstruction);
        assert(!model.bind_approach_connector(blocked+1,o));
        assert(model.accept(job(),Stage::approach,100,blocked,o)==Admit::failed_suffix);
        assert(model.contract().deadline==deadline && model.contract().epoch==epoch);
        auto progress=o;progress.own.position={0,1};progress.revision=2;
        progress.premises[0]={PremiseKind::connector_open,next,Truth::satisfied,{o.round,Source::own_state,Status::exact_now},2};
        assert(!model.alternate_route_progress(blocked+1,next,2,progress));
        auto bad=progress;bad.physical_provenance.status=Status::unknown;assert(!model.alternate_route_progress(blocked,next,2,bad));
        bad=progress;bad.revision=1;assert(!model.alternate_route_progress(blocked,next,2,bad));
        bad=progress;bad.premises[0].provenance.source=Source::personal_vision;assert(!model.alternate_route_progress(blocked,next,2,bad));
        bad=progress;bad.premises[0].provenance.observed_round=9;assert(!model.alternate_route_progress(blocked,next,2,bad));
        bad=progress;bad.own.position={0,0};assert(!model.alternate_route_progress(blocked,connector_id(bad.own.position),2,bad));
        assert(model.contract().failed && model.contract().deadline==deadline && model.contract().phase_sequence==sequence);
        assert(model.alternate_route_progress(blocked,next,2,progress));
        const auto& contract=model.contract();assert(!contract.failed && contract.stage==Stage::approach && contract.key==job());
        assert(contract.epoch==epoch && contract.phase_sequence==sequence+1 && contract.nominal_duration==2 && contract.deadline==15);
        assert(contract.completed_mask==1 && contract.completed[0].stock_epoch==77 && contract.remaining_group==1 && model.remaining().group==20);
        assert(!model.alternate_route_progress(blocked,next,2,progress));
        assert(model.accept(unrelated,Stage::approach,3,88,progress)==Admit::failed_suffix);
        auto other=job();other.first_group=60;other.second_group=-1;
        assert(model.accept(other,Stage::approach,3,88,progress)==Admit::accepted);
        assert(model.accept(job(),Stage::approach,3,blocked,progress)==Admit::accepted); // Only the obstruction failure was removed.
    }
}
void alternate_progress_cannot_repair_other_causes_or_interrupt_live_progress() {
    for(Cause cause:{Cause::unavailable_camp,Cause::deadline}) {
        Covenant model;auto o=observation();assert(model.accept(job(),Stage::approach,0,99,o)==Admit::accepted);
        assert(model.bind_approach_connector(connector_id({1,0}),o));
        if(cause==Cause::unavailable_camp) {o.premise_count=1;o.premises[0]=premise(PremiseKind::target_available,10,Truth::contradicted,o);}
        else at(o,13);
        assert(model.observe(o,quotes()).cause==cause);const auto deadline=model.contract().deadline;
        o.own.position={0,1};++o.revision;o.premise_count=1;
        o.premises[0]={PremiseKind::connector_open,connector_id(o.own.position),Truth::satisfied,{o.round,Source::own_state,Status::exact_now},o.revision};
        assert(!model.alternate_route_progress(1,connector_id(o.own.position),0,o));
        assert(model.contract().failed && model.contract().deadline==deadline);
    }
    Covenant model;auto o=observation();model.accept(job(),Stage::approach,2,99,o);
    o.own.channel=Channel::tribute;assert(!model.bind_approach_connector(1,o));
    o.own.channel=Channel::none;o.live_capture=true;o.capture_group=10;o.capture_provenance={10,Source::own_occupancy,Status::exact_now};
    assert(!model.bind_approach_connector(1,o));assert(model.observe(o,quotes()).protected_progress);
    assert(connector_id({-1,0})<0 && connector_id({16384,0})<0 && connector_id({0,16384})<0 && connector_id({16383,16383})==268435455);
}
}
int main() {
    frozen_epoch_deadline_and_failed_suffix_cooldown();
    only_new_exact_resolution_can_reopen_early();
    alternate_route_needs_new_exact_evidence();
    three_valued_premises_never_turn_occlusion_into_failure();
    common_minimal_repair_table_and_no_empty_tribute();
    protected_capture_channels_and_no_same_key_extension();
    first_capture_receipt_and_compatible_milestones_are_retained();
    keyed_exact_progress_and_no_replayed_capture_receipt();
    deterministic_guard_product_differs_only_in_prediction();
    unknown_future_no_script_and_completed_jobs_can_restart();
    actual_repair_completion_does_not_erase_failed_suffix_or_invent_capture();
    exact_late_progress_resolves_the_completed_suffix();
    alternate_step_preserves_key_acquisitions_and_only_resolves_its_suffix();
    alternate_progress_cannot_repair_other_causes_or_interrupt_live_progress();
    std::puts("plan4 covenant: 14 synthetic scenarios passed (native guard witnesses pending)");
}
