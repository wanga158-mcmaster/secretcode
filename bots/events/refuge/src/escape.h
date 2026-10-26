#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <tuple>

namespace p11_refuge {
struct Threat {
    int x=0,y=0,range=0;
    float damage=0;
};
struct Tile {
    int x=0,y=0,escape_steps=0,spawn_steps=0;
};
inline constexpr std::size_t max_tiles=9,max_threats=128;

// One next-phase estimate over the supplied legal local tiles. The adapter
// supplies awake camps and a visible-enemy basic proxy; unseen units, spells,
// future channels and exact enemy action/target choices are outside the model.
// No SDK calls or path search occur here. Origin is an ordinary admissible tile.
inline int choose_escape(float hp,std::span<const Tile> tiles,std::span<const Threat> threats) {
    if(!std::isfinite(hp)||hp<=0||tiles.empty()||tiles.size()>max_tiles||threats.size()>max_threats)return -1;
    for(const auto&tile:tiles)if(tile.escape_steps<0||tile.spawn_steps<0)return -1;
    std::array<Threat,max_threats> ordered{};
    for(std::size_t i=0;i<threats.size();++i) {
        if(threats[i].range<0||!std::isfinite(threats[i].damage)||threats[i].damage<0)return -1;
        ordered[i]=threats[i];
    }
    // Canonical damage order makes even widely separated finite float values
    // deterministic under threat permutations. Double holds every possible
    // bounded total without overflow; this is model arithmetic, not engine parity.
    std::sort(ordered.begin(),ordered.begin()+threats.size(),
              [](const Threat&a,const Threat&b){return a.damage<b.damage;});
    int best=-1;
    std::tuple<bool,double,int,int,int,int> best_key{};
    for(std::size_t i=0;i<tiles.size();++i) {
        const auto&tile=tiles[i];
        double loss=0;
        for(std::size_t j=0;j<threats.size();++j) {
            const auto&threat=ordered[j];
            const auto dx=std::int64_t(tile.x)-threat.x,dy=std::int64_t(tile.y)-threat.y;
            if(std::max(dx<0?-dx:dx,dy<0?-dy:dy)<=threat.range)loss+=threat.damage;
        }
        const auto key=std::tuple{!(double(hp)-loss>0),loss,tile.escape_steps,tile.spawn_steps,tile.x,tile.y};
        if(best<0||key<best_key){best=int(i);best_key=key;}
    }
    return best;
}
} // namespace p11_refuge
