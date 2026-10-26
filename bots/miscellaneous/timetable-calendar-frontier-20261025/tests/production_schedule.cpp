#include "production_schedule.h"
#include "production_comparison.h"
#include "service_price.h"
#include <array>
#include <cstdio>
#include <limits>
namespace {
int failures{},checks{};
void check(bool value,const char* label)
{ ++checks;if(!value){++failures;std::printf("FAIL %s\n",label);} }

void retained_acquisition_contract()
{
    using namespace tt::production_schedule;
    namespace comparison=tt::production_comparison;
    namespace pricing=mb::production::service;
    const std::array group_a{pricing::Member{0,{0,0},120,0}};
    const std::array group_b{pricing::Member{1,{0,0},40,0},pricing::Member{2,{2,0},40,0}};
    pricing::Pricer price_a{group_a,{40,0}},price_b{group_b,{40,0}};
    std::array<std::uint8_t,130> held{},family{};
    const comparison::Key key{7,11};
    comparison::Comparison equality{held.size(),family.size(),key};
    enum class Phase { Compare,PriceA,PriceB,Publish,WaitingForNewFacts };
    Phase phase=Phase::Compare;
    int best_group=-1,nomination=0,observer_calls=0,service_calls=0,escape_calls=0;
    std::uint64_t best_work=std::numeric_limits<std::uint64_t>::max();
    const auto escaped=enter({true,true,true,true,true,true},[&]{++escape_calls;return true;});
    check(escaped==Gate::Handled && escape_calls==1 && equality.compared()==0 &&
          price_a.completed_units()==0,"escape preempts acquisition without consuming query computation");
    check(enter({true,false,true,true,true,true},[&]{++escape_calls;return true;})==Gate::YieldToOwner &&
          escape_calls==1,"unknown escape retains priority and cannot fall through to acquisition");
    int owner_inputs=0;
    for(;owner_inputs<static_cast<int>(2*pricing::Pricer::max_work_units+8) && phase!=Phase::WaitingForNewFacts;++owner_inputs)
    {
        // New owner rounds do not recreate the held packet, prices, rank or
        // comparison. Acquisition precedes even older pending route machinery.
        const auto gate=enter({false,true,true,true,true,true},[]{return true;});
        if(gate==Gate::ContinueService){++observer_calls;++service_calls;}
        check(gate==Gate::Acquisition,"every retained query input owns the shared gate before observer or service");
        if(gate!=Gate::Acquisition)continue;
        switch(phase)
        {
        case Phase::Compare:
            equality.advance(held,family,key,64);
            check(equality.last_work()<=64,"actual comparison work is limited to sixty-four bytes per input");
            if(equality.status()==comparison::Status::Equal)phase=Phase::PriceA;
            break;
        case Phase::PriceA:
        {
            const auto before=price_a.completed_units();
            price_a.advance(1);
            check(price_a.last_work()==1 && price_a.completed_units()==before+1,"first retained Pricer advances one actual work unit");
            if(price_a.status()==pricing::Status::Ready)
            {best_group=0;best_work=price_a.result().basic_attacks;phase=Phase::PriceB;}
            break;
        }
        case Phase::PriceB:
        {
            check(best_group==0 && best_work==3,"completed first candidate rank survives later owner inputs");
            const auto before=price_b.completed_units();
            price_b.advance(1);
            check(price_b.last_work()==1 && price_b.completed_units()==before+1,"second retained Pricer advances without rebuilding first candidate");
            if(price_b.status()==pricing::Status::Ready)
            {
                if(price_b.result().basic_attacks<best_work){best_group=1;best_work=price_b.result().basic_attacks;}
                phase=Phase::Publish;
            }
            break;
        }
        case Phase::Publish:
            ++nomination;phase=Phase::WaitingForNewFacts;
            break;
        case Phase::WaitingForNewFacts: break;
        }
        check(observer_calls==0 && service_calls==0,"no optional observer or service runs before complete nomination");
    }
    check(phase==Phase::WaitingForNewFacts && owner_inputs>3 && equality.compared()==130 &&
          price_a.status()==pricing::Status::Ready && price_b.status()==pricing::Status::Ready &&
          best_group==1 && best_work==2 && nomination==1 && observer_calls==0 && service_calls==0,
          "real comparison and both price cursors finish across rounds with one complete retained nomination");

    // WaitingForNewFacts has no computation owner. This caller chooses one fresh
    // observer input, freezes the new packet and gives its refresh comparison the
    // following inputs; an incomplete comparison never reopens the observer.
    const auto waiting=enter({false,true,false,false,false,false},[]{return true;});
    if(waiting==Gate::ContinueService)++observer_calls;
    check(waiting==Gate::ContinueService && observer_calls==1,"WaitingForNewFacts permits one caller-owned fresh observer");
    family[129]=1;
    const comparison::Key refreshed_key{8,11};
    comparison::Comparison refresh{held.size(),family.size(),refreshed_key};
    for(int input=0;input<3;++input)
    {
        const auto gate=enter({false,true,false,false,false,true},[]{return true;});
        if(gate==Gate::ContinueService){++observer_calls;++service_calls;}
        if(gate==Gate::Acquisition)refresh.advance(held,family,refreshed_key,64);
        check(gate==Gate::Acquisition && observer_calls==1 && service_calls==0 && refresh.last_work()<=64,
              "fresh packet refresh comparison retains input ownership until all compared bytes resolve");
        if(input<2)check(refresh.status()==comparison::Status::NeedCompute,"partial refresh cannot trigger another fresh observer");
    }
    check(refresh.status()==comparison::Status::Different && refresh.compared()==130 && refresh.last_work()==2 &&
          observer_calls==1 && nomination==1,"late fresh-facts difference is complete while prior nomination remains a retained fact");
}
}
int main()
{
    using namespace tt::production_schedule;
    bool assembly=true,pending_route=false;
    int cursor=0,plan_calls=0,publication=0,service_reads=0,global_reads=0,exit_calls=0;
    // The same retained assembly yields twice, publishes once, then its route
    // computation yields once and publishes. The actual gate runs BEFORE either
    // observer and prevents a fresh service packet replacing partial progress.
    for(int input=0;input<5;++input)
    {
        // An older positive route cannot steal the quiet assembly's input.
        const auto gate=enter({false,true,assembly,pending_route,true},[&]{++exit_calls;return true;});
        if(gate==Gate::ContinueService) { ++service_reads; ++global_reads; }
        else if(gate==Gate::YieldToOwner && assembly)
        { if(++cursor==3){assembly=false;pending_route=true;} }
        else if(gate==Gate::YieldToOwner && pending_route)
        { if(++plan_calls==2){pending_route=false;++publication;} }
    }
    check(cursor==3 && plan_calls==2 && publication==1 && !assembly && !pending_route,
          "retained planning progress finishes exactly once across five inputs");
    check(service_reads==0 && global_reads==0 && exit_calls==0,
          "pending planning owns inputs before any service/global observation");
    check(enter({false,true,false,false,true},[]{return true;})==Gate::Transit,
          "finite positive work route has an explicit transit disposition");

    // The quiet owner above published once. Its exposed approach has inputs
    //3->2->1->0 remaining. Each positive input runs exactly one cheap transit
    // callback; neither service/global observer may run before the frontier.
    int distance=3,transit_calls=0;
    auto transit=[&]{++transit_calls;--distance;};
    for(int input=0;input<4;++input)
    {
        const int before=distance;
        const auto approach=enter({false,true,assembly,pending_route,distance>0},
                                  [&]{++exit_calls;return true;});
        if(approach==Gate::Transit) transit();
        else if(approach==Gate::ContinueService) {++service_reads;++global_reads;}
        if(before>0)
            check(approach==Gate::Transit && distance==before-1 && transit_calls==input+1 &&
                  service_reads==0 && global_reads==0,
                  "each exposed positive-distance input moves once before any observer");
        else
            check(approach==Gate::ContinueService && distance==0 && transit_calls==3 &&
                  service_reads==1 && global_reads==1,
                  "frontier input observes service once after the three transit steps");
    }
    check(publication==1 && cursor==3 && plan_calls==2 && transit_calls==3 && distance==0,
          "quiet assembly publishes once and its retained route reaches the frontier");
    // Reset counters for the retained withdrawal sequence below.
    service_reads=global_reads=0;
    bool escape_required=false;
    check(withdraw_authority(false,escape_required) && escape_required,
          "a deferred authority input commits egress rather than global observation");
    const auto gate=enter({escape_required,true,true,true,true},[&]{++exit_calls;return true;});
    if(gate==Gate::ContinueService) ++service_reads;
    check(gate==Gate::Handled && exit_calls==1 && service_reads==0,
          "withdrawal's next input attempts egress before retained planning or observation");
    check(!withdraw_authority(true,escape_required) && escape_required,
          "a later ready result cannot silently clear a prior escape commitment");
    check(enter({true,true,true,false,false},[]{return false;})==Gate::YieldToOwner,
          "actual quiet completion yields ownership rather than entering service in same gate");
    check(enter({true,false,false,false,true},[&]{++exit_calls;return true;})==Gate::YieldToOwner && exit_calls==1,
          "unknown escape witness retains ownership without invoking transit or escape");
    check(enter({false,true,true,false,true},[]{return true;})==Gate::YieldToOwner &&
          enter({false,true,false,true,true},[]{return true;})==Gate::YieldToOwner,
          "retained assembly and pending route each override positive transit");
    retained_acquisition_contract();
    std::printf("production_schedule checks=%d failures=%d\n",checks,failures);
    return failures?1:0;
}
