#include "production_transit.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

namespace {
using namespace tt::production_transit;
using Source=mb::production::potential::Source;
int failures{},checks{};
void check(bool condition,const char* label)
{
    ++checks;
    if (!condition) { ++failures; std::printf("FAIL %s\n",label); }
}
void finish(Field& field)
{ while (field.status()==Status::NeedCompute) field.advance(13); }
void finish(Ledger& ledger,const Field& field)
{ while (ledger.status()==Status::NeedCompute) ledger.advance(field,3); }
bool near_upper(double value,double exact)
{ return value>=exact && value-exact<1e-10; }
constexpr Key initial_key{4,12,33,50};
std::vector<Point> word()
{ return {{0,0},{1,0},{2,0},{3,0},{2,0},{1,0},{0,0}}; }
Field field()
{
    Field result(4,1,{{{1,0},2,0},{{2,0},7,0},{{3,0},3,0}});
    finish(result);
    return result;
}
Ledger ledger(std::size_t capacity=7)
{ return Ledger(word(),3,2,initial_key,capacity); }
}

int main()
{
    auto potential=field();
    auto retained=ledger();
    check(retained.status()==Status::NeedCompute && retained.matches(initial_key) &&
          !retained.current_origin() && !retained.next_destination() &&
          retained.required_hp().status==Status::Unknown && !retained.at_frontier(),
          "uncomputed ledger publishes no origin, destination or partial bound");
    check(retained.advance(potential,0)==Status::NeedCompute && retained.last_work()==0,
          "zero units cannot start or publish construction");
    std::size_t units=0;
    while (retained.status()==Status::NeedCompute)
    {
        retained.advance(potential,1);
        units+=retained.last_work();
        check(retained.last_work()<=1,"each staged charge or suffix publication uses one unit");
        if (retained.status()!=Status::Ready)
            check(retained.required_hp().status==Status::Unknown && !retained.current_origin(),
                  "partial word or suffix construction cannot publish authority");
    }
    check(units==11 && retained.status()==Status::Ready,
          "seven point charges plus four ingress suffix bounds finish incrementally");
    // Word rates:0,2,7,3,7,2,0. MAX=7; fixed return=9; two waits=6.
    // Origin0 additionally owes ingress2+7+3, giving34, not a monotone-gradient estimate.
    check(near_upper(retained.required_hp().upper,34) && retained.current_origin()==Point{0,0} &&
          retained.next_destination()==Point{1,0},
          "full nonmonotone path includes ingress, two frontier waits, fixed return and global maximum");
    auto direct=ledger();
    finish(direct,potential);
    check(direct.required_hp().upper==retained.required_hp().upper,
          "interrupted and direct construction produce identical complete arithmetic");

    int authority_calls=0;
    const auto request_leg=[&](const Ledger& candidate,Key actual,Point origin,bool trail_known)
    {
        const auto request=candidate.leg(actual,origin,trail_known);
        if (request) ++authority_calls; // stand-in for the parent's fresh SDK authority callback
        return request;
    };
    for (int mismatch=0;mismatch<4;++mismatch)
    {
        Key actual=initial_key;
        if (mismatch==0) ++actual.group;
        if (mismatch==1) ++actual.graph;
        if (mismatch==2) ++actual.field;
        if (mismatch==3) ++actual.walk;
        check(!request_leg(direct,actual,{0,0},true) && authority_calls==0,
              "wrong group, graph, field or actual Trail revision blocks authority callback");
    }
    check(!request_leg(direct,initial_key,{1,0},true) &&
          !request_leg(direct,initial_key,{0,0},false) && authority_calls==0,
          "wrong fresh origin or Unknown Trail prevents authority callback");
    auto unfinished=ledger();
    check(!request_leg(unfinished,initial_key,{0,0},true) && authority_calls==0,
          "partial construction cannot call the actual authority adapter");
    const auto first_request=request_leg(direct,initial_key,{0,0},true);
    check(first_request && authority_calls==1 && first_request->origin==Point{0,0} &&
          first_request->destination==Point{1,0} && first_request->required_hp==direct.required_hp().upper,
          "matching complete current Trail request calls authority once with the exact next leg");

    Key next=initial_key; ++next.walk;
    check(retained.consume_applied({0,0},{1,0},next) && retained.matches(next) &&
          !retained.matches(initial_key) && retained.current_origin()==Point{1,0} &&
          retained.next_destination()==Point{2,0} && near_upper(retained.required_hp().upper,32),
          "actual matching first leg advances cursor, residual bound and Trail revision");
    const auto after_first=retained.leg(next,{1,0},true);
    check(!retained.leg(initial_key,{0,0},true) && !retained.leg(initial_key,{1,0},true) &&
          !retained.leg(next,{0,0},true) && after_first && after_first->origin==Point{1,0} &&
          after_first->destination==Point{2,0} && near_upper(after_first->required_hp,32),
          "consumed prefix requires both the updated actual revision and advanced actual origin");
    next.walk+=2;
    check(retained.consume_applied({1,0},{2,0},next) && near_upper(retained.required_hp().upper,25),
          "later origin excludes consumed ingress charges and accepts increasing actual revisions");
    ++next.walk;
    check(retained.consume_applied({2,0},{3,0},next) && retained.at_frontier() &&
          !retained.next_destination() && near_upper(retained.required_hp().upper,22),
          "frontier retains waits plus fixed return plus MAX without another ingress leg");
    check(!request_leg(retained,next,{3,0},true) && authority_calls==1,
          "frontier has no next ingress leg and cannot call transit authority");
    check(!retained.consume_applied({3,0},{2,0},Key{4,12,33,next.walk+1}) &&
          retained.status()==Status::Unknown && retained.required_hp().status==Status::Unknown,
          "ledger cannot consume a reverse leg or silently reuse frontier authority");

    for (int mismatch=0;mismatch<4;++mismatch)
    {
        auto candidate=ledger(); finish(candidate,potential);
        Key changed=initial_key; ++changed.walk;
        if (mismatch==0) ++changed.group;
        if (mismatch==1) ++changed.graph;
        if (mismatch==2) ++changed.field;
        if (mismatch==3) changed.walk=initial_key.walk;
        check(!candidate.consume_applied({0,0},{1,0},changed) && candidate.status()==Status::Unknown &&
              !candidate.matches(initial_key),"group, graph, field or stale walk mismatch revokes the record");
    }
    auto backwards=ledger(); finish(backwards,potential);
    check(!backwards.consume_applied({0,0},{1,0},Key{4,12,33,49}) && backwards.status()==Status::Unknown,
          "decreasing walk revision is rejected");
    auto wrapping=Ledger(word(),3,2,Key{4,12,33,std::numeric_limits<std::uint64_t>::max()},7);
    finish(wrapping,potential);
    check(!wrapping.consume_applied({0,0},{1,0},Key{4,12,33,0}),"wrapped walk revision is never accepted");
    const std::array<std::array<Point,2>,3> invalid_pairs={
        std::array<Point,2>{Point{1,0},Point{2,0}},
        std::array<Point,2>{Point{0,0},Point{2,0}},
        std::array<Point,2>{Point{0,0},Point{0,0}}};
    for (const auto pair:invalid_pairs)
    {
        auto candidate=ledger(); finish(candidate,potential);
        check(!candidate.consume_applied(pair[0],pair[1],Key{4,12,33,51}) && candidate.status()==Status::Unknown,
              "wrong current origin, wrong next destination or stationary consume invalidates");
    }
    auto premature=ledger();
    check(!premature.consume_applied({0,0},{1,0},Key{4,12,33,51}) && premature.status()==Status::Unknown,
          "actual-prefix consume cannot publish an unfinished proof");

    auto too_small=ledger(6);
    Ledger empty({},0,2,initial_key,7),bad_frontier(word(),7,2,initial_key,7),zero_capacity(word(),3,2,initial_key,0);
    check(too_small.status()==Status::Unknown && empty.status()==Status::Unknown &&
          bad_frontier.status()==Status::Unknown && zero_capacity.status()==Status::Unknown,
          "invalid frontier, empty word and explicit capacity failures stay Unknown");
    const std::array<std::vector<Point>,3> invalid_words={
        std::vector<Point>{{0,0},{2,0},{0,0}},
        std::vector<Point>{{0,0},{0,0}},
        std::vector<Point>{{0,0},{1,0},{4,0}}};
    for (auto invalid_word:invalid_words)
    {
        Ledger invalid(std::move(invalid_word),0,2,initial_key,7); finish(invalid,potential);
        check(invalid.status()==Status::Unknown && !invalid.current_origin(),
              "nonadjacent, repeated-edge or out-of-bounds geometry cannot publish");
    }
    Ledger nonquiet({{0,0},{1,0}},0,2,initial_key,7); finish(nonquiet,potential);
    check(nonquiet.status()==Status::Unknown,"terminal point must have completed potential zero");

    Field partial(4,1,{});
    auto waiting=ledger();
    check(waiting.advance(partial,100)==Status::NeedCompute && waiting.last_work()==0 &&
          waiting.required_hp().status==Status::Unknown,"unfinished Field cannot leak partial point charges");
    Field unknown(0,1,{});
    check(waiting.advance(unknown,1)==Status::Unknown,"unknown Field invalidates construction");
    double result{};
    check(!tt::production_transit::detail::multiply_upper(std::numeric_limits<double>::max(),2,result) &&
          !tt::production_transit::detail::multiply_upper(std::numeric_limits<double>::infinity(),0,result),
          "overflow or nonfinite wait arithmetic cannot saturate to a lower finite promise");
    check(tt::production_transit::detail::multiply_upper(3,2,result) && near_upper(result,6),
          "frontier wait multiplication rounds conservatively upward");

    Field quiet(1,1,{}); finish(quiet);
    Ledger single({{0,0}},0,2,initial_key,1); finish(single,quiet);
    check(single.status()==Status::Ready && single.at_frontier() && single.required_hp().upper==0 &&
          !single.next_destination(),"single quiet frontier is a complete zero-charge word");
    auto moved=std::move(direct);
    check(moved.status()==Status::Ready && direct.status()==Status::Unknown && !direct.current_origin(),
          "moving a complete ledger revokes moved-from publication");
    auto staged=ledger(); staged.advance(potential,5);
    auto resumed=std::move(staged); finish(resumed,potential);
    check(staged.status()==Status::Unknown && resumed.status()==Status::Ready &&
          resumed.required_hp().upper==moved.required_hp().upper,
          "moving partial construction preserves its cursor and complete arithmetic");
    resumed.invalidate();
    check(!resumed.matches(initial_key) && resumed.required_hp().status==Status::Unknown,
          "external refusal or displacement can revoke accounting without storing epoch or HP");

    // The equal-gradient endpoint family must remain local: Right has raw rates
    //20,20 before quiet, but Left is entirely quiet. A completed Right HP failure
    // must offer Left instead of excluding the group or rebuilding Right forever.
    Field alternatives(5,1,{{{2,0},20,0},{{3,0},20,0}}); finish(alternatives);
    const Point actual_origin{1,0},right_first{2,0},left_first{0,0};
    Probe choices(initial_key,actual_origin,90,{right_first,left_first});
    Ledger right({{1,0},{2,0},{3,0},{4,0},{3,0},{2,0},{1,0}},3,2,initial_key,7);
    check(choices.matches(initial_key,actual_origin,90) && choices.next()==right_first && !choices.exhausted(),
          "finite probe begins at declared Right endpoint without excluding other words");
    // This is the actual shared-caller gate: unfinished Ledger yields no Leg,
    // hence there is no completed value to submit to the rejection transition.
    int rejection_calls=0;
    const auto reject_word=[&](Probe& probe,const Ledger& candidate,float fresh_hp)
    {
        if (!probe.matches(initial_key,actual_origin,fresh_hp)) return false;
        const auto request=candidate.leg(initial_key,actual_origin,true);
        if (!request) return false;
        ++rejection_calls;
        return probe.reject_completed(request->destination,request->required_hp,fresh_hp);
    };
    check(!reject_word(choices,right,90) && rejection_calls==0 && choices.next()==right_first &&
          choices.rejections().empty(),
          "unfinished word cannot consume an endpoint or turn budget work into HP rejection");
    Ledger capacity_unknown({{1,0},{2,0},{3,0},{4,0},{3,0},{2,0},{1,0}},3,2,initial_key,6);
    Ledger model_unknown({{1,0},{2,0}},1,2,initial_key,2); finish(model_unknown,alternatives);
    check(!reject_word(choices,capacity_unknown,90) && !reject_word(choices,model_unknown,90) &&
          rejection_calls==0 && choices.next()==right_first && choices.rejections().empty(),
          "capacity and model Unknown outcomes cannot consume or skip an endpoint");
    finish(right,alternatives);
    check(near_upper(right.required_hp().upper,100) && reject_word(choices,right,90) &&
          rejection_calls==1 && choices.next()==left_first && !choices.exhausted(),
          "completed Right bound100 failsHP90 and offers Left rather than banning the group");
    check(choices.rejections().size()==1 && choices.rejections()[0].first==right_first &&
          choices.rejections()[0].required_hp==right.required_hp().upper &&
          choices.rejections()[0].observed_hp==90 && choices.next()==left_first,
          "retained rejection binds the exact completed Right charge and observed HP before offering Left");
    Ledger left({{1,0},{0,0},{1,0}},1,2,initial_key,3); finish(left,alternatives);
    const auto left_leg=left.leg(initial_key,actual_origin,true);
    check(left_leg && left_leg->destination==left_first && left_leg->required_hp==0 &&
          !reject_word(choices,left,90) && choices.next()==left_first,
          "all-quiet Left bound0 retains a valid next leg and cannot be HP-rejected at90");

    for (int mismatch=0;mismatch<4;++mismatch)
    {
        Key actual=initial_key;
        if (mismatch==0) ++actual.group;
        if (mismatch==1) ++actual.graph;
        if (mismatch==2) ++actual.field;
        if (mismatch==3) ++actual.walk;
        check(!choices.matches(actual,actual_origin,90),"Probe rejects changed group, graph, field or walk binding");
    }
    check(!choices.matches(initial_key,{2,0},90) && !choices.matches(initial_key,actual_origin,91) &&
          !choices.matches(initial_key,actual_origin,0) &&
          !choices.matches(initial_key,actual_origin,std::numeric_limits<float>::quiet_NaN()),
          "changed origin, gained HP, nonpositive HP or nonfinite HP requires rebuilding");
    check(!choices.reject_completed(right_first,100,90) &&
          !choices.reject_completed(left_first,80,90) &&
          !choices.reject_completed(left_first,std::numeric_limits<double>::infinity(),90) &&
          !choices.reject_completed(left_first,std::numeric_limits<double>::quiet_NaN(),90) &&
          !choices.reject_completed(left_first,-1,90) &&
          !choices.reject_completed(left_first,100,std::numeric_limits<float>::infinity()) &&
          choices.next()==left_first && choices.rejections().size()==1,
          "wrong endpoint, passing HP or invalid arithmetic never advances the finite family");
    Probe lowered(initial_key,actual_origin,100,{right_first,left_first});
    check(lowered.reject_completed(right_first,90,80) &&
          lowered.matches(initial_key,actual_origin,80) && lowered.matches(initial_key,actual_origin,70) &&
          !lowered.matches(initial_key,actual_origin,95) &&
          !lowered.reject_completed(left_first,100,85) && lowered.next()==left_first,
          "rejection HP ceiling decreases so a later gain below original startHP cannot preserve stale rejection");
    Probe one(initial_key,actual_origin,90,{right_first});
    check(one.reject_completed(right_first,100,90) && one.exhausted() && !one.next() &&
          !one.reject_completed(right_first,100,90),
          "exhaustion means only this declared endpoint family has no remaining word");
    Probe equal_hp(initial_key,actual_origin,90,{right_first});
    check(equal_hp.reject_completed(right_first,90,90) && equal_hp.exhausted(),
          "HP equal to required bound fails the strict survival comparison");
    Probe no_endpoints(initial_key,actual_origin,90,{}),bad_hp(initial_key,actual_origin,0,{});
    check(no_endpoints.exhausted() && !no_endpoints.next() && !bad_hp.exhausted() &&
          !bad_hp.matches(initial_key,actual_origin,90),
          "empty declared family is distinct from an invalid HP snapshot");
    Probe moved_probe(std::move(choices));
    check(moved_probe.next()==left_first && moved_probe.matches(initial_key,actual_origin,90) &&
          moved_probe.rejections().size()==1 && moved_probe.rejections()[0].first==right_first &&
          moved_probe.rejections()[0].required_hp==right.required_hp().upper &&
          moved_probe.rejections()[0].observed_hp==90 && !choices.next() && !choices.exhausted() &&
          !choices.matches(initial_key,actual_origin,90) && choices.rejections().empty(),
          "moving the retained probe preserves cursor and rejection records while invalidating source publication");

    std::printf("production_transit checks=%d failures=%d\n",checks,failures);
    return failures?1:0;
}
