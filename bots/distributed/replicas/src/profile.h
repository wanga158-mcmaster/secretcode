#pragma once
#include "../../common/src/observe.h"
#include <array>

#ifndef P6_REPLICAS_INSPECTION_REPAIR
#define P6_REPLICAS_INSPECTION_REPAIR 0
#endif
#ifndef P6_REPLICAS_PARTIAL_INSPECTION_REPAIR
#define P6_REPLICAS_PARTIAL_INSPECTION_REPAIR 0
#endif
#ifndef P6_REPLICAS_STARTUP_PACKET
#define P6_REPLICAS_STARTUP_PACKET 0
#endif
#ifndef P6_REPLICAS_STARTUP_ALIAS_REPAIR
#define P6_REPLICAS_STARTUP_ALIAS_REPAIR 0
#endif
#ifndef P6_REPLICAS_STARTUP_DISPATCH_REPAIR
#define P6_REPLICAS_STARTUP_DISPATCH_REPAIR 0
#endif

namespace p6::replicas {
inline constexpr std::array<Champion, 5> roster{
    Champion::varg, Champion::ren, Champion::marcella,
    Champion::vesper_vale, Champion::stellar};
// Durable initial owner plus mobile stun-capable replica; Marcella keeps a
// separate conversion cycle. Extensions diversify independent output/coverage.
inline constexpr int live_shared_bytes = 0;
inline bool roster_actor(const Actor& a, int picks) {
    return picks >= 3 && picks <= 5 && a.team == Team::ally &&
           a.pick_ordinal >= 0 && a.pick_ordinal < picks &&
           a.type == roster[a.pick_ordinal];
}
struct Pad { int tile = -1; bool safe = false; };
struct Draft {
    int startup_group = -1;
    std::array<int, 5> independent_groups{-1, -1, -1, -1, -1};
    std::array<Pad, 8> pads{};
    int pad_count = 0;
};
// Static geometry chooses distinct safe tiles in tile-index order. A duplicate
// candidate does not create another pad. No safe pair means unavailable.
inline std::array<int, 2> startup_pads(const Draft& d) {
    std::array<int, 2> result{-1, -1};
    if (d.pad_count < 0 || d.pad_count > 8) return result;
    for (int i = 0; i < d.pad_count; ++i) {
        const auto p = d.pads[i];
        if (!p.safe || p.tile < 0 || p.tile == result[0] || p.tile == result[1]) continue;
        if (result[0] < 0 || p.tile < result[0]) {
            result[1] = result[0]; result[0] = p.tile;
        } else if (result[1] < 0 || p.tile < result[1]) result[1] = p.tile;
    }
    return result;
}
inline bool valid_draft(const Draft& d, int picks) {
    if (picks < 3 || picks > 5 || d.startup_group < 0) return false;
    for (int i = 0; i < picks; ++i) {
        if (d.independent_groups[i] < 0 || d.independent_groups[i] == d.startup_group) return false;
        for (int j = 0; j < i; ++j)
            if (d.independent_groups[i] == d.independent_groups[j]) return false;
    }
    return true;
}
} // namespace p6::replicas
