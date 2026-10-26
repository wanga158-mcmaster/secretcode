#pragma once
namespace lighthouse_policy {
constexpr bool fork(float first, float second, bool unknown_spawn) {
    return first > 0 && second > 0 && second >= first * 0.75f && unknown_spawn;
}
constexpr bool ward_allowed(int cooldown, bool restricted, bool channel, unsigned active_limit) {
    return cooldown == 0 && !restricted && !channel && active_limit > 0;
}
constexpr bool hold(unsigned turn, unsigned previous, bool valid_fork) {
    return valid_fork && turn >= previous && turn - previous < 16;
}
constexpr bool better(unsigned count, int tile, unsigned best_count, int best_tile) {
    return count > best_count || (count == best_count && count > 0 && tile < best_tile);
}
}
