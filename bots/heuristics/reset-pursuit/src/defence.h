#pragma once

namespace h12_reset_pursuit {
// The caller supplies personally observed enemies and the same bounded combat
// executor used for camp work. Range admission never spends movement to chase.
template<class Enemies, class Distance, class Fight>
bool defend_in_place(const Enemies& enemies, int attack_range, Distance distance, Fight fight) {
    for (const auto& enemy : enemies)
        if (distance(enemy) <= attack_range && fight(enemy)) return true;
    return false;
}
} // namespace h12_reset_pursuit
