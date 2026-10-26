#include "production_capture.h"
#include <array>
#include <cstdio>

namespace {
using namespace tt::production_capture;
int checks{},failures{};
void check(bool condition,const char* label)
{ ++checks;if(!condition){++failures;std::printf("FAIL %s\n",label);} }
constexpr auto unknown=std::numeric_limits<std::uint16_t>::max();
constexpr int group=6;
constexpr std::uint64_t graph=0x10000002a;
const Point origin{1,5},first{2,5},root{4,5};
std::vector<std::uint16_t> field()
{
    std::vector<std::uint16_t> values(100,unknown);
    values[51]=3;values[52]=2;values[53]=1;values[54]=0;
    return values;
}
CaptureWord word(std::size_t max_nodes=4) {return {10,10,group,graph,field(),max_nodes};}
void finish(CaptureWord& value){while(value.status()==Status::NeedCompute)value.advance(1);}
}

int main()
{
    auto retained=word();
    check(retained.status()==Status::Unknown && retained.path().empty() && !retained.leg(origin,group,graph,true),
          "dated field alone publishes no live or partial capture path");
    // Two exposed Capture inputs retain one owned geometric operator. The static
    // potential may remain positive after clear; actual hazards/safe waits are
    // deliberately not arguments or conclusions of this pure component.
    constexpr bool immutable_quiet=false;
    int begins=0,advances=0;
    for(int input=0;input<2;++input)
    {
        check(!immutable_quiet && retained.begin(origin,group,graph)==Status::NeedCompute,
              "same Capture origin retains staged build despite positive immutable potential");
        ++begins;retained.advance(1);++advances;
        check(retained.built_nodes()==static_cast<std::size_t>(input+1) && retained.path().empty() &&
              !retained.leg(origin,group,graph,true),"partial capture construction retains its cursor without publishing a leg");
    }
    check(begins==2 && advances==2 && retained.built_nodes()==2,"second input continues instead of resetting assembly phase zero");
    check(retained.advance(0)==Status::NeedCompute && retained.last_work()==0,"zero units cannot finish a partial path");
    finish(retained);
    const std::array<Point,4> expected{{{1,5},{2,5},{3,5},{4,5}}};
    check(retained.status()==Status::Ready && retained.path().size()==expected.size() &&
          std::equal(retained.path().begin(),retained.path().end(),expected.begin()),
          "Ready publishes only complete bounded strict gradient arrival at native group root");
    auto leg=retained.leg(origin,group,graph,true);
    check(leg && leg->kind==Kind::Step && leg->origin==origin && leg->destination==first,
          "fresh Reward and actual origin expose exactly the first geometry edge");
    check(!retained.leg(origin,group,graph,false) && retained.status()==Status::Ready,
          "missing fresh Reward gives no edge without inventing infeasibility or receipt");
    check(retained.begin(origin,group,graph)==Status::Ready && retained.built_nodes()==4,
          "repeated begin does not rebuild a complete same-origin continuation");
    check(retained.consume_applied(origin,first,group,graph),"actual matching first move consumes one exact edge");
    check(retained.begin(first,group,graph)==Status::Ready,"post-move begin binds the actual consumed origin");
    leg=retained.leg(first,group,graph,true);
    check(leg && leg->destination==Point{3,5},"next geometry proposal follows consumed actual prefix");
    check(retained.consume_applied(first,{3,5},group,graph) && retained.consume_applied({3,5},root,group,graph),
          "subsequent actual ordinary edges reach the pinned root");
    leg=retained.leg(root,group,graph,true);
    check(leg && leg->kind==Kind::Hold && leg->origin==root && leg->destination==root,
          "actual position on complete region root yields a geometry Hold only");
    for(int input=0;input<20;++input)
        check(retained.leg(root,group,graph,true)->kind==Kind::Hold,"elapsed holds never infer capture payout or receipt");
    check(!retained.leg(root,group,graph,false) && retained.status()==Status::Ready,
          "timer or old Reward cannot authorize a hold without fresh available result");
    check(!retained.consume_applied(root,root,group,graph) && retained.status()==Status::Unknown,
          "stationary Hold cannot be consumed as applied movement or capture progress");

    auto partial=word();partial.begin(origin,group,graph);partial.advance(2);
    check(partial.begin({2,5},group,graph)==Status::Unknown && partial.path().empty(),
          "displacement while building revokes the retained operator");
    auto uncovered=word();
    check(uncovered.begin({8,8},group,graph)==Status::Unknown,"old compute_to coverage missing future frontier remains Unknown");
    auto missing=field();missing[53]=unknown;
    CaptureWord truncated{10,10,group,graph,std::move(missing),4};truncated.begin(origin,group,graph);finish(truncated);
    check(truncated.status()==Status::Unknown && truncated.path().empty(),"truncated gradient cannot publish a partial capture word");
    auto plateau=field();plateau[53]=2;
    CaptureWord looping{10,10,group,graph,std::move(plateau),4};looping.begin(origin,group,graph);finish(looping);
    check(looping.status()==Status::Unknown,"equal-distance plateau cannot become an infinite path");
    auto alternate=field();alternate[63]=1;alternate[64]=0;
    CaptureWord compass{10,10,group,graph,std::move(alternate),4};compass.begin(origin,group,graph);finish(compass);
    check(compass.path().size()==4 && compass.path()[2]==Point{3,6} && compass.path()[3]==Point{4,6},
          "complete paths select deterministic native NE before E descents");

    for(int mismatch=0;mismatch<3;++mismatch)
    {
        auto candidate=word();candidate.begin(origin,group,graph);finish(candidate);
        const auto actual=mismatch==2?Point{2,5}:origin;
        check(!candidate.leg(actual,group+(mismatch==0),graph+(mismatch==1),true) && candidate.status()==Status::Unknown,
              "wrong group full64bit graph or actual origin invalidates a Ready continuation");
    }
    for(int mismatch=0;mismatch<5;++mismatch)
    {
        auto candidate=word();candidate.begin(origin,group,graph);finish(candidate);
        const auto from=mismatch==0?first:origin;
        const auto to=mismatch==1?Point{3,5}:mismatch==2?origin:first;
        check(!candidate.consume_applied(from,to,group+(mismatch==3),graph+(mismatch==4)) &&
              candidate.status()==Status::Unknown,"out of order wrong edge stationary or binding mismatch cannot consume");
    }
    auto premature=word();premature.begin(origin,group,graph);premature.advance(1);
    check(!premature.consume_applied(origin,first,group,graph) && premature.status()==Status::Unknown,
          "applied bookkeeping cannot promote incomplete geometry");
    auto too_small=word(3);
    check(too_small.begin(origin,group,graph)==Status::Unknown,"capacity overflow is Unknown not a capped path");
    auto malformed=field();malformed.pop_back();
    CaptureWord shape{10,10,group,graph,std::move(malformed),4};
    check(shape.begin(origin,group,graph)==Status::Unknown,"field buffer shape must match exact dimensions");
    CaptureWord huge{std::numeric_limits<int>::max(),std::numeric_limits<int>::max(),group,graph,{},4};
    check(huge.begin(origin,group,graph)==Status::Unknown,"dimension product cannot wrap the dated field shape");
    auto current_root=word();
    check(current_root.begin(root,group,graph)==Status::NeedCompute && current_root.path().empty(),
          "begin on a region root still cannot publish partial authority");
    check(current_root.advance(1)==Status::Ready && current_root.leg(root,group,graph,true)->kind==Kind::Hold,
          "one origin unit completes an already-on-root geometry continuation");
    auto staged=word();staged.begin(origin,group,graph);staged.advance(2);
    auto moved=std::move(staged);finish(moved);
    check(staged.status()==Status::Unknown && staged.path().empty() && moved.status()==Status::Ready &&
          moved.path().size()==4,"moving a partial word preserves cursors and revokes moved-from publication");
    std::printf("production capture: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
