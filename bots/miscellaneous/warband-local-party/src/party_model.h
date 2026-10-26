#pragma once
// Pure conditional model for a stationary, equal-distance two-melee camp wave.
// The caller supplies the actual named actors, live stats and a single living camp.
// Presence, legal useful basics, equal mitigation and next-round priority control
// are caller obligations; this model supplies none of those observations.
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace mb::party {

struct RotationForecast {
    bool valid = false;
    bool safe = false;
    int volleys = 0;
    std::array<float, 2> damage{};
    std::array<float, 2> hp{};
    float escape_reserve = 0;
};

inline RotationForecast rotate_remaining_wave(float hp0, float hp1, float camp_damage,
                                               float common_mitigation, float strength0,
                                               float strength1, float remaining_camp_hp) {
    // Credit only both actors' ordinary basic every future owner turn. Charge a
    // camp volley BEFORE every pair of basics, including the lethal pair. This
    // overcounts the unawakened first round rather than assuming a free first hit.
    // Optional spells, stuns, healing and shields contribute nothing here.
    RotationForecast out;
    const float inputs[] = {hp0, hp1, camp_damage, common_mitigation, strength0,
                            strength1, remaining_camp_hp};
    for (float input : inputs)
        if (!std::isfinite(input) || input <= 0.0f) return out;

    const double pair_basic = static_cast<double>(strength0) + strength1;
    const double needed = std::ceil(static_cast<double>(remaining_camp_hp) / pair_basic);
    if (!std::isfinite(needed) || needed < 1.0 || needed > 60.0) return out;
    const double volley_damage = static_cast<double>(camp_damage) * common_mitigation;
    const double reserve = 2.0 * volley_damage;
    const double float_limit = std::numeric_limits<float>::max();
    if (!std::isfinite(volley_damage) || volley_damage <= 0.0 ||
        !std::isfinite(reserve) || reserve > float_limit) return out;

    const int volleys = static_cast<int>(needed);
    std::array<double, 2> worst_hp{hp0, hp1};
    std::array<double, 2> worst_damage{};
    // Current priority may already name either tank: changing it now takes effect
    // next round. Certify BOTH possible first targets, then use the healthier
    // actor's next-round priority (index 0 wins ties within 0.1 HP).
    for (int first_target = 0; first_target < 2; ++first_target) {
        std::array<double, 2> hp{hp0, hp1};
        std::array<double, 2> damage{};
        for (int volley = 0; volley < volleys; ++volley) {
            const int target = volley == 0 ? first_target : (hp[1] > hp[0] + 0.1 ? 1 : 0);
            hp[target] -= volley_damage;
            damage[target] += volley_damage;
            if (!std::isfinite(hp[target]) || !std::isfinite(damage[target]) ||
                hp[target] < -float_limit || damage[target] > float_limit) return out;
        }
        for (int actor = 0; actor < 2; ++actor) {
            worst_hp[actor] = std::min(worst_hp[actor], hp[actor]);
            worst_damage[actor] = std::max(worst_damage[actor], damage[actor]);
        }
    }
    out.valid = true;
    out.volleys = volleys;
    out.escape_reserve = static_cast<float>(reserve);
    for (int actor = 0; actor < 2; ++actor) {
        out.hp[actor] = static_cast<float>(worst_hp[actor]);
        out.damage[actor] = static_cast<float>(worst_damage[actor]);
    }
    // These are independent worst-case per-body envelopes, not one joint
    // trajectory: the damage entries can sum to more than total camp damage.
    // Strict reserves on BOTH bodies also imply survival of every earlier volley.
    // Unsafe outputs reject continuation; they do not predict a clear after death.
    out.safe = worst_hp[0] > reserve && worst_hp[1] > reserve;
    return out;
}

} // namespace mb::party
