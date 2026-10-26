#pragma once
#include "profile.h"

namespace p6::salvage {
using namespace organization;
struct State {
    struct Epoch { int group = -1, epoch = -1; bool repaired = false, retired = false; };
    std::array<Epoch, 8> history{};
    int group = -1, epoch = -1, holder = -1;
    bool failure_latched = false;
    struct SelfFailure {
        ProjectID project{};
        Failure reason = Failure::none;
        bool operator==(const SelfFailure&) const = default;
    } pending_self{}, handled_self{};
    bool receive_failure(ProjectID project, Failure reason) {
        if (project.group < 0 || project.epoch < 0 ||
            (reason != Failure::observed_unavailable && reason != Failure::observed_failed_action)) return false;
        const SelfFailure notice{project, reason};
        if (notice == handled_self || notice == pending_self) return true;
        if (pending_self.reason != Failure::none) return false;
        pending_self = notice;
        return true;
    }
    Epoch* record(int candidate, int current_epoch) {
        for (auto& e : history) if (e.group == candidate && e.epoch == current_epoch) return &e;
        for (auto& e : history) if (e.group < 0 || e.epoch < current_epoch) {
            e = {candidate, current_epoch, false, false}; return &e;
        }
        return nullptr;
    }
};
inline Decision secure(const Input& in) {
    if (in.frame.own.actor.bag > 0)
        return {Intent::bank, in.current_group, -1, {}, false, "salvage-secure-real-inventory"};
    return independent(in);
}
inline Decision choose(State& state, const Input& in, bool control) {
    const auto& own = in.frame.own.actor;
    if (!in.frame.valid() || !in.safe_now ||
        (own.channel != Channel::none && own.channel_valid)) return independent(in);
    if (in.actual_payout) return secure(in);
    if (state.pending_self.reason != Failure::none) {
        // The native executor has already retired this actor's private cycle.
        // It is not evidence that another proposed source or crew failed, and
        // cannot reopen the retired epoch. Reevaluate independent work normally.
        const auto notice = state.pending_self;
        if (auto* failed = state.record(notice.project.group, notice.project.epoch)) failed->retired = true;
        state.handled_self = notice; state.pending_self = {};
        auto decision = secure(in); decision.reason = "salvage-retired-self-project";
        return decision;
    }
    const int candidate = in.current_group >= 0 ? in.current_group : in.proposed_group;
    if (candidate < 0) return independent(in);
    if (state.group != candidate || in.actual_bank || in.restored) {
        state.group = candidate; state.epoch = in.frame.clock.round / 64;
        state.holder = -1; state.failure_latched = false;
    }
    auto* epoch = state.record(candidate, state.epoch);
    if (!epoch || epoch->retired) return secure(in);
    const auto* site = in.site(candidate);
    const bool fresh_failure = in.observed_failure && !state.failure_latched;
    state.failure_latched = in.observed_failure;
    if (fresh_failure) {
        if (control || epoch->repaired) { epoch->retired = true; return secure(in); }
        epoch->repaired = true; // The single repair uses current HP/readiness only.
        if (!site || !site->known || site->blocked || !site->solo(in.frame, own.id) ||
            (!site->damaged_now && site->group.lifecycle != Lifecycle::reward_available)) {
            epoch->retired = true; return secure(in);
        }
        int replacement = -1;
        for (int i = 0; i < site->actor_count; ++i) {
            const int id = site->actors[i]; const auto* a = in.frame.visible(id);
            if (ready(a) && site->solo(in.frame, id) && (replacement < 0 || id < replacement))
                replacement = id;
        }
        if (replacement < 0) { epoch->retired = true; return secure(in); }
        state.holder = replacement;
    }
    if (!site || !site->known || site->blocked) return secure(in);
    if (epoch->repaired) {
        const auto* holder = in.frame.visible(state.holder);
        if (!ready(holder) || !site->solo(in.frame, state.holder)) return secure(in);
        if (state.holder == own.id && site->solo(in.frame, own.id))
            return work(candidate, "salvage-reduced-crew-holder");
        if (site->group.lifecycle == Lifecycle::reward_available &&
            holder->spawn_group == candidate)
            return secure(in); // Safely release extra reward occupants.
        if (joint(in, candidate) && site->solo(in.frame, own.id) && useful(holder, candidate))
            return work(candidate, "salvage-visible-replacement-helper");
        return secure(in);
    }
    if (joint(in, candidate) && site->solo(in.frame, own.id))
        return work(candidate, "salvage-original-visible-project");
    return independent(in);
}
struct Policy {
    State state;
    static const organization::Profile& profile() { return profile_value; }
    bool receive_failure(ProjectID project, Failure failure) { return state.receive_failure(project, failure); }
    organization::Decision choose(const organization::Input& in, bool control) {
        return salvage::choose(state, in, control);
    }
};
} // namespace p6::salvage
