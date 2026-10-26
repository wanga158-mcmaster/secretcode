#pragma once

namespace p11_triage {
struct RecoveryBankInput {
    int bag=0,needed=0,tribute_ticks=0,recall_ticks=0,remaining_rounds=0;
    bool fresh=false,safe=false,full_health=false,in_range=false,
         unlocked=false,channel_free=false;
};

// A sufficient tribute at the current altar can precede a full-health recall.
// These are conditional observed facts, not a forecast of enemy interruptions.
// Direct comparisons avoid overflowing a round-plus-duration calculation.
inline constexpr bool can_bank_before_recall(const RecoveryBankInput& input) {
    return input.fresh&&input.safe&&input.full_health&&input.in_range
        &&input.unlocked&&input.channel_free
        &&input.needed>0&&input.bag>=input.needed
        &&input.tribute_ticks>0&&input.recall_ticks>0
        &&input.tribute_ticks<=input.recall_ticks
        &&input.tribute_ticks<=input.remaining_rounds;
}
} // namespace p11_triage
