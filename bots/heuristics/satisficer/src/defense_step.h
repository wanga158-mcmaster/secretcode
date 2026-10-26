#pragma once
#include <algorithm>
#include <cstdlib>
#include <span>

namespace h12_satisficer {
struct StepThreat {int x,y,reach;float strength;};

// Only spend a leftover move after the existing defense action. Never move
// closer to any observed damaging actor or enter an additional basic envelope.
// Capped clearance is a local ordering, not an escape/survival certificate.
template<class Point,class Positions>
Point defense_destination(Point origin,const Positions&legal,std::span<const StepThreat> threats) {
    auto best=origin;int best_gain=0;
    for(const auto p:legal) {
        int gain=0;bool admissible=true;
        for(const auto& t:threats) {
            if(t.strength<=0)continue;
            const auto before=std::max(std::abs(int(origin.x)-t.x),std::abs(int(origin.y)-t.y));
            const auto after=std::max(std::abs(int(p.x)-t.x),std::abs(int(p.y)-t.y));
            if(after<before||(before>t.reach&&after<=t.reach)){admissible=false;break;}
            gain+=std::min(after,9)-std::min(before,9);
        }
        if(admissible&&gain>best_gain){best=p;best_gain=gain;}
    }
    return best;
}
}
