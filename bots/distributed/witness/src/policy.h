#pragma once
#include "profile.h"

namespace p6::witness {
using namespace organization;
struct State {
    struct Epoch {
        int group = -1, epoch = -1;
        bool retired = false;
        int inspections = 0, last_inspection_round = -1;
        std::int64_t travel_deadline = -1;
        int arrival_round = -1, legacy_started = -1;
        int last_verification_round = -1;
    };
    std::array<Epoch, 8> history{};
    int group = -1, epoch = -1, inspections = 0;
    int successful_core_verifications = 0;
    bool verification_active(int candidate, int round) const {
        for (const auto& e : history)
            if (e.group == candidate && e.epoch == round / 64) return !e.retired;
        return true;
    }
    Epoch* record(int candidate, int current_epoch) {
        for (auto& e : history) if (e.group == candidate && e.epoch == current_epoch) return &e;
        for (auto& e : history) if (e.group < 0 || e.epoch < current_epoch) {
            e = {candidate, current_epoch, false}; return &e;
        }
        return nullptr;
    }
};
inline bool current_basic_core(const Input& in, const Site& site) {
    if (in.picks < 3 || !site.known || !site.group.all_spawns_visible ||
        site.group.lifecycle != Lifecycle::living ||
        !in.witness_core_basic.current(in.frame.clock, site.group.id) ||
        in.witness_core_basic.value != 7) return false;
    std::array<int, 3> ids{-1, -1, -1};
    for (int ordinal = 0; ordinal < 3; ++ordinal) {
        const auto* a = in.ordinal_actor(ordinal);
        if (!ready(a) || !a->can_attack || a->pick_ordinal != ordinal) return false;
        for (int previous = 0; previous < ordinal; ++previous)
            if (ids[previous] == a->id) return false;
        ids[ordinal] = a->id;
    }
    return true;
}
inline bool current_solo_crew(const Input& in, const Site& site) {
    if (!site.known || !joint(in, site.group.id)) return false;
    for (int ordinal = 0; ordinal < std::min(3, in.picks); ++ordinal) {
        const auto* a = in.ordinal_actor(ordinal);
        if (!ready(a) || !site.solo(in.frame, a->id)) return false;
    }
    return true;
}
inline Decision choose(State& state, const Input& in, bool control) {
    const auto& own = in.frame.own.actor;
    if (!in.frame.valid() || !in.safe_now ||
        (own.channel != Channel::none && own.channel_valid)) return independent(in);
    if (in.phase == Phase::hold && in.current_group >= 0 &&
        own.spawn_group == in.current_group && !in.actual_payout)
        return work(in.current_group, "witness-preserve-capture");
    if (in.actual_payout && own.bag > 0)
        return {Intent::bank, in.current_group, -1, {}, false, "witness-actual-inventory"};
    const int target = in.proposed_group >= 0 ? in.proposed_group : in.independent_group;
    const auto* site = in.site(target);
    if (!site) return independent(in);
    if (control) return work(target, "witness-control-static-commitment");
    // A bank, restored health or a temporary source switch does not renew this
    // source's bounded verification in the same clock epoch.
    state.group = target; state.epoch = in.frame.clock.round / 64;
    auto* epoch = state.record(target, state.epoch);
    if (!epoch || epoch->retired) return independent(in);
    state.inspections = epoch->inspections;
    if (site->blocked || in.observed_failure) {
        epoch->retired = true; return independent(in);
    }
    const bool appointment = in.ordinal >= 0 && in.ordinal < 3 && in.picks >= 3 &&
                             in.verification_travel >= 0;
    const bool at_pad = in.at_rally && own.position == in.rally;
    const bool basic_core = appointment && at_pad && current_basic_core(in, *site);
    const int round = in.frame.clock.round;
    if (appointment) {
        if (epoch->travel_deadline < 0)
            epoch->travel_deadline = static_cast<std::int64_t>(round) + in.verification_travel + 8;
        if (at_pad && epoch->arrival_round < 0) epoch->arrival_round = round;
    }
    // Keep the old, fully certified speedup path distinct from a newly observed
    // basic-mask rendezvous. The latter supplies no cycle or action authority.
    if ((!appointment || basic_core) && current_solo_crew(in, *site))
        return work(target, "witness-personally-verified-crew");
    if (site->known && site->solo(in.frame, own.id))
        return work(target, "witness-personally-verified-solo-cycle");
    const int limit = in.ordinal == 0 ? 2 : 8;
    if (appointment) {
        if (round >= epoch->travel_deadline ||
            (epoch->arrival_round >= 0 && round - epoch->arrival_round >= 8)) {
            epoch->retired = true; return independent(in);
        }
        if (basic_core) {
            if (epoch->last_verification_round != round) {
                ++state.successful_core_verifications;
                epoch->last_verification_round = round;
            }
            // Missing crew, stale masks and deferred setup/evaluation are not
            // completed failed inspections. A finished own model, after an
            // actual current three-core rendezvous, may consume one check.
            if (in.verification_model_complete && epoch->last_inspection_round != round) {
                epoch->last_inspection_round = round;
                state.inspections = ++epoch->inspections;
                if (epoch->inspections >= limit) {
                    epoch->retired = true; return independent(in);
                }
            }
            return waypoint(Intent::approach, target, in.rally,
                            "witness-personally-verified-basic-crew");
        }
        return waypoint(Intent::approach, target, in.rally,
                        "witness-cheap-personal-verification");
    }
    // Extensions and unavailable appointments retain the private cheap probe.
    // Its elapsed bound is separate from completed at-pad model failures.
    if (epoch->legacy_started < 0) epoch->legacy_started = in.frame.clock.round;
    const bool at_verification = own.position == site->group.approach;
    if (at_verification && in.verification_model_complete &&
        epoch->last_inspection_round != in.frame.clock.round) {
        epoch->last_inspection_round = in.frame.clock.round;
        state.inspections = ++epoch->inspections;
    }
    if (epoch->inspections >= limit || in.frame.clock.round - epoch->legacy_started >= limit) {
        epoch->retired = true;
        return independent(in);
    }
    return waypoint(Intent::approach, target, site->group.approach,
                    "witness-cheap-personal-verification");
}
struct Policy {
    State state;
    static const organization::Profile& profile() { return profile_value; }
    organization::Decision choose(const organization::Input& in, bool control) {
        return witness::choose(state, in, control);
    }
    bool verification_active(int group, int round) const {
        return state.verification_active(group, round);
    }
};
} // namespace p6::witness
