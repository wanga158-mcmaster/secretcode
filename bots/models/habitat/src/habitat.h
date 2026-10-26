#pragma once

#include "../../common/pure.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace plan4::habitat {
inline constexpr const char* version = "plan4-habitat/1";
inline constexpr int max_consumers = 4;

enum class ProductionEvidence { unknown, visible_cleared_spawn, predicted_participation };
struct ConsumerSample {
    int actor_id = -1, group_id = -1;
    Point position{};
    Provenance provenance{};
    bool personally_visible = false, producing = false, ambiguous = false;
    ProductionEvidence evidence = ProductionEvidence::unknown;
};
struct ObservationFrame {
    int round = -1, count = 0;
    std::array<ConsumerSample, max_consumers> consumers{};
    // Only a legal observer covering every possible producer may set complete.
    // Full patch visibility means the entire catchment, not just its camps.
    bool complete = false, truncated = false;
    std::array<bool, max_altars> full_patch_visibility{};
};
struct Track {
    int actor_id = -1;
    ConsumerSample last{}, previous{};
    bool has_last = false, has_previous = false;
};
struct State {
    std::array<int, max_groups> group_ids{}, patch{};
    std::array<unsigned char, (max_groups + 7) / 8> resolved{};
    std::array<double, max_altars> capacity{};
    std::array<Track, max_consumers> tracks{};
    int group_count = 0, altar_count = 0;
    int frame_round = -1, previous_frame_round = -1;
    bool frame_complete = false, previous_frame_complete = false;
    std::array<bool, max_altars> full_visibility{}, previous_full_visibility{};
    std::uint64_t signature = 0, combat_version = 0;
    unsigned capacity_revision = 0;
    bool initialized = false, supported = false, unknown_geometry = true;
};
struct Path {
    std::array<unsigned char, (max_groups + 7) / 8> consumed{};
    bool coupling_enabled = true;
    bool unknown_pressure = false, unsupported_capacity = false;
    int supply_queries = 0, charged_queries = 0;
};
struct Pressure { int observed_consumers = 0; bool unknown = true; };

inline bool bit(const std::array<unsigned char, (max_groups + 7) / 8>& bits, int g) {
    return (bits[g / 8] & (1u << (g % 8))) != 0;
}
inline void set_bit(std::array<unsigned char, (max_groups + 7) / 8>& bits, int g) {
    bits[g / 8] |= static_cast<unsigned char>(1u << (g % 8));
}
inline std::uint64_t mix(std::uint64_t hash, int value) {
    return (hash ^ std::uint32_t(value)) * 1099511628211ULL;
}

// Public metadata only. Unknown cached routes do not establish a catchment.
// Unresolved rows are retried later; no partial capacity is presented as exact.
inline bool prepare_state(const Information& info, State& state, std::uint64_t combat_version = 0) {
    if (info.public_layout_truncated || info.group_count < 0 || info.group_count > max_groups ||
        info.altar_count < 0 || info.altar_count > max_altars) {
        state.supported = false; state.unknown_geometry = true; return false;
    }
    std::uint64_t signature = mix(1469598103934665603ULL, info.settings.capture_rounds);
    signature = mix(mix(signature, info.group_count), info.altar_count);
    for (int g = 0; g < info.group_count; ++g) {
        if (!can_refine(info)) { state.supported = false; return false; }
        const auto& row = info.groups[g];
        signature = mix(mix(mix(signature, row.id), row.position.x), row.position.y);
        signature = mix(mix(mix(signature, int(row.gems)), row.full_clear_rounds), row.respawn_rounds);
    }
    for (int a = 0; a < info.altar_count; ++a) {
        const auto& row = info.altars[a];
        signature = mix(mix(mix(signature, row.id), row.position.x), row.position.y);
        signature = mix(mix(mix(signature, row.altar_position.x), row.altar_position.y), row.accessible ? 1 : 0);
    }
    if (!state.initialized || signature != state.signature || combat_version != state.combat_version) {
        state.initialized = true; state.supported = false;
        state.signature = signature; state.combat_version = combat_version;
        state.group_count = info.group_count; state.altar_count = info.altar_count;
        state.resolved.fill(0); state.capacity.fill(0);
        ++state.capacity_revision;
        for (int g = 0; g < info.group_count; ++g) {
            state.group_ids[g] = info.groups[g].id; state.patch[g] = -1;
        }
    }
    if (state.supported) return true;
    bool complete = true;
    for (int g = 0; g < info.group_count; ++g) {
        if (bit(state.resolved, g)) continue;
        if (!can_refine(info)) { complete = false; break; }
        int nearest = -1, best = std::numeric_limits<int>::max();
        bool unknown = false;
        for (int a = 0; a < info.altar_count; ++a) {
            if (!info.altars[a].accessible) continue;
            const int d = distance(info, info.groups[g].position, info.altars[a].position);
            if (d < 0) { unknown = true; continue; }
            if (d < best || (d == best && (nearest < 0 ||
                point_less(info.altars[a].altar_position, info.altars[nearest].altar_position)))) {
                nearest = a; best = d;
            }
        }
        if (unknown || info.groups[g].full_clear_rounds < 0) { complete = false; continue; }
        state.patch[g] = nearest;
        set_bit(state.resolved, g);
        if (nearest >= 0) {
            // The capture service includes initiation and queried countdown.
            const int service = info.groups[g].full_clear_rounds +
                std::max(0, info.settings.capture_rounds) + 1 + std::max(0, info.groups[g].respawn_rounds);
            if (service > 0) state.capacity[nearest] += double(info.groups[g].gems) / service;
        }
    }
    for (int g = 0; g < info.group_count; ++g) complete &= bit(state.resolved, g);
    state.supported = complete; state.unknown_geometry = !complete;
    return complete;
}
inline int group_index(const State& state, int id) {
    for (int g = 0; g < state.group_count; ++g) if (state.group_ids[g] == id) return g;
    return -1;
}
inline bool legal_sample(const ConsumerSample& sample, int round, int own_id) {
    return sample.actor_id >= 0 && sample.actor_id != own_id && sample.personally_visible &&
        sample.provenance.observed_round == round && sample.provenance.source == Source::personal_vision &&
        sample.provenance.status == Status::exact_now;
}
// ObservationContext needs only round and own.id. This also accepts the small
// per-turn legal clock without copying or relabeling a full stale snapshot.
template<class ObservationContext>
inline void observe(State& state, const ObservationContext& info, const ObservationFrame& frame) {
    if (frame.round != info.round || frame.round < state.frame_round) return;
    if (frame.round != state.frame_round) {
        state.previous_frame_round = state.frame_round;
        state.previous_frame_complete = state.frame_complete;
        state.previous_full_visibility = state.full_visibility;
    }
    state.frame_round = frame.round;
    state.frame_complete = frame.complete && !frame.truncated && frame.count >= 0 && frame.count <= max_consumers;
    state.full_visibility = frame.full_patch_visibility;
    for (int n = 0; n < std::min(frame.count, max_consumers); ++n) {
        const auto& sample = frame.consumers[n];
        if (!legal_sample(sample, frame.round, info.own.id)) continue;
        bool duplicate = false;
        for (int p = 0; p < n; ++p) duplicate |= frame.consumers[p].actor_id == sample.actor_id;
        if (duplicate) continue;
        int slot = -1;
        for (int t = 0; t < max_consumers; ++t) if (state.tracks[t].actor_id == sample.actor_id) slot = t;
        if (slot < 0) {
            for (int t = 0; t < max_consumers; ++t) if (!state.tracks[t].has_last) { slot = t; break; }
        }
        if (slot < 0) {
            slot = 0;
            for (int t = 1; t < max_consumers; ++t)
                if (state.tracks[t].last.provenance.observed_round < state.tracks[slot].last.provenance.observed_round) slot = t;
            state.tracks[slot] = Track{};
        }
        auto& track = state.tracks[slot];
        if (track.has_last && track.last.provenance.observed_round < frame.round) {
            track.previous = track.last; track.has_previous = true;
        }
        track.actor_id = sample.actor_id; track.last = sample; track.has_last = true;
    }
}
inline bool engaged(const ConsumerSample& sample) {
    return sample.producing && !sample.ambiguous && sample.evidence != ProductionEvidence::unknown;
}
inline Pressure pressure(const State& state, int patch, int round) {
    Pressure result;
    if (patch < 0 || patch >= state.altar_count) return result;
    result.unknown = !(state.frame_round == round && state.previous_frame_round >= round - 2 &&
        state.frame_complete && state.previous_frame_complete && state.full_visibility[patch] &&
        state.previous_full_visibility[patch]);
    const bool current_full_sight = state.frame_round == round && state.frame_complete && state.full_visibility[patch];
    for (const auto& track : state.tracks) {
        if (!track.has_last || !track.has_previous) continue;
        const int last_round = track.last.provenance.observed_round, previous_round = track.previous.provenance.observed_round;
        // A complete current view can positively establish that an earlier
        // producer is absent; occlusion alone never supplies that evidence.
        if (current_full_sight && last_round < round) continue;
        if (last_round > round || previous_round > round || last_round < round - 2 ||
            previous_round < round - 2 || previous_round >= last_round ||
            !engaged(track.last) || !engaged(track.previous)) continue;
        const int g = group_index(state, track.last.group_id), old_g = group_index(state, track.previous.group_id);
        if (g >= 0 && old_g >= 0 && state.patch[g] == patch && state.patch[old_g] == patch) ++result.observed_consumers;
    }
    return result;
}
inline int supply_wait(int group, int, int ordinary_service, int, bool initially_ready,
                       const Information& info, const void* model, void* scratch) {
    const auto& state = *static_cast<const State*>(model);
    auto& path = *static_cast<Path*>(scratch);
    ++path.supply_queries;
    if (group < 0 || group >= state.group_count || !state.supported || state.patch[group] < 0) {
        path.unsupported_capacity = true; return 0;
    }
    const int patch = state.patch[group];
    const auto consumers = pressure(state, patch, info.round);
    path.unknown_pressure |= consumers.unknown;
    if (initially_ready && !bit(path.consumed, group)) return 0;
    const double capacity = state.capacity[patch];
    if (capacity <= 0) return 0; // Card's explicit C=0 M0 fallback.
    const int count = path.coupling_enabled ? consumers.observed_consumers : 0;
    const double wait = std::max(0.0, double(info.groups[group].gems) * (1 + count) / capacity - std::max(0, ordinary_service));
    if (wait <= 0) return 0;
    ++path.charged_queries;
    return int(std::min(double(max_horizon + 1), std::ceil(wait)));
}
inline void captured(int group, int, const Information&, const void*, void* scratch) {
    if (group >= 0 && group < max_groups) set_bit(static_cast<Path*>(scratch)->consumed, group);
}
inline ForecastHooks make_hooks(const State& state, Path& path, bool coupling_enabled) {
    path.coupling_enabled = coupling_enabled;
    ForecastHooks result{}; result.model = &state; result.path = &path;
    result.supply_wait = supply_wait; result.captured = captured;
    return result;
}
inline void predict_into(const Information& info, const Job& job, const Menu& menu,
                         const State& state, bool coupling_enabled, Forecast& out, Path* diagnostics = nullptr) {
    Path path;
    const auto hooks = make_hooks(state, path, coupling_enabled);
    plan4::predict_into(info, job, menu, out, hooks);
    if (diagnostics) *diagnostics = path;
}
} // namespace plan4::habitat
