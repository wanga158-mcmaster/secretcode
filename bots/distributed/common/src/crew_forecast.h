#pragma once
#include "observe.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

// Conditional work by three present core actors, from one personal view. This
// model grants no arrival, action, aggro distribution, healing or payout receipt.
namespace p6::forecast {
struct CrewRequest {
    std::array<int, 3> actors{-1, -1, -1};
    std::array<Request, 3> routes{};
    // Group-subject witness at the original observing clock: all three actors
    // are unstunned, personally see and geometrically cover every living member.
    // This is stationary work: no move is needed to attack or change targets.
    // It promises the normal next-owner-turn basics, never past/current hits.
    Observation<std::uint8_t> ready_basics{};
};
struct CrewResult {
    Status status = Status::unresolved;
    Issue issue = Issue::unknown_input;
    Failure abort = Failure::none;
    Schedule schedule{};
    std::array<CycleCertificate, 3> certificates{};
    std::array<int, 3> durations{};
    int clear_turns = 0, capture_tick = 0;
};

template<class Continue>
inline void build_crew_into(const Frame& frame, const CrewRequest& request, CrewResult& out,
                            Continue may_continue) {
    constexpr int work_cap = 1000000;
    constexpr Gems exact_gem_cap = 9007199254740991LL;
    out.status = Status::unresolved; out.issue = Issue::unknown_input;
    out.abort = Failure::none;
    out.schedule.actor_count = out.schedule.production_count = 0;
    out.schedule.target_id = -1;
    out.schedule.immediate_win = {}; out.schedule.terminal_tiebreak = {};
    out.certificates = {}; out.durations = {};
    out.clear_turns = out.capture_tick = 0;
    auto fail = [&](Issue issue, Status status = Status::unresolved,
                    Failure abort = Failure::none) {
        out.status = status; out.issue = issue; out.abort = abort;
        out.schedule.actor_count = out.schedule.production_count = 0;
        out.schedule.target_id = -1;
        out.certificates = {}; out.durations = {};
        out.clear_turns = out.capture_tick = 0;
    };
    auto keep_running = [&] {
        if (may_continue()) return true;
        fail(Issue::unknown_input, Status::unresolved, Failure::expired);
        return false;
    };
    if (!keep_running()) return;
    auto number = [](double value) { return std::isfinite(value) && value >= 0; };
    auto point_ok = [](Point p) {
        return p.x >= -work_cap && p.x <= work_cap && p.y >= -work_cap && p.y <= work_cap;
    };
    auto static_source = [](Source source) {
        return source == Source::public_map || source == Source::queried_setting;
    };
    if (!frame.valid() || !number(frame.own.actor.health) || frame.own.gold < 0)
        return fail(Issue::malformed, Status::invalid);
    if (std::find(request.actors.begin(), request.actors.end(), frame.clock.self) == request.actors.end())
        return fail(Issue::unavailable, Status::unresolved, Failure::observed_unavailable);
    const auto& first_route = request.routes[0];
    const auto& first_group = first_route.groups[0];
    const int group = first_group.observed.value.id;
    if (group < 0 || !request.ready_basics.current(frame.clock, group) || request.ready_basics.value != 7)
        return fail(Issue::unknown_input, Status::unresolved, Failure::not_observed);
    const auto& timing = first_route.timing;
    if (timing.settings.source != Source::queried_setting ||
        timing.tribute_multiplier.source != Source::queried_setting ||
        timing.tribute_base.source != Source::queried_setting ||
        timing.tribute_minimum.source != Source::queried_setting)
        return fail(Issue::unknown_input, Status::unresolved, Failure::not_observed);
    const auto settings = timing.settings.value;
    if (!p6::valid(settings) || !number(timing.tribute_multiplier.value) ||
        timing.tribute_minimum.value < 1 || timing.tribute_base.value < -work_cap ||
        timing.tribute_base.value > work_cap)
        return fail(Issue::malformed, Status::invalid);
    if (settings.max_rounds > work_cap || settings.capture_rounds >= work_cap ||
        timing.tribute_minimum.value > work_cap)
        return fail(Issue::overflow, Status::invalid);
    if (frame.clock.round > settings.max_rounds) return fail(Issue::horizon);
    const int horizon = settings.max_rounds - frame.clock.round;
    std::array<const Actor*, 3> actors{};
    std::array<double, 3> ambient{}, cumulative{};
    std::array<int, 3> capture_walk{}, bank_walk{}, renewal_walk{}, service{};
    std::array<Gems, 3> bags{};
    std::array<MonsterWork, 32> work{};
    int count = -1, lowest_id = request.actors[0];
    bool living = false;
    double combined_rate = 0, living_strength = 0;
    for (int i = 0; i < 3; ++i) {
        if (!keep_running()) return;
        const int id = request.actors[i];
        for (int j = 0; j < i; ++j)
            if (request.actors[j] == id) return fail(Issue::malformed, Status::invalid);
        lowest_id = std::min(lowest_id, id);
        const auto* actor = frame.visible(id);
        if (!actor) return fail(Issue::unknown_input, Status::unresolved, Failure::not_observed);
        actors[i] = actor;
        if (actor->team != Team::ally || actor->pick_ordinal != i ||
            !number(actor->health) || !number(actor->max_health) || !number(actor->strength) ||
            actor->max_health <= 0 || actor->health > actor->max_health || actor->bag < 0 ||
            !point_ok(actor->position))
            return fail(Issue::malformed, Status::invalid, Failure::conflicting_evidence);
        if (!actor->alive || actor->health <= 0 || actor->channel != Channel::none)
            return fail(Issue::unavailable, Status::unresolved, Failure::observed_unavailable);
        if (actor->bag > exact_gem_cap) return fail(Issue::overflow, Status::invalid);
        // Unlike a generic ranged route, mask7 proves that every target can be
        // attacked from each actor's present tile. Stationary ranged actors can
        // use a normal basic every owner turn without spending a move.
        combined_rate += actor->strength;
        ViewClock clock = frame.clock; clock.self = id;
        const auto& route = request.routes[i];
        const auto& input = route.groups[0];
        const auto& observed = input.observed.value;
        if (route.purchase_requested) return fail(Issue::unsupported_purchase);
        if (route.shield.enabled || route.recovery.enabled || route.recovery.requires_health_restore ||
            route.shape != Shape::produce_bank || route.group_count != 1 || route.project_group != group ||
            route.leg_count != 4 || route.enemy_cast_count < 0 || route.enemy_cast_count > 6)
            return fail(Issue::malformed, Status::invalid);
        if (!route.kit.current(clock, id) || !route.ambient_incoming.current(clock, id) ||
            !input.observed.current(clock, group))
            return fail(Issue::unknown_input, Status::unresolved, Failure::not_observed);
        const auto& kit = route.kit.value;
        if (!number(kit.strength) || kit.strength != actor->strength || kit.ranged != actor->ranged ||
            kit.scheduled_single_cast != 0 || kit.cast_ready || kit.cast_legal_at_first_turn ||
            kit.cast_reserved_for_protection || !number(route.ambient_incoming.value))
            return fail(Issue::malformed, Status::invalid, Failure::conflicting_evidence);
        const auto& t = route.timing;
        if (t.settings.source != Source::queried_setting ||
            t.tribute_multiplier.source != Source::queried_setting ||
            t.tribute_base.source != Source::queried_setting ||
            t.tribute_minimum.source != Source::queried_setting)
            return fail(Issue::unknown_input, Status::unresolved, Failure::not_observed);
        if (t.settings.value.max_rounds != settings.max_rounds ||
            t.settings.value.win_gems != settings.win_gems ||
            t.settings.value.capture_rounds != settings.capture_rounds ||
            t.tribute_multiplier.value != timing.tribute_multiplier.value ||
            t.tribute_base.value != timing.tribute_base.value ||
            t.tribute_minimum.value != timing.tribute_minimum.value)
            return fail(Issue::malformed, Status::invalid, Failure::conflicting_evidence);
        if (observed.id != group || observed.approach != actor->position ||
            !point_ok(observed.spawn) || !point_ok(route.bank_position) || !point_ok(route.renewal_position) ||
            route.bank_waypoint < 0 || route.renewal_waypoint < 0 ||
            input.member_count < 1 || input.member_count > 32)
            return fail(Issue::malformed, Status::invalid);
        if (!observed.original_spawn_visible || !observed.all_spawns_visible || !observed.uncontested_now ||
            !input.membership_complete.known() || !static_source(input.membership_complete.source) ||
            !input.membership_complete.value)
            return fail(Issue::unknown_input, Status::unresolved, Failure::not_observed);
        if (observed.lifecycle != Lifecycle::living && observed.lifecycle != Lifecycle::reward_available)
            return fail(Issue::unavailable, Status::unresolved, Failure::observed_unavailable);
        if (!input.gems.known() || !input.gold.known() || !static_source(input.gems.source) ||
            !static_source(input.gold.source) || input.gems.value < 0 || input.gold.value < 0)
            return fail(Issue::unknown_input, Status::unresolved, Failure::not_observed);
        if (i == 0) count = input.member_count;
        else if (input.member_count != count || observed.lifecycle != first_group.observed.value.lifecycle ||
                 input.gems.value != first_group.gems.value || input.gold.value != first_group.gold.value ||
                 route.bank_position != first_route.bank_position || route.bank_waypoint != first_route.bank_waypoint ||
                 route.renewal_position != first_route.renewal_position ||
                 route.renewal_waypoint != first_route.renewal_waypoint)
            return fail(Issue::malformed, Status::invalid, Failure::conflicting_evidence);
        for (int j = 0; j < i; ++j)
            if (observed.spawn == request.routes[j].groups[0].observed.value.spawn)
                return fail(Issue::malformed, Status::invalid, Failure::conflicting_evidence);
        double source_strength = 0;
        bool source_living = false;
        for (int m = 0; m < count; ++m) {
            if (!keep_running()) return;
            const auto& member = input.members[m];
            const auto& full = member.public_full.value;
            if (!member.public_full.known() || !static_source(member.public_full.source) ||
                !member.current.current(clock, full.id))
                return fail(Issue::unknown_input, Status::unresolved, Failure::not_observed);
            const auto& current = member.current.value;
            if (full.id < 0 || !number(full.health) || full.health <= 0 || !number(full.effective_strength) ||
                current.id != full.id || !number(current.health) || !number(current.effective_strength) ||
                current.health > full.health || current.effective_strength != full.effective_strength)
                return fail(Issue::malformed, Status::invalid, Failure::conflicting_evidence);
            for (int previous = 0; previous < m; ++previous)
                if (input.members[previous].public_full.value.id == full.id)
                    return fail(Issue::malformed, Status::invalid);
            if (i == 0) work[m] = current;
            else {
                int match = -1;
                for (int prior = 0; prior < count; ++prior)
                    if (first_group.members[prior].public_full.value.id == full.id) match = prior;
                if (match < 0) return fail(Issue::malformed, Status::invalid, Failure::conflicting_evidence);
                const auto& original = first_group.members[match];
                if (full.health != original.public_full.value.health ||
                    full.effective_strength != original.public_full.value.effective_strength ||
                    current.health != original.current.value.health ||
                    current.effective_strength != original.current.value.effective_strength)
                    return fail(Issue::malformed, Status::invalid, Failure::conflicting_evidence);
            }
            if (current.health > 0) { source_living = true; source_strength += current.effective_strength; }
        }
        if ((observed.lifecycle == Lifecycle::living) != source_living || !number(source_strength))
            return fail(Issue::malformed, Status::invalid, Failure::conflicting_evidence);
        if (i == 0) { living = source_living; living_strength = source_strength; }
        if (living && (id == frame.clock.self ? !actor->can_attack :
                       !(actor->can_attack || actor->completed_turn)))
            return fail(Issue::unavailable, Status::unresolved, Failure::observed_unavailable);
        if (!input.ambient_overlap.current(clock, group))
            return fail(Issue::unknown_input, Status::unresolved, Failure::not_observed);
        const double overlap = input.ambient_overlap.value;
        if (!number(overlap) || overlap > source_strength || overlap > route.ambient_incoming.value)
            return fail(Issue::malformed, Status::invalid, Failure::conflicting_evidence);
        // Charge all other awake monsters and all visible enemy basics/casts.
        // The represented source's old awake life is removed exactly once.
        ambient[i] = route.ambient_incoming.value - overlap;
        std::array<bool, 6> used{};
        int enemies = 0;
        for (int e = 0; e < frame.actor_count; ++e) {
            if (!keep_running()) return;
            const auto& seen = frame.actors[e];
            if (!seen.current(frame.clock, seen.subject) || seen.value.team != Team::enemy || !seen.value.alive) continue;
            const auto& enemy = seen.value;
            if (!number(enemy.health) || !number(enemy.strength) || enemy.health <= 0)
                return fail(Issue::malformed, Status::invalid);
            int match = -1;
            for (int cast = 0; cast < route.enemy_cast_count; ++cast)
                if (route.enemy_cast_stress[cast].current(clock, enemy.id)) {
                    if (match >= 0) return fail(Issue::malformed, Status::invalid);
                    match = cast;
                }
            if (match < 0) return fail(Issue::unknown_input, Status::unresolved, Failure::not_observed);
            if (!number(route.enemy_cast_stress[match].value)) return fail(Issue::malformed, Status::invalid);
            used[match] = true; ++enemies;
            ambient[i] += enemy.strength + route.enemy_cast_stress[match].value;
        }
        if (enemies != route.enemy_cast_count)
            return fail(Issue::malformed, Status::invalid, Failure::conflicting_evidence);
        for (int e = 0; e < route.enemy_cast_count; ++e)
            if (!used[e]) return fail(Issue::malformed, Status::invalid);
        if (!number(ambient[i])) return fail(Issue::overflow, Status::invalid);
        const std::array<Point, 5> points{actor->position, actor->position, observed.spawn,
                                         route.bank_position, route.renewal_position};
        std::array<int, 4> turns{};
        for (int leg = 0; leg < 4; ++leg) {
            if (!keep_running()) return;
            const auto& step = route.legs[leg];
            if (!step.distance.known() || !static_source(step.distance.source))
                return fail(Issue::unknown_input, Status::unresolved, Failure::not_observed);
            const int distance = step.distance.value;
            const auto dx = std::abs(std::int64_t(points[leg].x) - points[leg + 1].x);
            const auto dy = std::abs(std::int64_t(points[leg].y) - points[leg + 1].y);
            if (step.from != points[leg] || step.to != points[leg + 1] || distance < 0 || distance > 256 ||
                distance < std::max(dx, dy) || ((step.from == step.to) != (distance == 0)))
                return fail(Issue::malformed, Status::invalid, Failure::conflicting_evidence);
            if (distance && !(actor->can_move || actor->completed_turn))
                return fail(Issue::unavailable, Status::unresolved, Failure::observed_unavailable);
            turns[leg] = distance ? distance + 2 : 0;
        }
        capture_walk[i] = turns[1]; bank_walk[i] = turns[2]; renewal_walk[i] = turns[3];
    }
    if (!number(combined_rate) || (living && combined_rate <= 0))
        return fail(Issue::unavailable, Status::unresolved, Failure::observed_unavailable);
    // A currently present extra holder would change both exact reward splits.
    auto member_id = [&](int id) {
        return std::find(request.actors.begin(), request.actors.end(), id) != request.actors.end();
    };
    if (!member_id(frame.clock.self) && frame.own.actor.alive && frame.own.actor.spawn_group == group)
        return fail(Issue::unavailable, Status::unresolved, Failure::observed_unavailable);
    for (int a = 0; a < frame.actor_count; ++a) {
        if (!keep_running()) return;
        const auto& seen = frame.actors[a];
        if (seen.current(frame.clock, seen.subject) && seen.value.team == Team::ally &&
            seen.value.alive && seen.value.spawn_group == group && !member_id(seen.subject))
            return fail(Issue::unavailable, Status::unresolved, Failure::observed_unavailable);
    }
    std::sort(work.begin(), work.begin() + count, [](const auto& a, const auto& b) {
        return a.health == b.health ? a.id < b.id : a.health < b.health;
    });
    Issue progress_issue = Issue::none;
    Failure progress_abort = Failure::none;
    auto exposure = [&](int i, double next) {
        Exposure value;
        value.health = actors[i]->health; value.max_health = actors[i]->max_health;
        value.incoming_total = cumulative[i]; value.next_phase_incoming = next;
        value.observations_complete = true;
        return value;
    };
    auto advance = [&](int i, int& tick, int turns, double rate, double next) {
        if (!may_continue()) {
            progress_issue = Issue::unknown_input; progress_abort = Failure::expired; return false;
        }
        if (turns < 0 || turns > work_cap || tick > horizon || turns > horizon - tick) {
            progress_issue = Issue::horizon; progress_abort = Failure::expired; return false;
        }
        const double damage = turns * rate;
        if (!number(rate) || !number(next) || !number(damage) || !number(cumulative[i] + damage)) {
            progress_issue = Issue::overflow; return false;
        }
        cumulative[i] += damage; tick += turns;
        if (!survives(exposure(i, next))) {
            progress_issue = Issue::unsafe; progress_abort = Failure::unsafe; return false;
        }
        return true;
    };
    int clear_tick = 0;
    auto clear_advance = [&](int turns, double strength, double next_strength) {
        for (int i = 0; i < 3; ++i) {
            int personal_tick = clear_tick;
            if (!advance(i, personal_tick, turns, ambient[i] + strength, ambient[i] + next_strength)) return false;
        }
        clear_tick += turns;
        return true;
    };
    // One full round before any combined progress covers partners whose current
    // turn already happened. No current-round partner action is counted.
    if (living && !clear_advance(1, living_strength, living_strength))
        return fail(progress_issue, Status::unresolved, progress_abort);
    for (int m = 0; m < count; ++m) {
        if (!keep_running()) return;
        const auto& target = work[m];
        if (target.health <= 0) continue;
        // Every living target is already in each participant's personal basic
        // range. Changing targets needs no reposition turn. Round each target
        // separately so unused damage never carries into the following target.
        const double turns = std::ceil(target.health / combined_rate);
        if (!number(turns) || turns < 1 || turns > work_cap)
            return fail(Issue::overflow, Status::unresolved);
        const double next = std::max(0., living_strength - target.effective_strength);
        if (!clear_advance(static_cast<int>(turns), living_strength, next))
            return fail(progress_issue, Status::unresolved, progress_abort);
        living_strength = next;
    }
    for (int i = 0; i < 3; ++i) {
        if (!keep_running()) return;
        auto& certificate = out.certificates[i];
        certificate.clock = frame.clock; certificate.actor = request.actors[i]; certificate.group = group;
        certificate.solo = false; certificate.clear = exposure(i, ambient[i]);
        if (!survives(certificate.clear)) return fail(Issue::unsafe, Status::unresolved, Failure::unsafe);
    }
    const int longest_walk = *std::max_element(capture_walk.begin(), capture_walk.end());
    // Capture starts at the first arrival; the executor has no arrival barrier.
    // All three holders must reach their distinct pads before the earliest
    // possible payout, rather than stretching capture until the last arrival.
    // Positive walks already include their two normal-route slack turns.
    if (longest_walk > settings.capture_rounds)
        return fail(Issue::unavailable, Status::unresolved, Failure::observed_unavailable);
    const std::int64_t capture = std::int64_t(clear_tick) + longest_walk + settings.capture_rounds + 1;
    if (capture > work_cap || capture > horizon) return fail(Issue::horizon, Status::unresolved, Failure::expired);
    out.clear_turns = clear_tick; out.capture_tick = static_cast<int>(capture);
    for (int i = 0; i < 3; ++i) {
        if (!keep_running()) return;
        int tick = clear_tick;
        if (!advance(i, tick, out.capture_tick - clear_tick, ambient[i], ambient[i]))
            return fail(progress_issue, Status::unresolved, progress_abort);
        out.certificates[i].hold = exposure(i, ambient[i]);
        const auto& route = request.routes[i];
        const Gems share = first_group.gems.value / 3 +
            (request.actors[i] == lowest_id ? first_group.gems.value % 3 : 0);
        const Gems gold_share = first_group.gold.value / 3 +
            (request.actors[i] == lowest_id ? first_group.gold.value % 3 : 0);
        Gems gold = request.actors[i] == frame.clock.self ? frame.own.gold : 0;
        if (!p6::add(actors[i]->bag, share, bags[i]) || bags[i] > exact_gem_cap ||
            !p6::add(gold, gold_share, gold)) return fail(Issue::overflow, Status::invalid);
        int lock_turns = 0;
        const bool bank_clear = route.bank_available.current(route.kit.clock, route.bank_waypoint) && route.bank_available.value;
        if (!bank_clear) {
            if (route.bank_lock_hits.source != Source::queried_setting ||
                route.bank_lock_hits.value < 1 || route.bank_lock_hits.value >= work_cap)
                return fail(Issue::unknown_input, Status::unresolved, Failure::not_observed);
            lock_turns = route.bank_lock_hits.value + 1;
        }
        if (bags[i] > 0) {
            const double scaled = std::ceil(static_cast<double>(bags[i]) * timing.tribute_multiplier.value);
            if (!number(scaled) || scaled > work_cap) return fail(Issue::overflow);
            const auto tribute = std::max<std::int64_t>(timing.tribute_minimum.value,
                std::int64_t(timing.tribute_base.value) + static_cast<std::int64_t>(scaled));
            if (tribute < 1 || tribute > work_cap || tribute + lock_turns > work_cap) return fail(Issue::overflow);
            service[i] = static_cast<int>(tribute) + lock_turns;
        }
    }
    out.schedule.actor_count = 3; out.schedule.production_count = 1; out.schedule.target_id = group;
    auto& production = out.schedule.productions[0];
    production = {}; production.event_id = 1; production.tick = out.capture_tick;
    production.gems = {first_group.gems.value, Source::scheduled};
    production.gold = {first_group.gold.value, Source::scheduled};
    production.holder_count = 3; production.source = Source::scheduled;
    for (int i = 0; i < 3; ++i) production.holders[i] = request.actors[i];
    for (int i = 0; i < 3; ++i) {
        if (!keep_running()) return;
        const int id = request.actors[i];
        auto& scheduled = out.schedule.actors[i];
        scheduled = {}; scheduled.actor_id = id;
        scheduled.initial_bag = {actors[i]->bag, id == frame.clock.self ? Source::self_observed : Source::visible_subject};
        scheduled.initial_gold = {id == frame.clock.self ? frame.own.gold : 0,
                                 id == frame.clock.self ? Source::self_observed : Source::conservative};
        scheduled.events[scheduled.event_count++] = {EventKind::production, 1, out.capture_tick, 0, {}, group, Source::scheduled};
        int tick = out.capture_tick;
        // No dispatcher order is assumed. Bound waiting behind both other
        // holders' full lock/tribute service plus entry/clearance slack each.
        std::int64_t queue = 0;
        for (int other = 0; other < 3; ++other) if (other != i) queue += service[other] + 2;
        if (queue > work_cap) return fail(Issue::overflow);
        if (!advance(i, tick, bank_walk[i], ambient[i], ambient[i]) ||
            !advance(i, tick, static_cast<int>(queue), ambient[i], ambient[i]) ||
            !advance(i, tick, service[i], ambient[i], ambient[i]))
            return fail(progress_issue, Status::unresolved, progress_abort);
        if (bags[i] > 0)
            scheduled.events[scheduled.event_count++] = {EventKind::tribute, 2 + i, tick, -1, {},
                                                         request.routes[i].bank_waypoint, Source::scheduled};
        out.certificates[i].bank = exposure(i, ambient[i]);
        if (!advance(i, tick, renewal_walk[i], ambient[i], ambient[i]))
            return fail(progress_issue, Status::unresolved, progress_abort);
        scheduled.events[scheduled.event_count++] = {EventKind::renewal, 5 + i, tick, -1, {},
                                                     request.routes[i].renewal_waypoint, Source::scheduled};
        scheduled.occupied_turns = {tick, Source::scheduled};
        out.durations[i] = tick;
        auto& certificate = out.certificates[i];
        certificate.recovery = exposure(i, ambient[i]);
        certificate.route_known = certificate.bank_executable = certificate.renewal_reachable = true;
        if (!certificate.valid(frame, id, group))
            return fail(Issue::unsafe, Status::unresolved, Failure::unsafe);
    }
    if (!keep_running()) return;
    out.status = Status::feasible; out.issue = Issue::none; out.abort = Failure::none;
}

inline void build_crew_into(const Frame& frame, const CrewRequest& request, CrewResult& out) {
    build_crew_into(frame, request, out, [] { return true; });
}

class EvaluatedCrewQuote {
    template<class Policy> friend class p6::runtime::Bot;
    bool ready_ = false;
    Frame frame_{};
    CrewRequest request_{};
    CrewResult result_{};
    template<class FillRequest, class Continue>
    void evaluate_into(const Frame& frame, FillRequest fill, Continue may_continue) {
        ready_ = false;
        bool interrupted = false;
        auto continuing = [&] {
            if (may_continue()) return !interrupted;
            interrupted = true; return false;
        };
        if (!continuing()) return;
        frame_ = frame; fill(request_);
        if (!continuing()) return;
        build_crew_into(frame_, request_, result_, continuing);
        ready_ = continuing();
    }
public:
    EvaluatedCrewQuote() {}
    void evaluate(const Frame& frame, const CrewRequest& request) {
        ready_ = false; frame_ = frame; request_ = request;
        build_crew_into(frame_, request_, result_); ready_ = true;
    }
    bool ready() const { return ready_; }
    const CrewRequest& request() const { return request_; }
    const CrewResult& result() const { return result_; }
    bool matches_frame(const Frame& frame) const {
        if (!ready_ || !(frame_.clock == frame.clock) || !(frame_.own == frame.own) ||
            frame_.own_team_score != frame.own_team_score || frame_.actor_count != frame.actor_count ||
            frame.actor_count < 0 || frame.actor_count > static_cast<int>(frame.actors.size())) return false;
        for (int i = 0; i < frame.actor_count; ++i)
            if (!(frame_.actors[i] == frame.actors[i])) return false;
        // As in EvaluatedQuote, group inputs are request-owned and inactive
        // actor records have no semantics; neither can restamp a quote.
        return true;
    }
};
} // namespace p6::forecast
