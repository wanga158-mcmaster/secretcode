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
