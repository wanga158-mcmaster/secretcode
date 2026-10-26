#pragma once
#include <algorithm>
#include <cmath>

namespace siege_policy {
constexpr unsigned casts(float health, float raw_damage) {
    const float quotient = std::max(0.0f, health) / std::max(1.0f, raw_damage);
    const auto whole = static_cast<unsigned>(quotient);
    return whole + (quotient > whole ? 1u : 0u);
}
constexpr bool fits(unsigned now, unsigned work, unsigned limit, unsigned reserve = 64) {
    return now <= limit && reserve <= limit - now && work <= limit - now - reserve;
}
constexpr unsigned work(unsigned casts, unsigned cooldown, unsigned travel, unsigned capture_travel,
                        unsigned capture_ticks, unsigned altar_travel, unsigned tribute_ticks) {
    return casts * std::max(1u, cooldown) + travel + capture_travel + capture_ticks + 1 + altar_travel + tribute_ticks;
}
}
