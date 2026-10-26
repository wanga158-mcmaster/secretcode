#pragma once
#include "model.h"
#include <cstdint>
#include <optional>

namespace plan3_runtime {
// Own post-action state, paired with the score sampled in that same owner round.
// No camp state, ally-private information or assumed forecast enters this proof.
struct ConversionSnapshot {
    unsigned round=0, scoreRound=0, ownScore=0, bag=0;
    bool valid=false, alive=false, submitting=false;
    std::optional<plan3::JobId> bankingJob;
    unsigned jobStarted=0;
};
struct ConversionObservation {
    unsigned round=0, ownScore=0, bag=0;
    bool valid=false, alive=false, submitting=false, cancelled=false;
};
struct ConversionWitness {
    bool intervalKnown=false;
    unsigned lowerBound=0;
    std::optional<unsigned> exactAmount;
    bool confirmed() const { return lowerBound>0; }
};
inline ConversionWitness observe_conversion(const ConversionSnapshot& before,
        const ConversionObservation& now,int respawnTime) {
    ConversionWitness result;
    // Alive endpoints cannot exclude an intervening instant death/respawn under
    // these settings. A skipped callback or mixed-round snapshot is unknown.
    if(!before.valid || !now.valid || !before.alive || !now.alive || respawnTime<=1 ||
       before.scoreRound!=before.round || std::uint64_t(before.round)+1!=now.round ||
       now.ownScore<before.ownScore)return result;
    result.intervalKnown=true;
    if(!before.submitting || now.submitting || now.bag>0) {
        result.exactAmount=0;
        return result;
    }
    if(now.cancelled || before.bag==0)return result;
    const auto publicDelta=now.ownScore-before.ownScore;
    if(publicDelta<before.bag)return result;
    // Captures resolve before channels; tribute takes the entire current bag.
    // Extra public score may include a new capture or another actor's tribute.
    result.lowerBound=before.bag;
    if(publicDelta==before.bag)result.exactAmount=before.bag;
    return result;
}
inline bool conversion_completes_job(const ConversionSnapshot& before,
        const ConversionWitness& witness,const plan3::JobId& current,
        unsigned started,bool currentlyBanking) {
    return witness.confirmed() && before.bankingJob && *before.bankingJob==current &&
        before.jobStarted==started && currentlyBanking;
}
} // namespace plan3_runtime
