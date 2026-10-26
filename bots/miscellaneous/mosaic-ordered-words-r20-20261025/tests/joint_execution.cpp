#include <array>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <cassert>
#include "../src/joint_world.h"
namespace joint_world=mosaic::joint_world;
struct PureExecutor {
#define MOSAIC_JOINT_EXECUTION_PURE_ONLY
#include "../src/joint_execution.h"
#undef MOSAIC_JOINT_EXECUTION_PURE_ONLY
};
int main(){
    using E=PureExecutor;
    E::JointExecutionBudget budget{true,3,5,7,13,40,80,10,6};
    // Independently enumerate actual success and each possible first refusal.
    // Earlier successful work remains paid; the false attempt has zero effect
    // but still owes its feedback, closure, physical fallback and final yield.
    for(unsigned attempts=1;attempts<=6;++attempts){
        std::uint64_t oracle=10+40;
        for(unsigned n=0;n<attempts;++n)oracle+=3+5+7;
        for(unsigned refused=0;refused<attempts;++refused){
            std::uint64_t branch=10+13+80;
            for(unsigned n=0;n<=refused;++n)branch+=3+5+7;
            oracle=std::max(oracle,branch);
        }
        assert(E::joint_execution_reserve(budget,attempts)==oracle);
    }
    // Enough fuel to attack is insufficient for its complete physical word.
    assert(100>3+5+7&&100<E::joint_execution_reserve(budget,4));
    assert(E::joint_execution_reserve(budget,4)==163);
    // Alternative success/refusal tails are maxed, never charged together.
    auto success=budget;success.success_physical_tail=140;
    assert(E::joint_execution_reserve(success,4)==210);
    const auto invalid=std::numeric_limits<std::uint64_t>::max();
    auto overflow=budget;overflow.feedback_bound=invalid-1;
    assert(E::joint_execution_reserve(overflow,4)==invalid);
    auto undeclared=budget;undeclared.declared=false;
    assert(E::joint_execution_reserve(undeclared)==invalid);
    auto nofeedback=budget;nofeedback.feedback_bound=0;
    assert(E::joint_execution_reserve(nofeedback,1)==invalid);
    assert(E::joint_execution_reserve(budget,7)==invalid);
    auto bounded=budget;bounded.max_attempts=3;
    assert(E::joint_execution_reserve(bounded,4)==invalid);
    E::JointExecutionFallbackBudget fallback{true,100,budget,false};
    const auto repriced_word=E::joint_execution_reserve(budget,1);
    assert(E::joint_execution_fallback_reserve(fallback)==100+repriced_word);
    auto prefix=budget;prefix.refusal_physical_tail=100+repriced_word-1;
    assert(!E::joint_execution_fallback_prepaid(prefix,fallback));
    ++prefix.refusal_physical_tail;
    assert(E::joint_execution_fallback_prepaid(prefix,fallback));
    fallback.reprice_bound=invalid;
    assert(E::joint_execution_fallback_reserve(fallback)==invalid);
    assert(!E::joint_execution_fallback_prepaid(prefix,fallback));
    // A real new World/Candidate must join the actual refusal, not merely a
    // new knowledge nonce. Changing the model never refunds the input fence.
    mosaic::joint_world::World world{};world.owner=0;world.actor_count=1;
    world.frame=11;world.observation=7;world.effect=8;world.horizon_intervals=6;
    world.actors[0].position={1,1};
    mosaic::joint_world::Candidate alternative{};alternative.word_count=1;alternative.words[0].count=1;
    alternative.words[0].actions[0].kind=mosaic::joint_world::ActionKind::move;
    alternative.words[0].actions[0].point={2,1};
    E::JointExecutionRefusalContext debt{5,true,false,{1,1},{2,2},10,7,8,6,1};
    assert(E::joint_execution_fallback_matches(debt,world,alternative,5));
    auto stale=world;stale.frame=10;
    assert(!E::joint_execution_fallback_matches(debt,stale,alternative,5));
    stale=world;stale.effect=7;
    assert(!E::joint_execution_fallback_matches(debt,stale,alternative,5));
    stale=world;stale.observation=6;
    assert(!E::joint_execution_fallback_matches(debt,stale,alternative,5));
    stale=world;stale.horizon_intervals=5;
    assert(!E::joint_execution_fallback_matches(debt,stale,alternative,5));
    auto retry=alternative;retry.words[0].actions[0].point={2,2};
    assert(!E::joint_execution_fallback_matches(debt,world,retry,5));
    debt.fallback_used=true;world.frame=20;world.observation=100;world.effect=100;
    assert(!E::joint_execution_fallback_matches(debt,world,alternative,5));
    debt.fallback_used=false;
    assert(!E::joint_execution_fallback_matches(debt,world,alternative,6));
}
