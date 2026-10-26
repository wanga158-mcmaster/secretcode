#pragma once
// Pure, bounded Plan 5 accounting inherited by Plan 6. No SDK or native actions.
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace p6 {
using Gems = std::int64_t;
constexpr int max_actors = 6, max_productions = 12, max_actor_events = 7;
enum class Source { unknown, queried_setting, self_observed, visible_subject,
    public_score, public_map, private_history, scheduled, conservative, synthetic_fixture };
template<class T> struct Fact {
    T value{};
    Source source = Source::unknown;
    constexpr bool known() const { return source != Source::unknown; }
};
struct Settings { int max_rounds = 0, win_gems = 0, capture_rounds = 0; };
struct Clock { int round = 0, actor_id = -1; };
inline bool valid(Settings s) { return s.max_rounds > 0 && s.win_gems > 0 && s.capture_rounds > 0; }
inline bool add(Gems a, Gems b, Gems& result) {
    if (a < 0 || b < 0 || a > std::numeric_limits<Gems>::max() - b) return false;
    result = a + b; return true;
}
inline Gems ceil_div(Gems a, Gems b) { return a / b + (a % b != 0); }

struct ScenarioWindows {
    std::array<int, 3> deadlines{}, windows{};
    Gems rate_numerator = 0, rate_denominator = 1;
    int sampled_round = -1, sampled_actor_id = -1, sampled_enemy_score = 0;
    Source source = Source::unknown;
    bool valid = false;
};

class ScoreHistory {
    struct Sample { int round = -1, score = 0; };
    std::array<Sample, 65> samples_{};
    int owner_ = -1, latest_round_ = -1;
public:
    // One private history belongs to one actor. A second query during its turn
    // cannot overwrite that actor's once-per-round sample.
    bool sample(Clock clock, int score) {
        if (clock.actor_id < 0 || clock.round < 0 || score < 0 ||
            (owner_ >= 0 && owner_ != clock.actor_id) || clock.round <= latest_round_) return false;
        owner_ = clock.actor_id; latest_round_ = clock.round;
        samples_[clock.round % 65] = {clock.round, score}; return true;
    }
    ScenarioWindows windows(Settings settings, Clock clock, int score_now) const {
        ScenarioWindows out;
        if (!p6::valid(settings) || clock.actor_id != owner_ || clock.round != latest_round_ ||
            clock.round < 0 || clock.round > settings.max_rounds) return out;
        const Sample now = samples_[clock.round % 65];
        // Reject stale/mixed queries explicitly; callers keep their own sample.
        if (now.score != score_now) return out;
        Sample oldest = now;
        for (const auto& sample : samples_)
            if (sample.round >= std::max(0, clock.round - 64) && sample.round <= clock.round &&
                sample.round < oldest.round) oldest = sample;
        const Gems delta = std::max<Gems>(0, Gems(now.score) - oldest.score);
        const Gems elapsed = std::max(1, clock.round - oldest.round);
        const Gems remaining = settings.max_rounds - clock.round;
        const Gems need = std::max<Gems>(0, Gems(settings.win_gems) - now.score);
        // Integer rational arithmetic avoids floating rounding at deadlines.
        const std::array<Gems, 3> numerator{0, delta, std::max(elapsed, 2 * delta)};
        for (int i = 0; i < 3; ++i) {
            const Gems deadline = need == 0 ? 0 : numerator[i] == 0 ? remaining :
                std::min(remaining, ceil_div(need * elapsed, numerator[i]));
            out.deadlines[i] = static_cast<int>(deadline);
            out.windows[i] = std::min(120, out.deadlines[i]);
        }
        out.rate_numerator = delta; out.rate_denominator = elapsed;
        out.sampled_round = now.round; out.sampled_actor_id = owner_; out.sampled_enemy_score = now.score;
        out.source = Source::private_history; out.valid = true; return out;
    }
};

enum class EventKind { production, tribute, purchase, recovery, renewal };
struct Event {
    EventKind kind = EventKind::renewal;
    int event_id = -1, tick = -1, production_index = -1;
    Fact<Gems> gold_cost{}; // Used only for a purchase; no refund score credit.
    int waypoint_id = -1;
    Source source = Source::unknown;
};
struct Production {
    int event_id = -1, tick = -1;
    Fact<Gems> gems{}, gold{};
    std::array<int, max_actors> holders{};
    int holder_count = 0;
    Source source = Source::unknown;
};
struct ActorSchedule {
    int actor_id = -1;
    Fact<Gems> initial_bag{}, initial_gold{};
    // Unseen personal gold may be represented by {0, conservative}; an unknown
    // route/resource must remain unknown. This is an affordability lower bound.
    // Explicit occupied actor-turn count: completed-event ticks may contain
    // nonoccupied gaps, so final elapsed time is not itself an occupancy bound.
    // The schedule builder must include its travel, waiting and support turns.
    Fact<Gems> occupied_turns{};
    std::array<Event, max_actor_events> events{};
    int event_count = 0;
};
struct WinWitness {
    int actor_id = -1, tribute_event_id = -1;
    // Supplied by the legal executor, never inferred from range or bag alone.
    Fact<bool> legal_now{};
};
struct TerminalTiebreak {
    Fact<Gems> earned_gold_delta{}, kills_delta{};
    // Ranking uses these only at the actual official endpoint with a matched,
    // represented opponent continuation. They do not assert a guaranteed win.
    bool matched_opponent_continuation = false;
};
struct Schedule {
    std::array<ActorSchedule, max_actors> actors{};
    int actor_count = 0;
    std::array<Production, max_productions> productions{};
    int production_count = 0, target_id = -1;
    WinWitness immediate_win{};
    TerminalTiebreak terminal_tiebreak{};
};
enum class Status { feasible, unresolved, invalid };
enum class Issue { none, malformed, unknown_input, duplicate_owner_event,
    duplicate_payout, holder_mismatch, unaffordable_purchase, overflow,
    missing_liquidation, missing_renewal, unmatched_actors, terminal_unknown, history_mismatch };
struct ScoreEvaluation {
    Status status = Status::invalid;
    Issue issue = Issue::malformed;
    std::array<Gems, 3> own_score{}, denial{}, margin{}; // P6 denial is always zero.
    Gems min_margin = 0, nominal_margin = 0, occupancy = 0, peak_bag = 0;
    int productive_arrival = 0, target_id = -1;
    bool immediate_legal_win = false, terminal_endpoint = false, terminal_terms = false;
    Gems earned_gold_delta = 0, kills_delta = 0;
    Source source = Source::scheduled;
    bool eligible() const { return status == Status::feasible; }
};
inline int actor_index(const Schedule& schedule, int id) {
    for (int i = 0; i < schedule.actor_count; ++i) if (schedule.actors[i].actor_id == id) return i;
    return -1;
}
inline bool same_actors(const Schedule& a, const Schedule& b) {
    if (a.actor_count < 1 || a.actor_count > max_actors || a.actor_count != b.actor_count) return false;
    for (int i = 0; i < a.actor_count; ++i) if (actor_index(b, a.actors[i].actor_id) < 0) return false;
    return true;
}

inline ScoreEvaluation evaluate_schedule(const Schedule& schedule, const ScenarioWindows& scenarios,
    Settings settings, Clock clock, Gems current_team_score) {
    ScoreEvaluation out;
    auto fail = [&](Issue issue, Status status = Status::invalid) {
        out.issue = issue; out.status = status; return out;
    };
    if (!p6::valid(settings) || !scenarios.valid || current_team_score < 0 || clock.round < 0 ||
        clock.round > settings.max_rounds || schedule.actor_count < 1 || schedule.actor_count > max_actors ||
        schedule.production_count < 0 || schedule.production_count > max_productions || schedule.target_id < 0)
        return fail(Issue::malformed);
    if (scenarios.source == Source::unknown) return fail(Issue::unknown_input, Status::unresolved);
    if (scenarios.sampled_actor_id != clock.actor_id || scenarios.sampled_round != clock.round)
        return fail(Issue::history_mismatch);
    for (int s = 0; s < 3; ++s)
        if (scenarios.deadlines[s] < 0 || scenarios.deadlines[s] > settings.max_rounds - clock.round ||
            scenarios.windows[s] != std::min(120, scenarios.deadlines[s])) return fail(Issue::malformed);
    struct OrderedEvent { int actor = -1, event = -1; };
    std::array<OrderedEvent, max_actors * max_actor_events> ordered{};
    std::array<Gems, max_actors> bags{}, gold{};
    std::array<int, max_actors> renewal_ticks{};
    std::array<bool, max_productions> awarded{};
    int count = 0;
    for (int a = 0; a < schedule.actor_count; ++a) {
        const auto& actor = schedule.actors[a];
        if (actor.actor_id < 0 || actor.event_count < 1 || actor.event_count > max_actor_events) return fail(Issue::malformed);
        for (int b = 0; b < a; ++b) if (schedule.actors[b].actor_id == actor.actor_id) return fail(Issue::malformed);
        if (!actor.initial_bag.known() || !actor.initial_gold.known() || !actor.occupied_turns.known())
            return fail(Issue::unknown_input, Status::unresolved);
        if (actor.initial_bag.value < 0 || actor.initial_gold.value < 0 || actor.occupied_turns.value < 0)
            return fail(Issue::malformed);
        bags[a] = actor.initial_bag.value; gold[a] = actor.initial_gold.value;
        out.peak_bag = std::max(out.peak_bag, bags[a]);
        if (!add(out.occupancy, actor.occupied_turns.value, out.occupancy)) return fail(Issue::overflow);
        int production_count = 0, tribute_count = 0, purchase_count = 0, recovery_count = 0, renewal_count = 0;
        int last_tick = 0;
        for (int e = 0; e < actor.event_count; ++e) {
            const auto& event = actor.events[e];
            if (event.source == Source::unknown) return fail(Issue::unknown_input, Status::unresolved);
            if (event.event_id < 0 || event.tick < 0 || event.tick > settings.max_rounds - clock.round)
                return fail(Issue::malformed);
            for (int before = 0; before < e; ++before)
                if (actor.events[before].event_id == event.event_id) return fail(Issue::duplicate_owner_event);
            switch (event.kind) {
            case EventKind::production:
                ++production_count;
                if (event.production_index < 0 || event.production_index >= schedule.production_count)
                    return fail(Issue::malformed);
                break;
            case EventKind::tribute: ++tribute_count; break;
            case EventKind::purchase:
                ++purchase_count;
                if (!event.gold_cost.known()) return fail(Issue::unknown_input, Status::unresolved);
                if (event.gold_cost.value < 0) return fail(Issue::malformed);
                break;
            case EventKind::recovery: ++recovery_count; break;
            case EventKind::renewal:
                ++renewal_count;
                if (event.waypoint_id < 0) return fail(Issue::missing_renewal, Status::unresolved);
                renewal_ticks[a] = event.tick; break;
            default: return fail(Issue::malformed);
            }
            last_tick = std::max(last_tick, event.tick);
            ordered[count++] = {a, e};
        }
        if (production_count > 2 || tribute_count > 2 || purchase_count > 1 || recovery_count > 1 || renewal_count > 1)
            return fail(Issue::malformed);
        if (renewal_count != 1 || renewal_ticks[a] < last_tick) return fail(Issue::missing_renewal, Status::unresolved);
        out.productive_arrival = std::max(out.productive_arrival, renewal_ticks[a]);
    }
    for (int p = 0; p < schedule.production_count; ++p) {
        const auto& production = schedule.productions[p];
        if (production.source == Source::unknown || !production.gems.known() || !production.gold.known())
            return fail(Issue::unknown_input, Status::unresolved);
        if (production.event_id < 0 || production.tick < 0 || production.gems.value < 0 || production.gold.value < 0 ||
            production.holder_count < 1 || production.holder_count > schedule.actor_count) return fail(Issue::malformed);
        for (int q = 0; q < p; ++q) if (schedule.productions[q].event_id == production.event_id) return fail(Issue::duplicate_payout);
        for (int h = 0; h < production.holder_count; ++h) {
            const int actor = actor_index(schedule, production.holders[h]);
            if (actor < 0) return fail(Issue::holder_mismatch);
            for (int j = 0; j < h; ++j) if (production.holders[j] == production.holders[h]) return fail(Issue::holder_mismatch);
            int references = 0;
            for (int e = 0; e < schedule.actors[actor].event_count; ++e) {
                const auto& event = schedule.actors[actor].events[e];
                if (event.kind == EventKind::production && event.production_index == p &&
                    event.event_id == production.event_id && event.tick == production.tick) ++references;
            }
            if (references != 1) return fail(Issue::holder_mismatch);
        }
    }
    for (int i = 0; i < count; ++i) {
        const auto& key = ordered[i]; const auto& event = schedule.actors[key.actor].events[key.event];
        if (event.kind == EventKind::production) {
            const auto& production = schedule.productions[event.production_index];
            bool holder = false;
            for (int h = 0; h < production.holder_count; ++h) holder |= production.holders[h] == schedule.actors[key.actor].actor_id;
            if (!holder || event.event_id != production.event_id || event.tick != production.tick) return fail(Issue::holder_mismatch);
        }
    }
    auto phase = [](EventKind kind) { return kind == EventKind::production ? 0 : kind == EventKind::tribute ? 1 : 2; };
    std::sort(ordered.begin(), ordered.begin() + count, [&](OrderedEvent a, OrderedEvent b) {
        const auto& left = schedule.actors[a.actor].events[a.event];
        const auto& right = schedule.actors[b.actor].events[b.event];
        if (left.tick != right.tick) return left.tick < right.tick;
        if (phase(left.kind) != phase(right.kind)) return phase(left.kind) < phase(right.kind);
        if (schedule.actors[a.actor].actor_id != schedule.actors[b.actor].actor_id)
            return schedule.actors[a.actor].actor_id < schedule.actors[b.actor].actor_id;
        return left.event_id < right.event_id;
    });
    for (int i = 0; i < count; ++i) {
        const int a = ordered[i].actor;
        const auto& actor = schedule.actors[a]; const auto& event = actor.events[ordered[i].event];
        if (event.kind == EventKind::production && !awarded[event.production_index]) {
            const auto& production = schedule.productions[event.production_index];
            int lowest = production.holders[0];
            for (int h = 1; h < production.holder_count; ++h) lowest = std::min(lowest, production.holders[h]);
            for (int h = 0; h < production.holder_count; ++h) {
                const int owner = actor_index(schedule, production.holders[h]);
                const Gems gem_share = production.gems.value / production.holder_count +
                    (production.holders[h] == lowest ? production.gems.value % production.holder_count : 0);
                const Gems gold_share = production.gold.value / production.holder_count +
                    (production.holders[h] == lowest ? production.gold.value % production.holder_count : 0);
                if (!add(bags[owner], gem_share, bags[owner]) || !add(gold[owner], gold_share, gold[owner])) return fail(Issue::overflow);
                out.peak_bag = std::max(out.peak_bag, bags[owner]);
            }
            awarded[event.production_index] = true;
        } else if (event.kind == EventKind::tribute) {
            if (bags[a] == 0) return fail(Issue::malformed);
            for (int s = 0; s < 3; ++s) if (event.tick <= scenarios.windows[s])
                if (!add(out.own_score[s], bags[a], out.own_score[s])) return fail(Issue::overflow);
            const auto& win = schedule.immediate_win;
            Gems after = 0;
            if (win.actor_id == clock.actor_id && win.actor_id == actor.actor_id &&
                win.tribute_event_id == event.event_id && win.legal_now.known() && win.legal_now.value &&
                event.tick <= *std::min_element(scenarios.windows.begin(), scenarios.windows.end()) &&
                add(current_team_score, actor.initial_bag.value, after) && after >= settings.win_gems)
                out.immediate_legal_win = true;
            bags[a] = 0;
        } else if (event.kind == EventKind::purchase) {
            if (gold[a] < event.gold_cost.value) return fail(Issue::unaffordable_purchase);
            gold[a] -= event.gold_cost.value;
        }
    }
    for (int a = 0; a < schedule.actor_count; ++a)
        if (bags[a] != 0) return fail(Issue::missing_liquidation, Status::unresolved);
    out.margin = out.own_score;
    out.min_margin = *std::min_element(out.margin.begin(), out.margin.end());
    out.nominal_margin = out.margin[1]; out.target_id = schedule.target_id;
    bool terminal = true;
    for (int window : scenarios.windows) terminal &= window == settings.max_rounds - clock.round;
    out.terminal_endpoint = terminal;
    const auto& tie = schedule.terminal_tiebreak;
    if (terminal && tie.matched_opponent_continuation && tie.earned_gold_delta.known() && tie.kills_delta.known()) {
        if (tie.earned_gold_delta.value < 0 || tie.kills_delta.value < 0) return fail(Issue::malformed);
        out.terminal_terms = true; out.earned_gold_delta = tie.earned_gold_delta.value; out.kills_delta = tie.kills_delta.value;
    }
    out.status = Status::feasible; out.issue = Issue::none; return out;
}

// Positive means left is better. Unresolved candidates never become zero-score
// incumbents. Terminal tie terms are used only if both inputs know the terms.
inline int compare(const ScoreEvaluation& left, const ScoreEvaluation& right) {
    if (left.eligible() != right.eligible()) return left.eligible() ? 1 : -1;
    if (!left.eligible()) return 0;
    auto cmp = [](auto a, auto b) { return a > b ? 1 : a < b ? -1 : 0; };
    int c = cmp(left.immediate_legal_win, right.immediate_legal_win); if (c) return c;
    c = cmp(left.min_margin, right.min_margin); if (c) return c;
    c = cmp(left.nominal_margin, right.nominal_margin); if (c) return c;
    if (left.terminal_endpoint && right.terminal_endpoint && (!left.terminal_terms || !right.terminal_terms)) return 0;
    if (left.terminal_terms && right.terminal_terms) {
        c = cmp(left.earned_gold_delta, right.earned_gold_delta); if (c) return c;
        c = cmp(left.kills_delta, right.kills_delta); if (c) return c;
    }
    c = cmp(right.occupancy, left.occupancy); if (c) return c;
    c = cmp(right.peak_bag, left.peak_bag); if (c) return c;
    c = cmp(right.productive_arrival, left.productive_arrival); if (c) return c;
    return cmp(right.target_id, left.target_id);
}
struct Alternatives { ScoreEvaluation cooperative{}, independent{}; bool matched = false; };
inline Alternatives evaluate_alternatives(const Schedule& cooperative, const Schedule& independent,
    const ScenarioWindows& scenarios, Settings settings, Clock clock, Gems current_team_score) {
    Alternatives result;
    result.matched = same_actors(cooperative, independent);
    if (!result.matched) {
        result.cooperative.issue = result.independent.issue = Issue::unmatched_actors;
        return result;
    }
    result.cooperative = evaluate_schedule(cooperative, scenarios, settings, clock, current_team_score);
    result.independent = evaluate_schedule(independent, scenarios, settings, clock, current_team_score);
    return result;
}
inline bool joint_support_admissible(const Alternatives& options) {
    return options.matched && options.cooperative.eligible() && options.independent.eligible() &&
        options.cooperative.min_margin > options.independent.min_margin;
}
} // namespace p6
