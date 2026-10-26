#pragma once
#include "profile.h"
#include <array>
#include <span>

// Offline decision boundary only. These are proposed next actions, not an SDK
// adapter or a claim that a future participant will perform its schedule.
namespace p6::owner_builder {
enum class Arm { active, control };
enum class Kind { project, independent, bank, recover, inspect, release };
enum class Action { yield, preserve_channel, work, support_split, bank, recover,
                    inspect, release };
enum class Rejection { none, malformed, stale, resources, unsafe, wrong_owner,
    helper_missing, helper_not_useful, requires_helper, no_team_gain,
    unlicensed_release, accounting };

struct Continuation {
    int actor = -1;
    CycleCertificate cycle{};
};
struct Context {
    Frame frame{};
    Settings settings{};
    ScenarioWindows windows{};
    Gems team_score = 0;
    int picks = 3;
    // Every ranked option represents this exact set, including each helper's
    // independent continuation. Regenerate the set when personal vision changes.
    std::array<Continuation, max_actors> affected{};
    int affected_count = 0;
};
struct Option {
    ViewClock clock{};
    Kind kind = Kind::independent;
    int target = -1, owner = -1, helper = -1;
    // Active requires solo=true. Control may use a safe joint certificate with
    // solo=false only while its required helper is actually useful and visible.
    CycleCertificate owner_cycle{}, helper_cycle{};
    Exposure self_exposure{};
    bool executable = false;
    Schedule schedule{}, independent{};
};
struct Decision {
    Action action = Action::yield;
    int option = -1, target = -1;
    ScoreEvaluation forecast{};
    std::array<Rejection, max_options> rejected{};
    int inspected = 0;
};

inline bool valid_context(const Context& c) {
    if (!c.frame.valid() || !p6::valid(c.settings) || !c.windows.valid ||
        c.windows.sampled_round != c.frame.clock.round ||
        c.windows.sampled_actor_id != c.frame.clock.self || c.team_score < 0 ||
        c.affected_count < 1 || c.affected_count > max_actors ||
        !roster_actor(c.frame.own.actor, c.picks)) return false;
    bool includes_self = false;
    for (int i = 0; i < c.affected_count; ++i) {
        const int id = c.affected[i].actor;
        const auto* actor = c.frame.visible(id);
        if (!actor || !roster_actor(*actor, c.picks)) return false;
        if (id == c.frame.clock.self) includes_self = true;
        for (int j = 0; j < i; ++j)
            if (c.affected[j].actor == id) return false;
    }
    return includes_self;
}
inline const Continuation* continuation(const Context& c, int actor) {
    for (int i = 0; i < c.affected_count; ++i)
        if (c.affected[i].actor == actor) return &c.affected[i];
    return nullptr;
}
inline bool independent_safe(const Context& c, int actor) {
    const auto* next = continuation(c, actor);
    return next && next->cycle.solo &&
        next->cycle.valid(c.frame, actor, next->cycle.group);
}
inline bool resources_current(const Context& c, const Schedule& schedule) {
    if (schedule.actor_count != c.affected_count || schedule.actor_count < 1 ||
        schedule.actor_count > max_actors) return false;
    for (int i = 0; i < schedule.actor_count; ++i) {
        const auto& entry = schedule.actors[i];
        const auto* actor = c.frame.visible(entry.actor_id);
        if (!actor || !continuation(c, entry.actor_id) ||
            !entry.initial_bag.known() || entry.initial_bag.value != actor->bag)
            return false;
        if (entry.actor_id == c.frame.clock.self) {
            if (entry.initial_bag.source != Source::self_observed ||
                entry.initial_gold.source != Source::self_observed ||
                entry.initial_gold.value != c.frame.own.gold) return false;
        } else if (entry.initial_bag.source != Source::visible_subject ||
                   entry.initial_gold.source != Source::conservative ||
                   entry.initial_gold.value != 0) return false;
        for (int j = 0; j < i; ++j)
            if (schedule.actors[j].actor_id == entry.actor_id) return false;
    }
    return true;
}
inline bool useful_now(const Frame& frame, int actor, int group) {
    const auto* a = frame.visible(actor);
    const auto* target = frame.live_group(group);
    return a && participant_failure(frame, actor) == Failure::none &&
        ((a->action == WitnessAction::useful_attack && a->action_group == group) ||
         (a->action == WitnessAction::occupied_spawn && a->spawn_group == group) ||
         (a->can_attack && a->ready_attack_group == group && target &&
          target->lifecycle == Lifecycle::living));
}
inline Rejection admit(const Context& c, const Option& o, Arm arm,
                       ScoreEvaluation& forecast) {
    if (o.clock != c.frame.clock) return Rejection::stale;
    if (o.target < 0 || !o.executable || o.schedule.target_id != o.target)
        return Rejection::malformed;
    if (!resources_current(c, o.schedule)) return Rejection::resources;
    if (o.self_exposure.health != c.frame.own.actor.health ||
        o.self_exposure.max_health != c.frame.own.actor.max_health ||
        !survives(o.self_exposure)) return Rejection::unsafe;
    const Clock clock{c.frame.clock.round, c.frame.clock.self};
    forecast = evaluate_schedule(o.schedule, c.windows, c.settings, clock, c.team_score);
    if (!forecast.eligible()) return Rejection::accounting;
    if (o.kind == Kind::project || o.kind == Kind::release) {
        if (c.frame.clock.self != o.owner && c.frame.clock.self != o.helper)
            return Rejection::malformed;
        const auto* owner = c.frame.visible(o.owner);
        if (!owner || !fixed_owner(*owner, o.target, c.picks))
            return Rejection::wrong_owner;
        if (!o.owner_cycle.valid(c.frame, o.owner, o.target)) return Rejection::unsafe;
        if (arm == Arm::active && !o.owner_cycle.solo) return Rejection::requires_helper;
        if (o.helper >= 0) {
            if (o.helper == o.owner || !continuation(c, o.helper) ||
                participant_failure(c.frame, o.helper) != Failure::none)
                return Rejection::helper_missing;
            if (!o.helper_cycle.valid(c.frame, o.helper, o.target)) return Rejection::unsafe;
            if (!useful_now(c.frame, o.helper, o.target)) return Rejection::helper_not_useful;
        } else if (!o.owner_cycle.solo) return Rejection::requires_helper;
        if (o.kind == Kind::release) {
            if (o.helper != c.frame.clock.self ||
                !witnessed_release(c.frame, o.owner, o.target, o.owner_cycle) ||
                !independent_safe(c, o.helper)) return Rejection::unlicensed_release;
        } else if (o.helper >= 0) {
            if (!independent_safe(c, o.helper) || !independent_safe(c, o.owner))
                return Rejection::unsafe;
            if (!resources_current(c, o.independent)) return Rejection::resources;
            const auto alternatives = evaluate_alternatives(o.schedule, o.independent,
                c.windows, c.settings, clock, c.team_score);
            if (!joint_support_admissible(alternatives)) return Rejection::no_team_gain;
        }
    } else if (o.helper >= 0) return Rejection::malformed;
    // Any represented actor outside the actual crew continues independently;
    // being in a visible roster is never itself productive participation.
    for (int i = 0; i < c.affected_count; ++i) {
        const int actor = c.affected[i].actor;
        if (o.kind == Kind::project && (actor == o.owner || actor == o.helper)) continue;
        if (actor == c.frame.clock.self &&
            (o.kind == Kind::bank || o.kind == Kind::recover || o.kind == Kind::inspect)) continue;
        if (!independent_safe(c, actor)) return Rejection::unsafe;
    }
    return Rejection::none;
}
inline Decision select(const Context& c, std::span<const Option> options, Arm arm) {
    Decision out;
    if (!valid_context(c) || options.size() > max_options) return out;
    if (c.frame.own.actor.channel != Channel::none && c.frame.own.actor.channel_valid) {
        out.action = Action::preserve_channel;
        return out;
    }
    // At most eight distinct production groups, plus bounded bank/recovery and
    // inspection entries. A larger menu is rejected, never silently truncated.
    std::array<int, 8> groups{};
    int group_count = 0;
    for (const auto& o : options) if (o.kind == Kind::project) {
        bool seen = false;
        for (int i = 0; i < group_count; ++i) if (groups[i] == o.target) seen = true;
        if (!seen) {
            if (group_count == 8) return out;
            groups[group_count++] = o.target;
        }
    }
    for (std::size_t i = 0; i < options.size(); ++i) {
        ScoreEvaluation score;
        out.rejected[i] = admit(c, options[i], arm, score);
        ++out.inspected;
        if (out.rejected[i] != Rejection::none ||
            (out.option >= 0 && compare(score, out.forecast) <= 0)) continue;
        out.option = static_cast<int>(i); out.target = options[i].target; out.forecast = score;
        switch (options[i].kind) {
        case Kind::project: out.action = c.frame.clock.self == options[i].owner ?
            Action::work : Action::support_split; break;
        case Kind::bank: out.action = Action::bank; break;
        case Kind::recover: out.action = Action::recover; break;
        case Kind::inspect: out.action = Action::inspect; break;
        case Kind::release: out.action = Action::release; break;
        default: out.action = Action::work; break;
        }
    }
    return out;
}
} // namespace p6::owner_builder
