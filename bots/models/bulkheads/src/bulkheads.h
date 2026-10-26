#pragma once
#include "../../common/pure.h"

namespace plan4::bulkheads {
inline constexpr int max_causes = 8, fit_episodes = 8, residual_support = 4;
inline constexpr const char* version = "plan4-bulkheads/1";
enum class CauseClass { visible_unit_connector_block, hostile_altar_lock, own_recovery };
enum class Censor { none, unknown_onset, nonexogenous_policy, sight_lost, action_conditioned, bad_evidence };
struct PolicyReceipt {
    std::uint64_t fingerprint = 0;
    bool frozen = false, release_independent_of_own_actions = false;
};
struct Onset {
    int episode_id = -1, connector_id = -1, blocker_id = -1, round = -1;
    CauseClass cause_class = CauseClass::visible_unit_connector_block;
    Provenance current{}, preceding{};
    bool preceding_clear = false;
    PolicyReceipt policy{};
};
struct Observation {
    int episode_id = -1, round = -1;
    bool personally_visible = false, blocked = false, action_conditioned = false;
    Provenance provenance{};
    int connector_id = -1, blocker_id = -1;
};
struct Episode {
    int id = -1, start = -1, last = -1, duration = -1;
    std::uint64_t policy = 0;
    Censor censor = Censor::none;
    bool complete = false;
};
struct Cause {
    int episode_id = -1, connector_id = -1, blocker_id = -1, start = -1, last = -1;
    std::uint64_t policy = 0;
    bool onset_known = false, resolved = false, unknown = true;
};
struct Fit {
    std::array<Episode, fit_episodes> episodes{};
    std::array<int, fit_episodes> durations{};
    int started = 0;
    bool frozen = false, supported = false;
    std::uint64_t policy = 0;
};
struct State { Fit fit{}; std::array<Cause, max_causes> causes{}; };
struct Release {
    int episode_id = -1, age = -1, examples_beyond_age = 0, residual_rounds = 0, predicted_release = -1;
    double mean_residual = 0;
    bool supported = false, resolved = false, unknown = true;
    Status status = Status::unknown;
};
inline bool exact(const Provenance& provenance, int round) {
    return provenance.source == Source::personal_vision && provenance.status == Status::exact_now &&
        provenance.observed_round == round;
}
inline int episode_index(const Fit& fit, int id) {
    for (int n = 0; n < fit.started; ++n) if (fit.episodes[n].id == id) return n;
    return -1;
}
inline int cause_index(const State& state, int id) {
    for (int n = 0; n < max_causes; ++n) if (state.causes[n].episode_id == id) return n;
    return -1;
}
inline void censor(State& state, int episode, Censor reason) {
    const int n = episode_index(state.fit, episode);
    if (n >= 0 && !state.fit.frozen) {
        auto& row = state.fit.episodes[n];
        if (row.censor == Censor::none) row.censor = reason;
    }
}
// Each detected episode occupies its original cohort slot, including unknown
// onset, endogenous policy or later censored evidence. No survivor selection.
// Repeated calls with the same ID cannot restart its onset or add a fit row.
inline bool observe_onset(State& state, const Onset& onset) {
    if (onset.cause_class != CauseClass::visible_unit_connector_block || onset.episode_id < 0 ||
        onset.connector_id < 0 || onset.blocker_id < 0 || onset.round < 0) return false;
    if (cause_index(state, onset.episode_id) >= 0 || episode_index(state.fit, onset.episode_id) >= 0) return false;
    for (const auto& existing : state.causes)
        if (existing.episode_id >= 0 && !existing.resolved && !existing.unknown &&
            existing.connector_id == onset.connector_id && existing.blocker_id == onset.blocker_id)
            return false; // New IDs cannot duplicate one continuing physical cause.
    int slot = -1;
    for (int c = 0; c < max_causes; ++c) if (state.causes[c].episode_id < 0) { slot = c; break; }
    if (slot < 0) {
        for (int c = 0; c < max_causes; ++c)
            if (state.causes[c].resolved || state.causes[c].unknown)
                if (slot < 0 || state.causes[c].last < state.causes[slot].last) slot = c;
    }
    if (slot < 0) return false; // Eight unresolved personally witnessed causes.
    const bool current = exact(onset.current, onset.round);
    const bool known = current && onset.preceding_clear && onset.round > 0 && exact(onset.preceding, onset.round - 1);
    Censor reason = !known ? Censor::unknown_onset : Censor::none;
    if (!onset.policy.fingerprint || !onset.policy.frozen || !onset.policy.release_independent_of_own_actions)
        reason = Censor::nonexogenous_policy;
    if (!state.fit.frozen && state.fit.started < fit_episodes) {
        auto& row = state.fit.episodes[state.fit.started++];
        row = Episode{}; row.id = onset.episode_id; row.start = row.last = onset.round;
        row.policy = onset.policy.fingerprint; row.censor = reason;
    }
    auto& cause = state.causes[slot]; cause = Cause{};
    cause.episode_id = onset.episode_id; cause.connector_id = onset.connector_id; cause.blocker_id = onset.blocker_id;
    cause.start = cause.last = onset.round; cause.onset_known = known;
    cause.policy = onset.policy.fingerprint;
    cause.unknown = !known || reason == Censor::nonexogenous_policy;
    return current;
}
inline void observe(State& state, const Observation& observation) {
    const int c = cause_index(state, observation.episode_id);
    if (c < 0) return;
    auto& cause = state.causes[c];
    if (observation.round < cause.last || cause.resolved) return;
    if (observation.action_conditioned) {
        censor(state, observation.episode_id, Censor::action_conditioned);
        cause.unknown = true; cause.onset_known = false;
    }
    if (observation.connector_id != cause.connector_id || observation.blocker_id != cause.blocker_id) {
        censor(state, observation.episode_id, Censor::bad_evidence);
        cause.unknown = true; cause.onset_known = false;
        return; // A different actor's occupancy cannot extend this episode.
    }
    const bool legal = observation.personally_visible && exact(observation.provenance, observation.round);
    if (!legal || observation.round - cause.last > 1) {
        censor(state, observation.episode_id, Censor::sight_lost);
        cause.unknown = true; cause.onset_known = false;
    }
    if (observation.round == cause.last) {
        // Identical persistence cannot add age; contradictory same-frame release
        // is not a continuously observed positive-duration completion.
        if (!observation.blocked) {
            censor(state, observation.episode_id, Censor::bad_evidence);
            cause.unknown = true; cause.onset_known = false;
        }
        return;
    }
    cause.last = observation.round;
    const int n = episode_index(state.fit, observation.episode_id);
    if (n >= 0 && !state.fit.frozen) state.fit.episodes[n].last = observation.round;
    if (!legal) return;
    if (!observation.blocked) {
        cause.resolved = true; cause.unknown = false;
        if (n >= 0 && !state.fit.frozen) {
            auto& row = state.fit.episodes[n];
            if (row.censor == Censor::none && cause.onset_known) {
                row.duration = observation.round - row.start;
                row.complete = row.duration > 0;
                if (!row.complete) row.censor = Censor::bad_evidence;
            }
        }
    }
}
// Call every legal actor frame, including frames with no cause row. Omission
// censors continuous observation; absence is never a release receipt.
inline void finish_frame(State& state, int round) {
    for (auto& cause : state.causes) if (cause.episode_id >= 0 && !cause.resolved && cause.last < round) {
        censor(state, cause.episode_id, Censor::sight_lost);
        cause.unknown = true; cause.onset_known = false;
    }
}
inline bool freeze_fit(State& state) {
    auto& fit = state.fit;
    if (fit.frozen) return fit.supported;
    if (fit.started != fit_episodes) return false;
    fit.frozen = true; fit.supported = true; fit.policy = fit.episodes[0].policy;
    for (int n = 0; n < fit_episodes; ++n) {
        const auto& row = fit.episodes[n];
        fit.durations[n] = row.duration;
        fit.supported &= row.complete && row.censor == Censor::none && row.duration > 0 &&
            fit.policy && row.policy == fit.policy;
    }
    return fit.supported;
}
inline Release release_quote(const State& state, int episode_id, int decision_round) {
    Release result; result.episode_id = episode_id;
    const int c = cause_index(state, episode_id);
    if (c < 0) return result;
    const auto& cause = state.causes[c];
    if (cause.resolved && cause.last <= decision_round) {
        result.supported = result.resolved = true; result.unknown = false;
        result.predicted_release = cause.last;
        result.status = cause.last == decision_round ? Status::exact_now : Status::derived;
        // Historical completion of this episode is permanent. This does not
        // assert that the connector is currently clear of a different episode.
        return result;
    }
    if (!state.fit.frozen || !state.fit.supported || cause.resolved || cause.unknown || !cause.onset_known ||
        cause.policy != state.fit.policy || cause.last != decision_round || decision_round < cause.start) return result;
    result.age = decision_round - cause.start;
    std::int64_t sum = 0;
    for (int duration : state.fit.durations) if (duration > result.age) {
        sum += duration - result.age; ++result.examples_beyond_age;
    }
    if (result.examples_beyond_age < residual_support) return result;
    result.mean_residual = double(sum) / result.examples_beyond_age;
    const auto rounded = (sum + result.examples_beyond_age - 1) / result.examples_beyond_age;
    if (rounded > std::numeric_limits<int>::max() - decision_round) return result;
    result.residual_rounds = int(rounded); result.predicted_release = decision_round + int(rounded);
    result.supported = true; result.unknown = false; result.status = Status::predicted;
    return result;
}
struct Path {
    std::array<Release, max_causes> causes{};
    int count = 0, last_completion = -1;
    bool common_cause_enabled = true;
};
inline void prepare_path(const State& state, int round, bool enabled, Path& path) {
    path.count = 0; path.last_completion = round; path.common_cause_enabled = enabled;
    for (const auto& cause : state.causes) if (cause.episode_id >= 0)
        path.causes[path.count++] = release_quote(state, cause.episode_id, round);
}
struct Delay { int wait = 0, completion = -1; bool supported = false, unknown = true; Status status = Status::unknown; };
// A phase may depend on several causes simultaneously: use their maximum,
// never sum parallel release waits. Caller advances the common absolute clock
// before its ordinary physical phase; no extra camp exposure is fabricated.
inline Delay phase_wait(Path& path, const int* episode_ids, int count, int phase_arrival) {
    Delay result; result.completion = phase_arrival;
    if (count < 0 || count > max_causes || (count && !episode_ids) || phase_arrival < 0 || phase_arrival < path.last_completion) return result;
    int wait = 0;
    for (int n = 0; n < count; ++n) {
        const Release* quote = nullptr;
        for (int c = 0; c < path.count; ++c) if (path.causes[c].episode_id == episode_ids[n]) quote = &path.causes[c];
        if (!quote || !quote->supported) return result; // M0 fallback, tagged.
        const int delay = quote->resolved ? 0 : path.common_cause_enabled
            ? std::max(0, quote->predicted_release - phase_arrival) : quote->residual_rounds;
        wait = std::max(wait, delay);
    }
    if (wait > std::numeric_limits<int>::max() - phase_arrival) return result;
    result.wait = wait; result.completion = phase_arrival + wait;
    result.supported = true; result.unknown = false; result.status = Status::predicted;
    path.last_completion = result.completion;
    return result;
}
// Runtime integration still needs a phase-entry extra-wait hook and a public
// connector dependency map. Existing phase_duration would wrongly turn a wait
// into camp-action exposure. No Forecast wrapper claims that integration here.
// Rebind each connector dependency to its CURRENT witnessed episode at every
// decision. A historical resolved episode says nothing about a new occupant.
} // namespace plan4::bulkheads
