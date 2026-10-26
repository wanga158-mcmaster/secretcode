// Actual bank forecast with modeled SDK state. This proves incomplete-bank
// refusal and fresh retry semantics, not native fuel safety or game utility.
#if defined(__wasm__)
#undef __wasm__
#include <cambc.h>
#define __wasm__ 1
#else
#include <cambc.h>
#endif
#define PLAN3_BANK_MATERIALIZATION_FENCE 1
#include "../common/forecasts.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "backpressure-config-v1.h"
std::int64_t cache_fuel();
#include "backpressure-host-v1.h"
namespace {
int cost_zero_item=-1;
bool bp_hidden_collision=false;
std::uint64_t bp_camp_mask(){return 0;}
unsigned fence_calls=0,deny_at=0;
}
std::int64_t cache_fuel() {
    ++fence_calls;
    return deny_at && fence_calls>=deny_at?44000:1000000;
}
int main() {
    using namespace plan3_runtime;
    reset();bag=8;
    PersonalInformation personal;StaticPaths paths;
    personal.width=8;personal.spawn_positions={{0,0}};
    cambc::bitboard spawn;spawn.set({0,0});personal.own_spawn=spawn;
    paths.initialize({});personal.spawn_field=paths.add(spawn);
    cambc::bitboard pad;pad.set({5,5});
    personal.altars={{6,6}};personal.altar_fields={paths.add(pad)};
    const auto group=cambc::detail::handle_access::make_camp_group(
        static_cast<cambc::handle_camp_group_t>(0),30,22,40,{});
    personal.groups.push_back({group,pad,{5,5},45,personal.altar_fields[0],
        100,0,false,22,30,40,{{5,5}},{{{5,5},100,0}}});
    personal.observations.resize(1);
    for(unsigned i=0;i<16;++i){paths.begin_turn();paths.advance();}
    verify(paths.ready());
    plan3::InformationState state;state.round=32;state.roundLimit=2000;
    state.carriedGems=8;state.health=state.maximumHealth=1000;
    const auto context=[&] {
        ForecastContext c(personal,paths,state);c.terminal_renewal_loops=false;
        c.uncertain_group=0;return c;
    };
    auto good=context();fence_calls=0;
    const auto complete=good.bank(0);const unsigned full_calls=fence_calls;
    verify(complete.complete && !good.cutoff && complete.outcomes.size()>=4);
    verify(full_calls>4);
    for(const auto& o:complete.outcomes)verify(o.deltaS==8 && o.conversions.size()==1);
    for(unsigned stop=1;stop<=full_calls;++stop) {
        auto trial=context();fence_calls=0;deny_at=stop;
        const auto refused=trial.bank(0);
        verify(trial.cutoff && !refused.complete && refused.outcomes.empty());
        // A fresh next callback can construct the complete unchanged forecast.
        deny_at=0;fence_calls=0;auto next=context();const auto resumed=next.bank(0);
        verify(resumed.complete && resumed.outcomes.size()==complete.outcomes.size());
        for(unsigned i=0;i<complete.outcomes.size();++i) {
            verify(resumed.outcomes[i].tau==complete.outcomes[i].tau);
            verify(resumed.outcomes[i].deltaS==complete.outcomes[i].deltaS);
            verify(resumed.outcomes[i].response==complete.outcomes[i].response);
            verify(resumed.outcomes[i].lifecycle==complete.outcomes[i].lifecycle);
        }
    }
    std::printf("{\"checks\":%u,\"fenceCases\":%u,\"nativeLaunches\":0}\n",checks,full_calls);
}
