#pragma once
namespace choir {
constexpr bool deadline(unsigned turn, unsigned limit = 48) { return turn > limit; }
constexpr bool stalled(unsigned turn, unsigned last_progress, unsigned limit = 8) {
    return turn >= last_progress && turn - last_progress >= limit;
}
constexpr bool release(bool enabled, bool reward_seen, bool holder_seen) {
    return enabled && reward_seen && holder_seen;
}
constexpr bool complete_reward(unsigned gems_before, unsigned gold_before,
                               unsigned gems_after, unsigned gold_after,
                               unsigned expected_gems, unsigned expected_gold) {
    return gems_after >= gems_before && gold_after >= gold_before
        && gems_after-gems_before == expected_gems && gold_after-gold_before == expected_gold;
}
}
