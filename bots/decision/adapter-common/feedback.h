#pragma once
#include "bridge.h"
#include "../common/policy.h"

namespace plan3_adapter {
// SDK-free observer projection. Only the Campaign's existing exact witness is
// an exact reward; conversion=false, team score and lower bounds imply no zero.
inline Update observe(Bridge& bridge,const plan3::PolicyEvent& event) {
    if(!event.token)return Update{Status::unavailable};
    if(event.kind==plan3::PolicyEvent::Kind::committed) {
        if(event.round!=event.startedRound || event.exactConverted)return {};
        return Update{bridge.committed(event.token,event.job,event.startedRound)};
    }
    if(event.kind!=plan3::PolicyEvent::Kind::completed && event.kind!=plan3::PolicyEvent::Kind::aborted)return {};
    Feedback feedback;feedback.token=event.token;feedback.job=key(event.job);
    feedback.started=event.startedRound;feedback.ended=event.round;
    feedback.endpoint=event.kind==plan3::PolicyEvent::Kind::completed?Endpoint::completed:Endpoint::aborted;
    feedback.exactConverted=event.exactConverted;
    return bridge.endpoint(feedback);
}
} // namespace plan3_adapter
