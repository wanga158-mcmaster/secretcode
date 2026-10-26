#include "party_model.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

using mb::party::rotate_remaining_wave;

int main() {
    {
        // E31's symmetric basic-only lab inputs need 27 pairs of basics. Either
        // current tank may take the first hit; both must retain an escape reserve.
        const auto wave = rotate_remaining_wave(1200, 1200, 60, 1, 45, 45, 2400);
        assert(wave.valid && wave.safe && wave.volleys == 27);
        assert(wave.escape_reserve == 120);
        assert(wave.damage[0] == 840 && wave.damage[1] == 780);
        assert(wave.hp[0] == 360 && wave.hp[1] == 420);
    }
    {
        // A healthy partner cannot lend HP before the already configured first
        // volley. A pooled test would accept 1180 HP against only 60 damage.
        const auto weak = rotate_remaining_wave(180, 1000, 60, 1, 45, 45, 90);
        assert(weak.valid && !weak.safe && weak.hp[0] == 120);
        assert(weak.hp[1] == 940 && weak.escape_reserve == 120);
        // Per-body maxima can belong to different possible first tanks.
        assert(weak.damage[0] + weak.damage[1] > weak.volleys * 60);
        const auto above = rotate_remaining_wave(180.25f, 1000, 60, 1, 45, 45, 90);
        assert(above.valid && above.safe);
        const auto dead_first = rotate_remaining_wave(60, 1000, 60, 1, 45, 45, 90);
        assert(dead_first.valid && !dead_first.safe && dead_first.hp[0] == 0);
    }
    {
        // Even if the first BASIC would kill the camp, a next-round camp volley
        // comes before that owner action. Do not subtract it at the lethal bound.
        const auto lethal = rotate_remaining_wave(400, 400, 60, 1, 90, 45, 90);
        assert(lethal.valid && lethal.safe && lethal.volleys == 1);
        assert(lethal.damage[0] == 60 && lethal.damage[1] == 60);
        const auto exact = rotate_remaining_wave(400, 400, 60, 1, 45, 45, 180);
        const auto over = rotate_remaining_wave(400, 400, 60, 1, 45, 45, 180.25f);
        assert(exact.volleys == 2 && over.volleys == 3);
        assert(exact.hp[0] == 340 && exact.hp[1] == 340);
        assert(over.hp[0] == 280 && over.hp[1] == 340);
    }
    {
        // Rotation is not an assumed equal split: after either first tank, the
        // healthier actor takes later volleys until the actual HP ordering flips.
        const auto asymmetric = rotate_remaining_wave(240, 600, 60, 1, 45, 45, 270);
        assert(asymmetric.valid && asymmetric.safe && asymmetric.volleys == 3);
        assert(asymmetric.damage[0] == 60 && asymmetric.damage[1] == 180);
        assert(asymmetric.hp[0] == 180 && asymmetric.hp[1] == 420);
        const auto mitigation = rotate_remaining_wave(1200, 1200, 60, 0.5f, 45, 45, 2400);
        assert(mitigation.valid && mitigation.safe && mitigation.escape_reserve == 60);
        assert(mitigation.damage[0] == 420 && mitigation.damage[1] == 390);
        const auto swapped = rotate_remaining_wave(600, 240, 60, 1, 45, 45, 270);
        assert(swapped.valid && swapped.safe);
        assert(swapped.hp[0] == asymmetric.hp[1] && swapped.hp[1] == asymmetric.hp[0]);
    }
    {
        const auto bound = rotate_remaining_wave(10000, 10000, 60, 1, 45, 45, 5400);
        assert(bound.valid && bound.safe && bound.volleys == 60);
        assert(!rotate_remaining_wave(10000, 10000, 60, 1, 45, 45, 5400.25f).valid);
        for (int changed = 0; changed < 7; ++changed)
            for (float invalid : {-1.0f, 0.0f, std::numeric_limits<float>::quiet_NaN(),
                                  std::numeric_limits<float>::infinity()}) {
                float input[] = {1200, 1200, 60, 1, 45, 45, 2400};
                input[changed] = invalid;
                const auto bad = rotate_remaining_wave(input[0], input[1], input[2],
                    input[3], input[4], input[5], input[6]);
                assert(!bad.valid && !bad.safe);
            }
        assert(!rotate_remaining_wave(1200, 1200, std::numeric_limits<float>::max(),
                                       2, 45, 45, 90).valid);
        assert(!rotate_remaining_wave(1200, 1200, std::numeric_limits<float>::max() / 4,
                                       1, 1, 1, 120).valid);
    }
    std::puts("party model: individual survival, unknown first tank, ordering and finite bounds passed");
}
