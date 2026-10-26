#pragma once
#include "../../common/src/events.h"
#include <cstdint>

namespace p11_payday {
struct Rewards { int gold=0,gems=0; };
struct RewardEvents {
    Rewards payable{};
    int capture=-1,bank=-1;
};

// Supplied fixed costs must fit the shared model's integer arithmetic. This
// helper qualifies reward events, not future shopping or a full-game optimum.
// RoundUp is the same single ceiling operation in production and the fixture.
template<class RoundUp>
RewardEvents reward_events(const p11::State&s,const p11::Candidate&c,RoundUp round_up) {
    if(c.id<0||s.identity<0||s.round<0||s.limit<0||s.bag<0||s.recall<0
        ||c.travel<0||c.clear<0||c.capture<0||c.altar<0||c.allies<0
        ||c.gold<0||c.gems<0||!std::isfinite(s.tribute_multiplier)
        ||s.tribute_multiplier<0||s.tribute_base<0||s.tribute_minimum<1)return {};
    const auto maximum=std::numeric_limits<int>::max();
    const std::int64_t payout=std::int64_t(c.travel)+c.clear+c.capture;
    const std::int64_t bag=std::int64_t(s.bag)+c.gems;
    if(payout>maximum)return {};
    RewardEvents events;events.capture=int(payout);
    const std::int64_t horizon=std::int64_t(s.limit)-s.round;
    events.payable.gold=events.capture<=horizon?c.gold:0;
    // An unrepresentable bank duration is beyond every valid int horizon;
    // it cannot remove gold that already completes at capture.
    if(bag>maximum)return events;
    const double scaled=round_up(double(bag)*s.tribute_multiplier);
    if(!std::isfinite(scaled)||scaled>maximum-s.tribute_base)return events;
    const int ticks=bag>0?std::max(s.tribute_minimum,s.tribute_base+int(scaled)):0;
    const std::int64_t bank=payout+c.altar+ticks;
    if(bank>maximum)return events;
    events.bank=int(bank);
    // Earned gold belongs to capture, even when its shopping proxy extends
    // past the horizon. Gems retain the entire actual bag and queried channel.
    events.payable.gems=events.bank<=horizon?c.gems:0;
    return events;
}
inline RewardEvents reward_events(const p11::State&s,const p11::Candidate&c) {
    return reward_events(s,c,[](double value){return std::ceil(value);});
}
inline Rewards payable_rewards(const p11::State&s,const p11::Candidate&c) {
    return reward_events(s,c).payable;
}
} // namespace p11_payday
