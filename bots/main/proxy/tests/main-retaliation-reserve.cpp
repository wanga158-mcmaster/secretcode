// Source scalar contracts only: no SDK action, normal-retreat admission or engine
// transition is exercised here. Production-consumer proof needs paired real games.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include "retaliation-reserve.h"

int main()
{
    using mb::retaliation_spends_basic_reserve;
    using mb::retreat_basic_damage_upper_bound;
    constexpr float inf = std::numeric_limits<float>::infinity();
    const float upper = retreat_basic_damage_upper_bound(30.0f, 0.5f);

    // A006's exposed ordinary, unshielded values. This nominates a reserve loss,
    // without asserting historical retreat intent, a safe walk or a saved death.
    assert(retaliation_spends_basic_reserve(56.5599518f, 1031.1536865f, upper, 40.0f, 136.0f, 1.0f, 0.2f));
    assert(!retaliation_spends_basic_reserve(123.7599487f, 1074.4660645f, upper, 40.0f, 136.0f, 1.0f, 0.2f));

    // Before must survive strictly; after equality loses the reserve. Adjacent
    // floats test the boundaries without an arbitrary epsilon or decimal tie.
    assert(!retaliation_spends_basic_reserve(40.0f, 1000.0f, upper, 40.0f, 100.0f, 1.0f, 0.2f));
    assert(retaliation_spends_basic_reserve(std::nextafter(40.0f, inf), 1000.0f, upper, 40.0f, 100.0f, 1.0f, 0.2f));
    assert(retaliation_spends_basic_reserve(60.0f, 1000.0f, upper, 40.0f, 100.0f, 1.0f, 0.2f));
    assert(!retaliation_spends_basic_reserve(std::nextafter(60.0f, inf), 1000.0f, upper, 40.0f, 100.0f, 1.0f, 0.2f));

    // Heart's real basic can exceed Strength. The maximum unmitigated bound
    // preserves both base-Strength and missing-health killing opportunities.
    assert(upper > 45.0f);
    assert(!retaliation_spends_basic_reserve(56.5599518f, 30.0f, upper, 40.0f, 136.0f, 1.0f, 0.2f));
    assert(!retaliation_spends_basic_reserve(56.5599518f, 44.22876f, upper, 40.0f, 136.0f, 1.0f, 0.2f));
    assert(!retaliation_spends_basic_reserve(56.5599518f, upper, upper, 40.0f, 136.0f, 1.0f, 0.2f));
    assert(retaliation_spends_basic_reserve(56.5599518f, std::nextafter(upper, inf), upper, 40.0f, 136.0f, 1.0f, 0.2f));

    // Durability must affect BOTH retaliation and the later normal basic. Jungle
    // provenance and nonstacking are live-consumer obligations, not scalar proof.
    assert(retaliation_spends_basic_reserve(62.0f, 1000.0f, upper, 40.0f, 136.0f, 1.0f, 0.2f));
    assert(!retaliation_spends_basic_reserve(62.0f, 1000.0f, upper, 40.0f, 136.0f, 0.9f, 0.2f));

    // Unknown/invalid inputs cannot justify omitting a real action.
    const float valid[] = { 56.5599518f, 1031.1536865f, upper, 40.0f, 136.0f, 1.0f, 0.2f };
    const float invalid[] = { 0.0f, -1.0f, inf, -inf, std::numeric_limits<float>::quiet_NaN() };
    for (int index = 0; index < 7; ++index)
        for (float value : invalid)
        {
            float args[7];
            for (int i = 0; i < 7; ++i) args[i] = valid[i];
            args[index] = value;
            assert(!retaliation_spends_basic_reserve(args[0], args[1], args[2], args[3], args[4], args[5], args[6]));
        }
    assert(!retaliation_spends_basic_reserve(56.0f, 1000.0f, upper, 40.0f, 136.0f, 1.1f, 0.2f));
    assert(!retaliation_spends_basic_reserve(56.0f, 1000.0f, upper, 40.0f, std::numeric_limits<float>::max(), 1.0f, 2.0f));
    assert(std::isnan(retreat_basic_damage_upper_bound(0.0f, 0.5f)));
    assert(std::isnan(retreat_basic_damage_upper_bound(30.0f, -0.1f)));
    assert(!retaliation_spends_basic_reserve(56.0f, 1000.0f,
            retreat_basic_damage_upper_bound(std::numeric_limits<float>::max(), 0.5f), 40.0f, 136.0f, 1.0f, 0.2f));
    std::puts("retaliation reserve scalar contracts passed (not engine proof)");
}
