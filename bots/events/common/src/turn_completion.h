#pragma once
namespace p11 {
enum class ClearContinuation { hold, enter_spawn };
struct ClearBoundary {
    bool reward_available=false,on_spawn=false,move_available=false,same_round=false;
};
// An attack and a compatible move are actions inside the same owner turn, not
// two indivisible turns. Use fresh SDK allowances after the lethal action.
inline ClearContinuation finish_clear(const ClearBoundary&b) {
    return b.same_round&&b.reward_available&&!b.on_spawn&&b.move_available
        ?ClearContinuation::enter_spawn:ClearContinuation::hold;
}
}
