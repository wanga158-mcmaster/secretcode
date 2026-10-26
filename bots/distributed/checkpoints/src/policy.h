#pragma once
#include "profile.h"

namespace p6::checkpoints {
using namespace organization;
enum class Milestone { approach, clearance, holder_entry, bank_approach, restored_capacity };
inline Milestone stage(Phase phase) {
    if (phase == Phase::clear || phase == Phase::assemble) return Milestone::clearance;
    if (phase == Phase::hold || phase == Phase::allocate) return Milestone::holder_entry;
    if (phase == Phase::bank) return Milestone::bank_approach;
    if (phase == Phase::recover) return Milestone::restored_capacity;
    return Milestone::approach;
}
struct State {
    struct Epoch { int group = -1, epoch = -1; bool retired = false; };
    std::array<Epoch, 8> history{};
    int group = -1, epoch = -1, waits = 0, renewals = 0;
    Milestone signed_stage = Milestone::approach;
    bool signed_up = false;
    Epoch* record(int candidate, int current_epoch) {
        for (auto& e : history) if (e.group == candidate && e.epoch == current_epoch) return &e;
        for (auto& e : history) if (e.group < 0 || e.epoch < current_epoch) {
            e = {candidate, current_epoch, false}; return &e;
        }
        return nullptr;
    }
};
inline Decision continuation(const Input& in) {
    if (in.frame.own.actor.bag > 0)
        return {Intent::bank, in.current_group, -1, {}, false, "checkpoint-inventory-continuation"};
    if (in.phase == Phase::recover)
        return {Intent::recover, -1, -1, {}, false, "checkpoint-recovery-continuation"};
    return independent(in);
}
inline bool partners(const Input& in, const Site& site) {
    if (!joint(in, site.group.id)) return false;
    for (int ordinal = 0; ordinal < std::min(3, in.picks); ++ordinal) {
        const auto* a = in.ordinal_actor(ordinal);
        if (!ready(a) || !site.solo(in.frame, a->id)) return false;
        if (a->id != in.frame.clock.self && !useful(a, site.group.id) &&
            a->position != in.rally && a->position != site.group.approach) return false;
    }
    return true;
}
inline Decision choose(State& state, const Input& in, bool control) {
    const auto& own = in.frame.own.actor;
    if (!in.frame.valid() || in.ordinal < 0 || in.ordinal >= 3 ||
        (own.channel != Channel::none && own.channel_valid)) return independent(in);
    if (!in.safe_now) return {Intent::recover, -1, -1, {}, false, "checkpoint-safety-override"};
    const int candidate = in.current_group >= 0 ? in.current_group : in.proposed_group;
    if (candidate < 0) return continuation(in);
    if (state.group != candidate || in.restored) {
        state.group = candidate; state.epoch = in.frame.clock.round / 64;
        state.signed_up = false; state.waits = 0; state.renewals = 0;
    }
    auto* epoch = state.record(candidate, state.epoch);
    if (!epoch || epoch->retired) return continuation(in);
    const auto* site = in.site(candidate);
    const Milestone next = stage(in.phase);
    // A teammate's failed renewal never cancels our safe channel or capture.
    if (in.phase == Phase::hold && own.spawn_group == candidate && !in.actual_payout)
        return work(candidate, "checkpoint-preserve-completed-clearance");
    const bool checkpoint = !state.signed_up || next != state.signed_stage ||
        in.actual_payout || in.actual_bank || in.restored;
    if (checkpoint && (!control || !state.signed_up)) {
        if (!site || !site->known || !partners(in, *site) || in.observed_failure) {
            if (!state.signed_up && !in.observed_failure && ++state.waits <= 8)
                return waypoint(Intent::rally, candidate, in.rally, "checkpoint-initial-signing");
            epoch->retired = true;
            return continuation(in); // One failed re-signing retires this epoch.
        }
        state.signed_up = true; state.signed_stage = next; state.waits = 0;
        ++state.renewals;
    }
    if (!site || !site->solo(in.frame, own.id)) return continuation(in);
    if (in.actual_payout || in.phase == Phase::bank || in.phase == Phase::recover)
        return continuation(in);
    return work(candidate, control ? "checkpoint-control-whole-expedition" :
                                    "checkpoint-current-signed-stage");
}
struct Policy {
    State state;
    static const organization::Profile& profile() { return profile_value; }
    organization::Decision choose(const organization::Input& in, bool control) {
        return checkpoints::choose(state, in, control);
    }
};
} // namespace p6::checkpoints
