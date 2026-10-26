#include <cstdint>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <array>
#include "joint_world.h"

namespace joint_world=mosaic::joint_world;

// Exercise the exact resource helper included by Runtime, without SDK names,
// runtime stubs or a second copy of its admission arithmetic.
struct Pure {
#define MOSAIC_JOINT_CONTACT_PURE_ONLY
#include "../src/joint_contact.h"
#undef MOSAIC_JOINT_CONTACT_PURE_ONLY
};
struct PureExecution {
#define MOSAIC_JOINT_EXECUTION_PURE_ONLY
#include "../src/joint_execution.h"
#undef MOSAIC_JOINT_EXECUTION_PURE_ONLY
};
using Funding=Pure::JointContactFunding;
using Budget=Pure::JointContactBudget;
using Fuel=std::uint64_t;
unsigned checks=0;
void check(bool ok,int line){++checks;if(!ok){std::fprintf(stderr,"joint-contact failed %d check%u\n",line,checks);std::abort();}}
#define CHECK(x) check(bool(x),__LINE__)

// Independent sequential-expenditure oracle. Every complete operation leaves
// positive fuel; no addition, saturated sum or helper result is reused here.
template<std::size_t N> bool sequential(Fuel fuel,const std::array<Fuel,N>& costs){
    for(const auto cost:costs){if(cost>=fuel)return false;fuel-=cost;}return true;
}
void first_quote_must_be_funded_once(){
    constexpr Fuel command=39000,evaluation=16000;
    Funding funding{command,evaluation,false};
    CHECK(funding.retained()==55000);
    for(Fuel fuel=39000;fuel<=55000;++fuel){
        CHECK(!funding.price(fuel)&&!funding.executable(fuel));
        CHECK(!sequential(fuel,std::array<Fuel,2>{evaluation,command}));
    }
    // The first16000 evaluation is part of this55000 tail, not an additional
    // second evaluation charged on top of it by a preparation gate.
    Fuel actual=55001;CHECK(funding.price(actual));
    CHECK(sequential(actual,std::array<Fuel,2>{evaluation,command}));
    CHECK(!funding.preparation(actual,evaluation)); // this would charge71000
    CHECK(!funding.executable(actual)); // a funded quote has not yet been made
    actual-=evaluation;funding.incumbent=true;
    CHECK(actual==39001&&funding.retained()==command&&funding.executable(actual));
    CHECK(!funding.price(actual)); // a sibling still needs its own full evaluation
    actual-=command;CHECK(actual==1); // actual paid prefix and physical tail fit
}
void preparation_retains_first_quote(){
    constexpr Fuel command=39000,evaluation=16000,query=3000,enumeration=7000;
    Funding funding{command,evaluation,false};
    CHECK(!funding.preparation(58000,query)&&funding.preparation(58001,query));
    CHECK(!funding.preparation(62000,enumeration)&&funding.preparation(62001,enumeration));
    Fuel actual=58001;
    CHECK(sequential(actual,std::array<Fuel,3>{query,evaluation,command}));
    CHECK(funding.preparation(actual,query));actual-=query;
    CHECK(actual==55001&&funding.price(actual));actual-=evaluation;funding.incumbent=true;
    CHECK(funding.executable(actual));
    // After actual evaluation the preparation reserve shrinks to the physical
    // tail. Nothing has earned permission to charge the paid16000 again.
    CHECK(!funding.preparation(42000,query)&&funding.preparation(42001,query));
    CHECK(!funding.preparation(46000,enumeration)&&funding.preparation(46001,enumeration));
    CHECK(!funding.price(55000)&&funding.price(55001));

    constexpr std::array<Fuel,7> costs{3000,7000,2000,9000,4000,5000,1000};
    funding.incumbent=false;actual=80000;unsigned completed=0;
    for(const auto cost:costs){
        const bool oracle=sequential(actual,std::array<Fuel,3>{cost,evaluation,command});
        CHECK(funding.preparation(actual,cost)==oracle);
        if(!oracle)break;actual-=cost;++completed;
        CHECK(funding.retained()==55000&&funding.price(actual)&&!funding.executable(actual));
    }
    CHECK(completed>0&&completed<costs.size());
    CHECK(funding.price(actual));actual-=evaluation;funding.incumbent=true;
    CHECK(funding.executable(actual));
}
void stopped_siblings_preserve_paid_incumbent(){
    Funding funding{39000,16000,false};Fuel actual=55001;
    unsigned evaluations=0,selected=std::numeric_limits<unsigned>::max();
    CHECK(funding.price(actual));actual-=16000;++evaluations;selected=31;funding.incumbent=true;
    // A sibling query, a path enumeration and another model quote all stop.
    // They neither erase the selected record nor reevaluate its paid score.
    if(funding.preparation(actual,1000)){actual-=1000;selected=99;}
    if(funding.preparation(actual,7000)){actual-=7000;selected=98;}
    if(funding.price(actual)){actual-=16000;++evaluations;selected=97;}
    CHECK(selected==31&&evaluations==1&&actual==39001&&funding.incumbent);
    CHECK(funding.retained()==39000&&funding.executable(actual));
    // The same stop at a less marginal entry also preserves the incumbent.
    actual=46000;CHECK(!funding.preparation(actual,7000));
    CHECK(selected==31&&evaluations==1&&funding.executable(actual));
}
void independent_range_and_invalid_bounds(){
    constexpr Fuel command=39000,evaluation=16000;
    for(Fuel fuel=0;fuel<=85000;fuel+=137)for(Fuel cost:std::array<Fuel,4>{0,1,3000,11000}){
        Funding funding{command,evaluation,false};
        CHECK(funding.price(fuel)==sequential(fuel,std::array<Fuel,2>{evaluation,command}));
        CHECK(funding.preparation(fuel,cost)==sequential(fuel,std::array<Fuel,3>{cost,evaluation,command}));
        CHECK(!funding.executable(fuel));
        funding.incumbent=true;
        CHECK(funding.preparation(fuel,cost)==sequential(fuel,std::array<Fuel,2>{cost,command}));
        CHECK(funding.executable(fuel)==(fuel>command));
    }
    const auto maximum=std::numeric_limits<Fuel>::max();
    for(auto funding:std::array<Funding,5>{{{0,evaluation,false},{command,0,false},
            {maximum,1,false},{1,maximum,false},{maximum-3,8,false}}}){
        CHECK(!funding.price(maximum)&&!funding.preparation(maximum,0));
    }
    Funding valid{command,evaluation,false};
    CHECK(!valid.preparation(maximum,maximum));
    CHECK(!valid.preparation(maximum,maximum-50000));
    Budget declaration{command,3000,7000,evaluation,true};CHECK(Funding::valid(declaration));
    auto declined=declaration;declined.declared=false;CHECK(!Funding::valid(declined));
    for(unsigned zero=0;zero<4;++zero){
        auto missing=declaration;
        if(zero==0)missing.command_tail=0;else if(zero==1)missing.query_bound=0;
        else if(zero==2)missing.enumeration_bound=0;else missing.evaluation_bound=0;
        CHECK(!Funding::valid(missing));
    }
    auto overflowing=declaration;overflowing.command_tail=maximum-3;overflowing.evaluation_bound=8;
    CHECK(!Funding::valid(overflowing));
    Budget unknown{};CHECK(!Funding::valid(unknown));
    Funding absent{unknown.command_tail,unknown.evaluation_bound,false};
    CHECK(!absent.price(maximum)&&!absent.preparation(maximum,1)&&!absent.executable(maximum));
    // cost0 is deliberately a reserve-only post-check used by the SDK adapter;
    // unknown/zero command or evaluation declarations above remain rejected.
    CHECK(!valid.preparation(55000,0)&&valid.preparation(55001,0));
}
void family_build_retains_actual_callback_count(){
    const PureExecution::JointExecutionBudget execution{true,1500,2000,1500,3000,4000,32000,2000,4};
    const Fuel one=PureExecution::joint_execution_reserve(execution,1);
    const Fuel two=PureExecution::joint_execution_reserve(execution,2);
    const Fuel four=PureExecution::joint_execution_reserve(execution,4);
    CHECK(one==42000&&two==47000&&four==57000);
    // The same complete failure alternative remains funded for each count;
    // pricing fewer callbacks removes only their validation/action/feedback.
    CHECK(one-5000==37000&&two-10000==37000&&four-20000==37000);
    constexpr Fuel evaluation=16000,build=6000,entry=70000;
    Funding overreserved{four,evaluation,false},actual_callbacks{two,evaluation,false};
    CHECK(!overreserved.family(entry,build)&&actual_callbacks.family(entry,build));
    CHECK(!overreserved.family(79000,build)&&overreserved.family(79001,build));
    CHECK(!actual_callbacks.family(69000,build)&&actual_callbacks.family(69001,build));
    // Independent sequential path: build, evaluate, two actual callbacks,
    // refusal refresh, the SAME32K fallback and final closure. Total69K.
    constexpr std::array<Fuel,11> actual_path{6000,16000,1500,2000,1500,1500,2000,1500,3000,32000,2000};
    CHECK(sequential(entry,actual_path)&&!sequential(69000,actual_path));
    constexpr std::array<Fuel,17> four_path{6000,16000,1500,2000,1500,1500,2000,1500,
        1500,2000,1500,1500,2000,1500,3000,32000,2000};
    Funding real_four{four,evaluation,true};
    CHECK(!sequential(entry,four_path)&&!real_four.family(entry,build));

    Funding incumbent{one,evaluation,true};CHECK(incumbent.retained()==42000);
    // A new two-callback family preserves the larger of old/new command
    // tails, then one NEW evaluation; the old incumbent score is already paid.
    Funding next_family{std::max(incumbent.command,two),evaluation,true};
    CHECK(next_family.command==47000&&next_family.family(entry,build));
    CHECK(incumbent.retained()==42000&&incumbent.executable(entry));
    Fuel remaining=entry-build;CHECK(next_family.price(remaining));remaining-=evaluation;
    CHECK(remaining==48000&&next_family.executable(remaining)&&incumbent.executable(remaining));
    CHECK(incumbent.retained()==42000); // preparing a sibling did not replace it
    CHECK(!sequential(entry,std::array<Fuel,4>{build,evaluation,evaluation,two}));
    Budget declaration{two,1500,build,evaluation,true};CHECK(Funding::valid(declaration));
    declaration.command_tail=four;CHECK(Funding::valid(declaration));
    CHECK(PureExecution::joint_execution_reserve(execution,5)==std::numeric_limits<Fuel>::max());
}
int main(){
    first_quote_must_be_funded_once();preparation_retains_first_quote();
    stopped_siblings_preserve_paid_incumbent();independent_range_and_invalid_bounds();family_build_retains_actual_callback_count();
    std::printf("%u joint-contact checks passed\n",checks);
}
