#pragma once
#include "profile.h"
#include "../../common/src/execute.h"

// Offline policy only. The caller supplies current personal observations and
// complete bounded schedules; proposed actions are not successful SDK actions.
namespace p6::replicas {
enum class Arm { active, control };
enum class Mode { independent, startup_owner, redundant, takeover, released, unavailable };
enum class Rejection { none, malformed, resources, accounting, no_gain, unsafe, pads };
struct Projection {
    Schedule schedule{};
    // Each unique production event names its real group. Multiple holders share
    // one Production; two owners cannot each forecast the same group's payout.
    std::array<int, max_productions> groups{};
    Projection() { groups.fill(-1); }
};
struct Continuation { int actor = -1; CycleCertificate cycle{}; };
struct Context {
    ReferenceInput input{};
    Settings settings{};
    int enemy_score = 0, picks = 3;
    Draft draft{};
    CycleCertificate startup_cycle{}, independent_cycle{}, owner_cycle{};
    Projection startup{}, independent{};
    std::array<Continuation, max_actors> continuations{};
    int continuation_count = 0;
    bool at_startup_pad = false, at_independent_approach = false;
    // A fresh personally witnessed failed action/unavailable participant. Mere
    // absence, elapsed time and cached observations never create retry credit.
    Observation<Failure> failure{};
    int failure_group = -1;
    Observation<Outcome> failed_action_result{};
    Observation<int> blocked_pad{};
};
struct State {
    ActorState worker{};
    ScoreHistory score_history{};
    bool initial_attempted = false, startup_closed = false, released = false;
    bool owner_seen = false, repeat_used = false;
    int attempted_epoch = -1, repeat_epoch = -1, waiting = 0, last_round = -1;
    Failure contact = Failure::not_observed;
};
struct Decision {
    StepProposal next{};
    Mode mode = Mode::independent;
    Rejection rejection = Rejection::none;
    int approach_tile = -1;
    bool release_witness = false, repeat_admitted = false;
    Alternatives forecast{};
    ScenarioWindows windows{};
};

inline const Actor* primary(const Context& c) {
    const auto& f = c.input.frame;
    if (f.own.actor.pick_ordinal == 0) return &f.own.actor;
    for (int i = 0; i < f.actor_count; ++i) {
        const auto* a = f.visible(f.actors[i].subject);
        if (a && roster_actor(*a, c.picks) && a->pick_ordinal == 0) return a;
    }
    return nullptr;
}
inline bool resources_match(const Context& c, const Projection& p) {
    const auto& f = c.input.frame;
    const auto& s = p.schedule;
    if (s.actor_count < 1 || s.actor_count > max_actors ||
        c.continuation_count != s.actor_count || s.production_count < 0 ||
        s.production_count > max_productions) return false;
    bool has_self = false;
    for (int i = 0; i < s.actor_count; ++i) {
        const auto& scheduled = s.actors[i];
        const auto* a = f.visible(scheduled.actor_id);
        if (!a || !roster_actor(*a, c.picks)) return false;
        const bool self = a->id == f.clock.self;
        has_self |= self;
        if (scheduled.initial_bag.value != a->bag ||
            scheduled.initial_bag.source != (self ? Source::self_observed : Source::visible_subject) ||
            scheduled.initial_gold.value != (self ? f.own.gold : 0) ||
            scheduled.initial_gold.source != (self ? Source::self_observed : Source::conservative)) return false;
        const Continuation* next = nullptr;
        for (int j = 0; j < c.continuation_count; ++j)
            if (c.continuations[j].actor == a->id) {
                if (next) return false;
                next = &c.continuations[j];
            }
        if (!next || !next->cycle.solo || !next->cycle.valid(f, a->id, next->cycle.group)) return false;
        bool renewal = false;
        if (scheduled.event_count < 1 || scheduled.event_count > max_actor_events) return false;
        for (int e = 0; e < scheduled.event_count; ++e)
            if (scheduled.events[e].kind == EventKind::renewal)
                renewal = scheduled.events[e].waypoint_id == next->cycle.group;
        if (!renewal) return false;
    }
    for (int pindex = 0; pindex < s.production_count; ++pindex) {
        if (p.groups[pindex] < 0) return false;
        for (int j = 0; j < pindex; ++j)
            if (p.groups[pindex] == p.groups[j]) return false;
    }
    return has_self;
}
inline Alternatives alternatives(const Context& c, const ScenarioWindows& windows) {
    return evaluate_alternatives(c.startup.schedule, c.independent.schedule, windows,
        c.settings, {c.input.frame.clock.round, c.input.frame.clock.self}, c.input.frame.own_team_score);
}
inline bool useful_owner(const Context& c, const Actor* owner) {
    const auto& f = c.input.frame;
    const int group = c.draft.startup_group;
    const auto* g = f.live_group(group);
    if (!owner || owner->id == f.clock.self || !owner->alive ||
        !roster_actor(*owner, c.picks) || owner->pick_ordinal != 0 ||
        owner->channel != Channel::none || !g || !c.owner_cycle.solo ||
        !c.owner_cycle.valid(f, owner->id, group)) return false;
    const bool attack = g->lifecycle == Lifecycle::living &&
        owner->action == WitnessAction::useful_attack && owner->action_group == group;
    const bool hold = g->lifecycle == Lifecycle::reward_available && g->uncontested_now &&
        owner->spawn_group == group;
    return attack || hold;
}
inline bool observed_failure(const Context& c) {
    const auto& f = c.input.frame;
    const auto* a = f.visible(c.failure.subject);
    if (!a || !roster_actor(*a, c.picks) || a->pick_ordinal > 1 ||
        c.failure_group != c.draft.startup_group || !c.failure.current(f.clock, a->id)) return false;
    if (c.failure.value == Failure::observed_unavailable)
        return participant_failure(f, a->id) == Failure::observed_unavailable;
    return c.failure.value == Failure::observed_failed_action &&
        c.failed_action_result.current(f.clock, a->id) &&
        (c.failed_action_result.value == Outcome::hidden_collision ||
         c.failed_action_result.value == Outcome::unexpected_error);
}
inline void close_startup(State& s, Failure reason) {
    if (s.worker.active) s.worker.retire(reason);
    s.startup_closed = true; s.waiting = 0;
}

inline Decision step(State& s, const Context& c, Arm arm = Arm::active) {
    Decision d;
    const auto& f = c.input.frame;
    if (!f.valid() || s.last_round == f.clock.round) return d;
    s.last_round = f.clock.round;
    const bool sampled = p6::valid(c.settings) && c.enemy_score >= 0 &&
        s.score_history.sample({f.clock.round, f.clock.self}, c.enemy_score);
    if (sampled) d.windows = s.score_history.windows(c.settings, {f.clock.round, f.clock.self}, c.enemy_score);
    // A valid safe channel outranks draft, appointments, retry and accounting.
    if (f.own.actor.channel != Channel::none && f.own.actor.channel_valid && c.input.safe_now) {
        d.next = reference_step(s.worker, c.input); return d;
    }
    if (!roster_actor(f.own.actor, c.picks) || !sampled ||
        c.draft.independent_groups[f.own.actor.pick_ordinal] < 0) {
        d.rejection = Rejection::malformed; d.next = fallback(c.input); return d;
    }
    const int pick = f.own.actor.pick_ordinal;
    const int first = c.draft.startup_group;
    const int independent = c.draft.independent_groups[pick];
    if (c.input.safe_now && c.input.at_bank && c.input.altar_legal &&
        f.own.actor.bag > 0 && f.own_team_score >= 0 &&
        f.own.actor.bag >= std::max<Gems>(0, Gems(c.settings.win_gems) - f.own_team_score)) {
        // A directly legal winning own-bank outranks startup investment.
        if (s.worker.active) {
            s.worker.enter(Phase::bank, Stage::execute);
            auto winning = c.input;
            winning.solo = s.worker.project.group == first ? c.startup_cycle : c.independent_cycle;
            d.next = reference_step(s.worker, winning);
        } else d.next = {p6::Action::tribute, Stage::execute, Phase::bank};
        return d;
    }
    const auto pads = startup_pads(c.draft);
    const auto* owner = primary(c);
    const bool available_owner = owner && owner->alive && owner->channel == Channel::none;
    const bool was_source = s.worker.active && s.worker.project.group == first;
    bool use_source = pick <= 1 && !s.startup_closed &&
        (arm == Arm::active || pick == 0);
    if (s.worker.active && s.worker.project.group != first) use_source = false;
    if (use_source && !s.worker.active && !valid_draft(c.draft, c.picks)) {
        // An unavailable assignment elsewhere does not invalidate this actor's
        // own certified complete circuit.
        s.startup_closed = true; use_source = false;
        d.mode = Mode::unavailable; d.rejection = Rejection::malformed;
    }
    // Renewal is the safe terminal milestone. Crossing an epoch mid-channel or
    // capture never renames the latched project or supplies another attempt.
    if (was_source && s.worker.bank_seen && c.input.at_renewal) {
        s.startup_closed = true; use_source = false;
    }
    const bool failed = use_source && observed_failure(c);
    if (failed && arm == Arm::active && !s.repeat_used && s.repeat_epoch < 0)
        s.repeat_epoch = (s.worker.active ? s.worker.project.epoch : f.clock.round / 64) + 1;
    if (!s.worker.active && s.startup_closed && arm == Arm::active && pick <= 1 &&
        !s.repeat_used && s.repeat_epoch == f.clock.round / 64) {
        s.repeat_used = true; s.startup_closed = false; s.released = false;
        s.owner_seen = false; s.waiting = 0; use_source = true; d.repeat_admitted = true;
    }
    const bool needs_approach = !s.worker.active ||
        (s.worker.phase == Phase::travel && !s.worker.payout_seen && !s.worker.bank_seen);
    if (use_source && needs_approach && arm == Arm::active && (pads[0] < 0 || pads[1] < 0)) {
        close_startup(s, Failure::observed_unavailable); use_source = false;
        d.mode = Mode::unavailable; d.rejection = Rejection::pads;
    }
    if (use_source && needs_approach && arm == Arm::active &&
        c.blocked_pad.current(f.clock, f.clock.self) && c.blocked_pad.value == pads[pick]) {
        if (!s.repeat_used && s.repeat_epoch < 0) s.repeat_epoch = f.clock.round / 64 + 1;
        close_startup(s, Failure::observed_failed_action); use_source = false;
    }
    if (use_source && pick == 1 && !s.worker.payout_seen && s.worker.phase != Phase::bank) {
        s.contact = owner ? participant_failure(f, owner->id) : Failure::not_observed;
        const bool local_takeover = f.live_group(first) && c.startup_cycle.solo &&
            c.startup_cycle.valid(f, f.clock.self, first);
        if (c.input.safe_now && c.independent_cycle.solo &&
            c.independent_cycle.valid(f, f.clock.self, independent) && useful_owner(c, owner)) {
            close_startup(s, Failure::none); s.released = true; use_source = false;
            d.mode = Mode::released; d.release_witness = true;
        } else if (s.owner_seen && !available_owner && !local_takeover) {
            // Lost contact stays unknown. Never retain an unseen owner claim.
            close_startup(s, s.contact); use_source = false;
        } else if (!available_owner && local_takeover) d.mode = Mode::takeover;
        if (available_owner) s.owner_seen = true;
    }
    if (use_source && !s.worker.active) {
        s.initial_attempted = true; s.attempted_epoch = f.clock.round / 64;
        const bool missing_changed_owner = pick == 1 && available_owner &&
            (actor_index(c.startup.schedule, owner->id) < 0 ||
             actor_index(c.independent.schedule, owner->id) < 0);
        if (missing_changed_owner || !resources_match(c, c.startup) || !resources_match(c, c.independent)) {
            d.rejection = Rejection::resources; close_startup(s, Failure::conflicting_evidence); use_source = false;
        } else {
            d.forecast = alternatives(c, d.windows);
            const bool visible_peer = pick == 1 && available_owner;
            const bool gain = visible_peer ? joint_support_admissible(d.forecast) &&
                compare(d.forecast.cooperative, d.forecast.independent) > 0 :
                d.forecast.matched && d.forecast.cooperative.eligible() && d.forecast.independent.eligible() &&
                compare(d.forecast.cooperative, d.forecast.independent) >= 0;
            if (!gain) {
                d.rejection = d.forecast.cooperative.eligible() && d.forecast.independent.eligible() ?
                    Rejection::no_gain : Rejection::accounting;
                close_startup(s, Failure::conflicting_evidence); use_source = false;
            }
        }
    }
    if (use_source && (!c.input.safe_now || !c.startup_cycle.solo ||
        !c.startup_cycle.valid(f, f.clock.self, first))) {
        close_startup(s, Failure::unsafe); use_source = false; d.rejection = Rejection::unsafe;
    }
    if (use_source && !d.release_witness && s.waiting >= 8) {
        close_startup(s, Failure::missed_appointment); use_source = false;
    }
    auto in = c.input;
    in.assigned_group = use_source ? first : independent;
    in.solo = use_source ? c.startup_cycle : c.independent_cycle;
    in.at_approach = use_source ? c.at_startup_pad : c.at_independent_approach;
    if (use_source && !s.worker.active) {
        // Admission to a known route/approach is legal before seeing a camp's
        // lifecycle. The first action remains inspect, never a hidden attack.
        s.worker.admit(first, f.clock.round, in.forecast_rounds, in.remaining_rounds,
            f.own.actor.bag, f.own.gold);
    }
    d.next = reference_step(s.worker, in);
    if (use_source) {
        d.approach_tile = arm == Arm::active ? pads[pick] : pads[0];
        if (d.mode != Mode::takeover) d.mode = pick == 0 ? Mode::startup_owner : Mode::redundant;
        if (d.next.action == p6::Action::inspect || d.next.action == p6::Action::yield ||
            (d.next.action == p6::Action::move && c.at_startup_pad)) ++s.waiting;
        if (!s.worker.active) s.startup_closed = true;
    } else if (d.mode != Mode::released && d.mode != Mode::unavailable) d.mode = Mode::independent;
    return d;
}
} // namespace p6::replicas
