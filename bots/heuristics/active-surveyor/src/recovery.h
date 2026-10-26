#pragma once
#include "route-step.h"

namespace h12_active_surveyor {
struct RecoveryThreat {RoutePoint position;int cap=0;float weight=0;};

// The old local clearance objective is retained. A plateau is not progress:
// leave it to the existing danger-masked home route instead of alternating.
inline std::optional<RoutePoint> recovery_step(RoutePoint origin,
        std::span<const RoutePoint> legal,std::span<const RecoveryThreat> threats) {
    if(legal.empty()||legal.size()>9)return std::nullopt;
    const auto score=[&](RoutePoint p) {
        float value=0;
        for(const auto&t:threats)
            value+=std::min(std::max(std::abs(p.x-t.position.x),std::abs(p.y-t.position.y)),t.cap)*t.weight;
        return value;
    };
    float best_score=score(origin);
    std::optional<RoutePoint> best;
    for(const auto p:legal) {
        if(std::max(std::abs(p.x-origin.x),std::abs(p.y-origin.y))!=1)continue;
        const float value=score(p);
        if(value>best_score){best=p;best_score=value;}
    }
    return best;
}
}
