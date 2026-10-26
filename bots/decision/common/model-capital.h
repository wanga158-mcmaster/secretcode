#pragma once
#include "model.h"

namespace plan3 {
// A finite capital schedule, with conservative income recognition at completed
// funding conversions. A fixed repeated cycle includes regeneration/recovery.
// No hypothetical purchased stats are available until readyAt.
struct CapitalTiming {
    int horizon=128, scoreCapacity=0, oldBag=0, cashoutAt=0;
    double initialGold=0, blockCost=0, passiveGoldPerTurn=0;
    int firstFundingCompletion=0, fundingPeriod=0, fundingGems=0;
    double fundingGold=0;
    // A currently committed remainder is settled even when cash already buys
    // the block. Its rewards have not yet entered current cash or score.
    int settlementTurns=0, settlementGems=0;
    double settlementGold=0;
    int purchaseRecoveryReturn=0;
    std::vector<RenewalLoop> postPurchaseLoops;
};
struct CapitalOutput {
    bool feasible=false;
    int readyAt=0, fundingCycles=0;
    double remainingGold=0;
    std::vector<Conversion> fundingConversions;
    std::array<double,3> checkpoints{};
};
inline CapitalOutput capital_output(const CapitalTiming& x) {
    CapitalOutput r;
    if (x.horizon<0 || x.scoreCapacity<0 || x.oldBag<0 || x.cashoutAt<0 ||
        x.initialGold<0 || x.blockCost<0 || x.passiveGoldPerTurn<0 ||
        x.purchaseRecoveryReturn<0 || x.settlementTurns<0 || x.settlementGems<0 || x.settlementGold<0 ||
        (x.settlementTurns==0 && (x.settlementGems>0 || x.settlementGold>0)) ||
        !std::isfinite(x.initialGold+x.blockCost+x.passiveGoldPerTurn+x.fundingGold+x.settlementGold)) return r;
    if (x.oldBag>0 && x.cashoutAt==0) return r;
    int time=x.oldBag>0 ? x.cashoutAt:0;
    double cash=x.initialGold+x.passiveGoldPerTurn*time;
    if (x.oldBag>0) r.fundingConversions.push_back({time,x.oldBag});
    time+=x.settlementTurns;
    cash+=x.settlementGold+x.passiveGoldPerTurn*x.settlementTurns;
    if(x.settlementGems>0)r.fundingConversions.push_back({time,x.settlementGems});
    if (cash<x.blockCost) {
        if (x.firstFundingCompletion<=0 || x.fundingPeriod<=0 || x.fundingGems<0 ||
            x.fundingGold<0 || x.fundingGold+x.passiveGoldPerTurn*x.fundingPeriod<=0) return r;
        while (cash<x.blockCost && time<=x.horizon && r.fundingCycles<=128) {
            const int elapsed=r.fundingCycles==0 ? x.firstFundingCompletion:x.fundingPeriod;
            time+=elapsed;
            cash+=x.fundingGold+x.passiveGoldPerTurn*elapsed;
            if(x.fundingGems>0)r.fundingConversions.push_back({time,x.fundingGems});
            ++r.fundingCycles;
        }
    }
    if (cash<x.blockCost) return r;
    r.readyAt=time+x.purchaseRecoveryReturn;
    r.remainingGold=cash-x.blockCost+x.passiveGoldPerTurn*x.purchaseRecoveryReturn;
    const std::array<int,3> checkpoints{x.horizon/4,x.horizon/2,x.horizon};
    for (unsigned k=0;k<3;++k) {
        double secured=0;
        for (const auto& c:r.fundingConversions) if (c.completedAt<=checkpoints[k]) secured+=c.gems;
        if (r.readyAt<=checkpoints[k]) secured+=renewal_yield(x.postPurchaseLoops,checkpoints[k]-r.readyAt);
        r.checkpoints[k]=std::min<double>(x.scoreCapacity,secured);
    }
    r.feasible=r.readyAt<=x.horizon;
    return r;
}
} // namespace plan3
