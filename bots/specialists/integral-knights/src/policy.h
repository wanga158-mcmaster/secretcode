#pragma once
#include <cmath>
#include <algorithm>
#include <limits>

// Engine-independent policy: keep thresholds explicit and easy to test/change.
namespace fundamentals {
constexpr bool should_retreat(float health, float maximum, float incoming) {
    return maximum > 0 && (health <= maximum * 0.35f || health <= incoming * 2.0f + 60.0f);
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

namespace integral {
inline constexpr float epsilon = 0.0001f;
struct Candidate { float health, weight; unsigned work, tile; };
constexpr unsigned basic_work(float health, float damage) {
    // Epsilon bounds the denominator, never an observed health boundary.
    const double quotient = std::max<double>(0, health) / std::max<double>(epsilon, damage);
    if (quotient >= std::numeric_limits<unsigned>::max()) return std::numeric_limits<unsigned>::max();
    const auto whole = static_cast<unsigned>(quotient);
    return std::max(1u, whole + (quotient > whole ? 1u : 0u));
}
constexpr bool precedes(Candidate a, Candidate b) {
    const double left = static_cast<double>(a.weight) * b.work;
    const double right = static_cast<double>(b.weight) * a.work;
    if (left != right) return left > right;
    if (a.work != b.work) return a.work < b.work;
    if (a.health != b.health) return a.health < b.health;
    return a.tile < b.tile;
}
// Both sleeping monsters wake on the first hit. A monster killed by this hit
// contributes no subsequent camp phase; replay checks the actual phase offset.
constexpr float isolated_loss(Candidate first, Candidate second) {
    return (first.work - 1) * (first.weight + second.weight) + second.work * second.weight;
}
constexpr bool late_round(unsigned round, unsigned maximum) {
    return round >= (maximum > 100 ? maximum - 100 : 0);
}
}
