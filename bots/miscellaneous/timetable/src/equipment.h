#pragma once
#include <algorithm>
#include "model.h"

namespace tt {

struct EquipmentModel {
    float monster = 0, durability = 0, treasure = 0, jungle = 0;

    void observe(int passive, float magnitude)
    {
        if (passive == 1) monster = std::max(monster, magnitude);
        if (passive == 2) durability = std::max(durability, magnitude);
        if (passive == 3) treasure = std::max(treasure, magnitude);
        if (passive == 4) jungle = std::max(jungle, magnitude);
    }
    float camp_taken() const { return std::max(0.0f, 1-jungle) * std::max(0.0f, 1-durability); }
    float camp_scale() const { return 1+monster; }
    void apply(BodySpec& body) const
    {
        body.taken *= camp_taken();
        body.gold_mult += treasure; // champion and Treasure gold bonuses add (M37)
    }
};

} // namespace tt
