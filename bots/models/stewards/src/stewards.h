#pragma once

#include "../../common/pure.h"

namespace plan4::stewards {
inline constexpr int max_actors = 5;
inline constexpr int abandonment_rounds = 8;
inline constexpr const char* version = "plan4-stewards/1";

enum class Service { none, owner_presence, ally_cleared_spawn, predicted_participation, ambiguous };
struct GroupObservation {
    int group_id = -1;
    Lifecycle lifecycle = Lifecycle::unknown;
    Provenance lifecycle_provenance{}, visibility_provenance{}, service_provenance{};
    bool all_member_spawns_visible = false, service_observation_complete = false;
    Service service = Service::ambiguous;
    int servicing_actor = -1;
    // A participation inference is a prediction from personal health evidence,
    // never an SDK attack witness or an inference from the owner's hidden state.
    bool personally_observed_health_reduction = false, participation_ambiguous = true;
    bool own_capture_witnessed = false;
    Provenance capture_provenance{};
};
struct ObservationFrame {
    int round = -1, count = 0;
    const GroupObservation* groups = nullptr;
};
struct Record {
    int group_id = -1, last_round = -1, clock = 0;
    Lifecycle lifecycle = Lifecycle::unknown;
    bool full_sight = false, protected_capture = false, duplicate = false;
};
struct State {
    std::array<Record, max_groups> records{};
    std::array<int, max_actors> actors{};
    int group_count = 0, actor_count = 0, own_id = -1, own_slot = -1, round = -1;
    bool supported = false;
};
struct Availability {
    int primary_id = -1, backup_id = -1, predicted_owner_id = -1, full_sight_clock = 0;
    bool supported = false, backup_eligible = false, protected_capture = false;
    bool own_available = true, predicted_consumed = false;
    Status status = Status::unknown;
};
inline bool personal(const Provenance& p, int round, Status status = Status::exact_now) {
    return p.observed_round == round && p.status == status &&
        (p.source == Source::personal_vision || p.source == Source::own_occupancy);
}
inline int find(const State& state, int group_id) {
    int first = 0, last = state.group_count;
    while (first < last) {
        const int mid = first + (last - first) / 2;
        if (state.records[mid].group_id < group_id) first = mid + 1; else last = mid;
    }
    return first < state.group_count && state.records[first].group_id == group_id ? first : -1;
}
// IDs are actual completed-draft actor IDs and public physical group IDs.
// Reordering either input must not change the canonical ordinal assignment.
inline bool configure(State& state, const int* groups, int group_count,
                      const int* actors, int actor_count, int own_id, bool complete_draft_receipt) {
    const bool previously_supported = state.supported;
    state.supported = false;
    if (!complete_draft_receipt || !groups || !actors || group_count < 0 || group_count > max_groups ||
        actor_count < 1 || actor_count > max_actors) return false;
    std::array<int, max_groups> ordered;
    std::copy_n(groups, group_count, ordered.begin());
    std::sort(ordered.begin(), ordered.begin() + group_count);
    std::array<int, max_actors> roster;
    std::copy_n(actors, actor_count, roster.begin());
    std::sort(roster.begin(), roster.begin() + actor_count);
    for (int g = 0; g < group_count; ++g)
        if (ordered[g] < 0 || (g && ordered[g] == ordered[g - 1])) return false;
    int own_slot = -1;
    for (int a = 0; a < actor_count; ++a) {
        if (roster[a] < 0 || (a && roster[a] == roster[a - 1])) return false;
        if (roster[a] == own_id) own_slot = a;
    }
    if (own_slot < 0) return false;
    bool same = previously_supported && state.group_count == group_count && state.actor_count == actor_count && state.own_id == own_id;
    for (int g = 0; same && g < group_count; ++g) same &= state.records[g].group_id == ordered[g];
    for (int a = 0; same && a < actor_count; ++a) same &= state.actors[a] == roster[a];
    if (!same) {
        for (int g = 0; g < group_count; ++g) { state.records[g] = Record{}; state.records[g].group_id = ordered[g]; }
        state.round = -1;
    }
    std::copy_n(roster.begin(), actor_count, state.actors.begin());
    state.group_count = group_count; state.actor_count = actor_count;
    state.own_id = own_id; state.own_slot = own_slot; state.supported = true;
    return true;
}
inline bool roster_actor(const State& state, int actor) {
    for (int a = 0; a < state.actor_count; ++a) if (state.actors[a] == actor) return true;
    return false;
}
inline void observe(State& state, const ObservationFrame& frame) {
    if (!state.supported || frame.round < state.round || frame.round < 0) return;
    // Repeated same-round calls cannot extend an absence clock. A second row
    // for a group is conservatively invalidated, rather than cherry-picked.
    if (frame.count < 0 || frame.count > max_groups || (frame.count && !frame.groups)) {
        for (int g = 0; g < state.group_count; ++g) {
            state.records[g].clock = 0;
            state.records[g].full_sight = state.records[g].protected_capture = false;
        }
        state.round = frame.round; return;
    }
    const bool next_frame = frame.round != state.round;
    state.round = frame.round;
    for (int n = 0; n < frame.count; ++n) {
        const auto& observation = frame.groups[n];
        const int g = find(state, observation.group_id);
        if (g < 0) continue;
        auto& record = state.records[g];
        if (record.last_round == frame.round) {
            record.clock = 0; record.full_sight = record.protected_capture = false; record.duplicate = true;
            continue;
        }
        const bool consecutive = record.last_round == frame.round - 1;
        const auto previous = record.lifecycle;
        record.last_round = frame.round; record.duplicate = false;
        record.protected_capture = observation.own_capture_witnessed && observation.lifecycle == Lifecycle::cleared &&
            personal(observation.lifecycle_provenance, frame.round) &&
            personal(observation.capture_provenance, frame.round) &&
            observation.capture_provenance.source == Source::own_occupancy;
        record.full_sight = observation.all_member_spawns_visible &&
            personal(observation.visibility_provenance, frame.round) &&
            personal(observation.lifecycle_provenance, frame.round);
        record.lifecycle = personal(observation.lifecycle_provenance, frame.round)
            ? observation.lifecycle : Lifecycle::unknown;
        const int primary = state.actors[g % state.actor_count];
        bool observed_service = false, known_absence = false;
        if (observation.service == Service::none) {
            known_absence = observation.service_observation_complete && personal(observation.service_provenance, frame.round);
        } else if (observation.service == Service::owner_presence) {
            observed_service = observation.servicing_actor == primary && personal(observation.service_provenance, frame.round);
        } else if (observation.service == Service::ally_cleared_spawn) {
            observed_service = record.lifecycle == Lifecycle::cleared && roster_actor(state, observation.servicing_actor) &&
                personal(observation.service_provenance, frame.round);
        } else if (observation.service == Service::predicted_participation) {
            observed_service = roster_actor(state, observation.servicing_actor) &&
                observation.personally_observed_health_reduction && !observation.participation_ambiguous &&
                personal(observation.service_provenance, frame.round, Status::predicted);
        }
        const bool renewed = (previous == Lifecycle::captured || previous == Lifecycle::respawning) &&
            record.lifecycle == Lifecycle::living;
        const bool eligible_lifecycle = record.lifecycle == Lifecycle::living || record.lifecycle == Lifecycle::cleared;
        if (!record.full_sight || !eligible_lifecycle || renewed || observed_service || !known_absence)
            record.clock = 0;
        else record.clock = std::min(abandonment_rounds, consecutive ? record.clock + 1 : 1);
    }
    if (next_frame) for (int g = 0; g < state.group_count; ++g)
        if (state.records[g].last_round != frame.round) {
            state.records[g].clock = 0; state.records[g].full_sight = state.records[g].protected_capture = false;
        }
}
inline Availability availability(const State& state, int group_id, int decision_round, bool takeover_enabled) {
    Availability result;
    const int g = find(state, group_id);
    if (!state.supported || g < 0) return result; // Explicit M0 fallback.
    const auto& row = state.records[g];
    result.supported = true; result.status = Status::predicted;
    result.primary_id = state.actors[g % state.actor_count];
    result.backup_id = state.actors[(g + 1) % state.actor_count];
    result.full_sight_clock = row.last_round == decision_round ? row.clock : 0;
    result.backup_eligible = row.last_round == decision_round && row.full_sight && !row.duplicate && row.clock >= abandonment_rounds;
    result.protected_capture = row.last_round == decision_round && row.protected_capture;
    result.predicted_owner_id = takeover_enabled && result.backup_eligible ? result.backup_id : result.primary_id;
    result.own_available = result.protected_capture || state.own_id == result.predicted_owner_id;
    result.predicted_consumed = !result.own_available;
    // This is the static responsibility surrogate, not a claim of hidden
    // owner intent, death, location or SDK illegality. Menu rows are preserved.
    return result;
}
} // namespace plan4::stewards
