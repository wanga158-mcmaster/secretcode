// These are the production hold/defense/suppression functions used by turn().
// Native before/after games separately exercise their integration with the SDK.
#include <array>
#include "../src/policy.h"

constexpr bool ordinary_capture() {
    fireflies::CaptureHold hold;
    unsigned suppressed = 0;
    for (unsigned round = 20; round <= 25; ++round)
        if (!hold.keep(3, round, 5, false, suppressed)) return false;
    return suppressed == 0;
}
constexpr bool bounded_unknown_contest() {
    fireflies::CaptureHold hold;
    unsigned suppressed = 0;
    for (unsigned round = 20; round < 31; ++round)
        if (!hold.keep(3, round, 5, false, suppressed)) return false;
    if (hold.keep(3, 31, 5, false, suppressed) || suppressed != 42) return false;
    // Both the occupied-spawn guard and farm's candidate test use this exact
    // availability function; neither may immediately reselect the failed hold.
    if (fireflies::capture_group_available(32, suppressed) || hold.keep(3, 32, 5, false, suppressed)) return false;
    hold.reset();
    return hold.keep(3, 42, 5, false, suppressed);
}
constexpr bool progress_and_reentry() {
    fireflies::CaptureHold hold;
    unsigned suppressed = 0;
    if (!hold.keep(3, 20, 5, false, suppressed) || !hold.keep(3, 30, 5, true, suppressed)) return false;
    if (!hold.keep(3, 40, 5, false, suppressed)) return false;
    hold.reset(); // death, departure, and later reentry start a fresh episode
    return hold.keep(3, 60, 5, false, suppressed);
}
static_assert(ordinary_capture());
static_assert(bounded_unknown_contest());
static_assert(progress_and_reentry());
static_assert(fireflies::capture_wait_limit(3) == 7); // queried duration, no copied five

struct Enemy { int id; bool visible, on_group; };
int main() {
    constexpr std::array enemies{Enemy{1, false, true}, Enemy{2, true, false}, Enemy{3, true, true}};
    int calls = 0, chosen = 0;
    auto result = fireflies::defend_capture(enemies,
        [](Enemy enemy) { return enemy.visible && enemy.on_group; },
        [&](Enemy enemy) { ++calls; chosen = enemy.id; return fireflies::CaptureDefenseResult{true, true}; });
    if (!result.acted || !result.progressed || calls != 1 || chosen != 3) return 1;
    constexpr std::array no_contestor{Enemy{1, false, true}, Enemy{2, true, false}};
    calls = 0;
    result = fireflies::defend_capture(no_contestor,
        [](Enemy enemy) { return enemy.visible && enemy.on_group; },
        [&](Enemy) { ++calls; return fireflies::CaptureDefenseResult{true, true}; });
    return result.acted || calls != 0;
}
