#pragma once
// Pure, bounded SUPPORT planner. FARM has residual 0. Candidate order cannot affect ties.
// No game queries, allocation, random draws or persistent commitment state live here.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace mb::support_plan {

inline constexpr int kFeatures = 16;
inline constexpr int kScreenRadius = 2;
inline constexpr int kMaxDistance = 10;
inline constexpr int kMaxCampDistance = 6;
inline constexpr int kMaxCombatDistance = 12;
inline constexpr int kCombatAllyRadius = 6;

enum class TaskKind { none, camp, combat };

// A camp task finishes work the selected ally has already begun. The adapter
// supplies personal-vision evidence and the selected ally's published target.
inline bool camp_task_eligible(bool valid_group, bool engaging, bool visible_living, bool awake,
                               int ally_distance, int ally_effect_range, int own_distance)
{
    return valid_group && engaging && visible_living && awake &&
           ally_distance >= 0 && ally_effect_range >= 1 && ally_distance <= ally_effect_range &&
           own_distance >= 0 && own_distance <= kMaxCampDistance;
}

// enemy_threatens_ally must come from the existing enemy-reach model, rather
// than a second, weaker range rule. Only a nearby visible enemy can be a task.
inline bool combat_task_eligible(bool visible_living, bool enemy_threatens_ally,
                                 int ally_distance, int own_distance)
{
    return visible_living && enemy_threatens_ally && ally_distance >= 0 &&
           own_distance >= 0 && own_distance <= kMaxCombatDistance;
}

inline int effect_arrival(int distance, int effect_range)
{
    return std::max(0, distance - std::max(0, effect_range));
}

// Reusing the camp executor must not enter its fresh-field branch after a
// strike dash kills a nearby member or moves away from surviving members.
inline bool camp_move_local(bool cached_field, bool strike_dash, int farthest_visible)
{
    return cached_field || (!strike_dash && farthest_visible >= 0 && farthest_visible <= kMaxCampDistance);
}

// Layout of the appended learn::support block, indices 144..159. Costs and power are
// estimates from personal vision; target detour may use an already cached distance field.
enum Feature : int
{
    bias, arrival, detour, own_health, ally_health, allied_number, enemy_number,
    allied_power_share, contact, target_overlap, target_progress, early,
    lead_power, lead_score, early_contact, lead_power_balance
};

struct Observation
{
    int distance{}, target_detour{}, allies{}, enemies{}, contacts{}, round{};
    float own_hp{}, ally_hp{}, allied_power{}, enemy_power{}, progress{}, power_lead{}, score_lead{};
    bool same_target{};
    // -1 retains the original radius-only feature. Task mode supplies the
    // remaining number of ordinary steps before our attack can affect its target.
    int effect_arrival{ -1 };
};

inline std::array<float, kFeatures> features(const Observation& o)
{
    const float young = std::clamp(1.0f - static_cast<float>(o.round) / 200.0f, 0.0f, 1.0f);
    const float power = o.allied_power / std::max(1.0f, o.allied_power + o.enemy_power);
    const float score = std::clamp(o.score_lead / 2.0f, -1.0f, 1.0f);
    const float touching = std::min(1.0f, static_cast<float>(o.contacts) / 4.0f);
    const int steps = o.effect_arrival >= 0 ? o.effect_arrival : std::max(0, o.distance - kScreenRadius);
    return { 1.0f, static_cast<float>(steps) / 8.0f,
             std::clamp(static_cast<float>(o.target_detour) / 10.0f, -1.0f, 1.0f),
             std::clamp(o.own_hp, 0.0f, 1.0f), std::clamp(o.ally_hp, 0.0f, 1.0f),
             std::min(1.0f, static_cast<float>(o.allies) / 5.0f),
             std::min(1.0f, static_cast<float>(o.enemies) / 5.0f), power, touching,
             o.same_target ? 1.0f : 0.0f, std::clamp(o.progress, 0.0f, 1.0f), young,
             std::clamp(o.power_lead / 2.0f, -0.5f, 1.0f), score,
             young * touching, score * (2.0f * power - 1.0f) };
}

struct Candidate
{
    int ally{ -1 };
    bool valid{};
    std::array<float, kFeatures> x{};
};

struct Choice
{
    int candidate{ -1 }, ally{ -1 };
    float score{};
};

// Acyclic assignments ensure a positive SUPPORT bias cannot make a whole formation
// wait on itself. The lowest eligible stable identity remains free to farm.
inline bool eligible_ally(int self, int ally) { return self >= 0 && ally >= 0 && ally < self; }

inline bool active(const std::array<float, kFeatures>& weights)
{
    for (const float w : weights)
        if (w != 0.0f) return true;
    return false;
}

// The disabled/zero case must not even invoke make_candidate: callers can skip all
// observation, lead, field and feature work, rather than merely returning the same answer.
template <class MakeCandidate>
Choice choose(bool enabled, const std::array<float, kFeatures>& weights, int count, MakeCandidate make_candidate)
{
    Choice best;
    if (!enabled || !active(weights)) return best;
    for (int i = 0; i < count; ++i)
    {
        const Candidate c = make_candidate(i);
        if (!c.valid || c.ally < 0) continue;
        float score = 0.0f;
        for (int j = 0; j < kFeatures; ++j) score += weights[j] * c.x[j];
        if (std::isfinite(score) && score > 0.0f &&
            (score > best.score || (score == best.score && c.ally < best.ally)))
            best = { i, c.ally, score };
    }
    return best;
}

inline bool may_move(bool can_move, bool already_moved, int distance)
{
    return can_move && !already_moved && distance > kScreenRadius;
}

inline bool may_task_move(bool can_move, bool already_moved)
{
    return can_move && !already_moved;
}

struct ActionFlags
{
    bool moved{}, attacked{}, cast{};
};

struct ActionDelta
{
    bool moved{}, attacked{}, cast{};
    bool any() const { return moved || attacked || cast; }
};

// A helper's return value is not an effect: e.g. waiting for a tank may return
// true without using an allowance. Only newly recorded actions commit SUPPORT.
inline ActionDelta effect_delta(ActionFlags before, ActionFlags after)
{
    return { !before.moved && after.moved, !before.attacked && after.attacked, !before.cast && after.cast };
}

// Entry admission must have reserved the whole finite proposal budget above
// the FARM floor. Before each bounded chunk, reserve its cost in both ledgers.
// Subtractions avoid overflowing farm_floor + budget/chunk_reserve.
inline bool proposal_budget_available(std::uint64_t entry, std::uint64_t remaining,
                                      std::uint64_t budget, std::uint64_t farm_floor,
                                      std::uint64_t chunk_reserve = 0)
{
    if (remaining > entry || entry < farm_floor || budget > entry - farm_floor || remaining < farm_floor)
        return false;
    const auto consumed = entry - remaining;
    return consumed <= budget && chunk_reserve <= budget - consumed && chunk_reserve <= remaining - farm_floor;
}

// Staying scores 0. Every accepted step strictly closes the ally gap. Increased incoming
// damage is a continuous cost, so support can approach a fight rather than exclude it by
// enemy proximity; an expensive approach waits. Damage saved can improve the same step.
inline int step_score(int distance, int next_distance, float incoming, float next_incoming, float max_hp)
{
    if (distance <= kScreenRadius || next_distance >= distance) return -1;
    const float value = 100.0f * static_cast<float>(distance - std::max(kScreenRadius, next_distance)) +
                        400.0f * (incoming - next_incoming) / std::max(1.0f, max_hp);
    return static_cast<int>(std::clamp(value, -100000.0f, 100000.0f));
}

// Distances here are remaining steps to our own attack range, not distances
// to the buddy. Reaching effect range stops the approach; the existing threat
// handler decides combat next turn. Staying and rejected moves score <= 0.
inline int combat_task_step_score(int effect_distance, int next_effect_distance, int next_ally_distance,
                                  float incoming, float next_incoming, float max_hp)
{
    if (effect_distance <= 0 || next_effect_distance < 0 || next_effect_distance >= effect_distance ||
        next_ally_distance < 0 || next_ally_distance > kCombatAllyRadius ||
        !std::isfinite(incoming) || !std::isfinite(next_incoming) || !std::isfinite(max_hp)) return -1;
    const float value = 100.0f * static_cast<float>(effect_distance - next_effect_distance) +
                        400.0f * (incoming - next_incoming) / std::max(1.0f, max_hp);
    return static_cast<int>(std::clamp(value, -100000.0f, 100000.0f));
}

} // namespace mb::support_plan
