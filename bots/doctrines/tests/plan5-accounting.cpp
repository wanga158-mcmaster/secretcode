#include "../common/src/accounting.h"
#include <cstdio>
#include <cstdlib>

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #condition); \
    std::abort(); } } while (false)

using namespace p5;

static FinishWindows windows(int calm = 20, int nominal = 20, int stress = 20) {
    FinishWindows out;
    out.valid = true;
    out.official_remaining = 120;
    out.deadline = out.window = {calm, nominal, stress};
    return out;
}
static AccountingSchedule base(AccountingStock bag = 20, int actors = 1) {
    AccountingSchedule out;
    out.actor_count = actors;
    out.win_score = 1000; // synthetic settings, deliberately supplied by fixture
    out.tribute_gems_per_tick = 10;
    out.target_id = 7;
    for (int i = 0; i < actors; ++i) {
        auto& a = out.actors[i];
        a.id = i + 10; a.self = i == 0; a.visible = true;
        a.bag_known = true; a.bag = i == 0 ? bag : 0;
        a.occupied_turns = 20;
    }
    return out;
}
static void add(AccountingSchedule& s, AccountingEventKind kind, int tick, int actor,
                AccountingStock gems = 0, AccountingStock gold = 0,
                int group = -1, unsigned participants = 0, unsigned holders = 0) {
    CHECK(s.event_count < accounting_event_cap);
    AccountingEvent e;
    e.id = s.event_count + 1; e.kind = kind; e.tick = tick; e.actor = actor;
    e.gems = gems; e.gold = gold; e.group = group;
    e.participants = participants; e.holders = holders; e.waypoint = 500 + actor;
    if (kind == AccountingEventKind::income || kind == AccountingEventKind::refund)
        e.payout_id = e.id;
    s.events[s.event_count++] = e;
}
static void bank(AccountingSchedule& s, int start, int finish, int actor = 0) {
    add(s, AccountingEventKind::tribute_start, start, actor, -1);
    add(s, AccountingEventKind::tribute_complete, finish, actor, -1);
}
static void renew(AccountingSchedule& s, int tick, int actor = 0) {
    add(s, AccountingEventKind::return_to_work, tick, actor);
}
static AccountingSchedule early() {
    auto s = base();
    bank(s, 0, 2);
    add(s, AccountingEventKind::capture, 6, -1, 10, 0, 101, 1, 1);
    bank(s, 11, 12); renew(s, 16);
    return s;
}
static AccountingSchedule retain() {
    auto s = base();
    add(s, AccountingEventKind::capture, 5, -1, 30, 0, 102, 1, 1);
    bank(s, 9, 14); renew(s, 18);
    return s;
}
static AccountingResult checked(const AccountingSchedule& s, FinishWindows w = windows()) {
    auto out = forecast_schedule(s, w);
    if (out.status != AccountingStatus::feasible)
        std::fprintf(stderr, "Unexpected accounting rejection: %s\n", out.reason);
    CHECK(out.status == AccountingStatus::feasible);
    return out;
}
static void hand_oracle() {
    // Independently specified event arithmetic: 20+10=30; 20+30=50.
    auto a = checked(early()); auto b = checked(retain());
    CHECK(a.Y[0] == 30 && b.Y[0] == 50);
    CHECK(compare_results(b, a) > 0);
    a = checked(early(), windows(20,20,10));
    b = checked(retain(), windows(20,20,10));
    CHECK(a.Y[2] == 20 && b.Y[2] == 0);
    CHECK(compare_results(a, b) > 0);
    auto unbanked = retain(); unbanked.event_count = 1;
    renew(unbanked, 18);
    auto result = forecast_schedule(unbanked, windows());
    CHECK(result.status == AccountingStatus::unresolved && result.Y[0] == 0);
    auto no_return = early(); --no_return.event_count;
    CHECK(forecast_schedule(no_return, windows()).status == AccountingStatus::unresolved);
}
static void helper_counterfactual() {
    auto cooperate = base(20, 2);
    add(cooperate, AccountingEventKind::capture, 5, -1, 30, 0, 102, 3, 1);
    bank(cooperate, 9, 14); renew(cooperate, 18); renew(cooperate, 18, 1);
    auto together = checked(cooperate);
    CHECK(together.Y[0] == 50 && together.final_bag[1] == 0);
    auto independent = base(20, 2); bank(independent, 0, 2);
    add(independent, AccountingEventKind::capture, 5, -1, 10, 0, 201, 2, 2);
    bank(independent, 7, 8, 1); renew(independent, 18); renew(independent, 18, 1);
    auto apart = checked(independent);
    CHECK(apart.Y[0] == 30 && same_actors(together, apart));
    CHECK(compare_results(together, apart) > 0);
    independent.events[2].gems = 40; independent.events[4].tick = 11;
    apart = checked(independent);
    CHECK(apart.Y[0] == 60 && compare_results(apart, together) > 0);
    auto different_actors = apart; different_actors.actor_ids[1] = 99;
    CHECK(!same_actors(together, different_actors));
    CHECK(compare_results(together, different_actors) == 0);
}
static void rejection_and_order() {
    auto dup = early();
    add(dup, AccountingEventKind::capture, 8, -1, 10, 0, 101, 1, 1);
    CHECK(forecast_schedule(dup, windows()).status == AccountingStatus::invalid);
    dup = early(); dup.events[1].id = dup.events[0].id;
    CHECK(forecast_schedule(dup, windows()).status == AccountingStatus::invalid);
    auto partial = early(); partial.events[0].gems = 10;
    CHECK(forecast_schedule(partial, windows()).status == AccountingStatus::invalid);
    partial = early(); partial.events[1].gems = 10;
    CHECK(forecast_schedule(partial, windows()).status == AccountingStatus::invalid);
    auto simultaneous = base(10);
    bank(simultaneous, 2, 3);
    // Array order is deliberately channel completion, then capture; engine
    // phase order still supplies all 18 gems to that completion.
    add(simultaneous, AccountingEventKind::capture, 3, -1, 8, 0, 103, 1, 1);
    renew(simultaneous, 5);
    CHECK(checked(simultaneous).Y[0] == 18);
    auto split = base(0, 2);
    split.actors[0].id = 9; split.actors[1].id = 2;
    add(split, AccountingEventKind::capture, 0, -1, 11, 13, 104, 3, 3);
    bank(split, 0, 1, 0); bank(split, 0, 1, 1);
    renew(split, 2, 0); renew(split, 2, 1);
    auto result = checked(split);
    CHECK(result.Y[0] == 11 && result.final_gold[0] == 6 && result.final_gold[1] == 7);
    CHECK(result.peak_bag == 6 && result.earned_gold == 13);
    auto unknown = early(); unknown.actors[0].bag_known = false;
    CHECK(forecast_schedule(unknown, windows()).status == AccountingStatus::unresolved);
    unknown = base(0, 2); unknown.actors[1].visible = false;
    CHECK(forecast_schedule(unknown, windows()).status == AccountingStatus::invalid);
    unknown.actors[1].visible = true; unknown.actors[1].gold = 1;
    CHECK(forecast_schedule(unknown, windows()).status == AccountingStatus::invalid);
}
static void financing_and_rebuilding() {
    auto finance = base(20);
    finance.actors[0].tribute_gold_per_gem = 2; // queried synthetic Marcella passive
    bank(finance, 0, 2);
    add(finance, AccountingEventKind::purchase, 3, 0, 0, 40);
    add(finance, AccountingEventKind::capture, 7, -1, 10, 8, 105, 1, 1);
    bank(finance, 9, 10); renew(finance, 15);
    auto result = checked(finance);
    CHECK(result.Y[0] == 30 && result.final_bag[0] == 0);
    CHECK(result.final_gold[0] == 28 && result.earned_gold == 68);
    auto future = finance; future.events[2].tick = 1;
    CHECK(forecast_schedule(future, windows()).status == AccountingStatus::invalid);
    auto over = finance; over.events[2].gold = 41;
    CHECK(forecast_schedule(over, windows()).status == AccountingStatus::invalid);
    auto refunded = finance;
    add(refunded, AccountingEventKind::refund, 4, 0, 0, 9);
    result = checked(refunded);
    CHECK(result.final_gold[0] == 37 && result.earned_gold == 68);
    auto twice = finance; add(twice, AccountingEventKind::purchase, 12, 0, 0, 1);
    CHECK(forecast_schedule(twice, windows()).status == AccountingStatus::invalid);
    auto repeated_refund = refunded;
    add(repeated_refund, AccountingEventKind::refund, 5, 0, 0, 9);
    repeated_refund.events[repeated_refund.event_count-1].payout_id =
        repeated_refund.events[repeated_refund.event_count-2].payout_id;
    CHECK(forecast_schedule(repeated_refund, windows()).status == AccountingStatus::invalid);
    auto queried_min = base(1); queried_min.tribute_min_ticks = 3;
    bank(queried_min, 0, 3); renew(queried_min, 4);
    CHECK(checked(queried_min).Y[0] == 1);
    queried_min.events[1].tick = 1;
    CHECK(forecast_schedule(queried_min, windows()).status == AccountingStatus::invalid);
    auto no_rebuild = base(20); bank(no_rebuild, 0, 2);
    CHECK(forecast_schedule(no_rebuild, windows()).status == AccountingStatus::unresolved);
    no_rebuild.win_score = 20; no_rebuild.terminal_win = true;
    no_rebuild.terminal_win_tick = 2; no_rebuild.immediate_legal_winning_bank = true;
    CHECK(checked(no_rebuild).immediate_win);
    no_rebuild.win_score = 21;
    CHECK(forecast_schedule(no_rebuild, windows()).status == AccountingStatus::invalid);
    auto overflow = finance;
    overflow.actors[0].tribute_gold_per_gem = std::numeric_limits<AccountingStock>::max();
    CHECK(forecast_schedule(overflow, windows()).status == AccountingStatus::invalid);
    auto late_win = base(20); bank(late_win, 4, 6); late_win.win_score = 20;
    late_win.terminal_win = true; late_win.terminal_win_tick = 6;
    late_win.immediate_legal_winning_bank = true;
    CHECK(forecast_schedule(late_win, windows()).status == AccountingStatus::invalid);
}
static void terminal_tiebreak() {
    auto a = early(), b = early();
    add(a, AccountingEventKind::income, 4, 0, 0, 9);
    add(b, AccountingEventKind::refund, 4, 0, 0, 20);
    auto endpoint = windows(); endpoint.official_remaining = 20;
    auto scored_a = checked(a, endpoint), scored_b = checked(b, endpoint);
    CHECK(scored_a.Y[0] == scored_b.Y[0]);
    CHECK(scored_a.earned_gold == 9 && scored_b.earned_gold == 0);
    CHECK(compare_results(scored_a, scored_b) > 0);
    auto other_windows = checked(a, windows(20,20,10));
    CHECK(compare_results(scored_a, other_windows) == 0);
    auto denied = early(); denied.doctrine_id = 12; denied.denial_evidenced = true;
    denied.denied_gems = 7; denied.denied_bank_tick = 9;
    CHECK(checked(denied).M[0] == 37);
    denied.denial_evidenced = false;
    CHECK(forecast_schedule(denied, windows()).status == AccountingStatus::invalid);
}
static void score_history() {
    ScoreHistory h;
    CHECK(h.observe(10, 0));
    CHECK(!h.observe(10, 5));
    auto first = h.windows(10, 100, 100, 0);
    CHECK(first.valid && first.rate[1] == 0 && first.rate[2] == 1);
    CHECK(first.window[0] == 90 && first.window[2] == 90);
    CHECK(h.observe(20, 20));
    auto next = h.windows(20, 100, 100, 20);
    CHECK(next.rate[1] == 2 && next.rate[2] == 4);
    CHECK(next.window[0] == 80 && next.window[1] == 40 && next.window[2] == 20);
    CHECK(!h.windows(20,100,100,21).valid);
    for (int t = 21; t <= 90; ++t) CHECK(h.observe(t, 2*t - 20));
    next = h.windows(90, 200, 1000, 160);
    CHECK(next.rate[1] == 2); // oldest permitted sample is round26, retained65
    CHECK(!h.observe(89, 0));
    ScoreHistory sparse; CHECK(sparse.observe(0,0)); CHECK(sparse.observe(100,10));
    CHECK(sparse.windows(100,200,1000,10).rate[1] == 0);
    ScoreHistory zero; CHECK(zero.observe(100,50)); CHECK(zero.observe(101,40));
    CHECK(zero.windows(101,200,1000,40).rate[1] == 0);
}
int main() {
    hand_oracle(); helper_counterfactual(); rejection_and_order();
    financing_and_rebuilding(); terminal_tiebreak(); score_history();
    std::puts("Plan 5 accounting: hand oracle, conservation, ownership, funding and deadlines pass.");
}
