// Synthetic semantic schedules, not claims about native route/channel timing.
#include "../common/src/contract.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
using namespace p6;
int checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
template<class T> Fact<T> known(T value) { return {value, Source::synthetic_fixture}; }
const Settings settings{2000, 1000, 5};
const Clock clock{0, 1};
ScenarioWindows windows(int zero, int nominal, int stress) {
    ScenarioWindows value;
    value.deadlines = {zero, nominal, stress}; value.windows = {zero, nominal, stress};
    value.sampled_actor_id = clock.actor_id; value.sampled_round = clock.round;
    value.source = Source::synthetic_fixture; value.valid = true; return value;
}
ActorSchedule actor(int id, Gems bag, Gems gold = 0) {
    ActorSchedule value;
    value.actor_id = id; value.initial_bag = known(bag); value.initial_gold = known(gold);
    value.occupied_turns = known<Gems>(15); return value;
}
void event(ActorSchedule& owner, EventKind kind, int id, int tick, int production = -1, Gems cost = 0) {
    Event value;
    value.kind = kind; value.event_id = id; value.tick = tick; value.production_index = production;
    value.gold_cost = known(cost); value.waypoint_id = kind == EventKind::renewal ? 99 : -1;
    value.source = Source::synthetic_fixture; owner.events[owner.event_count++] = value;
}
void production(Schedule& value, int id, int tick, Gems gems, int holder, Gems gold = 0) {
    Production item;
    item.event_id = id; item.tick = tick; item.gems = known(gems); item.gold = known(gold);
    item.holders[0] = holder; item.holder_count = 1; item.source = Source::synthetic_fixture;
    value.productions[value.production_count++] = item;
}
Schedule bank_and_rebuild() {
    Schedule value; value.actor_count = 1; value.target_id = 10;
    value.actors[0] = actor(1, 20);
    production(value, 100, 8, 10, 1);
    event(value.actors[0], EventKind::tribute, 101, 2);
    event(value.actors[0], EventKind::production, 100, 8, 0);
    event(value.actors[0], EventKind::tribute, 102, 12);
    event(value.actors[0], EventKind::renewal, 103, 15); return value;
}
Schedule retain_and_bank() {
    Schedule value; value.actor_count = 1; value.target_id = 10;
    value.actors[0] = actor(1, 20);
    production(value, 100, 8, 30, 1);
    event(value.actors[0], EventKind::production, 100, 8, 0);
    event(value.actors[0], EventKind::tribute, 101, 14);
    event(value.actors[0], EventKind::renewal, 102, 15); return value;
}
Schedule helper_option(bool cooperate, Gems helper_independent) {
    Schedule value; value.actor_count = 2; value.target_id = 10;
    value.actors[0] = actor(1, 20); value.actors[1] = actor(2, 0);
    if (cooperate) {
        production(value, 100, 8, 30, 1);
        event(value.actors[0], EventKind::production, 100, 8, 0);
        event(value.actors[0], EventKind::tribute, 101, 14);
    } else {
        event(value.actors[0], EventKind::tribute, 101, 2);
        production(value, 200, 8, helper_independent, 2);
        event(value.actors[1], EventKind::production, 200, 8, 0);
        event(value.actors[1], EventKind::tribute, 201, 14);
    }
    event(value.actors[0], EventKind::renewal, 102, 15);
    event(value.actors[1], EventKind::renewal, 202, 15); return value;
}
void oracle_and_bounded_events() {
    const auto normal = windows(20, 20, 20), stress = windows(20, 20, 10);
    auto rebuild = bank_and_rebuild(), retain = retain_and_bank();
    auto banked = evaluate_schedule(rebuild, normal, settings, clock, 0);
    auto capital = evaluate_schedule(retain, normal, settings, clock, 0);
    check(banked.eligible() && capital.eligible(), "both explicit hand schedules are feasible");
    check(banked.own_score[1] == 30 && capital.own_score[1] == 50, "20-round oracle secures 30 versus 50");
    check(compare(capital, banked) > 0, "20-round oracle chooses retained capacity");
    Schedule bank_only; bank_only.actor_count = 1; bank_only.target_id = 10;
    bank_only.actors[0] = actor(1, 20);
    event(bank_only.actors[0], EventKind::tribute, 101, 2);
    event(bank_only.actors[0], EventKind::renewal, 102, 3);
    check(evaluate_schedule(bank_only, normal, settings, clock, 0).own_score[1] == 20 && capital.own_score[1] == 50,
        "bank-only fixture explicitly distinguishes twenty secured gems from retained fifty");
    banked = evaluate_schedule(rebuild, stress, settings, clock, 0);
    capital = evaluate_schedule(retain, stress, settings, clock, 0);
    check(banked.own_score[2] == 20 && capital.own_score[2] == 0, "ten-round stress secures 20 versus zero");
    check(compare(banked, capital) > 0, "minimum scenario reverses retained-capacity choice");
    check(banked.peak_bag == 20 && capital.peak_bag == 50, "first bank empties bag before rebuilding");
    check(banked.denial == std::array<Gems, 3>{0, 0, 0} && banked.margin == banked.own_score,
        "P6 never adds denial credit or inherits P5 numeric card allowlists");
    retain.actors[0].events[1] = retain.actors[0].events[2]; retain.actors[0].event_count = 2;
    const auto stranded = evaluate_schedule(retain, normal, settings, clock, 0);
    check(stranded.status == Status::unresolved && stranded.issue == Issue::missing_liquidation && !stranded.eligible(),
        "unliquidated 50 are unresolved and cannot become a scored incumbent");
    rebuild.actors[0].events[2].event_id = rebuild.actors[0].events[0].event_id;
    check(evaluate_schedule(rebuild, normal, settings, clock, 0).issue == Issue::duplicate_owner_event,
        "one owner/event cannot secure the same bank twice");
    auto capped = bank_and_rebuild();
    event(capped.actors[0], EventKind::production, 104, 8, 0);
    event(capped.actors[0], EventKind::production, 105, 8, 0);
    check(evaluate_schedule(capped, normal, settings, clock, 0).issue == Issue::malformed,
        "three projected production events exceed an actor's bounded representation");
}
void matched_helper_counterfactuals() {
    const auto full = windows(20, 20, 20);
    auto cooperative = helper_option(true, 0), independent = helper_option(false, 10);
    auto alternatives = evaluate_alternatives(cooperative, independent, full, settings, clock, 0);
    check(alternatives.matched && alternatives.cooperative.min_margin == 50 && alternatives.independent.min_margin == 30,
        "zero-bag helper enables team 50 versus collector20 plus helper10");
    check(joint_support_admissible(alternatives), "twenty-gem matched helper gain admits support");
    independent = helper_option(false, 40);
    alternatives = evaluate_alternatives(cooperative, independent, full, settings, clock, 0);
    check(alternatives.independent.min_margin == 60 && !joint_support_admissible(alternatives),
        "independent helper40 makes team60 superior to cooperation50");
    independent = helper_option(false, 30);
    alternatives = evaluate_alternatives(cooperative, independent, full, settings, clock, 0);
    check(!joint_support_admissible(alternatives), "equal minimum score does not authorize new joint support");
    independent = helper_option(false, 29);
    alternatives = evaluate_alternatives(cooperative, independent, full, settings, clock, 0);
    check(joint_support_admissible(alternatives), "one-gem conservative gain meets strict support threshold");
    independent.actors[1].actor_id = 3;
    check(!evaluate_alternatives(cooperative, independent, full, settings, clock, 0).matched,
        "releasing helper cannot substitute another affected actor");
}
void sharing_and_affordability() {
    Schedule shared; shared.actor_count = 2; shared.target_id = 10;
    shared.actors[0] = actor(9, 0); shared.actors[1] = actor(3, 0);
    production(shared, 100, 4, 5, 9, 7);
    shared.productions[0].holders[1] = 3; shared.productions[0].holder_count = 2;
    for (int a = 0; a < 2; ++a) {
        event(shared.actors[a], EventKind::production, 100, 4, 0);
        event(shared.actors[a], EventKind::tribute, 101, 4);
        event(shared.actors[a], EventKind::purchase, 102, 5, -1, a == 0 ? 3 : 4);
        event(shared.actors[a], EventKind::renewal, 103, 6);
    }
    auto outcome = evaluate_schedule(shared, windows(10, 10, 10), settings, clock, 0);
    check(outcome.eligible() && outcome.own_score[0] == 5 && outcome.peak_bag == 3,
        "capture before channel awards five total, lowest ID receives gem/gold remainder once");
    shared.actors[0].events[2].gold_cost = known<Gems>(4);
    check(evaluate_schedule(shared, windows(10, 10, 10), settings, clock, 0).issue == Issue::unaffordable_purchase,
        "a holder cannot spend the other holder's gold remainder");
    auto duplicate = helper_option(false, 10);
    production(duplicate, 200, 8, 20, 1);
    check(evaluate_schedule(duplicate, windows(20, 20, 20), settings, clock, 0).issue == Issue::duplicate_payout,
        "same camp payout cannot appear twice in conflicting independent forecasts");
    shared.actors[0].events[2].gold_cost = known<Gems>(0);
    shared.actors[0].initial_gold = {};
    check(evaluate_schedule(shared, windows(10, 10, 10), settings, clock, 0).status == Status::unresolved,
        "unknown personal resources do not silently become zero");
    shared.actors[0].initial_gold = {0, Source::conservative};
    check(evaluate_schedule(shared, windows(10, 10, 10), settings, clock, 0).eligible(),
        "explicit conservative personal-gold lower bound remains attributable");
    auto overflow = retain_and_bank(); overflow.actors[0].initial_bag = known(std::numeric_limits<Gems>::max());
    check(evaluate_schedule(overflow, windows(20, 20, 20), settings, clock, 0).issue == Issue::overflow,
        "bag addition rejects signed overflow before any credit");
}
void score_history_and_interleaving() {
    ScoreHistory history;
    check(history.sample({0, 1}, 0), "initial private score sample accepted");
    auto scenario = history.windows(settings, {0, 1}, 0);
    check(scenario.valid && scenario.deadlines == std::array<int, 3>{2000, 2000, 1000},
        "one sample has zero nominal rate and one-gem stress floor");
    check(!history.sample({0, 1}, 50) && !history.windows(settings, {0, 1}, 50).valid,
        "second score observation cannot overwrite same-round sample or mix into forecast");
    check(history.sample({1, 1}, 3) && history.sample({65, 1}, 131), "sparse score samples advance monotonically");
    scenario = history.windows(settings, {65, 1}, 131);
    check(scenario.rate_numerator == 128 && scenario.rate_denominator == 64 &&
        scenario.deadlines == std::array<int, 3>{1935, 435, 218},
        "oldest retained sample uses exact rational rate and ceiling deadlines");
    check(!history.sample({64, 1}, 130) && !history.sample({66, 2}, 132), "rollback or another actor cannot mutate private ring");
    check(history.sample({66, 1}, 132), "history advances beyond 64-round horizon");
    scenario = history.windows(settings, {66, 1}, 132);
    check(scenario.rate_numerator == 1 && scenario.rate_denominator == 1,
        "expired 65-round-old sample is unknown rather than a retained baseline");
    ScoreHistory early, late;
    check(early.sample({10, 3}, 900) && late.sample({10, 9}, 930), "different actors sample public score on their own turns");
    const auto before = early.windows(settings, {10, 3}, 900), after = late.windows(settings, {10, 9}, 930);
    check(before.deadlines[2] == 100 && after.deadlines[2] == 70,
        "tribute between allied turns produces different private deadlines, not a common snapshot");
    check(before.sampled_actor_id == 3 && after.sampled_actor_id == 9 && before.sampled_round == 10,
        "each scenario set retains the actor and round of its private score sample");
    check(evaluate_schedule(retain_and_bank(), before, settings, {10, 9}, 0).issue == Issue::history_mismatch,
        "same-round private scenario histories cannot be swapped between actors");
    check(evaluate_schedule(retain_and_bank(), before, settings, {11, 3}, 0).issue == Issue::history_mismatch,
        "private scenario history cannot be reused across round boundaries");
    ScoreHistory near_end;
    check(near_end.sample({1995, 1}, 999), "near-end score sample accepted");
    scenario = near_end.windows(settings, {1995, 1}, 999);
    check(scenario.deadlines == std::array<int, 3>{5, 5, 1} && scenario.windows[0] == 5,
        "queried official remaining rounds cap zero-rate deadline");
    ScoreHistory large;
    const int bound = std::numeric_limits<int>::max();
    check(large.sample({0, 1}, 0) && large.sample({64, 1}, bound - 1), "large score fixtures accepted");
    scenario = large.windows({bound, bound, 1}, {64, 1}, bound - 1);
    check(scenario.valid && scenario.deadlines[1] == 1 && scenario.deadlines[2] == 1,
        "deadline products and doubled rates use int64 arithmetic");
}
void ranking_and_win_evidence() {
    const auto full = windows(20, 20, 20);
    auto value = retain_and_bank();
    value.immediate_win = {1, 101, known(true)};
    check(!evaluate_schedule(value, full, settings, clock, 950).immediate_legal_win,
        "future production cannot turn today's insufficient bag into an immediate winning bank");
    auto rank = evaluate_schedule(value, full, settings, clock, 980);
    check(rank.immediate_legal_win, "actual legal witness plus whole-bag threshold enables winning-bank rank");
    auto other_actor_windows = full; other_actor_windows.sampled_actor_id = 2;
    check(!evaluate_schedule(value, other_actor_windows, settings, {0, 2}, 980).immediate_legal_win,
        "visible ally bank capability cannot become authenticated self's immediate winning action");
    value.immediate_win.legal_now = {};
    check(!evaluate_schedule(value, full, settings, clock, 980).immediate_legal_win,
        "large bag alone cannot certify immediate bank legality");
    auto fast = rank, slow = rank;
    fast.immediate_legal_win = slow.immediate_legal_win = false;
    fast.occupancy = 20; slow.occupancy = 21;
    check(compare(fast, slow) > 0, "fewer occupied actor turns breaks equal-score tie");
    slow.occupancy = 20; fast.peak_bag = 30; slow.peak_bag = 40;
    check(compare(fast, slow) > 0, "smaller exposed bag follows occupancy tie");
    slow.peak_bag = 30; fast.productive_arrival = 10; slow.productive_arrival = 11;
    check(compare(fast, slow) > 0, "earlier return to next productive waypoint follows bag tie");
    slow.productive_arrival = 10; fast.target_id = 3; slow.target_id = 4;
    check(compare(fast, slow) > 0, "stable lower target ID breaks final tie");
    const Settings end_settings{20, 1000, 5};
    value.terminal_tiebreak = {known<Gems>(100), known<Gems>(2), true};
    auto terminal = evaluate_schedule(value, full, end_settings, clock, 0);
    check(terminal.terminal_endpoint && terminal.terminal_terms && terminal.earned_gold_delta == 100,
        "earned-gold/kills terms are available at actual official endpoint with represented continuation");
    auto ordinary = evaluate_schedule(value, full, settings, clock, 0);
    check(!ordinary.terminal_terms, "gold/kills cannot add ordinary-window score weight");
    auto missing = terminal; missing.terminal_terms = false; missing.occupancy = 0;
    check(compare(terminal, missing) == 0, "unknown terminal tiebreak cannot be replaced with zero or favorable occupancy");
    value.actors[0].events[2].waypoint_id = -1;
    check(evaluate_schedule(value, full, settings, clock, 0).issue == Issue::missing_renewal,
        "liquidation without named next productive waypoint is unresolved");
}
} // namespace
int main() {
    oracle_and_bounded_events(); matched_helper_counterfactuals(); sharing_and_affordability();
    score_history_and_interleaving(); ranking_and_win_evidence();
    std::printf("plan6-accounting: %d checks passed\n", checks); return 0;
}
