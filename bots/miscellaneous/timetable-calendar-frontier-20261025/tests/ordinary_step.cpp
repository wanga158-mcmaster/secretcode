#define TT_ORDINARY_STEP_FIXTURE
#define TT_POTENTIAL_FIXTURE
#define TT_CAPTURE_FIXTURE
#include "../src/ordinary_step.h"
#include "../src/independent_potential.cpp"
#include "../src/independent_capture.cpp"
#include <array>
#include <cstdio>
#include <string>
#include <vector>

namespace {
using Point=mb::production::potential::Point;
struct Facts
{
    bool terrain{},champion{},camp{},teleport{};
    // Hidden facts are intentionally not exposed by the point predicates, just
    // as SDK has_* returns false for hidden occupants. No visibility query exists.
    bool hidden{true};
    int* epoch{};
    char suspend_at{};
    std::string journal;
    bool query(char kind,bool value)
    { journal+=kind; if(kind==suspend_at)++*epoch; return value; }
    bool attempt(Point point)
    {
        return tt::ordinary_step_detail::may_attempt(point,100,100,
            [&](Point) { return query('T',terrain); },
            [&](Point) { return query('H',champion); },
            [&](Point) { return query('C',camp); },
            [&](Point) { return query('P',teleport); });
    }
};
template<class Flags> struct Actor
{
    Point pos{56,2},last{};
    float hp{1000};
    Flags state{true,false,true};
    tt::capture_detail::Bag contents{23,11};
    int* epoch{};
    int position_calls{},suspend_position{},absolute_calls{},relative_calls{};
    bool accepted{true};
    Point position() { if(++position_calls==suspend_position)++*epoch; return pos; }
    float health() const { return hp; }
    Flags flags() const { return state; }
    tt::capture_detail::Bag bag() const { return contents; }
    bool move_absolute(Point destination)
    { ++absolute_calls; last=destination; if(accepted)pos=destination; return accepted; }
    bool move(Point delta) { ++relative_calls; pos={pos.x+delta.x,pos.y+delta.y}; return true; }
};
struct Member { Point pos; float strength{}; int range{}; };
struct Group { std::vector<Member> members; };
int checks{},failures{};
void check(bool good,const char* label)
{ ++checks; if(!good){++failures;std::printf("FAIL %s\n",label);} }
}

int main()
{
    const Point origin{56,2},destination{55,3};
    int epoch=64;
    Facts facts; facts.epoch=&epoch;
    check(facts.hidden && facts.attempt(destination) && facts.journal=="THCP",
          "hidden landing with no observed blockers permits an attempt without a visibility prerequisite");
    for(const Point invalid : std::array<Point,4>{{{-1,3},{55,-1},{100,3},{55,100}}})
    {
        facts=Facts{};facts.epoch=&epoch;
        check(!facts.attempt(invalid) && facts.journal.empty(),"bounds precede all point queries");
    }
    facts=Facts{};facts.epoch=&epoch;facts.terrain=true;
    check(!facts.attempt(destination) && facts.journal=="T","static forbidden terrain rejects before dynamic queries");
    facts=Facts{};facts.epoch=&epoch;facts.champion=true;
    check(!facts.attempt(destination) && facts.journal=="TH","observed champion rejects before camp and teleport queries");
    facts=Facts{};facts.epoch=&epoch;facts.camp=true;
    check(!facts.attempt(destination) && facts.journal=="THC","observed camp rejects before teleport query");
    facts=Facts{};facts.epoch=&epoch;facts.teleport=true;
    check(!facts.attempt(destination) && facts.journal=="THCP","observed teleport rejects with exact shared query order");

    using PotentialActor=Actor<tt::potential_detail::Flags>;
    using PotentialStatus=tt::potential_detail::Status;
    PotentialActor actor;actor.epoch=&epoch;
    const auto reset=[&] {
        actor=PotentialActor{};actor.epoch=&epoch;
        facts=Facts{};facts.epoch=&epoch;
    };
    const auto transit=[&] {
        return tt::potential_detail::transit_authority(80.0,origin,destination,actor,
            [&]{return epoch;},[&](Point point){return facts.attempt(point);},[]{return true;});
    };
    reset();auto result=transit();
    check(result.status==PotentialStatus::Ready && result.moved && actor.pos==destination &&
          actor.last==destination && actor.absolute_calls==1 && actor.relative_calls==0,
          "actual transit composition attempts hidden landing at absolute destination");
    reset();actor.accepted=false;result=transit();
    check(result.status==PotentialStatus::NeedObservation && !result.moved && result.pos==origin &&
          actor.pos==origin && actor.absolute_calls==1 && actor.state.can_move,
          "native refusal produces no successful movement bookkeeping and withdraws authority");
    reset();facts.champion=true;result=transit();
    check(result.status==PotentialStatus::NeedObservation && !result.moved && actor.absolute_calls==0 &&
          facts.journal=="TH","actual transit composition preserves observed occupant rejection");
    reset();facts.suspend_at='C';result=transit();
    check(result.status==PotentialStatus::Stale && !result.moved && actor.absolute_calls==0,
          "point-query suspension is caught by the existing final origin then epoch fence");
    for(const int suspended : {1,3})
    {
        reset();actor.suspend_position=suspended;result=transit();
        check(result.status==PotentialStatus::Stale && !result.moved && actor.absolute_calls==0,
              "prefix or final own-position suspension cannot authorize a native move");
    }
    reset();actor.state.can_move=false;result=transit();
    check(result.status==PotentialStatus::Ready && !result.moved && actor.absolute_calls==0 && facts.journal.empty(),
          "spent movement retains Ready observation without point queries or an attempted move");

    mb::production::potential::Field field{100,100,{}};
    while(field.status()==mb::production::potential::Status::NeedCompute)field.advance(1024);
    const std::array<Point,2> path{origin,destination};
    reset();result=tt::potential_detail::authority(field,std::span<const Point>{path},true,actor,
        [&]{return epoch;},[&](Point point){return facts.attempt(point);},[]{return true;});
    check(result.status==PotentialStatus::Ready && result.moved && actor.last==destination && facts.journal=="THCP",
          "full exit authority uses the same point-attempt permission while retaining its path bound");

    using CaptureActor=Actor<tt::capture_detail::Flags>;
    using CaptureStatus=tt::capture_detail::Status;
    const std::vector<Group> groups{{{{destination,200,5}}},{{{{90,90},20,3}}}};
    CaptureActor captor;captor.epoch=&epoch;
    const auto capture_reset=[&] {
        captor=CaptureActor{};captor.epoch=&epoch;
        facts=Facts{};facts.epoch=&epoch;
    };
    const auto capture=[&] {
        return tt::capture_detail::authority(groups,0,origin,destination,captor,[&]{return epoch;},
            [](int){return tt::capture_detail::Life::Reward;},[]{return false;},
            [&](Point point){return facts.attempt(point);},[]{return true;});
    };
    capture_reset();auto captured=capture();
    check(captured.status==CaptureStatus::Ready && captured.safe_input && captured.zero_external && captured.moved &&
          captor.last==destination && captor.absolute_calls==1 && captor.relative_calls==0 && facts.journal=="THCP",
          "fresh Reward capture Step composes hidden point-attempt permission with zero external exposure");
    capture_reset();captor.accepted=false;captured=capture();
    check(captured.status==CaptureStatus::NeedObservation && !captured.moved && !captured.holding &&
          captured.pos==origin && captor.pos==origin && captor.absolute_calls==1 && captured.gems==11,
          "capture native refusal invents no movement, holding or receipt");
    capture_reset();facts.terrain=true;captured=capture();
    check(captured.status==CaptureStatus::NeedObservation && !captured.moved && captor.absolute_calls==0 &&
          facts.journal=="T","capture Step preserves static terrain rejection");
    capture_reset();facts.suspend_at='P';captured=capture();
    check(captured.status==CaptureStatus::Stale && !captured.moved && captor.absolute_calls==0,
          "capture final epoch fence rejects a suspending point predicate");
    std::printf("ordinary-step %d checks %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
