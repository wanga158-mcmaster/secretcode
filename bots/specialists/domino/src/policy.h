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

namespace banking {
enum class BankReason { none, threshold, nearby, late };
// Inherited bank threshold remains 24. Runtime
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
} // namespace banking

namespace domino {
inline constexpr float epsilon = 0.0001f;
constexpr bool lethal(float health, float damage) {
    return health > 0 && damage > 0 && health <= damage + epsilon;
}
constexpr unsigned sequence(bool basic_available, int cooldown, bool reset_ready,
                            bool has_lethal, bool has_nonlethal) {
    if (!basic_available) return 0;
    if (cooldown > 0 && has_lethal) return 1;
    if (reset_ready && has_lethal && has_nonlethal) return 2;
    return 3;
}
constexpr bool precedes(float score, unsigned member, float incumbent_score, unsigned incumbent) {
    return score < incumbent_score || (score == incumbent_score && member < incumbent);
}
constexpr bool refresh_still_useful(unsigned kill_round, int replaced_cooldown, unsigned use_round) {
    return replaced_cooldown > 0 && use_round >= kill_round
        && use_round-kill_round < static_cast<unsigned>(replaced_cooldown);
}
} // namespace domino
