#pragma once
#include "profile.h"
#include "../../common/src/execute.h"
#include <span>

// Pure proposals for the offline card. A future adapter must construct fresh
// observations, legal schedules and receipts; this file executes no SDK action.
namespace p6::firebrigade {
enum class Arm { active, control };
enum class Kind { circuit, local_circuit, committed_circuit, bank, recovery, inspection, intervention };
enum class Action { yield, produce, bank, recover, inspect, intervene, preserve_capture, preserve_channel };
enum class Work { independent_circuit, local_production, committed_production,
                  stabilize_hold, clear_approach, escort_return };
enum class Fallback { none, independent_cycle, bank_to_altar, recovery_to_spawn, inspect_primary_source };
enum class IncidentKind { exposed_hold, interrupted_approach, dangerous_return };
enum class Terminal { none, safe_payout, protected_bank, recovery_waypoint };
enum class Rejection { none, malformed, stale, resources, missing_actor, unsafe,
    wrong_arm, alarm_unwitnessed, no_team_gain, accounting, retired, repair_unlicensed, late_terminal };
struct Incident {
    IncidentKind kind = IncidentKind::exposed_hold;
    int owner = -1, group = -1;
    int threat = -1;
    // The tag classifies the checked visible predicate below; it cannot supply
    // a remote failed-action receipt or a predicted no-show by itself.
    Failure witness = Failure::none;
};
struct Milestone {
    int owner = -1, group = -1;
    Terminal kind = Terminal::none;
    int waypoint = -1;
    bool attribution_unambiguous = false;
};
struct Continuation {
    int actor = -1, renewal_waypoint = -1;
    CycleCertificate solo{};
};
struct Context {
    Frame frame{};
    Settings settings{};
    ScenarioWindows windows{};
    int picks = 3;
    int recovery_waypoint = -1; // Draft-known spawn/recovery pad, never a fabricated tile.
    // All alternatives represent this same currently visible affected set.
    std::array<Continuation, max_actors> affected{};
    int affected_count = 0;
    ViewClock safety_clock{};
    Exposure self_exposure{};
    CycleCertificate active_capture{};
    Observation<Incident> alarm{};
    Observation<Milestone> milestone{};
    CycleCertificate terminal_cycle{};
    Observation<Failure> failure{};
};
struct Option {
    ViewClock clock{};
    Kind kind = Kind::circuit;
    int target = -1, owner = -1;
    bool executable = false, repair = false;
    Terminal terminal = Terminal::none;
    int terminal_event_id = -1;
    // Production attribution belongs to the explicit schedule builder. Common
    // accounting has event IDs but no camp IDs; never substitute another camp's
    // earlier income for the designated rescued payout.
    std::array<int, max_productions> production_groups = [] {
        std::array<int, max_productions> ids{}; ids.fill(-1); return ids;
    }();
    int terminal_waypoint = -1;
    Point terminal_position{};
    // Certificates use affected[] order, independent schedules end at that
    // actor's named continuation. Occupancy includes travel, rescue and repair.
    std::array<CycleCertificate, max_actors> cycles{};
    Schedule schedule{}, independent{};
};
struct State {
    ActorState execution{};
    int self = -1, owner = -1, started_turn = 0;
    IncidentKind incident = IncidentKind::exposed_hold;
    Terminal terminal = Terminal::none;
    int terminal_event_id = -1;
    int terminal_waypoint = -1;
    Point terminal_position{};
    Gems owner_bag_baseline = 0, owner_gold_baseline = 0;
    bool bank_started = false;
    bool payout_eligible = false;
    int bank_start_round = -1;
    Gems bank_bag = 0, bank_score = 0;
};
struct Decision {
    Action action = Action::yield;
    Work work = Work::independent_circuit;
    Fallback fallback = Fallback::none;
    int option = -1, target = -1, owner = -1, inspected = 0, intervention_turns = 0;
    ProjectID project{};
    Failure reason = Failure::none;
    bool ended = false, terminal_witnessed = false;
    ScoreEvaluation forecast{};
    std::array<Rejection, max_options> rejected{};
};
inline bool safe_self(const Context& c) {
    return c.safety_clock == c.frame.clock &&
        c.self_exposure.health == c.frame.own.actor.health &&
        c.self_exposure.max_health == c.frame.own.actor.max_health && survives(c.self_exposure);
}
inline const Continuation* continuation(const Context& c, int id) {
    if (c.affected_count < 1 || c.affected_count > max_actors) return nullptr;
    for (int i = 0; i < c.affected_count; ++i) if (c.affected[i].actor == id) return &c.affected[i];
    return nullptr;
}
inline bool valid_context(const Context& c) {
    if (!c.frame.valid() || !p6::valid(c.settings) || !c.windows.valid ||
        c.windows.sampled_round != c.frame.clock.round ||
        c.windows.sampled_actor_id != c.frame.clock.self || c.frame.own_team_score < 0 ||
        c.affected_count < 1 || c.affected_count > max_actors ||
        !roster_actor(c.frame.own.actor, c.picks)) return false;
    bool self = false;
    for (int i = 0; i < c.affected_count; ++i) {
        const auto& a = c.affected[i];
        const auto* visible = c.frame.visible(a.actor);
        if (!visible || !roster_actor(*visible, c.picks) || a.renewal_waypoint < 0 ||
            !a.solo.solo || !a.solo.valid(c.frame, a.actor, a.solo.group)) return false;
        for (int j = 0; j < i; ++j) if (c.affected[j].actor == a.actor) return false;
        self = self || a.actor == c.frame.clock.self;
    }
    return self;
}
inline bool resources_current(const Context& c, const Schedule& schedule, bool independent) {
    if (schedule.actor_count != c.affected_count) return false;
    for (int i = 0; i < schedule.actor_count; ++i) {
        const auto& a = schedule.actors[i];
        const auto* visible = c.frame.visible(a.actor_id);
        const auto* next = continuation(c, a.actor_id);
        if (!visible || !next || !a.initial_bag.known() || a.initial_bag.value != visible->bag ||
            a.event_count < 1 || a.event_count > max_actor_events) return false;
        if (a.actor_id == c.frame.clock.self) {
            if (a.initial_bag.source != Source::self_observed ||
                a.initial_gold.source != Source::self_observed ||
                a.initial_gold.value != c.frame.own.gold) return false;
        } else if (a.initial_bag.source != Source::visible_subject ||
                   a.initial_gold.source != Source::conservative || a.initial_gold.value != 0) return false;
        const auto& last = a.events[a.event_count - 1];
        if (last.kind != EventKind::renewal || last.waypoint_id < 0 ||
            (independent && last.waypoint_id != next->renewal_waypoint)) return false;
        for (int j = 0; j < i; ++j) if (schedule.actors[j].actor_id == a.actor_id) return false;
    }
    return true;
}
inline bool alarm_witnessed(const Context& c, const Option& o) {
    if (!c.alarm.current(c.frame.clock, o.owner)) return false;
    const auto& alarm = c.alarm.value;
    const auto* owner = c.frame.visible(o.owner);
    const auto* threat = c.frame.visible(alarm.threat);
    if (!owner || !roster_actor(*owner, c.picks) || !owner->alive || owner->pick_ordinal > 1 ||
        alarm.owner != o.owner || alarm.group != o.target || !threat || !threat->alive ||
        threat->team != Team::enemy || threat->strength <= 0) return false;
    auto distance = [](Point a, Point b) {
        const auto dx = std::abs(static_cast<long long>(a.x) - b.x);
        const auto dy = std::abs(static_cast<long long>(a.y) - b.y);
        return std::max(dx, dy);
    };
    const bool in_threat_reach = threat->attack_range >= 0 &&
        distance(owner->position, threat->position) <= threat->attack_range;
    switch (alarm.kind) {
    case IncidentKind::exposed_hold: {
        const auto* g = c.frame.live_group(o.target);
        return alarm.witness == Failure::unsafe && in_threat_reach && owner->spawn_group == o.target &&
            owner->action == WitnessAction::occupied_spawn && g &&
            g->lifecycle == Lifecycle::reward_available;
    }
    case IncidentKind::interrupted_approach: {
        const auto* g = c.frame.live_group(o.target);
        return alarm.witness == Failure::observed_unavailable && g &&
            threat->position == g->approach && owner->position != g->approach &&
            distance(owner->position, g->approach) <= 1 && owner->channel == Channel::none;
    }
    case IncidentKind::dangerous_return:
        return alarm.witness == Failure::unsafe && in_threat_reach && owner->bag > 0;
    }
    return false;
}
inline bool terminal_in_budget(const Option& o, int remaining_turns) {
    const int owner = actor_index(o.schedule, o.owner);
    if (owner < 0 || remaining_turns < 1) return false;
    const auto& plan = o.schedule.actors[owner];
    for (int i = 0; i < plan.event_count; ++i) {
        const auto& event = plan.events[i];
        if (event.event_id != o.terminal_event_id || event.tick < 0 || event.tick > remaining_turns) continue;
        if (o.terminal == Terminal::protected_bank && event.kind == EventKind::tribute) return true;
        if (o.terminal == Terminal::recovery_waypoint && event.kind == EventKind::recovery &&
            event.waypoint_id == o.terminal_waypoint) return true;
        if (o.terminal == Terminal::safe_payout && event.kind == EventKind::production &&
            event.production_index >= 0 && event.production_index < o.schedule.production_count &&
            event.production_index < max_productions &&
            o.production_groups[event.production_index] == o.target &&
            o.schedule.productions[event.production_index].gems.known() &&
            o.schedule.productions[event.production_index].gems.value > 0) return true;
    }
    return false;
}
inline Rejection admit(const Context& c, const Option& o, Arm arm, bool continuing, int remaining_turns,
                       ScoreEvaluation& forecast) {
    if (o.clock != c.frame.clock) return Rejection::stale;
    if (!o.executable || o.target < 0 || o.schedule.target_id != o.target)
        return Rejection::malformed;
    if (!resources_current(c, o.schedule, false)) return Rejection::resources;
    for (int i = 0; i < c.affected_count; ++i) {
        const int who = c.affected[i].actor;
        const auto* a = c.frame.visible(who);
        if (!a || !a->alive) return Rejection::missing_actor;
        const auto& cert = o.cycles[i];
        if (!cert.valid(c.frame, who, cert.group)) return Rejection::unsafe;
        if (o.kind != Kind::intervention && !cert.solo) return Rejection::unsafe;
    }
    const bool reserve = responder(c.frame.own.actor);
    if (o.kind == Kind::intervention) {
        if (arm != Arm::active || !reserve) return Rejection::wrong_arm;
        const auto* beneficiary = c.frame.visible(o.owner);
        if (!beneficiary || !roster_actor(*beneficiary, c.picks) ||
            beneficiary->pick_ordinal > 1 || o.owner == c.frame.clock.self ||
            !continuation(c, o.owner)) return Rejection::missing_actor;
        if (o.terminal == Terminal::none ||
            (o.terminal == Terminal::recovery_waypoint && o.terminal_waypoint < 0))
            return Rejection::malformed;
        if (!terminal_in_budget(o, remaining_turns)) return Rejection::late_terminal;
        for (int i = 0; i < c.affected_count; ++i)
            if ((c.affected[i].actor == o.owner || c.affected[i].actor == c.frame.clock.self) &&
                o.cycles[i].group != o.target) return Rejection::unsafe;
        if ((!continuing || o.repair) && !alarm_witnessed(c, o)) return Rejection::alarm_unwitnessed;
        if (!resources_current(c, o.independent, true)) return Rejection::resources;
        const auto alternatives = evaluate_alternatives(o.schedule, o.independent, c.windows,
            c.settings, {c.frame.clock.round, c.frame.clock.self}, c.frame.own_team_score);
        if (!joint_support_admissible(alternatives)) return Rejection::no_team_gain;
        forecast = alternatives.cooperative;
    } else {
        if (reserve && o.kind != Kind::bank && o.kind != Kind::recovery && o.kind != Kind::inspection &&
            o.kind != (arm == Arm::active ? Kind::local_circuit : Kind::committed_circuit))
            return Rejection::wrong_arm;
        if (!reserve && o.kind != Kind::circuit && o.kind != Kind::bank &&
            o.kind != Kind::recovery && o.kind != Kind::inspection) return Rejection::wrong_arm;
        forecast = evaluate_schedule(o.schedule, c.windows, c.settings,
            {c.frame.clock.round, c.frame.clock.self}, c.frame.own_team_score);
        if (!forecast.eligible()) return Rejection::accounting;
    }
    return Rejection::none;
}
inline bool witnessed_terminal(const Context& c, const State& s) {
    const auto& receipt = c.milestone;
    if (!receipt.current(c.frame.clock, s.owner) || !receipt.value.attribution_unambiguous ||
        receipt.value.owner != s.owner || receipt.value.group != s.execution.project.group ||
        receipt.value.kind != s.terminal || !c.terminal_cycle.solo ||
        !c.terminal_cycle.valid(c.frame, s.owner, s.execution.project.group)) return false;
    const auto* owner = c.frame.visible(s.owner);
    if (!owner) return false;
    switch (s.terminal) {
    case Terminal::safe_payout:
        return s.payout_eligible && c.frame.live_group(s.execution.project.group) &&
            c.frame.live_group(s.execution.project.group)->lifecycle == Lifecycle::respawning &&
            (owner->bag > s.owner_bag_baseline || (s.owner == c.frame.clock.self &&
             c.frame.own.gold > s.owner_gold_baseline));
    case Terminal::protected_bank:
        return s.bank_started && c.frame.clock.round > s.bank_start_round &&
            owner->bag == 0 && owner->channel == Channel::none &&
            c.frame.own_team_score >= s.bank_score &&
            c.frame.own_team_score - s.bank_score >= s.bank_bag;
    case Terminal::recovery_waypoint:
        return receipt.value.waypoint == s.terminal_waypoint && owner->position == s.terminal_position;
    default: return false;
    }
}
inline void remember_bank(State& s, const Context& c) {
    const auto* owner = c.frame.visible(s.owner);
    const auto* group = c.frame.live_group(s.execution.project.group);
    if (s.execution.active && !s.payout_eligible && owner && group &&
        owner->spawn_group == group->id && group->lifecycle == Lifecycle::reward_available &&
        group->uncontested_now) {
        s.payout_eligible = true; s.owner_bag_baseline = owner->bag;
    }
    if (s.execution.active && !s.bank_started && owner && owner->bag > 0 &&
        owner->channel == Channel::tribute && owner->channel_valid) {
        s.bank_started = true; s.bank_start_round = c.frame.clock.round;
        s.bank_bag = owner->bag; s.bank_score = c.frame.own_team_score;
    }
}
inline Decision step(State& s, const Context& c, std::span<const Option> options, Arm arm) {
    Decision out;
    if (!c.frame.valid() || !roster_actor(c.frame.own.actor, c.picks) ||
        (s.self >= 0 && s.self != c.frame.clock.self) ||
        c.frame.clock.round <= s.execution.last_round) return out;
    s.self = c.frame.clock.self; s.execution.last_round = c.frame.clock.round; ++s.execution.owner_turn;
    auto end = [&](Failure reason) {
        if (s.execution.active) { s.execution.retire(reason); out.ended = true; }
        out.reason = reason;
    };
    auto bounded_fallback = [&]() {
        const auto* own = continuation(c, c.frame.clock.self);
        if (s.execution.failed_inspections < 2 && own && own->solo.solo &&
            own->solo.valid(c.frame, c.frame.clock.self, own->solo.group)) {
            ++s.execution.failed_inspections; out.action = Action::inspect;
            out.target = own->solo.group; out.fallback = Fallback::inspect_primary_source;
        } else if (c.recovery_waypoint >= 0) {
            out.action = Action::recover; out.target = c.recovery_waypoint;
            out.fallback = Fallback::recovery_to_spawn;
        }
    };
    out.project = s.execution.project;
    if (s.execution.active) {
        out.intervention_turns = s.execution.owner_turn - s.started_turn;
        // A completion first visible after the eighth action is an observed
        // endpoint. Test it before expiry; no ninth intervention is licensed.
        if (safe_self(c) && witnessed_terminal(c, s)) {
            out.terminal_witnessed = true; end(Failure::none);
        } else if (out.intervention_turns >= intervention_limit) end(Failure::expired);
    }
    // Expiry retires the intervention but never cancels a valid channel or a
    // separately safe own capture. Such continuation receives no rescue credit.
    if (c.frame.own.actor.channel != Channel::none && c.frame.own.actor.channel_valid) {
        out.action = Action::preserve_channel; remember_bank(s, c); return out;
    }
    if (safe_self(c) && witnessed_release(c.frame, c.frame.clock.self,
        c.active_capture.group, c.active_capture)) {
        out.action = Action::preserve_capture; out.target = c.active_capture.group;
        remember_bank(s, c); return out;
    }
    if (!c.frame.own.actor.alive || !safe_self(c)) {
        end(Failure::unsafe); out.action = Action::recover;
        out.target = c.recovery_waypoint; out.fallback = Fallback::recovery_to_spawn; return out;
    }
    if (!valid_context(c) || options.size() > max_options) {
        end(Failure::not_observed);
        if (p6::valid(c.settings) && c.windows.valid &&
            c.windows.sampled_actor_id == c.frame.clock.self &&
            c.windows.sampled_round == c.frame.clock.round) bounded_fallback();
        return out;
    }
    std::array<int, 8> groups{};
    int group_count = 0;
    for (const auto& o : options) {
        if (o.kind == Kind::bank || o.kind == Kind::recovery || o.kind == Kind::inspection) continue;
        bool known = false;
        for (int i = 0; i < group_count; ++i) known = known || groups[i] == o.target;
        if (!known) {
            if (group_count == 8) { end(Failure::conflicting_evidence); bounded_fallback(); return out; }
            groups[group_count++] = o.target;
        }
    }
    int best_solo = -1, best_joint = -1;
    ScoreEvaluation solo{}, joint{};
    const bool failure = c.failure.current(c.frame.clock, c.frame.clock.self) &&
        (c.failure.value == Failure::observed_failed_action || c.failure.value == Failure::conflicting_evidence);
    for (std::size_t i = 0; i < options.size(); ++i) {
        const auto& o = options[i];
        ScoreEvaluation score;
        const bool same = s.execution.active && o.target == s.execution.project.group && o.owner == s.owner;
        const int remaining = s.execution.active ?
            intervention_limit - (s.execution.owner_turn - s.started_turn) : intervention_limit;
        auto rejected = admit(c, o, arm, same, remaining, score);
        if (rejected == Rejection::none && o.kind == Kind::intervention) {
            if (s.execution.active && (!same || o.terminal != s.terminal ||
                o.terminal_event_id != s.terminal_event_id ||
                o.terminal_waypoint != s.terminal_waypoint || o.terminal_position != s.terminal_position))
                rejected = Rejection::malformed;
            else if (!s.execution.active && (!s.execution.may_admit(o.target, c.frame.clock.round) || out.ended))
                rejected = Rejection::retired;
            else if (o.repair != failure || (o.repair && (!same || s.execution.repairs != 0)))
                rejected = Rejection::repair_unlicensed;
        }
        out.rejected[i] = rejected; ++out.inspected;
        if (rejected != Rejection::none) continue;
        if (o.kind == Kind::intervention) {
            if (best_joint < 0 || compare(score, joint) > 0) { best_joint = static_cast<int>(i); joint = score; }
        } else if (best_solo < 0 || compare(score, solo) > 0) {
            best_solo = static_cast<int>(i); solo = score;
        }
    }
    // The represented independent counterfactual must also beat every offered
    // productive incumbent, so a deliberately weak rescue baseline cannot win.
    if (best_joint >= 0 && (best_solo < 0 ||
        (!solo.immediate_legal_win && joint.min_margin > solo.min_margin))) {
        const auto& o = options[best_joint];
        if (!s.execution.active) {
            if (!s.execution.admit(o.target, c.frame.clock.round, 0,
                c.settings.max_rounds - c.frame.clock.round, c.frame.own.actor.bag, c.frame.own.gold))
                best_joint = -1;
            else {
                s.owner = o.owner; s.started_turn = s.execution.owner_turn;
                s.incident = c.alarm.value.kind; s.terminal = o.terminal;
                s.terminal_event_id = o.terminal_event_id;
                s.terminal_waypoint = o.terminal_waypoint; s.terminal_position = o.terminal_position;
                s.owner_bag_baseline = c.frame.visible(o.owner)->bag;
                s.owner_gold_baseline = o.owner == s.self ? c.frame.own.gold : 0;
                s.bank_started = s.payout_eligible = false;
            }
        } else if (o.repair && !s.execution.repair()) best_joint = -1;
        if (best_joint >= 0) {
            s.execution.failed_inspections = 0;
            out.action = Action::intervene; out.option = best_joint; out.target = o.target; out.owner = o.owner;
            out.forecast = joint; out.project = s.execution.project;
            out.intervention_turns = s.execution.owner_turn - s.started_turn + 1;
            out.work = s.incident == IncidentKind::exposed_hold ? Work::stabilize_hold :
                s.incident == IncidentKind::interrupted_approach ? Work::clear_approach : Work::escort_return;
            remember_bank(s, c); return out;
        }
    }
    if (s.execution.active) end(failure ? c.failure.value : Failure::not_observed);
    if (best_solo >= 0) {
        const auto& o = options[best_solo];
        out.option = best_solo; out.target = o.target; out.forecast = solo;
        out.fallback = Fallback::independent_cycle;
        if (o.kind == Kind::bank) { out.action = Action::bank; out.fallback = Fallback::bank_to_altar; }
        else if (o.kind == Kind::recovery) { out.action = Action::recover; out.fallback = Fallback::recovery_to_spawn; }
        else if (o.kind == Kind::inspection) {
            bounded_fallback();
        }
        else {
            out.action = Action::produce;
            out.work = o.kind == Kind::local_circuit ? Work::local_production :
                o.kind == Kind::committed_circuit ? Work::committed_production : Work::independent_circuit;
        }
        if (out.action != Action::inspect) s.execution.failed_inspections = 0;
    } else {
        bounded_fallback();
    }
    return out;
}
} // namespace p6::firebrigade
