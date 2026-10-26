#pragma once

// Engine-independent policy: keep thresholds explicit and easy to test/change.
namespace fundamentals {
constexpr bool should_retreat(float health, float maximum, float incoming) {
    return maximum > 0 && (health <= maximum * 0.35f || health <= incoming * 2.0f + 60.0f);
}

constexpr bool should_bank(unsigned gems, unsigned round, int altar_distance) {
    return gems > 0 && (gems >= 24 || altar_distance <= 4 || round >= 1900);
}

constexpr bool affordable_camp(float health, float maximum, float monster_health,
                               float incoming, float damage_per_turn) {
    if (incoming <= 0) return true;
    if (damage_per_turn <= 0) return false;
    // Include the next camp phase and a reserve for travel/ambushes. Conservative
    // by design: this first bot does not depend on teammates joining a fight.
    const float expected_loss = incoming * (monster_health / damage_per_turn + 2.0f);
    return health - expected_loss >= maximum * 0.25f;
}

enum class BuildStage { monster_strength, jungle_health, power_strength };
constexpr BuildStage build_stage(unsigned monster_shards, unsigned jungle_shards) {
    if (monster_shards < 5) return BuildStage::monster_strength;
    if (jungle_shards < 5) return BuildStage::jungle_health;
    return BuildStage::power_strength;
}
} // namespace fundamentals

namespace fireflies {
struct CaptureDefenseResult { bool acted = false, progressed = false; };

template<class Enemies, class Eligible, class Defend>
CaptureDefenseResult defend_capture(const Enemies& enemies, Eligible eligible, Defend defend) {
    for (const auto& enemy : enemies)
        if (eligible(enemy)) {
            const auto result = defend(enemy);
            if (result.acted) return result;
        }
    return {};
}

constexpr unsigned capture_wait_limit(unsigned capture_ticks) {
    return 2 * (capture_ticks ? capture_ticks : 1) + 1;
}
constexpr bool capture_group_available(unsigned round, unsigned unavailable_until) {
    return round >= unavailable_until;
}

class CaptureHold {
    int group_ = -1;
    unsigned since_ = 0;
public:
    constexpr int group() const { return group_; }
    constexpr void reset() { group_ = -1; since_ = 0; }
    constexpr bool keep(int group, unsigned round, unsigned capture_ticks, bool observed_progress,
                        unsigned& unavailable_until) {
        if (!capture_group_available(round, unavailable_until)) return false;
        if (group_ != group || observed_progress) { group_ = group; since_ = round; }
        const unsigned limit = capture_wait_limit(capture_ticks);
        if (round - since_ < limit) return true;
        unavailable_until = round + limit;
        return false;
    }
};

// A physical group can straddle terrain components. Its arbitrary anchor does
// not decide reachability: keep it whenever any original spawn is reachable.
template<class Members, class Reachable>
constexpr bool any_member_reachable(const Members& members, Reachable reachable) {
    for (const auto& member : members) if (reachable(member)) return true;
    return false;
}

enum class BankReason { none, threshold, nearby, late };
// The only competitive treatment is threshold 12 rather than 24. Runtime
// max_rounds makes the inherited final-100-round exception engine-relative.
constexpr bool late_round(unsigned round, unsigned max_rounds) {
    return round >= (max_rounds > 100 ? max_rounds - 100 : 0);
}
constexpr BankReason bank_reason(unsigned gems, unsigned round,
                                 unsigned max_rounds, int altar_distance,
                                 unsigned threshold) {
    if (!gems) return BankReason::none;
    if (gems >= threshold) return BankReason::threshold;
    if (altar_distance <= 4) return BankReason::nearby;
    return late_round(round, max_rounds) ? BankReason::late : BankReason::none;
}
constexpr const char* reason_name(BankReason reason) {
    switch (reason) {
    case BankReason::threshold: return "threshold";
    case BankReason::nearby: return "nearby";
    case BankReason::late: return "late";
    default: return "none";
    }
}
} // namespace fireflies
