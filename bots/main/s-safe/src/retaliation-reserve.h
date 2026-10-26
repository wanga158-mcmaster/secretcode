#pragma once
// Scalar contract for one optional ordinary retreat basic. This is not an engine
// damage forecast: the live consumer establishes all action/buff/intent exclusions.
#include <cmath>
#include <limits>

namespace mb {

// Ignore target mitigation and round upward, preserving any possible killing hit.
// max_bonus is the SDK passive's maximum outgoing bonus, not current missing HP.
inline float retreat_basic_damage_upper_bound(float strength, float max_bonus)
{
    if (!std::isfinite(strength) || !std::isfinite(max_bonus) || strength <= 0.0f || max_bonus < 0.0f)
        return std::numeric_limits<float>::quiet_NaN();
    return std::nextafter(strength * (1.0f + max_bonus), std::numeric_limits<float>::infinity());
}

inline bool retaliation_spends_basic_reserve(float hp, float target_hp, float outgoing_upper,
                                            float enemy_strength, float enemy_focus,
                                            float incoming_multiplier, float retaliation_ratio)
{
    const float values[] = { hp, target_hp, outgoing_upper, enemy_strength, enemy_focus,
                             incoming_multiplier, retaliation_ratio };
    for (float value : values)
        if (!std::isfinite(value) || value <= 0.0f) return false;
    if (incoming_multiplier > 1.0f || target_hp <= outgoing_upper) return false;
    const float next_basic = enemy_strength * incoming_multiplier;
    const float retaliation = (enemy_focus * retaliation_ratio) * incoming_multiplier;
    if (!std::isfinite(next_basic) || !std::isfinite(retaliation)) return false;
    // Float subtraction matches the HP-side question; equality is lethal. Already
    // being below this boundary does not establish this optional attack's cost.
    return hp > next_basic && static_cast<float>(hp - retaliation) <= next_basic;
}

} // namespace mb
