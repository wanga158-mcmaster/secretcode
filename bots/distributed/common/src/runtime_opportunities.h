#pragma once
#include <array>
#include <cstdint>
#include <span>

namespace p6::runtime_opportunities {
inline constexpr int capacity = 8;
inline constexpr int cooldown_rounds = 8;
enum class Kind { none, deferred, inspection, quote };
struct Candidate {
    int group = -1;
    Kind kind = Kind::none;
    bool eligible = false;
    bool protected_incumbent = false;
};

// Defer production quoting until the exact public approach, allowing the last
// safe inspection step to retain its action budget. A current legal basic or
// occupied original spawn already supplies an immediate work location. This
// predicate grants no movement, attack, visibility or lifecycle permission;
// unknown and respawning sites keep their ordinary caller classifications.
inline bool defer_production_approach(bool known, bool respawning,
    std::int64_t distance_to_approach, int group, int ready_attack_group, int spawn_group) {
    return known && !respawning && distance_to_approach > 0 &&
           ready_attack_group != group && spawn_group != group;
}

// Selection memory only: no ownership, failed-action receipt, camp lifecycle or
// physical blockage is inferred. Indices belong to the caller's fixed site list.
class History {
    struct Entry {
        int group = -1, inspections = 0, last_inspection_round = -1;
        std::int64_t retry_round = -1;
        bool operator==(const Entry&) const = default;
    };
    std::array<Entry, capacity> entries_{};
    static bool valid(int index, int group, int round) {
        return index >= 0 && index < capacity && group >= 0 && round >= 0;
    }
    Entry& entry(int index, int group) {
        auto& value = entries_[index];
        if (value.group != group) { value = {}; value.group = group; }
        return value;
    }
    static void cool(Entry& value, int round) {
        value.retry_round = static_cast<std::int64_t>(round) + cooldown_rounds;
        value.inspections = 0;
        value.last_inspection_round = round;
    }
public:
    bool operator==(const History&) const = default;
    bool available(int index, int group, int round) const {
        if (!valid(index, group, round)) return false;
        const auto& value = entries_[index];
        return value.group != group || round >= value.retry_round;
    }
    // The caller must attest a completed current SELF model rejection. Missing
    // models, foreign results, fuel/setup gates and cold preparation never call
    // this method. Repeated calls during cooldown cannot extend that cooldown.
    bool reject(int index, int group, int round) {
        if (!available(index, group, round)) return false;
        cool(entry(index, group), round);
        return true;
    }
    // A completed current observation counts once per round, at the exact pad.
    // Budget/setup deferral is a pure no-op, including any existing inspection
    // count. Return true only when this probe starts a new cooldown.
    bool inspect(int index, int group, int round, bool exact_pad, bool complete,
                 bool deferred = false) {
        if (deferred || !available(index, group, round)) return false;
        if (!exact_pad && !complete) return false;
        auto& value = entry(index, group);
        if (complete) {
            value.inspections = 0;
            value.last_inspection_round = -1;
            return false;
        }
        if (round <= value.last_inspection_round) return false;
        value.last_inspection_round = round;
        if (++value.inspections < 2) return false;
        cool(value, round);
        return true;
    }
};

// Eligibility contains the caller's policy assignments and reference admission
// fence. Protected conversion/renewal may proceed without a production quote.
inline bool usable(const Candidate& candidate, const History& history, int index, int round) {
    return index >= 0 && index < capacity && round >= 0 && candidate.eligible && candidate.group >= 0 &&
        (candidate.protected_incumbent || (candidate.kind != Kind::none &&
            history.available(index, candidate.group, round)));
}
inline bool deferred(const Candidate& candidate) {
    return candidate.kind == Kind::deferred && !candidate.protected_incumbent;
}
// A walking inspection is an unfinished request for information, not a claim
// about an unseen source. Keep that current eligible request ahead of other
// inspections; completed quotes and protected work retain the selector's rank.
inline int inspection_preference(std::span<const Candidate> candidates,const History& history,
    int round,int requested_index,int public_preference) {
    if(requested_index>=0 && requested_index<static_cast<int>(candidates.size()) &&
       candidates[requested_index].kind==Kind::inspection &&
       usable(candidates[requested_index],history,requested_index,round))return requested_index;
    return public_preference;
}
// Rank only current quotes. Otherwise prefer an actual inspection opportunity;
// a deferred candidate retains its assignment and supplies a safe yield when no
// actionable alternative exists. No policy choice or SDK query occurs here.
template<class Better>
int select(std::span<const Candidate> candidates, const History& history, int round, Better better,
           int preferred_inspection = -1) {
    if (candidates.size() > capacity) return -1;
    int quote = -1, inspection = -1, waiting = -1;
    for (int i = 0; i < static_cast<int>(candidates.size()); ++i) {
        const auto& candidate = candidates[i];
        if (!usable(candidate, history, i, round)) continue;
        if (candidate.protected_incumbent) return i;
        switch (candidate.kind) {
        case Kind::quote: if (quote < 0 || better(i, quote)) quote = i; break;
        case Kind::inspection: if (inspection < 0 || i == preferred_inspection) inspection = i; break;
        case Kind::deferred: if (waiting < 0 || i == preferred_inspection) waiting = i; break;
        default: break;
        }
    }
    return quote >= 0 ? quote : inspection >= 0 ? inspection : waiting;
}
} // namespace p6::runtime_opportunities
