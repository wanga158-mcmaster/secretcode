// Actual current capital/forecast code; synthetic SDK metadata and 8x8 map.
#if defined(__wasm__)
#undef __wasm__
#include <cambc.h>
#define __wasm__ 1
#else
#include <cambc.h>
#endif
#include "../common/capital.h"
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
plan3_runtime::TreasuryPlanner* watching=nullptr;
bool stop_scan=false;
}
std::int64_t cache_fuel() {
    if(watching && stop_scan) {
        const auto& cache=watching->post_purchase_loops_[0];
        const auto found=cache.entries.find(18);
        if(found!=cache.entries.end() && !found->second.complete && found->second.cursor>=1)return 44000;
    }
    return fuel;
}
namespace {
using namespace plan3_runtime;
cambc::bitboard mask(cambc::vec2 p){cambc::bitboard b;b.set(p);return b;}
struct World {
    PersonalInformation personal;
    StaticPaths paths;
    TreasuryPlanner planner;
    World() {
        personal.width=8;personal.team_size=1;personal.own_spawn=mask({0,0});personal.spawn_positions={{0,0}};
        paths.initialize({});personal.spawn_field=paths.add(personal.own_spawn);
        personal.altars={{6,6}};personal.altar_fields={paths.add(mask({5,5}))};
        for(int i=0;i<3;++i) {
            const cambc::vec2 p{4,2+i};auto region=mask(p);
            auto group=cambc::detail::handle_access::make_camp_group(
                static_cast<cambc::handle_camp_group_t>(i),30,22,40,{});
            personal.groups.push_back({group,region,p,p.y*8+p.x,paths.add(region),100,0,false,22,30,40,{p},{{p,100,0}}});
        }
        personal.observations.resize(3);
        for(int i=0;i<16;++i){paths.begin_turn();paths.advance();}verify(paths.ready());
        while(!planner.advance_catalogue(4)){}
        planner.enable_design_reuse();
    }
    ForecastContext context(int cash) {
        plan3::InformationState s;s.round=current_round;s.roundLimit=current_round+128;s.winningGems=1000;
        s.ownPermanentScore=100;s.health=s.maximumHealth=1000;s.gold=cash;
        ForecastContext c(personal,paths,s);c.strength=10;c.focus=0;c.recall=5;
        return c;
    }
};
}
int main() {
    reset();gold=1000;World w;watching=&w.planner;
    auto c=w.context(1000);(void)c.terminal({2,2},1000);verify(!c.cutoff);
    stop_scan=true;
    const auto stopped=w.planner.build_schedules(c,{});
    verify(!stopped.complete && stopped.work.cutoff && stopped.proposals.empty());
    verify(stopped.work.unknown.code==CapitalUnknown::loopsAfterCutoff);
    const auto& first=w.planner.post_purchase_loops_[0].entries.at(18);
    verify(first.cursor==1 && !first.complete && first.loops.size()==1);
    const auto partial_signature=w.planner.post_purchase_loops_[0].signature;

    stop_scan=false;c.cutoff=false;
    const auto resumed=w.planner.build_schedules(c,{});
    verify(resumed.complete && !resumed.work.cutoff && resumed.work.targets==4);
    verify(w.planner.post_purchase_loops_[0].entries.at(18).complete);
    verify(w.planner.post_purchase_loops_[0].entries.at(18).cursor==3);
    verify(w.planner.post_purchase_loops_[0].signature==partial_signature);
    for(const auto& cache:w.planner.post_purchase_loops_)verify(!cache.entries.empty());

    // Newly owned Power changes hypothetical Monster gear. Its old completed
    // loops must be invalidated, then a fresh interrupted prefix retained.
    ++items[static_cast<unsigned>(cambc::item_type::power_shard_of_strength)];
    stop_scan=true;c.cutoff=false;
    const auto changed=w.planner.build_schedules(c,{});
    verify(!changed.complete && changed.work.unknown.code==CapitalUnknown::loopsAfterCutoff);
    verify(w.planner.post_purchase_loops_[0].signature!=partial_signature);
    verify(w.planner.post_purchase_loops_[0].entries.at(18).cursor==1);
    verify(!w.planner.post_purchase_loops_[0].entries.at(18).complete);

    // No cached proposal/funding cash: insufficient CURRENT cash and no
    // funding menu must not reuse an earlier affordable purchase.
    stop_scan=false;c.cutoff=false;c.info.gold=0;gold=0;
    const auto broke=w.planner.build_schedules(c,{});
    verify(broke.complete && broke.proposals.empty() && broke.work.loopRequests==0);
    for(const auto& s:broke.schedules)if(s.block!=plan3::InvestmentBlock::defer)verify(!s.forecast.available);
    std::printf("{\"checks\":%u,\"release\":true,\"nativeLaunches\":0}\n",checks);
}
