#pragma once
#include <bit>
#include <cstdint>

namespace p11 {
// Per-group memoization of the unchanged public hit-work computation. No
// position, live camp observation, ranking, or policy commitment is cached.
// Float bit keys avoid assuming damage is stable across purchases/passives.
class PublicHitWork {
    mutable bool valid_ = false;
    mutable std::uint32_t health_ = 0, damage_ = 0;
    mutable int work_ = 0;
public:
    template<class Compute> int get(float health, float damage, Compute compute) const {
        const auto health_bits = std::bit_cast<std::uint32_t>(health);
        const auto damage_bits = std::bit_cast<std::uint32_t>(damage);
        if (valid_ && health_ == health_bits && damage_ == damage_bits) return work_;
        // A failed computation leaves the previous complete entry intact.
        const int work = compute(health, damage);
        health_ = health_bits;
        damage_ = damage_bits;
        work_ = work;
        valid_ = true;
        return work;
    }
};
} // namespace p11
