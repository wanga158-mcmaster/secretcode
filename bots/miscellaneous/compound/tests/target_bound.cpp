#include <cmath>
#include <cstdio>
#include "model.h"
#include "target_bound.h"

int main()
{
    int failures = 0;
    const auto check = [&](bool ok) { if (!ok) ++failures; };
    // Actual forecast function used by score_group: an equipped solo elk beats the old
    // static-party envelope and therefore gets incorrectly discarded after a prior 1.50 score.
    cm::MonsterSet elk;
    elk.add({2400.0f, 60.0f});
    const auto fc = cm::forecast_camp(elk, 200.0f, 1.0f);
    const float damage = fc.damage;
    const float solo = 69.0f / (20.0f + fc.rounds + 6.0f + damage / 1600.0f * 16.0f);
    const cm::SmithConstants sc{144000.0f, 60.0f, 2400.0f};
    const float old_damage = cm::estimate_camp_damage(sc, 400.0f, 1.0f) / 2.0f;
    const float old_bound = (15.0f + 90.0f * 0.6f * 1.25f) / 2.0f /
        (20.0f + 3.0f + 2400.0f / 400.0f * 0.8f + old_damage / 1600.0f * 16.0f * 0.8f);
    const float bound = cm::target_score_bound(69.0f, 2400.0f, 200.0f, 20, 16, 6, false);
    check(old_bound < 1.50f && solo > 1.50f && bound > solo);
    // Audit every actual offered party size over equipped/partial-health/continuing/heal cases.
    int cases = 0;
    for (int m = 1; m <= 5; ++m)
        for (float hp : {0.0f, 1.0f, 359.99f, 2400.0f})
            for (float dps : {0.5f, 60.0f, 200.0f, 600.0f})
                for (int travel : {0, 20, 100})
                    for (bool heal : {false, true})
                        for (bool continuing : {false, true})
                        {
                            cm::MonsterSet ms;
                            if (hp > 0.0f) ms.add({hp, 60.0f});
                            const auto fight = cm::forecast_camp(ms, std::max(1.0f, dps * m));
                            const float time = static_cast<float>(heal ? 16 : travel) + fight.rounds + 6.0f / m
                                + (m > 1 ? 8.0f : 0.0f) + fight.damage / m / 1600.0f * 16.0f;
                            const float score = 87.0f / m / std::max(1.0f, time) * (continuing ? 1.15f : 1.0f);
                            check(cm::target_score_bound(87.0f, hp, dps, travel, 16, 6, continuing) + 1e-5f >= score);
                            ++cases;
                        }
    // Mixed fractional HP and Smith permutations exercise the actual float summation order.
    for (int members : {2, 16, 32})
        for (bool reverse : {false, true})
            for (int m = 1; m <= 5; ++m)
                for (float dps : {0.5f, 60.0f, 200.0f, 600.0f})
                {
                    cm::MonsterSet ms;
                    float health = 0.0f;
                    for (int at = 0; at < members; ++at)
                    {
                        const int i = reverse ? members - at - 1 : at;
                        const float hp = 137.1f + static_cast<float>((i * 71) % 800) + static_cast<float>(i) * 0.13f;
                        ms.add({hp, static_cast<float>((i * 37) % 200)});
                        health += hp;
                    }
                    const auto fight = cm::forecast_camp(ms, std::max(1.0f, dps * m));
                    const float score = 87.0f / m / (16.0f + fight.rounds + 6.0f / m) * 1.15f;
                    check(cm::target_score_bound(87.0f, health, dps, 100, 16, 6, true) + 1e-5f >= score);
                    ++cases;
                }
    std::printf("target bound %d forecast cases, failures %d; old %.3f solo %.3f bound %.3f\n", cases, failures, old_bound, solo, bound);
    return failures ? 1 : 0;
}
