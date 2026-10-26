#pragma once
#include "changepoint.h"
#include "../common/model.h"

namespace plan3_changepoint {
struct Previous {
    bool known=false;
    int round=-1,actor=-1,opponentScore=0;
    double health=0;
};
struct Mapped {
    bool available=false;
    Observation observation;
    Previous snapshot;
};
// Campaign supplies info.champions from personal_.enemies only. This is a
// literal detection stream, NOT opponent intent: public score rise > own
// consecutive health decrease > current personal enemy position. No event is
// unknown/omitted evidence, never a negative sighting or a zero damage sample.
inline Mapped map_observation(const plan3::InformationState& info,const Previous& previous) {
    Mapped result;result.observation.round=info.round;
    if(info.round<0 || info.roundLimit<=info.round || info.opponentPermanentScore<0 ||
       !std::isfinite(info.health) || !std::isfinite(info.maximumHealth) || info.health<=0 ||
       info.maximumHealth<info.health || info.champions.size()>5)return result;
    if(previous.known && (previous.actor!=info.actorOrdinal || previous.round>info.round ||
       previous.opponentScore>info.opponentPermanentScore || !std::isfinite(previous.health)))return result;
    result.available=true;result.snapshot={true,info.round,info.actorOrdinal,info.opponentPermanentScore,info.health};
    if(previous.known && previous.round<info.round && info.opponentPermanentScore>previous.opponentScore)
        result.observation.category=Category::score_increase;
    else if(previous.known && info.round-previous.round==1 && info.health<previous.health)
        result.observation.category=Category::health_decrease;
    else for(const auto& enemy:info.champions)
        if(enemy.position.value && enemy.position.value->tile>=0 &&
           enemy.position.provenance==plan3::Provenance::personally_observed && enemy.position.observedRound==info.round) {
            result.observation.category=Category::fresh_enemy;break;
        }
    return result;
}
template<class Ready>
Status observe(State& state,Previous& previous,const Config& config,const plan3::InformationState& info,Ready&& ready) {
    if(!ready())return Status::cutoff;
    // Companion calls cannot reinterpret the already-consumed snapshot.
    if(info.round<=state.lastRound)
        return update(state,config,{info.round,Category::unknown},ready);
    const auto mapped=map_observation(info,previous);if(!mapped.available)return Status::invalid;
    auto next=state;
    const auto status=update(next,config,mapped.observation,ready);
    if(status==Status::updated || status==Status::skipped) {
        if(!ready())return Status::cutoff;
        state=next;previous=mapped.snapshot;
    }
    return status;
}
inline Status observe(State& state,Previous& previous,const Config& config,const plan3::InformationState& info) {
    return observe(state,previous,config,info,[] {return true;});
}
} // namespace plan3_changepoint
