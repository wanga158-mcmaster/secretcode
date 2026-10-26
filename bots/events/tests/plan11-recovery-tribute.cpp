#include <array>
#include <cassert>
#include <limits>
#include <type_traits>
#include "../common/src/event_kernel.h"
#include "../triage/src/policy.h"

#ifdef NDEBUG
#error This fixture requires assertions enabled.
#endif

using Policy=p11_triage::Policy;
using Input=Policy::RecoveryBankInput;

namespace {
constexpr Input witness{26,17,3,5,128,true,true,true,true,true,true};

// The initiating owner turn ends once before the next owner's start. A timer
// reaching zero at owner end still requires the following owner-start event.
int completion_round(p11::kernel::ChannelSnapshot& channel,int start,int bag) {
    using namespace p11::kernel;
    for(int round=start;round<start+8;++round) {
        if(round!=start) {
            const auto result=owner_start(channel,bag);
            if(!preserve_channel(channel)) {
                if(result.recall_arrival)assert(result.banked==0);
                else assert(result.banked==bag);
                return round;
            }
        }
        assert(preserve_channel(channel));
        assert(owner_end(channel).status==Status::applied);
        assert(preserve_channel(channel));
    }
    assert(false);
    return -1;
}
} // namespace

int main() {
    static_assert(std::is_same_v<Input,p11_triage::RecoveryBankInput>);
    static_assert(&Policy::bank==&p11_tempo::Policy::bank);
    static_assert(&Policy::retreat==&p11_tempo::Policy::retreat);
    static_assert(Policy::can_bank_before_recall(witness));
    static_assert(!Policy::can_bank_before_recall(Input{}));

    // Retained state: full-health Varg at an altar, 26 held and 17 needed.
    // This confirms the conditional clock, not uninterrupted engine execution.
    using namespace p11::kernel;
    ChannelSnapshot tribute{ChannelKind::tribute,witness.tribute_ticks,witness.bag};
    ChannelSnapshot recall{ChannelKind::recall,witness.recall_ticks,witness.bag};
    assert(completion_round(tribute,1872,witness.bag)==1875);
    assert(completion_round(recall,1872,witness.bag)==1877);
    assert(tribute.bag==0&&recall.bag==26);

    // Every missing or false observation is conservative, independently.
    const std::array<bool Input::*,6> flags{
        &Input::fresh,&Input::safe,&Input::full_health,&Input::in_range,
        &Input::unlocked,&Input::channel_free
    };
    for(const auto flag:flags) {
        auto input=witness;input.*flag=false;
        assert(!Policy::can_bank_before_recall(input));
    }
    Input missing{26,17,3,5,128};
    for(const auto flag:flags)assert(!(missing.*flag));
    assert(!Policy::can_bank_before_recall(missing));

    auto input=witness;
    input.bag=16;assert(!Policy::can_bank_before_recall(input));
    input.bag=17;assert(Policy::can_bank_before_recall(input));
    input.bag=-1;assert(!Policy::can_bank_before_recall(input));
    input=witness;input.needed=0;assert(!Policy::can_bank_before_recall(input));
    input.needed=-1;assert(!Policy::can_bank_before_recall(input));
    input=witness;input.recall_ticks=3;assert(Policy::can_bank_before_recall(input));
    input.recall_ticks=2;assert(!Policy::can_bank_before_recall(input));
    input=witness;input.remaining_rounds=3;assert(Policy::can_bank_before_recall(input));
    input.remaining_rounds=2;assert(!Policy::can_bank_before_recall(input));

    // Integer extremes must be handled by comparisons, without additions or
    // subtraction that can wrap a deadline or make an invalid duration legal.
    constexpr int low=std::numeric_limits<int>::min();
    constexpr int high=std::numeric_limits<int>::max();
    const std::array<int Input::*,5> integers{
        &Input::bag,&Input::needed,&Input::tribute_ticks,
        &Input::recall_ticks,&Input::remaining_rounds
    };
    for(const auto field:integers) {
        input=witness;input.*field=low;
        assert(!Policy::can_bank_before_recall(input));
    }
    for(const auto field:{&Input::tribute_ticks,&Input::recall_ticks,&Input::remaining_rounds}) {
        input=witness;input.*field=0;
        assert(!Policy::can_bank_before_recall(input));
        input.*field=-1;
        assert(!Policy::can_bank_before_recall(input));
    }
    input=witness;input.bag=high;input.needed=high;
    assert(Policy::can_bank_before_recall(input));
    input.tribute_ticks=high;input.recall_ticks=high;input.remaining_rounds=high;
    assert(Policy::can_bank_before_recall(input));
    input.remaining_rounds=high-1;assert(!Policy::can_bank_before_recall(input));
    input=witness;input.recall_ticks=high;input.remaining_rounds=high;
    assert(Policy::can_bank_before_recall(input));

    // The option is pure and does not replace or cancel any existing channel,
    // including a zero timer waiting for its next owner-start boundary.
    for(const auto kind:{ChannelKind::recall,ChannelKind::tribute,ChannelKind::teleport}) {
        for(const int remaining:{0,3}) {
            const ChannelSnapshot active{kind,remaining,26};
            const auto saved=active;
            input=witness;input.channel_free=false;
            assert(!Policy::can_bank_before_recall(input));
            assert(active.kind==saved.kind&&active.remaining==saved.remaining
                   &&active.bag==saved.bag&&preserve_channel(active));
        }
    }
}
