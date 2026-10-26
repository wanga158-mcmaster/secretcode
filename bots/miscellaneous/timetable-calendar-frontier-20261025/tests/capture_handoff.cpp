#include "../src/capture_handoff.h"
#define TT_CAPTURE_FIXTURE
#include "../src/independent_capture.cpp"
#define TT_POTENTIAL_FIXTURE
#include "../src/independent_potential.cpp"
#include "../src/production_walk.h"
#include <array>
#include <cstdio>
#include <vector>

namespace {
namespace capture=tt::capture_detail;
namespace potential=mb::production::potential;
namespace handoff=tt::capture_handoff;
using Point=potential::Point;
struct Member {Point pos;float strength{};int range{};};
struct Group {std::vector<Member> members;};
struct Body
{
    Point pos{2,2};float hp{100};int epoch{60};
    bool alive{true},channel{},can_move{true},accepted{true};
    int capture_moves{},ordinary_moves{},lifecycle_queries{},capture_legal_queries{};
};
struct CaptureActor
{
    Body& body;
    Point position() const{return body.pos;}
    float health() const{return body.hp;}
    capture::Bag bag() const{return {};}
    capture::Flags flags() const{return {body.alive,body.channel,body.can_move};}
    bool move_absolute(Point to){++body.capture_moves;if(body.accepted){body.pos=to;body.can_move=false;}return body.accepted;}
};
struct OrdinaryActor
{
    Body& body;
    Point position() const{return body.pos;}
    float health() const{return body.hp;}
    tt::potential_detail::Flags flags() const{return {body.alive,body.channel,body.can_move};}
    bool move_absolute(Point to){++body.ordinary_moves;if(body.accepted){body.pos=to;body.can_move=false;}return body.accepted;}
};
int checks{},failures{};
void check(bool good,const char* label){++checks;if(!good){++failures;std::printf("FAIL %s\n",label);}}

handoff::Facts facts(const capture::Result<Point>& observed,Point expected,int epoch,
                    bool member=false,bool quiet=true,int selected=0)
{
    return {true,observed.epoch==epoch && observed.group==selected && observed.pos==expected,
            member,quiet,observed.status==capture::Status::Ready,
            observed.status==capture::Status::NeedObservation,
            observed.availability==capture::Life::Unknown,observed.availability==capture::Life::Alive,
            observed.availability==capture::Life::Respawning};
}
potential::Field ready_field()
{
    potential::Field field{30,30,{{{4,2},40,0},{{25,25},10,1}}};
    while(field.status()==potential::Status::NeedCompute)field.advance(1024);
    return field;
}

void actual_guard_handoff_contract()
{
    const std::vector<Group> groups{{{{{4,2},40,0}}},{{{{25,25},10,1}}}};
    Body body;CaptureActor actor{body};capture::Life life=capture::Life::Unknown;
    const auto capture_step=[&](Point destination,bool staging=false) {
        const auto from=body.pos;
        return capture::authority(groups,0,from,destination,actor,[&]{return body.epoch;},
            [&](int){++body.lifecycle_queries;return life;},[]{return false;},
            [&](Point){++body.capture_legal_queries;return true;},[]{return true;},staging);
    };
    auto result=capture_step({2,2},true);
    check(result.status==capture::Status::NeedObservation && result.availability==capture::Life::Unknown &&
          !result.safe_input && !result.holding && !result.moved && body.capture_moves==0 &&
          body.lifecycle_queries==1 && body.capture_legal_queries==0,
          "actual unknown capture staging reads lifecycle but emits no capture move or hold authority");
    const auto choice=handoff::decide(facts(result,{2,2},60));
    check(choice==handoff::Decision::ObservationIngress && body.pos==Point{2,2} && body.capture_moves==0,
          "dated unknown nonmember P0 nomination hands off without acting or deleting its identity");

    auto field=ready_field();
    tt::production_walk::Trail history{16};history.anchor_quiet({2,2});
    const auto quiet=[&](tt::production_walk::Point p){return field.at({p.x,p.y}).upper==0;};
    const auto word=tt::production_walk::prefix_word(history,{2,2},{3,2},quiet);
    std::vector<Point> route;for(const auto p:word)route.push_back({p.x,p.y});
    OrdinaryActor ordinary{body};
    const auto admitted=tt::potential_detail::authority(field,std::span<const Point>{route},true,ordinary,
        [&]{return body.epoch;},[](Point){return true;},[]{return true;},true);
    check(choice==handoff::Decision::ObservationIngress && admitted.status==tt::potential_detail::Status::Ready &&
          admitted.safe_return && admitted.moved && body.pos==Point{3,2} && body.ordinary_moves==1 && body.capture_moves==0,
          "yielded input advances only through actual fresh CertifiedEntry authority over a complete P0 word");
    check(history.record({2,2},{3,2},true,true)==tt::production_walk::Status::Known &&
          history.make_return({3,2})==std::vector<tt::production_walk::Point>({{3,2}}),
          "actual ordinary movement records a new quiet anchor without inventing capture progress");

    ++body.epoch;body.can_move=true; // the ordinary edge consumed its owner input
    life=capture::Life::Reward;
    result=capture_step({4,2});
    check(handoff::decide(facts(result,{4,2},61,true))==handoff::Decision::KeepGuardedCapture &&
          result.status==capture::Status::Ready && result.availability==capture::Life::Reward &&
          result.safe_input && result.moved && body.capture_moves==1 && body.pos==Point{4,2},
          "subsequent actual Reward guard alone enables the selected capture step");
    ++body.epoch;
    result=capture_step({4,2});
    check(result.status==capture::Status::Ready && result.safe_input && result.holding && !result.moved &&
          body.capture_moves==1,"later capture Hold re-establishes actual Reward instead of reusing ingress authority");

    body=Body{};life=capture::Life::Alive;result=capture_step({3,2});
    check(handoff::decide(facts(result,{2,2},60))==handoff::Decision::RevalueWork &&
          !result.safe_input && !result.moved && body.capture_moves==0,
          "actual Alive withdraws stale Capture for work valuation without issuing a capture command");
    life=capture::Life::Respawning;result=capture_step({3,2});
    check(handoff::decide(facts(result,{2,2},60))==handoff::Decision::RefuteNominee &&
          !result.safe_input && !result.moved && body.capture_moves==0,
          "actual Respawning refutes only this nomination and never manufactures availability at timer zero");
}

void ordinary_authority_cannot_be_skipped()
{
    auto field=ready_field();
    const std::array<Point,2> word{{{2,2},{3,2}}};
    const handoff::Facts input{true,true,false,true,false,true,true,false,false};
    check(handoff::decide(input)==handoff::Decision::ObservationIngress,"quiet unknown frame nominates ordinary ingress");
    Body body;OrdinaryActor actor{body};
    auto result=tt::potential_detail::authority(field,std::span<const Point>{word},true,actor,
        [&]{return body.epoch;},[](Point){return false;},[]{return true;},true);
    check(result.status==tt::potential_detail::Status::NeedObservation && !result.moved && body.ordinary_moves==0,
          "handoff cannot bypass a freshly blocked ordinary destination");
    body.can_move=false;
    result=tt::potential_detail::authority(field,std::span<const Point>{word},true,actor,
        [&]{return body.epoch;},[](Point){return true;},[]{return true;},true);
    check(result.status==tt::potential_detail::Status::Ready && !result.can_move && !result.moved && body.ordinary_moves==0,
          "handoff cannot restore an already spent movement allowance");
    body.can_move=true;
    result=tt::potential_detail::authority(field,std::span<const Point>{word},true,actor,
        [&]{return body.epoch;},[&](Point){++body.epoch;return true;},[]{return true;},true);
    check(result.status==tt::potential_detail::Status::Stale && !result.moved && body.ordinary_moves==0,
          "fresh ordinary epoch fence rejects a suspension after handoff");
}

void restricted_decision_contract()
{
    const handoff::Facts base{true,true,false,true,false,true,true,false,false};
    auto f=base;f.dated_capture=false;
    check(handoff::decide(f)==handoff::Decision::KeepGuardedCapture,"no dated Capture task means no capture observation handoff");
    f=base;f.same_group_input=false;
    check(handoff::decide(f)==handoff::Decision::KeepGuardedCapture,"group/origin/owner-input mismatch withdraws handoff");
    f=base;f.on_member=true;
    check(handoff::decide(f)==handoff::Decision::KeepGuardedCapture,"unknown source occupancy cannot become an observation departure claim");
    f=base;f.quiet_origin=false;
    check(handoff::decide(f)==handoff::Decision::KeepGuardedCapture,"positive exposure retains the existing recovery/guard owner");
    f=base;f.need_observation=false;
    check(handoff::decide(f)==handoff::Decision::KeepGuardedCapture,"NeedCompute or unsupported result does not masquerade as missing observation");
    f=base;f.unknown=false;
    check(handoff::decide(f)==handoff::Decision::KeepGuardedCapture,"a refused Reward guard cannot become unknown ingress");
    f=base;f.ready=true;
    check(handoff::decide(f)==handoff::Decision::KeepGuardedCapture,"contradictory status flags grant no handoff");
    f=base;f.alive=true;
    check(handoff::decide(f)==handoff::Decision::KeepGuardedCapture,"contradictory lifecycle flags grant no handoff");
    f=base;f.need_observation=false;f.ready=true;f.unknown=false;
    check(handoff::decide(f)==handoff::Decision::KeepGuardedCapture,"fresh Reward remains solely with actual capture Step/Hold guard");
}
} // namespace

int main()
{
    actual_guard_handoff_contract();ordinary_authority_cannot_be_skipped();restricted_decision_contract();
    std::printf("capture handoff: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
