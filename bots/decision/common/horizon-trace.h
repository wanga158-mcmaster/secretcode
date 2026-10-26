#pragma once
#include "model.h"
#include "telemetry.h"

namespace plan3_runtime {
// Compact lossless witness, within the existing frame budget. Trace slots are:
// [selected JobId, companion JobId, [S,enemyS,T,winning,teamSize], lastBlockEnd,
//  four [ownDelta,enemyDelta,personalDelta,coverage] blocks,
//  two [tau,deltaS,[[conversionTime,gems],...]] nominal forecasts].
// All block starts/ends follow from lastBlockEnd and the fixed32-round width.
inline TelemetryFields horizon_trace_fields(const plan3::InformationState& state,
        const std::vector<plan3::ScoreBlock>& history,const std::array<plan3::JobId,2>& jobs,
        const std::array<const plan3::Outcome*,2>& forecasts,bool assumed,bool fork) {
    TelemetryFields fields;
    if(!plan3::horizon_ready(history,state.round))return fields;
    for(const auto* forecast:forecasts)if(!forecast || forecast->conversions.size()>2)return fields;
    fields<<",\"warmup\":false,\"share_assumed\":"<<assumed<<",\"fork\":"<<fork<<",\"trace\":[";
    for(unsigned i=0;i<2;++i) {
        if(i)fields<<',';
        const auto& id=jobs[i];
        fields<<'['<<static_cast<int>(id.kind)<<','<<id.firstGroup<<','<<id.secondGroup<<','<<id.altarTile<<','<<id.equipmentBlock<<']';
    }
    fields<<",["<<state.ownPermanentScore<<','<<state.opponentPermanentScore<<','<<state.roundLimit<<','
        <<state.winningGems<<','<<state.teamSize<<"],"<<history.back().endRound<<",[";
    for(std::size_t i=history.size()-4;i<history.size();++i) {
        if(i!=history.size()-4)fields<<',';
        const auto& b=history[i];
        fields<<'['<<b.ownIncrement<<','<<b.opponentIncrement<<','<<b.personallyCompletedSubmissions<<','
            <<int(b.personalCoverageComplete)<<']';
    }
    fields<<"],[";
    for(unsigned i=0;i<2;++i) {
        if(i)fields<<',';
        const auto& n=*forecasts[i];
        fields<<'['<<n.tau<<','<<n.deltaS<<",[";
        for(std::size_t c=0;c<n.conversions.size();++c) {
            if(c)fields<<',';
            fields<<'['<<n.conversions[c].completedAt<<','<<n.conversions[c].gems<<']';
        }
        fields<<"]]";
    }
    fields<<"]]";
    return fields;
}
} // namespace plan3_runtime
