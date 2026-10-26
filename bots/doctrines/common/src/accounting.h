#pragma once

// Offline event accounting only. Geometry, combat, channel safety and actual
// payout reobservation remain caller obligations; this header imports no SDK.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace p5 {
inline constexpr int accounting_actor_cap = 6;
inline constexpr int accounting_event_cap = 64;
using AccountingStock = std::int64_t;

struct FinishWindows {
    std::array<int, 3> deadline{};
    std::array<int, 3> window{};
    std::array<double, 3> rate{};
    int official_remaining = 0;
    bool valid = false;
};

class ScoreHistory {
    struct Sample { int round = -1; AccountingStock score = 0; };
    std::array<Sample, 65> samples_{};
    int next_ = 0;
    int last_round_ = -1;
public:
    // Duplicate-round calls leave the first observation intact. Time reversal
    // and negative scores are rejected rather than silently changing history.
    bool observe(int round, AccountingStock score) {
        if (round < 0 || score < 0 || round <= last_round_) return false;
        samples_[next_] = {round, score};
        next_ = (next_ + 1) % 65;
        last_round_ = round;
        return true;
    }
    FinishWindows windows(int round, int round_limit, AccountingStock win_score,
                          AccountingStock enemy_score) const {
        FinishWindows out;
        if (round < 0 || round_limit < round || win_score <= 0 || enemy_score < 0 ||
            last_round_ != round) return out;
        const Sample* oldest = nullptr;
        const Sample* current = nullptr;
        for (const auto& sample : samples_) {
            if (sample.round == round) current = &sample;
            if (sample.round >= 0 && sample.round >= round - 64 &&
                sample.round <= round && (!oldest || sample.round < oldest->round))
                oldest = &sample;
        }
        if (!oldest || !current || current->score != enemy_score) return out;
        const double rate = double(std::max<AccountingStock>(0, enemy_score - oldest->score)) /
                            std::max(1, round - oldest->round);
        out.official_remaining = round_limit - round;
        out.rate = {0.0, rate, std::max(1.0, 2.0 * rate)};
        const AccountingStock needed = std::max<AccountingStock>(0, win_score - enemy_score);
        for (int s = 0; s < 3; ++s) {
            double finish = out.rate[s] == 0 ? out.official_remaining :
                            std::ceil(double(needed) / out.rate[s]);
            out.deadline[s] = int(std::min<double>(out.official_remaining, finish));
            out.window[s] = std::min(120, out.deadline[s]);
        }
        out.valid = true;
        return out;
    }
};

struct AccountingActor {
    int id = -1;
    bool self = false;
    bool visible = false;
    bool bag_known = false;
    AccountingStock bag = 0;
    // Self: actual observed gold. Other visible actors: MUST be zero; only
    // their attributable future income can fund a lower-bound purchase.
    AccountingStock gold = 0;
    AccountingStock tribute_gold_per_gem = 0; // queried own passive, else zero
    int occupied_turns = 0; // includes assembly, escort, recovery, return
};

enum class AccountingEventKind {
    capture, tribute_start, tribute_complete, purchase, income, refund,
    bag_loss, return_to_work
};
struct AccountingEvent {
    int id = -1; // globally unique within this schedule
    int tick = 0;
    AccountingEventKind kind = AccountingEventKind::return_to_work;
    int actor = -1; // array index, except capture
    int group = -1; // capture's public group identity
    int payout_id = -1; // attributable income/refund source; unique if present
    unsigned participants = 0; // capture: all affected working actors
    unsigned holders = 0; // capture: actual actors present for payout
    AccountingStock gems = 0; // capture total, tribute amount (-1=whole bag), loss
    AccountingStock gold = 0; // capture total, purchase cost, income, refund
    int waypoint = -1; // named productive waypoint for return_to_work
    bool legal = true; // caller certifies channel/shop/route prerequisites
};
template<std::size_t ActorCapacity = accounting_actor_cap,
         std::size_t EventCapacity = accounting_event_cap>
struct AccountingScheduleStorage {
    static_assert(ActorCapacity > 0 && ActorCapacity <= accounting_actor_cap);
    static_assert(EventCapacity > 0 && EventCapacity <= accounting_event_cap);
    std::array<AccountingActor, ActorCapacity> actors{};
    int actor_count = 0;
    std::array<AccountingEvent, EventCapacity> events{};
    int event_count = 0;
    int target_id = -1;
    AccountingStock starting_team_score = 0;
    AccountingStock win_score = 0; // queried setting, mandatory
    int tribute_gems_per_tick = 0; // queried setting, mandatory
    int tribute_min_ticks = 1; // queried minimum; caller must override if different
    // Terminal schedules may omit return only after a represented winning bank.
    bool terminal_win = false;
    int terminal_win_tick = -1;
    // Caller additionally certifies an immediate bank is legally available.
    bool immediate_legal_winning_bank = false;
    int doctrine_id = 0;
    bool denial_evidenced = false;
    AccountingStock denied_gems = 0; // at most one enemy cycle
    int denied_bank_tick = -1;
    int projected_kills = 0;
};
using AccountingSchedule = AccountingScheduleStorage<>;
enum class AccountingStatus { invalid, unresolved, feasible };
struct AccountingResult {
    AccountingStatus status = AccountingStatus::invalid;
    const char* reason = "unvalidated";
    std::array<AccountingStock, 3> Y{}, D{}, M{};
    std::array<AccountingStock, accounting_actor_cap> final_bag{}, final_gold{};
    std::array<int, accounting_actor_cap> actor_ids{};
    int actor_count = 0;
    AccountingStock earned_gold = 0; // refunds deliberately excluded
    int kills = 0;
    int occupied_turns = 0;
    AccountingStock peak_bag = 0;
    int productive_arrival = 0;
    int target_id = -1;
    std::array<int, 3> windows{};
    bool immediate_win = false;
    bool official_endpoint = false;
};

inline bool same_actors(const AccountingResult& a, const AccountingResult& b) {
    return a.actor_count >= 0 && a.actor_count <= accounting_actor_cap &&
           a.actor_count == b.actor_count &&
           std::equal(a.actor_ids.begin(), a.actor_ids.begin() + a.actor_count,
                      b.actor_ids.begin());
}

inline int accounting_phase(AccountingEventKind kind) {
    // Captures resolve before any simultaneous channel completion.
    if (kind == AccountingEventKind::capture) return 0;
    if (kind == AccountingEventKind::tribute_complete) return 1;
    if (kind == AccountingEventKind::income || kind == AccountingEventKind::refund) return 2;
    if (kind == AccountingEventKind::tribute_start) return 3;
    if (kind == AccountingEventKind::purchase) return 4;
    return 5;
}

template<std::size_t ActorCapacity, std::size_t EventCapacity>
inline AccountingResult forecast_schedule(const AccountingScheduleStorage<ActorCapacity, EventCapacity>& schedule,
                                           const FinishWindows& windows) {
    AccountingResult out;
    auto fail = [&](const char* reason) { out.reason = reason; return out; };
    auto add_stock = [](AccountingStock& stock, AccountingStock amount) {
        if (amount < 0 || stock > std::numeric_limits<AccountingStock>::max() - amount) return false;
        stock += amount;
        return true;
    };
    if (!windows.valid || schedule.actor_count < 1 || schedule.actor_count > 6 ||
        std::size_t(schedule.actor_count) > ActorCapacity ||
        schedule.event_count < 0 || schedule.event_count > accounting_event_cap ||
        std::size_t(schedule.event_count) > EventCapacity ||
        schedule.win_score <= 0 || schedule.tribute_gems_per_tick <= 0 || schedule.tribute_min_ticks <= 0 ||
        schedule.starting_team_score < 0 || schedule.projected_kills < 0)
        return fail("invalid bounds or queried settings");
    for (int s = 0; s < 3; ++s)
        if (windows.window[s] < 0 || windows.window[s] > 120 ||
            windows.window[s] > windows.deadline[s] ||
            windows.deadline[s] > windows.official_remaining)
            return fail("invalid evaluation windows");
    out.actor_count = schedule.actor_count;
    out.target_id = schedule.target_id;
    out.windows = windows.window;
    std::array<int, ActorCapacity> productions{}, tributes{}, purchases{}, due{}, returned{};
    due.fill(-1);
    returned.fill(-1);
    int self_count = 0, self_index = -1;
    for (int i = 0; i < schedule.actor_count; ++i) {
        const auto& actor = schedule.actors[i];
        if (actor.id < 0 || (!actor.self && !actor.visible) || actor.bag < 0 ||
            actor.gold < 0 || (!actor.self && actor.gold != 0) ||
            actor.tribute_gold_per_gem < 0 || actor.occupied_turns < 0)
            return fail("invalid actor or inaccessible gold");
        if (!actor.bag_known) { out.status = AccountingStatus::unresolved;
                               return fail("unknown bag cannot certify a complete cycle"); }
        for (int j = 0; j < i; ++j)
            if (schedule.actors[j].id == actor.id) return fail("duplicate actor");
        self_count += actor.self ? 1 : 0;
        if (actor.self) self_index = i;
        out.actor_ids[i] = actor.id;
        out.final_bag[i] = actor.bag;
        out.final_gold[i] = actor.gold;
        if (actor.occupied_turns > std::numeric_limits<int>::max() - out.occupied_turns)
            return fail("occupied-turn overflow");
        out.occupied_turns += actor.occupied_turns;
        out.peak_bag = std::max(out.peak_bag, actor.bag);
    }
    if (self_count != 1) return fail("exactly one executing self required");
    if (schedule.immediate_legal_winning_bank &&
        schedule.actors[self_index].bag < schedule.win_score - schedule.starting_team_score)
        return fail("immediate winning bank depends on future income");
    std::sort(out.actor_ids.begin(), out.actor_ids.begin() + out.actor_count);
    std::array<int, EventCapacity> order{};
    for (int i = 0; i < schedule.event_count; ++i) {
        order[i] = i;
        const auto& event = schedule.events[i];
        if (event.id < 0 || event.tick < 0 || !event.legal || event.gold < 0 ||
            event.tick > windows.official_remaining)
            return fail("invalid or illegal event");
        for (int j = 0; j < i; ++j) {
            if (event.id == schedule.events[j].id) return fail("duplicate event");
            if (event.payout_id >= 0 && event.payout_id == schedule.events[j].payout_id)
                return fail("duplicate income payout");
            if (event.kind == AccountingEventKind::capture &&
                schedule.events[j].kind == AccountingEventKind::capture &&
                event.group == schedule.events[j].group)
                return fail("duplicate group payout");
        }
    }
    std::sort(order.begin(), order.begin() + schedule.event_count, [&](int a, int b) {
        const auto& x = schedule.events[a]; const auto& y = schedule.events[b];
        if (x.tick != y.tick) return x.tick < y.tick;
        if (accounting_phase(x.kind) != accounting_phase(y.kind))
            return accounting_phase(x.kind) < accounting_phase(y.kind);
        return x.id < y.id;
    });
    AccountingStock all_score = schedule.starting_team_score;
    bool represented_win = false, immediate_started = false;
    for (int k = 0; k < schedule.event_count; ++k) {
        const auto& event = schedule.events[order[k]];
        if (schedule.terminal_win && event.tick > schedule.terminal_win_tick)
            return fail("event after terminal win");
        if (event.kind == AccountingEventKind::capture) {
            const unsigned valid_mask = (1u << schedule.actor_count) - 1u;
            if (event.group < 0 || event.gems < 0 || !event.holders ||
                (event.participants & ~valid_mask) || (event.holders & ~event.participants))
                return fail("invalid capture holder set");
            int holder_count = 0, lowest = -1;
            for (int i = 0; i < schedule.actor_count; ++i) {
                if (event.participants & (1u << i)) {
                    if (++productions[i] > 2) return fail("more than two production groups");
                    returned[i] = -1;
                }
                if (event.holders & (1u << i)) {
                    ++holder_count;
                    if (lowest < 0 || schedule.actors[i].id < schedule.actors[lowest].id) lowest = i;
                }
            }
            for (int i = 0; i < schedule.actor_count; ++i) if (event.holders & (1u << i)) {
                const AccountingStock gold = event.gold / holder_count +
                    (i == lowest ? event.gold % holder_count : 0);
                const AccountingStock gems = event.gems / holder_count +
                    (i == lowest ? event.gems % holder_count : 0);
                if (!add_stock(out.final_bag[i], gems) || !add_stock(out.final_gold[i], gold) ||
                    (event.tick <= windows.window[0] && !add_stock(out.earned_gold, gold)))
                    return fail("capture accounting overflow");
                out.peak_bag = std::max(out.peak_bag, out.final_bag[i]);
            }
            continue;
        }
        const int i = event.actor;
        if (i < 0 || i >= schedule.actor_count) return fail("invalid actor index");
        auto& bag = out.final_bag[i]; auto& gold = out.final_gold[i];
        switch (event.kind) {
        case AccountingEventKind::tribute_start:
            if (due[i] >= 0 || bag <= 0 || (event.gems != -1 && event.gems != bag))
                return fail("partial or overlapping tribute start");
            {
                const AccountingStock duration = std::max<AccountingStock>(schedule.tribute_min_ticks,
                    bag / schedule.tribute_gems_per_tick + (bag % schedule.tribute_gems_per_tick != 0));
                if (duration > windows.official_remaining - event.tick)
                    return fail("tribute completes beyond official horizon");
                due[i] = event.tick + int(duration);
            }
            returned[i] = -1;
            if (i == self_index && event.tick == 0) immediate_started = true;
            break;
        case AccountingEventKind::tribute_complete: {
            if (due[i] != event.tick || bag <= 0 ||
                (event.gems != -1 && event.gems != bag) || ++tributes[i] > 2)
                return fail("partial, duplicate or mistimed tribute");
            const AccountingStock multiplier = schedule.actors[i].tribute_gold_per_gem;
            if (multiplier != 0 && bag > std::numeric_limits<AccountingStock>::max() / multiplier)
                return fail("tribute income overflow");
            const AccountingStock income = bag * multiplier;
            for (int s = 0; s < 3; ++s) if (event.tick <= windows.window[s])
                if (!add_stock(out.Y[s], bag)) return fail("score overflow");
            if (!add_stock(all_score, bag) || !add_stock(gold, income) ||
                (event.tick <= windows.window[0] && !add_stock(out.earned_gold, income)))
                return fail("tribute accounting overflow");
            bag = 0; due[i] = -1;
            if (all_score >= schedule.win_score && event.tick == schedule.terminal_win_tick)
                represented_win = true;
            break;
        }
        case AccountingEventKind::purchase:
            if (++purchases[i] > 1 || gold < event.gold || due[i] >= 0)
                return fail("unaffordable, repeated or channelled purchase");
            gold -= event.gold;
            returned[i] = -1;
            break;
        case AccountingEventKind::income:
            if (event.payout_id < 0) return fail("unattributed future income");
            if (!add_stock(gold, event.gold) ||
                (event.tick <= windows.window[0] && !add_stock(out.earned_gold, event.gold)))
                return fail("income accounting overflow");
            break;
        case AccountingEventKind::refund:
            if (event.payout_id < 0) return fail("unattributed refund");
            if (!add_stock(gold, event.gold)) return fail("refund overflow");
            break;
        case AccountingEventKind::bag_loss:
            if (event.gems < 0 || event.gems > bag) return fail("invalid gem loss");
            bag -= event.gems; break;
        case AccountingEventKind::return_to_work:
            if (event.waypoint < 0 || due[i] >= 0) return fail("invalid productive return");
            returned[i] = event.tick;
            out.productive_arrival = std::max(out.productive_arrival, event.tick);
            break;
        case AccountingEventKind::capture: break;
        }
    }
    if (schedule.terminal_win && !represented_win) return fail("unrepresented terminal win");
    if (schedule.immediate_legal_winning_bank &&
        (!schedule.terminal_win || !represented_win || !immediate_started || schedule.terminal_win_tick < 0))
        return fail("unrepresented immediate winning bank");
    for (int i = 0; i < schedule.actor_count; ++i) {
        if (!schedule.terminal_win &&
            (due[i] >= 0 || returned[i] < 0 || out.final_bag[i] > 0)) {
            out.status = AccountingStatus::unresolved;
            return fail("missing liquidation or productive return");
        }
    }
    if (schedule.denied_gems < 0 || schedule.denied_bank_tick > windows.official_remaining ||
        (schedule.denied_gems > 0 &&
        (!schedule.denial_evidenced || schedule.denied_bank_tick < 0 ||
         (schedule.doctrine_id != 11 && schedule.doctrine_id != 12 && schedule.doctrine_id != 16))))
        return fail("unsupported denial");
    for (int s = 0; s < 3; ++s) {
        if (schedule.denied_bank_tick >= 0 && schedule.denied_bank_tick <= windows.window[s])
            out.D[s] = schedule.denied_gems;
        out.M[s] = out.Y[s];
        if (!add_stock(out.M[s], out.D[s])) return fail("margin overflow");
    }
    out.kills = schedule.projected_kills;
    out.immediate_win = schedule.immediate_legal_winning_bank;
    out.official_endpoint = windows.window[0] == windows.official_remaining &&
        windows.window[1] == windows.official_remaining && windows.window[2] == windows.official_remaining;
    out.status = AccountingStatus::feasible;
    out.reason = "complete bounded schedule; legality relies on caller certificates";
    return out;
}

// Positive means a is preferred. A mismatch is deliberately incomparable;
// callers must check same_actors before treating a tie as an equal forecast.
inline int compare_results(const AccountingResult& a, const AccountingResult& b) {
    if (!same_actors(a, b) || a.windows != b.windows) return 0;
    if (a.status != b.status) return a.status == AccountingStatus::feasible ? 1 :
                                   b.status == AccountingStatus::feasible ? -1 : 0;
    if (a.status != AccountingStatus::feasible) return 0;
    auto cmp = [](auto x, auto y) { return x > y ? 1 : x < y ? -1 : 0; };
    int c = cmp(a.immediate_win, b.immediate_win); if (c) return c;
    c = cmp(*std::min_element(a.M.begin(), a.M.end()),
            *std::min_element(b.M.begin(), b.M.end())); if (c) return c;
    c = cmp(a.M[1], b.M[1]); if (c) return c;
    if (a.official_endpoint && b.official_endpoint) {
        c = cmp(a.Y[0], b.Y[0]); if (c) return c;
        c = cmp(a.earned_gold, b.earned_gold); if (c) return c;
        c = cmp(a.kills, b.kills); if (c) return c;
    }
    c = cmp(b.occupied_turns, a.occupied_turns); if (c) return c;
    c = cmp(b.peak_bag, a.peak_bag); if (c) return c;
    c = cmp(b.productive_arrival, a.productive_arrival); if (c) return c;
    return cmp(b.target_id, a.target_id);
}
} // namespace p5
