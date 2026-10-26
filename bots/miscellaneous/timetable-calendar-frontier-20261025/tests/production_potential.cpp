#include "production_potential.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

namespace {
using namespace mb::production::potential;
int failures{},checks{};
void check(bool condition,const char* label)
{
    ++checks;
    if (!condition) { ++failures; std::printf("FAIL %s\n",label); }
}
void finish(Field& field)
{
    while (field.status()==Status::NeedCompute) field.advance(7);
}
bool near_upper(double value,double exact)
{ return value>=exact && value-exact<1e-10; }
}

int main()
{
    // 1: Empty construction does not initialize or publish a single tile.
    Field empty(3,2,{});
    check(empty.status()==Status::NeedCompute && empty.initialized_tiles()==0,
          "constructor reserves but performs no whole-grid initialization");
    check(empty.advance(0)==Status::NeedCompute && empty.last_work()==0 && empty.at({0,0}).status==Status::Unknown,
          "zero work cannot publish unfinished entries");
    check(empty.advance(5)==Status::NeedCompute && empty.initialized_tiles()==5 && empty.last_work()==5,
          "zero initialization consumes exact bounded advance work");
    check(empty.at({0,0}).status==Status::Unknown && empty.advance(1)==Status::Ready && empty.at({2,1}).upper==0,
          "even initialized entries remain unknown until entire field complete");

    // 2: All possible overlapping sources count; no wake/life/shield state exists.
    Field overlap(5,5,{{{1,1},20,1},{{3,1},30,1}});
    finish(overlap);
    check(overlap.status()==Status::Ready && near_upper(overlap.at({2,1}).upper,50),
          "overlapping potentially sleeping or dead sources are both counted");
    check(overlap.at({0,0}).upper==20 && overlap.at({4,0}).upper==30 && overlap.at({2,4}).upper==0,
          "fixed Chebyshev range is inclusive and clipped to map");

    // 3: Repeated interruptions preserve exact source/cell ordering.
    const std::vector<Source> sources={{{0,0},7,1},{{2,1},11,2},{{1,2},0,0}};
    Field interrupted(4,3,sources),direct(4,3,sources);
    while (interrupted.status()==Status::NeedCompute)
    {
        interrupted.advance(1);
        check(interrupted.last_work()<=1,"every interrupted call respects one work unit");
        if (interrupted.status()!=Status::Ready)
            check(interrupted.at({0,0}).status==Status::Unknown,"partial source contributions never publish Ready");
    }
    direct.advance(1000);
    bool equal=direct.status()==Status::Ready;
    for (int y=0;y<3;++y) for (int x=0;x<4;++x)
        equal=equal && direct.at({x,y}).upper==interrupted.at({x,y}).upper;
    check(equal,"incremental field equals direct build bit for bit");

    // 4: Both path clocks include the anchor and all intermediate landings.
    const std::array<Point,4> path={Point{0,0},Point{1,0},Point{2,0},Point{3,0}};
    Field strip(4,1,{{{0,0},10,1},{{2,0},20,0}});
    finish(strip);
    const auto sum=bound_path(strip,path);
    check(sum.status==Status::Ready && near_upper(sum.exit_before_yield,30) && near_upper(sum.wait_then_exit,40),
          "exit excludes current point while waiting adds its raw exposure");
    const std::array<Point,2> repeated={Point{0,0},Point{0,0}};
    const auto repeated_sum=bound_path(strip,repeated);
    check(near_upper(repeated_sum.exit_before_yield,10) && near_upper(repeated_sum.wait_then_exit,20),
          "repeated represented landings are charged rather than deduplicated");

    // 5: A one-point path distinguishes immediate exit from waiting there.
    const std::array<Point,1> anchor={Point{0,0}};
    const auto single=bound_path(strip,anchor);
    check(single.status==Status::Ready && single.exit_before_yield==0 && single.wait_then_exit==10,
          "single point exposes the exact index zero distinction");
    check(bound_path(strip,std::span<const Point>{}).status==Status::Unknown,
          "empty path cannot claim a finite execution witness");

    // 6: No shield/HP/mitigation input can discount raw potential.
    Field raw(1,1,{{{0,0},250,0}});
    finish(raw);
    check(raw.at({0,0}).upper==250 && bound_path(raw,anchor).wait_then_exit==250,
          "shield and beneficial mitigation receive no exposure credit");

    // 7: Invalid sizes, positions and source values remain Unknown.
    Field bad_size(0,1,{}),negative_size(-1,1,{});
    Field overflowing_size(std::numeric_limits<int>::max(),std::numeric_limits<int>::max(),{});
    check(bad_size.status()==Status::Unknown && negative_size.status()==Status::Unknown &&
          overflowing_size.status()==Status::Unknown,
          "invalid or overflowing dimensions are Unknown rather than unreachable");
    for (const auto source:std::array<Source,5>{{{{-1,0},1,0},{{1,0},1,0},{{0,0},-1,0},
              {{0,0},std::numeric_limits<float>::infinity(),0},{{0,0},1,-1}}})
    {
        Field invalid(1,1,{source}); finish(invalid);
        check(invalid.status()==Status::Unknown && invalid.at({0,0}).status==Status::Unknown,
              "invalid source cannot publish even a zero-initialized field");
    }
    Field nan(1,1,{{{0,0},std::numeric_limits<float>::quiet_NaN(),0}});
    finish(nan);
    check(nan.status()==Status::Unknown,"NaN source is rejected");

    // 8: INT_MAX range is mathematically valid and must not overflow coordinates.
    Field huge_range(2,2,{{{1,1},3,std::numeric_limits<int>::max()}});
    finish(huge_range);
    check(huge_range.status()==Status::Ready && huge_range.at({0,0}).upper==3 && huge_range.at({1,1}).upper==3,
          "maximal range clips safely using wide arithmetic");
    check(huge_range.at({-1,0}).status==Status::Unknown && huge_range.at({2,0}).status==Status::Unknown,
          "out-of-bounds lookup never masquerades as zero exposure");
    const std::array<Point,2> invalid_path={Point{0,0},Point{2,0}};
    check(bound_path(huge_range,invalid_path).status==Status::Unknown,
          "one invalid path point invalidates the complete charge");

    // 9: Loss of a small positive addend and saturation cannot lower the bound.
    double upper{};
    check(mb::production::potential::detail::add_upper(1e100,1,upper) && upper>1e100,
          "small positive contribution cannot vanish under rounding");
    check(!mb::production::potential::detail::add_upper(std::numeric_limits<double>::max(),1,upper) &&
          !mb::production::potential::detail::add_upper(1,std::numeric_limits<double>::infinity(),upper),
          "unrepresentable upper bound is Unknown instead of saturated downward");

    // 10: Zero-strength validation still consumes bounded work before Ready.
    Field zeros(1,1,{{{0,0},0,0},{{0,0},0,0}});
    check(zeros.advance(1)==Status::NeedCompute && zeros.initialized_tiles()==1,
          "initialized grid alone cannot bypass pending source validation");
    check(zeros.advance(1)==Status::NeedCompute && zeros.advance(1)==Status::Ready && zeros.at({0,0}).upper==0,
          "each zero source costs a work unit and only final validation publishes Ready");
    check(zeros.advance(100)==Status::Ready && zeros.last_work()==0,
          "finished field performs no repeated construction work");

    Field moved(std::move(zeros));
    check(moved.status()==Status::Ready && moved.at({0,0}).upper==0 &&
          zeros.status()==Status::Unknown && zeros.at({0,0}).status==Status::Unknown &&
          zeros.advance(1)==Status::Unknown,
          "moving Ready field invalidates source authority before any reuse");
    Field partial(2,2,{{{0,0},5,1}});
    partial.advance(6); // four zeros, one source setup, one tile contribution
    Field resumed(std::move(partial));
    finish(resumed);
    check(partial.status()==Status::Unknown && resumed.status()==Status::Ready &&
          resumed.at({0,0}).upper==5 && resumed.at({1,1}).upper==5,
          "moving partial field preserves exact destination cursor without publishing source");
    moved=std::move(resumed);
    check(moved.status()==Status::Ready && moved.width()==2 && moved.at({1,1}).upper==5 &&
          resumed.status()==Status::Unknown && resumed.at({1,1}).status==Status::Unknown,
          "move assignment replaces complete state and revokes moved-from lookup");

    std::printf("production_potential checks=%d failures=%d\n",checks,failures);
    return failures?1:0;
}
