#pragma once
#include "profile.h"

namespace p6::contingencies {
using namespace organization;
enum class Branch { none, holder_recall, blocked_approach, support_unavailable };
struct State {
    struct Epoch { int group = -1, epoch = -1; bool repaired = false, retired = false; };
    std::array<Epoch, 8> history{};
    int group = -1, epoch = -1, replacement = -1, lower_group = -1, rally_turns = 0, branch_turns = 0;
    Branch branch = Branch::none;
    Point alternative{};
    bool conflict = false;
    Epoch* record(int candidate, int current_epoch) {
        for (auto& e : history) if (e.group == candidate && e.epoch == current_epoch) return &e;
        for (auto& e : history) if (e.group < 0 || e.epoch < current_epoch) {
            e = {candidate, current_epoch, false, false}; return &e;
        }
        return nullptr;
    }
};
inline Decision fallback(const Input& in) {
    if (in.frame.own.actor.bag > 0)
        return {Intent::bank, in.current_group, -1, {}, false, "contingency-retain-own-bag"};
    return independent(in);
}
inline int lower(const Input& in, int current) {
    int selected = -1;
    for (int i = 0; i < in.site_count; ++i) {
        const auto& s = in.sites[i];
        if (s.group.id != current && s.known && !s.blocked &&
            s.solo(in.frame, in.frame.clock.self) &&
            (selected < 0 || s.group.id < selected)) selected = s.group.id;
    }
    return selected;
}
inline Branch witnessed_branch(const Input& in, const Site& site) {
    const auto* holder = in.ordinal_actor(0);
    const auto* support = in.ordinal_actor(1);
    if (holder && holder->alive && holder->channel == Channel::recall) return Branch::holder_recall;
    if (site.blocked || in.hidden_collision) return Branch::blocked_approach;
    // Absence is unknown. Only a current visible unavailability licenses this branch.
    if (support && (!support->alive || support->channel != Channel::none))
        return Branch::support_unavailable;
    return Branch::none;
}
inline bool destination_taken(const Input& in, Point destination, int allowed = -1) {
    for (int i = 0; i < in.frame.actor_count; ++i) {
        const auto& seen = in.frame.actors[i];
        if (seen.current(in.frame.clock, seen.subject) && seen.value.alive &&
            seen.value.team == Team::ally && seen.value.id != allowed &&
            seen.value.position == destination) return true;
    }
    return false;
}
inline Decision choose(State& state, const Input& in, bool control) {
    const auto& own = in.frame.own.actor;
    if (!in.frame.valid() || !in.safe_now || in.ordinal < 0 || in.ordinal >= 3 ||
        (own.channel != Channel::none && own.channel_valid)) return independent(in);
    if (in.actual_payout) return fallback(in);
    const int candidate = in.current_group >= 0 ? in.current_group : in.proposed_group;
    if (candidate < 0) return fallback(in);
    if (state.group != candidate || in.actual_bank || in.restored) {
        state.group = candidate; state.epoch = in.frame.clock.round / 64;
        state.branch = Branch::none; state.conflict = false; state.rally_turns = state.branch_turns = 0;
        state.replacement = -1; state.lower_group = lower(in, candidate);
        if (const auto* s = in.site(candidate)) state.alternative = s->group.retreat;
    }
    auto* epoch = state.record(candidate, state.epoch);
    if (!epoch || epoch->retired) return fallback(in);
    const auto* site = in.site(candidate);
    if (!site || !site->known) return fallback(in);
    if (in.phase == Phase::hold && own.spawn_group == candidate && !site->blocked)
        return work(candidate, "contingency-preserve-own-safe-capture");
    const Branch observed = witnessed_branch(in, *site);
    if (control && (observed != Branch::none || in.observed_failure)) {
        epoch->retired = true; return fallback(in);
    }
    if (state.branch == Branch::none && observed != Branch::none) {
        if (epoch->repaired) { epoch->retired = true; return fallback(in); }
        epoch->repaired = true; state.branch = observed;
        if (observed == Branch::holder_recall) {
            for (int i = 0; i < site->actor_count; ++i) {
                const int id = site->actors[i];
                if (ready(in.frame.visible(id)) && site->solo(in.frame, id) &&
                    (state.replacement < 0 || id < state.replacement)) state.replacement = id;
            }
            if (state.replacement < 0) state.conflict = true;
        }
        if (observed == Branch::support_unavailable && state.lower_group < 0) state.conflict = true;
    }
    if (state.branch != Branch::none && !state.conflict) {
        // Conflict is physical evidence, never presumed private disagreement.
        state.conflict = observed != state.branch;
        if (state.branch == Branch::holder_recall) {
            const auto* replacement = in.frame.visible(state.replacement);
            if (replacement && replacement->action == WitnessAction::useful_attack &&
                replacement->action_group != candidate) state.conflict = true;
            if (destination_taken(in, site->group.spawn, state.replacement)) state.conflict = true;
        } else if (state.branch == Branch::blocked_approach &&
                   destination_taken(in, state.alternative)) state.conflict = true;
    }
    if (state.conflict) {
        if (state.rally_turns++ < 4)
            return waypoint(Intent::rally, candidate, in.rally, "contingency-visible-conflict-rally");
        epoch->retired = true; return fallback(in);
    }
    if (state.branch == Branch::holder_recall) {
        const auto* holder = in.frame.visible(state.replacement);
        if (ready(holder) && site->solo(in.frame, state.replacement)) {
            if (state.replacement == own.id)
                return work(candidate, "contingency-visible-holder-transfer");
            if (joint(in, candidate) && site->solo(in.frame, own.id) && useful(holder, candidate))
                return work(candidate, "contingency-visible-transfer-support");
        }
        return fallback(in);
    }
    if (state.branch == Branch::blocked_approach)
    {
        if (state.branch_turns++ < 8)
            return waypoint(Intent::alternate, candidate, state.alternative, "contingency-listed-alternate");
        epoch->retired = true; return fallback(in);
    }
    if (state.branch == Branch::support_unavailable) {
        const auto* solo = in.site(state.lower_group);
        if (solo && solo->known && !solo->blocked && solo->solo(in.frame, own.id))
            return work(state.lower_group, "contingency-declared-lower-solo-group");
        return fallback(in);
    }
    if (joint(in, candidate) && site->solo(in.frame, own.id))
        return work(candidate, "contingency-original-circuit");
    return fallback(in);
}
struct Policy {
    State state;
    static const organization::Profile& profile() { return profile_value; }
    organization::Decision choose(const organization::Input& in, bool control) {
        return contingencies::choose(state, in, control);
    }
};
} // namespace p6::contingencies
