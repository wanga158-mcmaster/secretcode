#pragma once
// Envelope for the actual score_group solo/party options, not a forecast of their feasibility.
#include <algorithm>

namespace cm {

inline float target_score_bound(float value, float health, float dps, int travel, int heal_trip,
                                int capture, bool continuing)
{
    // Multiplying a party's denominator by its size cancels reward/capture sharing. Its
    // pooled fight costs at least H/d champion-rounds; travel and heal replacement each cost
    // at least their solo minimum. Ignore only nonnegative wait, stage and health-recovery cost.
    // forecast_camp's ceil(x - 1e-4) permits <=5e-4 slack for at most five offered champions.
    // Its Smith order may sum the <=32 positive float HPs in a different order. A 1e-5 relative
    // allowance covers both sum orders and the two divisions (normal finite current-bundle HP).
    const float rounds = health / std::max(1.0f, dps);
    const float fight = std::max(0.0f, rounds * (1.0f - 0.00001f) - 0.001f);
    const float time = static_cast<float>(std::min(travel, heal_trip) + capture) + fight;
    return std::max(0.0f, value) * (continuing ? 1.15f : 1.0f) / std::max(1.0f, time);
}

} // namespace cm
