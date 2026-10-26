#pragma once
#include <algorithm>
#include <cstdint>

namespace heuristics {
// Missing a payout after a full capture interval is evidence of a hidden
// contest, even when no hostile holder is in personal vision. Call only for
// an actual reward-available hold. Explicitly reset on departure, death or a
// changed lifecycle; empty enemy vision is not a reset condition.
class CaptureWatch {
    int source_=-1,started_=0;
public:
    int source()const{return source_;}
    void reset(){source_=-1;started_=0;}
    bool expired(int source,int round,int capture,int slack=2) {
        if(source<0){reset();return false;}
        if(source_!=source||round<started_){source_=source;started_=round;}
        const auto elapsed=std::int64_t(round)-started_;
        const auto allowed=std::int64_t(std::max(1,capture))+std::max(0,slack);
        return elapsed>=allowed;
    }
};
} // namespace heuristics
