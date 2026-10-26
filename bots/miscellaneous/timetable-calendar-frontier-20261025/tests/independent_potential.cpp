#define TT_POTENTIAL_FIXTURE
#include "../src/independent_potential.cpp"
#include <array>
#include <cstdio>
#include <limits>
#include <vector>

namespace {
using Point=mb::production::potential::Point;
using Field=mb::production::potential::Field;
using tt::potential_detail::Flags;
using tt::potential_detail::Status;
struct Actor
{
    Point pos{17,29},last{};
    float hp{100};
    Flags state{true,false,true};
    bool accepted{true},suspend_health{},suspend_flags{},suspend_move{};
    int* epoch{};
    int position_calls{},suspend_position{},absolute_calls{},relative_calls{};
    Point position() { if (++position_calls==suspend_position) ++*epoch; return pos; }
    float health() const { if(suspend_health)++*epoch; return hp; }
    Flags flags() const { if(suspend_flags)++*epoch; return state; }
    bool move_absolute(Point destination)
    { ++absolute_calls; last=destination; if(accepted)pos=destination; if(suspend_move)++*epoch; return accepted; }
    bool move(Point delta) { ++relative_calls; pos={pos.x+delta.x,pos.y+delta.y}; return true; }
};
int checks{},failures{};
void check(bool value,const char* label)
{ ++checks; if(!value){++failures;std::printf("FAIL %s\n",label);} }
Field ready(std::vector<mb::production::potential::Source> sources={})
{
    Field field{40,40,std::move(sources)};
    while (field.status()==mb::production::potential::Status::NeedCompute) field.advance(1024);
    return field;
}
}

int main()
{
    auto field=ready({{{17,29},10,0},{{18,30},20,0}});
    const std::array<Point,3> path{{{17,29},{18,30},{19,30}}};
    int epoch=60,legal_calls=0;
    Actor actor; actor.epoch=&epoch;
    auto invoke=[&](const Field& selected,std::span<const Point> selected_path,bool execute,bool legal=true,bool fuel=true,bool refusal_cover=false) {
        return tt::potential_detail::authority(selected,selected_path,execute,actor,[&]{return epoch;},
            [&](Point destination){++legal_calls;return legal && destination==path[1];},[&]{return fuel;},refusal_cover);
    };
    auto result=invoke(field,path,false);
    check(result.status==Status::Ready && result.safe_exit && result.safe_wait && result.hp==100 &&
          result.pos==path.front() && result.can_move && !result.moved,"fresh own-state prefix authorizes conditional read-only bound");
    check(actor.absolute_calls==0 && legal_calls==0,"read-only authority does not query destination occupancy or act");
    // The full approach cannot be admitted from the next-leg/backtrack price:
    // 30 ingress + 10 return + two20 service phases + MAX20 =100.
    const std::array<Point,5> complete_approach{{{17,29},{18,29},{19,29},{18,29},{17,29}}};
    auto approach_field=ready({{{18,29},10,0},{{19,29},20,0}});
    auto approach=[&](float hp,unsigned waits,Point service) {
        actor=Actor{};actor.epoch=&epoch;actor.hp=hp;
        return tt::potential_detail::authority(approach_field,std::span<const Point>{complete_approach},true,actor,
            [&]{return epoch;},[](Point){return true;},[]{return true;},true,
            std::optional<Point>{service},waits);
    };
    auto admitted=approach(100,2,{19,29});
    check(admitted.status==Status::Ready && !admitted.safe_return && !admitted.moved,
          "complete ingress service return and failure equality rejects admission");
    admitted=approach(101,2,{19,29});
    check(admitted.status==Status::Ready && admitted.safe_return && admitted.moved && actor.pos==Point{18,29},
          "whole conditional useful-frontier word admits only its fresh first absolute leg");
    admitted=approach(101,3,{19,29});
    check(admitted.status==Status::NeedObservation && !admitted.moved,
          "unsupported service phase capacity remains Unknown");
    admitted=approach(101,2,{20,29});
    check(admitted.status==Status::NeedObservation && !admitted.moved,
          "service charge must belong to represented physical word");
    actor=Actor{};actor.epoch=&epoch;legal_calls=0;
    actor.hp=30;
    result=invoke(field,path,false);
    check(result.status==Status::Ready && result.safe_exit && !result.safe_wait,"strict HP bounds separate exit now from waiting");
    check(!result.safe_return && !result.safe_service,
          "one refusal at the later higher-exposure landing is charged rather than only the current origin");
    actor.hp=20;
    result=invoke(field,path,true);
    check(result.status==Status::Ready && !result.safe_exit && !result.moved && actor.absolute_calls==0,
          "failed sufficient exit bound is Ready unsafe not infeasible or an action");
    actor.hp=100;
    result=invoke(field,path,true);
    check(result.status==Status::Ready && result.moved && result.pos==path[1] && !result.can_move &&
          actor.last==Point{18,30} && actor.pos==Point{18,30} && actor.absolute_calls==1 && actor.relative_calls==0,
          "actual caller commands absolute destination rather than interpreting it as delta");
    actor=Actor{}; actor.epoch=&epoch; legal_calls=0;
    result=invoke(field,path,true,false);
    check(result.status==Status::NeedObservation && !result.moved && actor.absolute_calls==0 && legal_calls==1,
          "known blocked or hidden first landing withdraws action authority");
    actor.accepted=false;
    result=invoke(field,path,true);
    check(result.status==Status::NeedObservation && !result.moved && actor.absolute_calls==1,
          "native refusal revokes authority and requires revalidation, not deletion of historical geometry");
    auto refusal_field=ready({{{17,29},20,0}});
    const std::array<Point,2> refusal_path{{{17,29},{18,30}}};
    actor=Actor{}; actor.epoch=&epoch; actor.hp=15;
    result=invoke(refusal_field,refusal_path,true,true,true,true);
    check(result.status==Status::Ready && result.safe_exit && !result.safe_wait && !result.moved && actor.absolute_calls==0,
          "certified entry refuses an unpriced origin-failure input even when successful movement alone would survive");
    result=invoke(refusal_field,refusal_path,true);
    check(result.status==Status::Ready && result.moved && !result.safe_wait,
          "best-effort exit may reach actual quiet without claiming refusal-robust safety");
    actor=Actor{}; actor.epoch=&epoch; actor.hp=15; actor.accepted=false;
    result=invoke(refusal_field,refusal_path,true);
    check(result.status==Status::NeedObservation && !result.moved && !result.safe_wait && actor.pos==refusal_path.front(),
          "best-effort refusal remains an explicitly uncovered origin contingency");
    auto quiet_entry_field=ready({{{18,30},20,0}});
    const std::array<Point,3> quiet_entry{{{17,29},{18,30},{17,29}}};
    actor=Actor{}; actor.epoch=&epoch; actor.hp=25;
    result=invoke(quiet_entry_field,quiet_entry,true,true,true,true);
    check(result.status==Status::Ready && result.safe_exit && result.safe_wait && !result.safe_return &&
          !result.moved && actor.absolute_calls==0,
          "quiet-origin entry prices a possible refusal at its later damaging station");
    actor.hp=41;
    result=invoke(quiet_entry_field,quiet_entry,true,true,true,true);
    check(result.status==Status::Ready && result.safe_return && result.moved,
          "entry with successful pathloss plus maximum refusal-origin charge admits actual first step");
    actor=Actor{}; actor.epoch=&epoch; actor.state.can_move=false;
    result=invoke(field,path,true);
    check(result.status==Status::Ready && !result.can_move && !result.moved && actor.absolute_calls==0,
          "spent movement cannot be restored by a favorable potential bound");
    actor.state={true,true,true};
    result=invoke(field,path,true);
    check(result.status==Status::Ready && !result.can_move && actor.absolute_calls==0,"active channel forbids exit command");
    actor.state={false,false,true}; actor.hp=0;
    result=invoke(field,path,true);
    check(result.status==Status::Ready && !result.can_move && !result.safe_exit && actor.absolute_calls==0,"dead body cannot act");

    for (int suspension=1;suspension<=4;++suspension)
    {
        actor=Actor{}; actor.epoch=&epoch; actor.suspend_position=suspension;
        result=invoke(field,path,true);
        check(result.status==Status::Stale && actor.absolute_calls==0,"position-query epoch advance at unchanged origin prevents actual command");
    }
    actor=Actor{}; actor.epoch=&epoch;
    result=tt::potential_detail::authority(field,std::span<const Point>{path},true,actor,[&]{return epoch;},
        [&](Point){++epoch;return true;},[]{return true;});
    check(result.status==Status::Stale && actor.absolute_calls==0,"occupancy-query suspension revokes command authority");
    actor=Actor{}; actor.epoch=&epoch;
    result=invoke(field,path,true,true,false);
    check(result.status==Status::NeedCompute && actor.absolute_calls==0,"common reserve guard defers work without acting");
    Field incomplete{40,40,{}};
    result=invoke(incomplete,path,true);
    check(result.status==Status::NeedCompute && actor.absolute_calls==0,"unfinished potential is compute deferred");
    Field invalid{0,40,{}};
    result=invoke(invalid,path,true);
    check(result.status==Status::NeedObservation && actor.absolute_calls==0,"invalid potential supplies no action authority");
    result=invoke(field,{},true);
    check(result.status==Status::NeedObservation && actor.absolute_calls==0,"empty return suffix is unknown");
    const std::array<Point,2> jump{{{17,29},{19,30}}};
    result=invoke(field,jump,true);
    check(result.status==Status::NeedObservation && actor.absolute_calls==0,"nonadjacent suffix is rejected");
    const std::array<Point,2> exposed{{{17,29},{18,30}}};
    result=invoke(field,exposed,true);
    check(result.status==Status::NeedObservation && actor.absolute_calls==0,"exposed terminal is not a quiet anchor");
    const std::array<Point,2> displaced{{{16,29},{17,29}}};
    result=invoke(field,displaced,true);
    check(result.status==Status::NeedObservation && actor.absolute_calls==0,"actual origin mismatch rejects historical suffix");
    const std::array<Point,2> outside{{{17,29},{40,40}}};
    result=invoke(field,outside,true);
    check(result.status==Status::NeedObservation && actor.absolute_calls==0,"out of bounds suffix never acts");
    actor.hp=std::numeric_limits<float>::quiet_NaN();
    result=invoke(field,path,true);
    check(result.status==Status::NeedObservation && actor.absolute_calls==0,"invalid HP is unknown not safe");
    actor=Actor{}; actor.epoch=&epoch; actor.pos={19,30};
    const std::array<Point,1> quiet{{{19,30}}};
    result=invoke(field,quiet,true);
    check(result.status==Status::Ready && result.safe_exit && result.safe_wait && !result.moved && actor.absolute_calls==0,
          "already quiet one-point suffix requires no fictitious move");

    // Retained scalar refinement uses this exact production-shared caller. It
    // cannot import a field, replay a path, or retain prior HP/action authority.
    const Point transit_origin{17,29},transit_destination{18,30};
    const auto reset=[&] { actor=Actor{}; actor.epoch=&epoch; legal_calls=0; };
    const auto transit=[&](double required,bool legal=true,bool fuel=true) {
        return tt::potential_detail::transit_authority(required,transit_origin,transit_destination,
            actor,[&]{return epoch;},[&](Point destination){++legal_calls;return legal && destination==transit_destination;},
            [&]{return fuel;});
    };
    reset();
    result=transit(99);
    check(result.status==Status::Ready && result.safe_return && result.moved &&
          result.pos==transit_destination && actor.last==transit_destination &&
          actor.absolute_calls==1 && actor.relative_calls==0,
          "transit refines a retained word scalar into an actual absolute destination command");
    check(!result.safe_exit && !result.safe_wait && !result.safe_service,
          "transit does not invent separate wait service or exit certificates from a scalar");
    reset(); result=transit(100);
    check(result.status==Status::Ready && !result.safe_return && !result.moved &&
          actor.absolute_calls==0 && legal_calls==0,"strict scalar HP equality rejects movement admission");
    reset(); result=transit(101);
    check(result.status==Status::Ready && !result.safe_return && !result.moved,
          "retained proof cannot override actual insufficient HP");
    reset(); actor.hp=1; result=transit(0);
    check(result.status==Status::Ready && result.safe_return && result.moved,
          "zero bound still requires actual strictly positive HP");
    for (const double invalid : {std::numeric_limits<double>::quiet_NaN(),
                                 std::numeric_limits<double>::infinity(),-1.0})
    {
        reset(); result=transit(invalid);
        check(result.status==Status::NeedObservation && !result.moved &&
              actor.absolute_calls==0 && actor.position_calls==0,
              "malformed retained scalar is Unknown before own-state queries");
    }
    reset();
    result=tt::potential_detail::transit_authority(0.0,transit_origin,transit_origin,actor,
        [&]{return epoch;},[](Point){return true;},[]{return true;});
    check(result.status==Status::NeedObservation && actor.absolute_calls==0 && actor.position_calls==0,
          "transit requires a distinct adjacent proposed destination");
    result=tt::potential_detail::transit_authority(0.0,transit_origin,Point{19,29},actor,
        [&]{return epoch;},[](Point){return true;},[]{return true;});
    check(result.status==Status::NeedObservation && actor.absolute_calls==0,
          "transit scalar cannot authorize a nonadjacent jump");
    reset(); actor.pos={16,29}; result=transit(0);
    check(result.status==Status::NeedObservation && actor.absolute_calls==0 && legal_calls==0,
          "retained word origin mismatch invalidates its scalar admission");
    reset(); actor.hp=std::numeric_limits<float>::quiet_NaN(); result=transit(0);
    check(result.status==Status::NeedObservation && actor.absolute_calls==0,
          "nonfinite actual HP never supplies scalar authority");
    reset(); result=transit(0,false);
    check(result.status==Status::NeedObservation && !result.moved && actor.absolute_calls==0 && legal_calls==1,
          "current blocked or unobserved point withdraws an otherwise safe scalar leg");
    reset(); actor.accepted=false; result=transit(0);
    check(result.status==Status::NeedObservation && !result.moved && actor.absolute_calls==1 && actor.pos==transit_origin,
          "hidden native refusal preserves actual origin and revokes movement authority");
    reset(); actor.state.can_move=false; result=transit(0);
    check(result.status==Status::Ready && result.safe_return && !result.can_move && !result.moved &&
          actor.absolute_calls==0 && legal_calls==0,"spent movement returns a fresh scalar assessment without moving");
    reset(); actor.state.alive=false; result=transit(0);
    check(result.status==Status::NeedObservation && !result.can_move && actor.absolute_calls==0,
          "dead body withdraws transit action authority despite a favorable scalar");
    reset(); actor.state.channeling=true; result=transit(0);
    check(result.status==Status::NeedObservation && !result.can_move && actor.absolute_calls==0,
          "active channel withdraws transit action authority");
    reset(); result=transit(0,true,false);
    check(result.status==Status::NeedCompute && actor.absolute_calls==0 && actor.position_calls==0,
          "missing entry headroom defers before any actor query or command");
    for (int suspension=1;suspension<=3;++suspension)
    {
        reset(); actor.suspend_position=suspension; result=transit(0);
        check(result.status==Status::Stale && actor.absolute_calls==0,
              "transit rejects an epoch advance during every position-query boundary");
    }
    reset(); actor.suspend_health=true; result=transit(0);
    check(result.status==Status::Stale && actor.absolute_calls==0,
          "epoch was captured before health query suspension");
    reset(); actor.suspend_flags=true; result=transit(0);
    check(result.status==Status::Stale && actor.absolute_calls==0,
          "epoch was captured before flags query suspension");
    for (int suspension=2;suspension<=3;++suspension)
    {
        reset(); int clock_calls=0;
        result=tt::potential_detail::transit_authority(0.0,transit_origin,transit_destination,actor,
            [&]{if(++clock_calls==suspension)++epoch;return epoch;},[](Point){return true;},[]{return true;});
        check(result.status==Status::Stale && actor.absolute_calls==0,
              "epoch query resumes in its new owner epoch before publication or command");
    }
    reset();
    result=tt::potential_detail::transit_authority(0.0,transit_origin,transit_destination,actor,
        [&]{return epoch;},[&](Point){++epoch;return true;},[]{return true;});
    check(result.status==Status::Stale && actor.absolute_calls==0,
          "point-occupancy query suspension cannot consume retained scalar authority");
    reset(); int headroom_calls=0;
    result=tt::potential_detail::transit_authority(0.0,transit_origin,transit_destination,actor,
        [&]{return epoch;},[](Point){return true;},[&]{return ++headroom_calls==1;});
    check(result.status==Status::NeedCompute && actor.absolute_calls==0 && headroom_calls==2,
          "headroom is freshly checked after current landing observations");
    reset(); actor.suspend_move=true; result=transit(0);
    check(result.status==Status::Stale && !result.moved && actor.absolute_calls==1,
          "post-command epoch change is reported stale without a new movement certificate");
    std::printf("independent potential: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
