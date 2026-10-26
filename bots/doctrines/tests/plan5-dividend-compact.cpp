#include "../dividend/src/policy.h"
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <type_traits>

using namespace p5;
namespace d = p5::dividend;
static int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::fprintf(stderr, "Compact check %d failed at line %d: %s\n", checks, __LINE__, #condition); \
    std::abort(); } } while (false)

// Retained pre-template declaration: Build25 accounting.h, SHA256
// 08ef1f8749a5d71802c0bb22399d873b09eb00c01d4c0311d5e5a02ea0745f69.
// Actor/event record definitions are unchanged by this storage-only increment.
struct LegacyAccountingSchedule {
    std::array<AccountingActor, accounting_actor_cap> actors{};
    int actor_count = 0;
    std::array<AccountingEvent, accounting_event_cap> events{};
    int event_count = 0;
    int target_id = -1;
    AccountingStock starting_team_score = 0;
    AccountingStock win_score = 0;
    int tribute_gems_per_tick = 0;
    int tribute_min_ticks = 1;
    bool terminal_win = false;
    int terminal_win_tick = -1;
    bool immediate_legal_winning_bank = false;
    int doctrine_id = 0;
    bool denial_evidenced = false;
    AccountingStock denied_gems = 0;
    int denied_bank_tick = -1;
    int projected_kills = 0;
};
static_assert(std::is_standard_layout_v<AccountingSchedule> && std::is_aggregate_v<AccountingSchedule>);
static_assert(sizeof(AccountingSchedule) == sizeof(LegacyAccountingSchedule));
static_assert(alignof(AccountingSchedule) == alignof(LegacyAccountingSchedule));
static_assert(std::is_same_v<AccountingSchedule, AccountingScheduleStorage<6, 64>>);
static_assert(std::is_same_v<d::DividendSoloSchedule, AccountingScheduleStorage<1, 7>>);
static_assert(sizeof(d::DividendSoloSchedule) < sizeof(AccountingSchedule));
#define SAME_OFFSET(field) static_assert(offsetof(AccountingSchedule, field) == offsetof(LegacyAccountingSchedule, field))
SAME_OFFSET(actors); SAME_OFFSET(actor_count); SAME_OFFSET(events); SAME_OFFSET(event_count);
SAME_OFFSET(target_id); SAME_OFFSET(starting_team_score); SAME_OFFSET(win_score);
SAME_OFFSET(tribute_gems_per_tick); SAME_OFFSET(tribute_min_ticks); SAME_OFFSET(terminal_win);
SAME_OFFSET(terminal_win_tick); SAME_OFFSET(immediate_legal_winning_bank); SAME_OFFSET(doctrine_id);
SAME_OFFSET(denial_evidenced); SAME_OFFSET(denied_gems); SAME_OFFSET(denied_bank_tick); SAME_OFFSET(projected_kills);
#undef SAME_OFFSET

#define SCHEDULE_SCALARS(X) \
    X(actor_count); X(event_count); X(target_id); X(starting_team_score); X(win_score); \
    X(tribute_gems_per_tick); X(tribute_min_ticks); X(terminal_win); X(terminal_win_tick); \
    X(immediate_legal_winning_bank); X(doctrine_id); X(denial_evidenced); \
    X(denied_gems); X(denied_bank_tick); X(projected_kills)
static void default_layout() {
    const AccountingSchedule current;
    const LegacyAccountingSchedule legacy;
#define SAME_DEFAULT(field) CHECK(current.field == legacy.field)
    SCHEDULE_SCALARS(SAME_DEFAULT);
#undef SAME_DEFAULT
    for (std::size_t i = 0; i < current.actors.size(); ++i) {
        const auto& a = current.actors[i]; const auto& b = legacy.actors[i];
        CHECK(a.id == b.id && a.self == b.self && a.visible == b.visible && a.bag_known == b.bag_known);
        CHECK(a.bag == b.bag && a.gold == b.gold && a.tribute_gold_per_gem == b.tribute_gold_per_gem);
        CHECK(a.occupied_turns == b.occupied_turns);
    }
    for (std::size_t i = 0; i < current.events.size(); ++i) {
        const auto& a = current.events[i]; const auto& b = legacy.events[i];
        CHECK(a.id == b.id && a.tick == b.tick && a.kind == b.kind && a.actor == b.actor);
        CHECK(a.group == b.group && a.payout_id == b.payout_id && a.participants == b.participants && a.holders == b.holders);
        CHECK(a.gems == b.gems && a.gold == b.gold && a.waypoint == b.waypoint && a.legal == b.legal);
    }
    std::printf("COMPACT layout default=%zu compact=%zu decision=%zu actor-cap=1 event-cap=7\n",
        sizeof(AccountingSchedule), sizeof(d::DividendSoloSchedule), sizeof(d::Decision));
}
static AccountingSchedule widen(const d::DividendSoloSchedule& source) {
    AccountingSchedule out;
#define COPY_SCALAR(field) out.field = source.field
    SCHEDULE_SCALARS(COPY_SCALAR);
#undef COPY_SCALAR
    // Copy physical prefixes, never use a deliberately malformed count here.
    for (std::size_t i = 0; i < source.actors.size(); ++i) out.actors[i] = source.actors[i];
    for (std::size_t i = 0; i < source.events.size(); ++i) out.events[i] = source.events[i];
    return out;
}
#undef SCHEDULE_SCALARS
static void same_result(const AccountingResult& a, const AccountingResult& b) {
    CHECK(a.status == b.status); CHECK(std::strcmp(a.reason, b.reason) == 0);
#define SAME_RESULT(field) CHECK(a.field == b.field)
    SAME_RESULT(Y); SAME_RESULT(D); SAME_RESULT(M); SAME_RESULT(final_bag); SAME_RESULT(final_gold);
    SAME_RESULT(actor_ids); SAME_RESULT(actor_count); SAME_RESULT(earned_gold); SAME_RESULT(kills);
    SAME_RESULT(occupied_turns); SAME_RESULT(peak_bag); SAME_RESULT(productive_arrival); SAME_RESULT(target_id);
    SAME_RESULT(windows); SAME_RESULT(immediate_win); SAME_RESULT(official_endpoint);
#undef SAME_RESULT
}
static AccountingResult parity(const d::DividendSoloSchedule& small, const FinishWindows& windows) {
    const auto a = forecast_schedule(small, windows);
    same_result(a, forecast_schedule(widen(small), windows));
    return a;
}
static RouteLeg route(int destination) { return {destination, 0, true, true}; }
static d::Observation input() {
    d::Observation o;
    o.settings = {2, 10, 1, 200, 1000, 2}; // Explicit synthetic settings.
    o.windows.valid = true; o.windows.official_remaining = 200;
    o.windows.deadline = o.windows.window = {120, 30, 6};
    o.self.id = 7; o.self.self = o.self.visible = o.self.own_team = true;
    o.self.bag = {20, Knowledge::current, 0}; o.self.gold = {5, Knowledge::current, 0};
    o.stats.id = 7; o.stats.self = o.stats.visible = true;
    o.stats.health = o.stats.max_health = 200; o.stats.strength = 20;
    o.current_to_altar = route(50); o.altar_to_spawn = o.current_to_spawn = route(60);
    o.bank_return = route(900); o.bank_legal = o.bank_route_safe = true;
    o.count = 1;
    auto& g = o.opportunities[0]; g.group = 44; g.gems = 10; g.gold = 4;
    g.live_observed = g.reward_or_alive = true;
    g.from_current = g.from_spawn = g.from_altar = route(44);
    g.to_altar = route(50); g.next_productive = route(900);
    g.combat.actor_count = g.combat.monster_count = 1;
    g.combat.actors[0] = o.stats; g.combat.monsters[0] = {90, 40, 40, 3, true};
    o.purchase_count = 1; o.purchases[0] = {0, 1, 30, 10, 0, 0, 0, true};
    return o;
}
static d::Decision financed() {
    const auto o = input(); return d::project(o, o.opportunities[0], true, &o.purchases[0]);
}
static void valid_paths() {
    const auto o = input();
    const auto check = [&](const d::Decision& decision, d::Job job, int events, AccountingStock score,
                           AccountingStock gold, AccountingStock earned) {
        CHECK(decision.feasible && decision.job == job);
        CHECK(decision.schedule.actor_count == 1 && decision.schedule.event_count == events);
        const auto rank = parity(decision.schedule, o.windows);
        same_result(rank, decision.rank);
        CHECK(rank.status == AccountingStatus::feasible && rank.Y[0] == score);
        CHECK(rank.final_bag[0] == 0 && rank.final_gold[0] == gold && rank.earned_gold == earned);
        CHECK(rank.actor_count == 1 && rank.actor_ids[0] == 7);
    };
    check(d::project(o, o.opportunities[0], false), d::Job::produce_bank, 4, 30, 69, 64);
    check(d::project(o, o.opportunities[0], true), d::Job::bank_produce, 6, 30, 69, 64);
    const auto finance = financed();
    check(finance, d::Job::finance_produce, 7, 30, 39, 64);
    const std::array<AccountingEventKind, 7> order{AccountingEventKind::tribute_start,
        AccountingEventKind::tribute_complete, AccountingEventKind::purchase, AccountingEventKind::capture,
        AccountingEventKind::tribute_start, AccountingEventKind::tribute_complete, AccountingEventKind::return_to_work};
    for (std::size_t i = 0; i < order.size(); ++i) CHECK(finance.schedule.events[i].kind == order[i]);
    auto cash = o; cash.self.bag.value = 0; cash.self.gold.value = 32;
    check(d::project(cash, cash.opportunities[0], false, &cash.purchases[0], nullptr, true),
        d::Job::cash_produce, 5, 10, 26, 24);
    check(d::bank_only(o), d::Job::bank_now, 3, 20, 45, 40);
    auto winning = o; winning.team_score = 980;
    const auto terminal = d::bank_only(winning);
    check(terminal, d::Job::bank_now, 2, 20, 45, 40);
    CHECK(terminal.schedule.terminal_win && terminal.rank.immediate_win);
    // Exercise all result metadata, including nonzero generic denial/kills.
    auto metadata = finance.schedule; metadata.doctrine_id = 11; metadata.denial_evidenced = true;
    metadata.denied_gems = 3; metadata.denied_bank_tick = 5; metadata.projected_kills = 2;
    const auto varied = parity(metadata, o.windows);
    CHECK(varied.status == AccountingStatus::feasible && varied.D[0] == 3 && varied.M[0] == 33 && varied.kills == 2);
    std::printf("COMPACT six-path parity financed=7 events score=30 gold=39; cash=5 score=10 gold=26\n");
}
static void invalid_content() {
    const auto o = input(); const auto good = financed().schedule;
    const auto invalid = [&](d::DividendSoloSchedule s, const char* reason) {
        const auto result = parity(s, o.windows);
        CHECK(result.status == AccountingStatus::invalid && std::strcmp(result.reason, reason) == 0);
    };
    auto s = good; s.events[2].id = s.events[1].id; invalid(s, "duplicate event");
    s = good; s.events[0].payout_id = s.events[1].payout_id = 9; invalid(s, "duplicate income payout");
    s = good; s.events[0].kind = AccountingEventKind::capture; s.events[0].group = 44;
    invalid(s, "duplicate group payout");
    for (const auto masks : {std::array<unsigned, 2>{3, 1}, {1, 2}, {1, 0}, {0, 1}}) {
        s = good; s.events[3].participants = masks[0]; s.events[3].holders = masks[1];
        invalid(s, "invalid capture holder set");
    }
    s = good; s.events[2].actor = 1; invalid(s, "invalid actor index");
    s = good; s.events[0].gems = 19; invalid(s, "partial or overlapping tribute start");
    s = good; s.events[1].gems = 19; invalid(s, "partial, duplicate or mistimed tribute");
    s = good; ++s.events[1].tick; invalid(s, "partial, duplicate or mistimed tribute");
    s = d::project(o, o.opportunities[0], false).schedule;
    s.actors[0].bag = std::numeric_limits<AccountingStock>::max(); invalid(s, "capture accounting overflow");
    s = d::project(o, o.opportunities[0], false).schedule;
    s.actors[0].gold = std::numeric_limits<AccountingStock>::max(); invalid(s, "capture accounting overflow");
    s = d::bank_only(o).schedule; s.actors[0].bag = 2;
    s.actors[0].tribute_gold_per_gem = std::numeric_limits<AccountingStock>::max(); s.events[1].tick = 1;
    invalid(s, "tribute income overflow");
}
static void capacity_and_append() {
    const auto o = input(); const auto good = financed().schedule;
    const auto bounds = [&](const d::DividendSoloSchedule& s) {
        const auto r = forecast_schedule(s, o.windows);
        CHECK(r.status == AccountingStatus::invalid && std::strcmp(r.reason, "invalid bounds or queried settings") == 0);
    };
    for (int count : {-1, 0, 2, 6, 7, std::numeric_limits<int>::max()}) {
        auto s = good; s.actor_count = count; s.events[0].id = -99; bounds(s);
    }
    for (int count : {-1, 8, 64, 65, std::numeric_limits<int>::max()}) {
        auto s = good; s.event_count = count; s.actors[0].id = -99; bounds(s);
    }
    auto full = good; const auto before = full.events;
    d::event(full, AccountingEventKind::return_to_work, 100, 0, 0, -1, 901);
    CHECK(full.event_count == 8); bounds(full);
    for (std::size_t i = 0; i < before.size(); ++i) {
        const auto& a = before[i]; const auto& b = full.events[i];
        CHECK(a.id == b.id && a.tick == b.tick && a.kind == b.kind && a.actor == b.actor);
        CHECK(a.group == b.group && a.payout_id == b.payout_id && a.participants == b.participants && a.holders == b.holders);
        CHECK(a.gems == b.gems && a.gold == b.gold && a.waypoint == b.waypoint && a.legal == b.legal);
    }
    d::event(full, AccountingEventKind::capture, 110, 1, 0, 99);
    d::bank(full, o.settings, 111, 1); CHECK(full.event_count == 8); bounds(full);
    auto negative = good; negative.event_count = -1;
    d::event(negative, AccountingEventKind::return_to_work, 100); CHECK(negative.event_count == 8); bounds(negative);
    auto last = good; last.event_count = 6;
    d::event(last, AccountingEventKind::return_to_work, good.events[6].tick, 0, 0, -1, 900);
    CHECK(last.event_count == 7); CHECK(parity(last, o.windows).status == AccountingStatus::feasible);
    // A two-event bank with only one slot cannot make a valid partial prefix.
    auto partial = good; partial.event_count = 6;
    d::bank(partial, o.settings, 100, 1); CHECK(partial.event_count == 8); bounds(partial);
}
static void boundaries_and_default_capacity() {
    const auto o = input(); const auto finance = financed().schedule;
    const auto bank_tick = finance.events[5].tick;
    for (int finish : {bank_tick - 1, bank_tick, bank_tick + 1}) {
        auto windows = o.windows; windows.deadline = windows.window = {finish, finish, finish};
        const auto r = parity(finance, windows);
        CHECK(r.status == AccountingStatus::feasible);
        CHECK(r.Y[0] == (finish < bank_tick ? 20 : 30));
    }
    auto unfinished = finance; unfinished.event_count = 6;
    CHECK(parity(unfinished, o.windows).status == AccountingStatus::unresolved);
    auto w = o.windows; w.official_remaining = finance.events[6].tick - 1;
    w.deadline = w.window = {w.official_remaining, w.official_remaining, w.official_remaining};
    CHECK(parity(finance, w).status == AccountingStatus::invalid);
    auto maximum = widen(d::initial_schedule(o, 44));
    maximum.actors[0].bag = maximum.actors[0].gold = 0; maximum.actors[0].occupied_turns = 0;
    maximum.event_count = 64;
    for (int i = 0; i < 64; ++i) {
        auto& e = maximum.events[i]; e.id = i; e.tick = 0; e.actor = 0;
        e.kind = i == 63 ? AccountingEventKind::return_to_work : AccountingEventKind::income;
        e.gold = i == 63 ? 0 : 1; e.payout_id = 100 + i; e.waypoint = 900;
    }
    const auto max_rank = forecast_schedule(maximum, o.windows);
    CHECK(max_rank.status == AccountingStatus::feasible && max_rank.final_gold[0] == 63);
    maximum.event_count = 65; CHECK(forecast_schedule(maximum, o.windows).status == AccountingStatus::invalid);
    auto six = widen(d::initial_schedule(o, 44)); six.actor_count = six.event_count = 6;
    for (int i = 0; i < 6; ++i) {
        auto& a = six.actors[i]; a = {}; a.id = 7 + i; a.self = i == 0; a.visible = a.bag_known = true;
        auto& e = six.events[i]; e = {}; e.id = i; e.tick = 1; e.actor = i;
        e.kind = AccountingEventKind::return_to_work; e.waypoint = 900 + i;
    }
    const auto six_rank = forecast_schedule(six, o.windows);
    CHECK(six_rank.status == AccountingStatus::feasible && six_rank.actor_count == 6);
}
int main() {
    default_layout(); valid_paths(); invalid_content(); capacity_and_append(); boundaries_and_default_capacity();
    std::printf("Dividend compact/default checks passed: %d (pure synthetic, no native/fuel claim)\n", checks);
    return 0;
}
