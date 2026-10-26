#pragma once
#include "../../distributed-common/src/observe.h"
#include <array>
#include <cstdint>

// Bounded Plan 8 shared identities and budgets. Pure policy types; the native
// SDK boundary and entrypoint orchestration live in sdk.h/runtime.h separately.
namespace p8::common {
// User override: role needs and complete cycles govern drafting, rather than
// the plan's fixed five-name prefix. These eight single-body picks have pinned
// SDK representation; linked picks/Ruin need a separate supported adapter.
inline constexpr std::array<p6::Champion, 8> champion_pool{
    p6::Champion::tavi, p6::Champion::marcella, p6::Champion::varg,
    p6::Champion::vendetta, p6::Champion::vesper_vale, p6::Champion::willow,
    p6::Champion::prism, p6::Champion::ren};
inline constexpr bool allow_denial = false;
inline constexpr int max_options = 8, max_contexts = 8;
inline constexpr int qualification_rounds = 256, project_rounds = 64;
inline constexpr int max_probe_turns = 8, max_probes_match = 2;
inline constexpr int forecast_window = 120;
inline constexpr double minimum_trial_health_fraction = .35;
// Context entry bands: [75%,100%], [50%,75%), [0%,50%). Actual
// entry stats/equipment are exact, and the two starts freeze purchases.
// Episode envelopes separately represent expected damage/readiness/bag changes.
inline bool supported_champion(p6::Champion champion) {
    for (auto candidate : champion_pool) if (candidate == champion) return true;
    return false;
}
struct DraftRoster {
    std::array<p6::Champion,5> champions{p6::Champion::other,p6::Champion::other,
        p6::Champion::other,p6::Champion::other,p6::Champion::other};
    std::array<int,5> actors{-1,-1,-1,-1,-1};
    int picks = 0;
    bool valid() const {
        if (picks < 3 || picks > 5) return false;
        for (int i = 0; i < picks; ++i) {
            if (actors[i] < 0 || !supported_champion(champions[i])) return false;
            for (int j = 0; j < i; ++j)
                if (actors[i] == actors[j] || champions[i] == champions[j]) return false;
        }
        return true;
    }
};
inline bool roster_actor(const p6::Actor& actor, int picks) {
    return picks >= 3 && picks <= 5 && actor.pick_ordinal >= 0 &&
        actor.pick_ordinal < picks && supported_champion(actor.type);
}
inline bool roster_actor(const p6::Actor& actor, const DraftRoster& draft) {
    return draft.valid() && roster_actor(actor,draft.picks) && actor.team == p6::Team::ally &&
        draft.champions[actor.pick_ordinal] == actor.type &&
        draft.actors[actor.pick_ordinal] == actor.id;
}
inline bool leader_actor(const p6::Actor& actor, int picks) {
    return roster_actor(actor, picks) && actor.pick_ordinal == 0;
}
// Paid slots belong to this persistent actor object. No death/recall/epoch
// reset method exists; a newly initialized guest has a new empty object.
struct ProbeBudget {
    std::array<int, max_probes_match> spent_epochs{-1, -1};
    int used = 0;
    bool overrun = false;
    bool available(int latched_epoch) const {
        if (overrun || latched_epoch < 0 || used >= max_probes_match) return false;
        for (int i = 0; i < used; ++i)
            if (spent_epochs[i] == latched_epoch) return false;
        return true;
    }
    bool spend(int latched_epoch) {
        if (!available(latched_epoch)) return false;
        spent_epochs[used++] = latched_epoch;
        return true;
    }
    void observed_diagnostic_turns(int marked_turns) {
        if (marked_turns > max_probe_turns) overrun = true;
    }
};
} // namespace p8::common
