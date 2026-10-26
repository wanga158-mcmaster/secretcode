#pragma once
#include <algorithm>
#include <cstdint>
#include <span>

namespace p11 {
// A native distance field describes immutable terrain only. Dynamic occupants
// and future combat are deliberately not part of this route cost.
inline int minimum_distance(std::span<const std::uint16_t> field,std::span<const int> goals) {
    int best=10000;
    for(const int tile:goals)
        if(tile>=0&&std::size_t(tile)<field.size()&&field[tile]!=65535)
            best=std::min(best,int(field[tile]));
    return best;
}
}
