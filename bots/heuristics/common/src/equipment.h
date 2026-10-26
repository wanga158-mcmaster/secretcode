#pragma once

// A pure purchase plan for policies and fixtures. The executor maps the plan
// to an SDK item and queries its current price and legality before buying.
namespace heuristics {
enum class Purchase {
    monster_strength=0, jungle_health=1, power_strength=2,
    monster_focus=3, jungle_focus=4, power_focus=5,
    monster_health=6, jungle_strength=7, power_health=8
};
} // namespace heuristics
