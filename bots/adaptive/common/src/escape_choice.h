#pragma once
#include <array>
#include <cmath>

namespace p8::native {
inline constexpr int max_escape_neighbors=8;
struct EscapeNeighbor {
    double damage=0;
    int clearance=0;
};
struct EscapeInputs {
    std::array<EscapeNeighbor,max_escape_neighbors> neighbors{};
    int count=0;
    double origin_damage=0;
    int origin_clearance=0;
    double health=0;
};

// The caller supplies only currently legal neighbors, in its existing y/x
// order. Clearance is its numeric distance beyond the observed threat range;
// this selector supplies neither legal-move nor unseen-threat evidence.
inline int choose_escape(const EscapeInputs& in) {
    if(in.count<=0||in.count>max_escape_neighbors||
       !std::isfinite(in.origin_damage)||in.origin_damage<0||
       !std::isfinite(in.health)||in.health<=0)return -1;
    int selected=-1;
    double best=in.origin_damage;
    bool lower_exists=false;
    for(int i=0;i<in.count;++i) {
        const auto& neighbor=in.neighbors[i];
        if(!std::isfinite(neighbor.damage)||neighbor.damage<0)continue;
        if(neighbor.damage<in.origin_damage)lower_exists=true;
        if(neighbor.damage<best&&neighbor.damage<in.health) {
            best=neighbor.damage;selected=i;
        }
    }
    if(lower_exists)return selected;
    int clearance=in.origin_clearance;
    for(int i=0;i<in.count;++i) {
        const auto& neighbor=in.neighbors[i];
        if(neighbor.damage==in.origin_damage&&neighbor.damage<in.health&&
           neighbor.clearance>clearance) {
            clearance=neighbor.clearance;selected=i;
        }
    }
    return selected;
}
} // namespace p8::native
