#include "../repairman/src/repairman.h"
#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace plan4;
namespace r = plan4::repairman;
namespace {
int checks = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
r::AttackContext actor() {
    r::AttackContext context;
    context.observed_round = 10; context.actor_id = 1; context.target_id = 200;
    context.health = 500; context.maximum_health = 1000; context.predicted_incoming = 20;
    context.already_attackable = true; context.position = {3, 3};
    return context;
}
r::EscapeProof escape() {
    r::EscapeProof proof;
    proof.supported = proof.based_on_observed_threats = proof.endpoint_safe = true;
    proof.observed_round = 10; proof.escape_count = 3;
    proof.escape[0] = {10, true, true, true};
    proof.escape[1] = {10, true, true, true};
    proof.escape[2] = {0, true, true, true};
    proof.recall_ticks = proof.recall_phase_count = 5;
    for (int phase = 0; phase < 5; ++phase) proof.recall[phase] = {0, true, false, true};
    return proof;
}
r::CycleInput cycle(float health = 200) {
    r::CycleInput input;
    input.actor = actor(); input.escape = escape();
    input.round = 10; input.group_id = 100; input.current_group_health = health; input.damage_per_attack = 10;
    input.fresh_group_observation = input.group_work_is_attackable_target = true;
    input.return_route_supported = input.return_attackable = input.bank_route_supported = true;
    input.return_rounds = input.return_phase_count = 2;
    for (int phase = 0; phase < 2; ++phase) input.returning[phase] = {0, true, true, true};
    input.capture_entry_rounds = 1; input.bank_rounds = 3; input.group_gems = 10;
    input.settings.spawn_heal_fraction = 0.2f;
    return input;
}
r::GroupObservation sight(int round = 10, float health = 200) {
    r::GroupObservation observation;
    observation.round = round; observation.group_id = 100; observation.lifecycle_version = 1;
    observation.health = health; observation.lifecycle = Lifecycle::living;
    observation.visible = observation.all_remaining_work_observed = true;
    observation.provenance = {round, Source::personal_vision, Status::exact_now};
    return observation;
}
bool no_budget(const void*) { return false; }
void segment_admission() {
    const Settings settings;
    auto context = actor(); auto proof = escape();
    auto quote = r::safe_segment_quote(context, proof, settings);
    check(quote.attacks == 4 && quote.failure == r::Failure::none, "choose largest legal safe segment of at most four basic attacks");
    check(quote.boundary == 350 && quote.health_after_attacks == 420 && quote.health_after_extra_exposure == 400,
          "admission reserves k-plus-one incoming camp exposures against strict B0 retreat boundary");
    check(quote.health_at_recall_arrival == 380 && quote.elapsed_rounds == 13 && quote.recall_ticks == 5,
          "every escape camp phase and real recall countdown contributes to return forecast");
    context.health = 450;
    quote = r::safe_segment_quote(context, proof, settings);
    check(quote.attacks == 3 && quote.health_after_extra_exposure == 370,
          "health equal to retreat boundary rejects four attacks and chooses largest smaller segment");
    context.health = 390;
    quote = r::safe_segment_quote(context, proof, settings);
    check(!quote.attacks && quote.failure == r::Failure::unsafe_segment, "no qualifying k means no attack");
    context = actor(); proof.escape[0].incoming = proof.escape[1].incoming = proof.escape[2].incoming = 200;
    quote = r::safe_segment_quote(context, proof, settings);
    check(!quote.attacks && quote.failure == r::Failure::unsafe_escape,
          "stationary k safety cannot hide lethal accumulated escape phases");
    proof = escape(); proof.recall[2].incoming = 1;
    quote = r::safe_segment_quote(context, proof, settings);
    check(!quote.attacks && quote.failure == r::Failure::unsafe_recall,
          "damage during recall rejects an interrupted channel even with ample health");
    proof = escape(); proof.recall_ticks = proof.recall_phase_count = 3;
    check(r::safe_segment_quote(context, proof, settings).failure == r::Failure::unsafe_recall,
          "a shortened unquoted recall cannot replace queried duration");
    context.legal_recall_ticks = 3;
    context.recall_quote = {10, Source::own_state, Status::derived};
    check(r::safe_segment_quote(context, proof, settings).attacks == 4,
          "fresh authoritative kit-adjusted recall quote can supply its actual duration");
    context.recall_quote.observed_round = 9;
    check(!r::safe_segment_quote(context, proof, settings).attacks, "stale kit quote cannot shorten recall");
    context = actor(); proof = escape(); proof.supported = false;
    check(!r::safe_segment_quote(context, proof, settings).attacks, "unsupported escape premise refuses admission");
    proof = escape(); proof.escape[1].known = false;
    check(!r::safe_segment_quote(context, proof, settings).attacks, "one unaccounted escape phase invalidates the whole safety proof");
    context.work_budget = {no_budget, nullptr};
    check(r::safe_segment_quote(context, escape(), settings).failure == r::Failure::budget,
          "safety proof cannot start optional work below its caller-owned fuel reserve");
}
void segment_endpoints() {
    auto context = actor(); const auto quote = r::safe_segment_quote(context, escape(), Settings{});
    auto token = r::begin_segment(context, quote);
    check(r::segment_still_valid(token, context), "fresh stationary target starts a bounded segment");
    for (int attack = 0; attack < 4; ++attack) r::consumed_attack(token);
    check(!r::segment_still_valid(token, context), "four attacks force new admission before a fifth");
    token = r::begin_segment(context, quote); context.position = {4, 3};
    check(!r::segment_still_valid(token, context), "movement ends segment even if attack target stays visible");
    context = actor(); context.target_changed = true;
    check(!r::segment_still_valid(token, context), "target change forces fresh admission");
    context = actor(); context.ability_timing_changed = true;
    check(!r::segment_still_valid(token, context), "ability affecting timing ends the ordinary-attack segment");
    context = actor(); context.new_visible_rival = true;
    check(!r::segment_still_valid(token, context), "new personally visible rival aborts an otherwise safe segment");
    context = actor(); context.escape_premise_lost = true;
    check(!r::segment_still_valid(token, context), "lost escape premise ends segment");
    context = actor(); context.emergency = true;
    check(!r::segment_still_valid(token, context), "common emergency safety outranks retained admission");
    context = actor(); context.alive = false;
    check(!r::segment_still_valid(token, context), "a later death invalidates a formerly admitted segment token");
    context = actor(); context.health = 0;
    check(!r::segment_still_valid(token, context), "zero current health invalidates segment even if an emergency flag is missing");
    context = actor(); token = r::begin_segment(context,quote); r::consumed_attack(token); context.health=480;
    check(r::segment_still_valid(token,context),"expected exposure after one attack preserves the same remaining safe segment");
    context.health=380;
    check(!r::segment_still_valid(token,context),"unexplained health loss above B0 boundary still forces reassessment of remaining k-plus-one exposure");
    context=actor(); context.predicted_incoming=21;
    check(!r::segment_still_valid(token,context),"higher fresh incoming damage requires a new escape and admission quote");
    context=actor(); context.maximum_health=1001;
    check(!r::segment_still_valid(token,context),"a changed health kit invalidates the old segment's retreat and escape premises");
    context=actor(); context.health=1000; auto costly_escape=escape(); costly_escape.escape[0].incoming=700; costly_escape.escape[1].incoming=0;
    token=r::begin_segment(context,r::safe_segment_quote(context,costly_escape,Settings{})); r::consumed_attack(token); context.health=700;
    check(!r::segment_still_valid(token,context),"retained escape loss can invalidate a token even while its stationary retreat margin remains positive");
}
void work_reobservation() {
    r::WorkRecord work;
    check(r::observe(work, sight(), 10) && r::return_attack_authorized(work, 10), "fresh legal health/lifecycle observation supports current work");
    r::processed(work, 4, 10);
    check(work.predicted_remaining == 160 && work.observed_health == 200 &&
          r::health_provenance(work, 10).status == Status::derived && !r::return_attack_authorized(work, 10),
          "processed work is a prediction, never an overwritten exact health observation");
    r::departed(work);
    check(!r::return_attack_authorized(work, 20) && r::health_provenance(work, 20).status == Status::derived,
          "return travel cannot authorize an attack using absent retained health");
    auto stale = sight(10, 160);
    check(!r::observe(work, stale, 20) && !r::return_attack_authorized(work, 20), "stale legal history is insufficient return reobservation");
    auto current = sight(20, 150);
    check(r::observe(work, current, 20) && work.predicted_remaining == 150 && work.return_prediction_error == -10 &&
          r::return_attack_authorized(work, 20), "fresh return health replaces prediction and records discrepancy");
    r::departed(work); current = sight(30, 200); current.lifecycle_version = 2;
    check(!r::observe(work, current, 30) && !work.valid && work.invalidation == r::Failure::new_lifecycle,
          "visible new lifecycle invalidates retained work instead of using damage from a prior spawn");
    check(r::start_new_work(work, current, 30), "a separate fresh ordinary job can start on the new lifecycle");
    r::departed(work); current = sight(40, 0); current.lifecycle = Lifecycle::captured;
    check(!r::observe(work, current, 40) && !work.valid && !work.predicted_remaining,
          "rival-completed capture invalidates stolen unfinished work");
    current = sight(50, 100); r::start_new_work(work, current, 50);
    current = sight(51, 100); current.new_visible_rival = true;
    check(!r::observe(work, current, 51) && work.invalidation == r::Failure::rival,
          "visible rival abort is not treated as harmless persistent-work evidence");
    current = sight(60, 100); r::start_new_work(work, current, 60);
    current = sight(61, 100); current.emergency = true;
    check(!r::observe(work, current, 61) && !r::return_attack_authorized(work, 61), "emergency retreat cancels return permission");
}
[[gnu::noinline]] void two_excursion_forecasts() {
    auto input = std::make_unique<r::CycleInput>(cycle());
    auto active = std::make_unique<r::CycleForecast>(r::predict_two_excursions(*input, true));
    const auto control = std::make_unique<r::CycleForecast>(r::predict_two_excursions(*input, false));
    check(active->excursion_count == 2 && active->excursions[0].attacks == 6 && active->excursions[1].attacks == 14,
          "two excursions reuse predicted unfinished work through repeated four-attack reassessments");
    check(active->completed_capture && active->completed_conversion && active->secured == 10 && active->completion_round == 56,
          "reward appears only after full clear, capture and completed conversion");
    check(active->renewal_rounds == 15 && active->return_scheduled && active->requires_return_reobservation &&
          !active->return_attack_authorized && active->work_status == Status::predicted,
          "forecast includes extra camp exposure, each escape phase, recall, restore and return with an unresolved reobserve guard");
    check(control->excursion_count == 1 && control->secured == 0 && !control->return_scheduled &&
          control->excursions[0].attacks == active->excursions[0].attacks,
          "control shares segment safety but cannot schedule return from retained work");
    check(control->restored_at_spawn && control->final_health == input->actor.maximum_health &&
          control->renewal_rounds == 13 && control->final_round == 29 && control->remaining_work == 140,
          "disabled partial excursion pays identical extra exposure, escape, recall and full heal before common continuation");
    *input = cycle(500);
    *active = r::predict_two_excursions(*input, true);
    check(active->excursion_count == 2 && active->remaining_work == 130 && !active->secured &&
          !active->completed_capture && !active->completed_conversion,
          "work unfinished after two excursions earns zero and never schedules a third");
    check(active->restored_at_spawn && active->final_health == input->actor.maximum_health,
          "unfinished second excursion also leaves a safe restored endpoint before ordinary continuation");
    *input = cycle(); input->return_route_supported = false;
    const auto no_return_control = r::predict_two_excursions(*input, false);
    *active = r::predict_two_excursions(*input, true);
    check(no_return_control.restored_at_spawn && no_return_control.final_round == 29 &&
          no_return_control.failure == r::Failure::unfinished && active->restored_at_spawn &&
          active->final_round == no_return_control.final_round && active->failure == r::Failure::unsupported,
          "unavailable retained-work return route cannot prevent either arm paying its shared first restoration");
    *input = cycle(); input->settings.max_rounds = 28;
    *active = r::predict_two_excursions(*input, false);
    check(!active->restored_at_spawn && active->failure == r::Failure::horizon && !active->secured,
          "a restoration cut off before its final heal never advertises a safe spawn endpoint");
    *input = cycle(35);
    *active = r::predict_two_excursions(*input, true);
    const auto easy_control = r::predict_two_excursions(*input, false);
    check(active->excursion_count == 1 && easy_control.excursion_count == 1 &&
          active->secured == easy_control.secured && active->completion_round == easy_control.completion_round,
          "easy full clear is identical in active and disabled arms");
    *input = cycle(); input->settings.max_rounds = 55;
    *active = r::predict_two_excursions(*input, true);
    check(active->completed_capture && !active->completed_conversion && !active->secured && active->final_bag == 10,
          "tribute straddling horizon leaves carried gems rather than fractional secured output");
    *input = cycle(); input->returning[1].known = false;
    *active = r::predict_two_excursions(*input, true);
    check(!active->secured && active->failure == r::Failure::unsupported,
          "one missing return exposure prevents unsupported second-excursion reward");
    *input = cycle(); input->remaining_targets = 2;
    *active = r::predict_two_excursions(*input, true);
    check(!active->secured && active->failure == r::Failure::unsupported,
          "multi-target change uses ordinary fallback rather than silently extending stationary proof");
    *input = cycle(); input->fresh_group_observation = false;
    *active = r::predict_two_excursions(*input, true);
    check(!active->secured && active->failure == r::Failure::unsupported, "no fresh initial group observation means no assured work reward");
    *input = cycle(); input->actor.work_budget = {no_budget, nullptr};
    *active = r::predict_two_excursions(*input, true);
    check(!active->secured && active->failure == r::Failure::budget && !active->excursions[0].attacks,
          "bounded forecast reports fuel interruption before an unproved attack or reward");
    *input = cycle(1e30f); input->damage_per_attack = 1e-10f;
    *active = r::predict_two_excursions(*input, true);
    check(!active->secured && active->excursion_count == 2 && active->failure == r::Failure::unfinished,
          "extreme finite work cannot overflow attack-count conversion or evade the two-excursion bound");
    *input = cycle(); input->settings.spawn_heal_fraction = 1e-30f;
    *active = r::predict_two_excursions(*input, true);
    check(!active->secured && active->failure == r::Failure::horizon,
          "restoration longer than the horizon truncates safely without overflowing a duration");
}
} // namespace
int main() {
    segment_admission(); segment_endpoints(); work_reobservation(); two_excursion_forecasts();
    std::printf("Plan4 Repairman: %d checks passed; synthetic pure cases, no engine witness claimed.\n", checks);
}
