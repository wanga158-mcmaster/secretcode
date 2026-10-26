#include "../src/route.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <type_traits>

namespace A=mosaic::adaptive;
namespace G=mosaic::native_service;
static unsigned checks{},groups{};
static void check(bool value,const char* expression,int line){
    ++checks;if(!value){std::fprintf(stderr,"route check failed at %d: %s\n",line,expression);std::abort();}
}
#define CHECK(...) check(bool(__VA_ARGS__),#__VA_ARGS__,__LINE__)
static constinit A::Route<> constant_route{};
static_assert(!std::is_copy_constructible_v<A::Route<>>&&!std::is_move_constructible_v<A::Route<>>);
static_assert(std::is_same_v<decltype(constant_route.navigation()),const G::Navigation<>&>);

// These are actual retained geometry and receipt reducers. Admission callbacks
// count real one-unit leaves; they do not measure native fuel or grant SDK
// movement, occupancy, visibility, survival, capture or banking authority.
template<std::size_t Cells> static void finish(A::Route<Cells>& route){
    unsigned guard=0;
    while(route.navigation().status()==A::RouteStatus::preparing){
        unsigned calls=0;const auto progress=route.advance(3,[&]{++calls;return true;});
        CHECK(progress.units<=3&&progress.units==calls&&progress.progressed);
        CHECK(++guard<Cells*4+300);
    }
}
template<std::size_t Cells> static void same_geometry(const A::Route<Cells>& route,
                                                     const G::Navigation<Cells>& reference,int width,int height){
    CHECK(route.navigation().status()==reference.status());
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        CHECK(route.navigation().available({x,y})==reference.available({x,y}));
        CHECK(route.navigation().distance({x,y})==reference.distance({x,y}));
    }
}
static bool contains(const A::Candidates& candidates,A::Point point){
    for(unsigned i=0;i<candidates.count;++i)if(candidates.points[i]==point)return true;
    return false;
}

static void denied_preparation_retains_progress_and_the_physical_direct_nominee(){
    ++groups;A::Route<25> route;G::Navigation<25> reference;
    std::array<std::uint8_t,25> board{};board.fill(1);const std::array<A::Point,1> roots{{{4,4}}};
    CHECK(route.begin(5,5,board,roots,100)&&reference.begin(5,5,board,roots,100));
    CHECK(route.begin_input({1,0,0},{0,0}));const auto revision=route.revision();unsigned calls=0;
    const auto denied=route.advance(32,[&]{++calls;return false;});
    CHECK(denied.status==A::RouteStatus::preparing&&denied.units==0&&!denied.progressed&&calls==1);
    same_geometry(route,reference,5,5);
    const auto direct=route.nominees(A::Point{4,4});
    CHECK(direct.count==1&&direct.direct&&!direct.field_ready&&direct.points[0]==A::Point{1,1});
    CHECK(route.ledger().attempts==0&&route.ledger().accepted==0);
    calls=0;const auto partial=route.advance(7,[&]{++calls;return true;});
    CHECK(partial.units==7&&partial.progressed&&calls==7&&reference.step(7).units==7);
    CHECK(route.begin(5,5,board,roots,100)&&route.revision()==revision&&route.semantic()==100);
    same_geometry(route,reference,5,5);
    calls=0;CHECK(route.advance(32,[&]{++calls;return false;}).units==0&&calls==1);
    CHECK(route.advance(18,[]{return true;}).units==18&&reference.step(18).units==18);
    CHECK(!route.navigation().available({4,4}));
    CHECK(route.advance(1,[]{return true;}).units==1&&reference.step(1).units==1);
    CHECK(route.navigation().distance({4,4})==0);same_geometry(route,reference,5,5);
    while(route.navigation().status()==A::RouteStatus::preparing){
        const auto progress=route.advance(3,[]{return true;});const auto expected=reference.step(3);
        CHECK(progress.units==expected.units&&progress.status==expected.status);same_geometry(route,reference,5,5);
    }
    CHECK(route.revision()==revision&&route.navigation().distance({0,0})==4);
    const auto ready=route.nominees(A::Point{4,4});
    CHECK(ready.count==1&&ready.field_ready&&!ready.direct&&ready.points[0]==A::Point{1,1});
}

static void observed_occupants_are_not_future_landing_prohibitions(){
    ++groups;A::Route<9> route;std::array<std::uint8_t,9> board{};board.fill(1);
    CHECK(route.begin(3,3,board,std::array<A::Point,1>{{{2,1}}},1));finish(route);
    const auto revision=route.revision();CHECK(route.begin_input({10,1,0},{0,1}));
    const auto initial=route.nominees();CHECK(initial.field_ready&&initial.count==3);
    CHECK(contains(initial,{1,0})&&contains(initial,{1,1})&&contains(initial,{1,2}));
    CHECK(route.reject({0,1},{1,0},{10,1,0},A::BlockKind::champion));
    CHECK(route.begin_input({10,2,1},{0,1})); // FALSE advances attempt, not the owner input.
    const auto alternative=route.nominees();CHECK(alternative.count==2&&!contains(alternative,{1,0}));
    CHECK(route.reject({0,1},{1,1},{10,2,1},A::BlockKind::teleport));
    CHECK(route.reject({0,1},{1,2},{10,2,2},A::BlockKind::unknown));
    CHECK(route.nominees().count==0);
    CHECK(route.begin_input({11,0,0},{0,1})&&route.nominees().count==3&&route.revision()==revision);
    CHECK(route.record({1,{10,2,2},{0,1},{1,1},A::MoveOutcome::refused}).recorded);
    CHECK(route.ledger().refused==1&&route.nominees().count==3); // Late history is not a new occupancy observation.
    unsigned round=12;
    for(const auto kind:std::array<A::BlockKind,4>{A::BlockKind::camp,A::BlockKind::champion,A::BlockKind::teleport,A::BlockKind::unknown}){
        CHECK(route.begin_input({int(round),0,0},{0,1}));
        CHECK(route.reject({0,1},{1,1},{int(round),0,0},kind));
        CHECK(!contains(route.nominees(),{1,1}));
        CHECK(route.begin_input({int(++round),0,0},{0,1}));
        CHECK(contains(route.nominees(),{1,1})&&route.revision()==revision);
    }
    // Fresh clear evidence can release a landing within this same input. Its
    // tombstone also prevents an older delayed blocked observation reviving it.
    CHECK(route.begin_input({20,1,0},{0,1}));
    CHECK(route.reject({0,1},{1,1},{20,2,3},A::BlockKind::camp));
    CHECK(route.begin_input({20,3,4},{0,1})&&!contains(route.nominees(),{1,1}));
    CHECK(!route.observe_clear({0,1},{1,1},{20,1,2})&&!contains(route.nominees(),{1,1}));
    CHECK(route.observe_clear({0,1},{1,1},{20,3,4})&&contains(route.nominees(),{1,1}));
    CHECK(!route.reject({0,1},{1,1},{20,2,3},A::BlockKind::unknown)&&contains(route.nominees(),{1,1}));
    CHECK(route.reject({0,1},{1,1},{20,4,4},A::BlockKind::camp));
    CHECK(!route.begin_input({19,100,100},{0,1})&&!contains(route.nominees(),{1,1}));
    CHECK(route.begin_input({20,5,4},{1,1}));
    const auto moved=route.nominees();CHECK(moved.count==1&&moved.points[0]==A::Point{2,1});
    CHECK(!route.reject({0,1},{1,1},{20,5,4},A::BlockKind::camp));
    CHECK(!route.reject({1,1},{2,1},{20,5,4},static_cast<A::BlockKind>(255)));
}

static void cold_direct_keeps_a_supported_alternative_without_inventing_free_occupancy(){
    ++groups;A::Route<10> route;std::array<std::uint8_t,10> board{};board.fill(1);
    const std::array<A::Point,1> roots{{{4,1}}};CHECK(route.begin(5,2,board,roots,1));
    CHECK(route.begin_input({1,0,0},{0,0}));
    const auto first=route.nominees(A::Point{4,1});
    CHECK(first.count==1&&first.direct&&!first.field_ready&&first.points[0]==A::Point{1,1});
    CHECK(route.reject({0,0},{1,1},{1,0,0},A::BlockKind::camp));
    const auto alternative=route.nominees(A::Point{4,1});
    CHECK(alternative.count==1&&alternative.direct&&alternative.points[0]==A::Point{1,0});
    CHECK(route.reject({0,0},{1,0},{1,0,1},A::BlockKind::champion));
    CHECK(route.nominees(A::Point{4,1}).count==0);
    CHECK(route.begin_input({2,0,0},{0,0}));
    CHECK(route.nominees(A::Point{4,1}).points[0]==A::Point{1,1});
    CHECK(route.ledger().attempts==0&&route.ledger().accepted==0&&route.navigation().status()==A::RouteStatus::preparing);
    route.retire();board[6]=0;CHECK(route.begin(5,2,board,roots,2));
    CHECK(route.nominees(A::Point{4,1}).points[0]==A::Point{1,0});
}

static void per_origin_ready_and_partial_admission_do_not_wait_for_global_ready(){
    ++groups;A::Route<9> route;std::array<std::uint8_t,9> board{};board.fill(1);
    CHECK(route.begin(9,1,board,std::array<A::Point,1>{{{0,0}}},17));CHECK(route.begin_input({1,0,0},{2,0}));
    const auto revision=route.revision();unsigned calls=0;
    const auto partial=route.advance(32,[&]{return ++calls<=3;});
    CHECK(partial.units==3&&partial.progressed&&partial.status==A::RouteStatus::preparing&&calls==4);
    for(unsigned guard=0;!route.navigation().available({2,0});++guard){
        CHECK(guard<16&&route.advance(1,[]{return true;}).units==1);
    }
    CHECK(route.navigation().status()==A::RouteStatus::preparing);
    const auto candidates=route.nominees(A::Point{8,0});
    CHECK(candidates.field_ready&&!candidates.direct&&candidates.count==1&&candidates.points[0]==A::Point{1,0});
    CHECK(route.navigation().distance({2,0})==2);
    calls=0;const auto denied=route.advance(32,[&]{++calls;return false;});
    CHECK(denied.units==0&&!denied.progressed&&calls==1&&route.navigation().distance({2,0})==2);
    CHECK(route.revision()==revision&&route.nominees().count==1);
    calls=0;const auto finished=route.advance(32,[&]{++calls;return true;});
    CHECK(finished.units==7&&finished.units==calls&&finished.status==A::RouteStatus::ready);
    calls=0;const auto idle=route.advance(32,[&]{++calls;return true;});
    CHECK(idle.units==0&&!idle.progressed&&idle.status==A::RouteStatus::ready&&calls==0);
}

static void move_receipts_distinguish_prevalidation_false_and_actual_applied(){
    ++groups;A::Route<5> route;std::array<std::uint8_t,5> board{};board.fill(1);
    CHECK(route.begin(5,1,board,std::array<A::Point,1>{{{0,0}}},1));finish(route);
    CHECK(route.begin_input({7,0,0},{4,0}));
    const A::MoveRecord no_attempt{1,{7,0,0},{4,0},{3,0},A::MoveOutcome::not_attempted};
    const auto prevalidation=route.record(no_attempt);
    CHECK(prevalidation.recorded&&!prevalidation.raw_attempt&&!prevalidation.accepted&&!prevalidation.strict_progress);
    CHECK(route.ledger().not_attempted==1&&route.ledger().attempts==0&&route.ledger().accepted==0);
    CHECK(!route.record(no_attempt).recorded&&route.nominees().count==0);
    CHECK(route.begin_input({7,0,1},{4,0})&&route.nominees().count==0);
    const auto refused=route.record({2,{7,0,1},{4,0},{3,0},A::MoveOutcome::refused});
    CHECK(refused.recorded&&refused.raw_attempt&&!refused.accepted&&!refused.strict_progress);
    CHECK(route.ledger().attempts==1&&route.ledger().refused==1&&route.ledger().not_attempted==1);
    CHECK(route.begin_input({8,0,0},{4,0})&&route.nominees().count==1);
    const auto accepted=route.record({3,{8,0,0},{4,0},{3,0},A::MoveOutcome::applied});
    CHECK(accepted.recorded&&accepted.raw_attempt&&accepted.accepted&&accepted.strict_progress);
    CHECK(route.ledger().accepted==1&&route.ledger().attempts==2&&route.ledger().distance_progress==1);
    // Command truth does not manufacture a fresh self observation or restore M.
    CHECK(route.nominees().points[0]==A::Point{3,0});
    CHECK(route.begin_input({8,1,1},{3,0})&&route.nominees().points[0]==A::Point{2,0});
    const A::MoveRecord unknown{4,{8,1,1},{3,0},{2,0},A::MoveOutcome::unknown};
    const auto pending=route.record(unknown);
    CHECK(pending.recorded&&pending.raw_attempt&&!pending.accepted&&route.ledger().unresolved==1&&route.ledger().attempts==3);
    auto resolved=unknown;resolved.outcome=A::MoveOutcome::applied;
    const auto resolution=route.record(resolved);
    CHECK(resolution.recorded&&resolution.accepted&&resolution.strict_progress);
    CHECK(route.ledger().unresolved==0&&route.ledger().attempts==3&&route.ledger().accepted==2&&route.ledger().distance_progress==2);
    CHECK(!route.record(resolved).recorded&&!route.record({2,{7,0,1},{4,0},{3,0},A::MoveOutcome::applied}).recorded);
    CHECK(route.begin_input({9,0,0},{2,0}));
    const A::MoveRecord late{5,{9,0,0},{2,0},{1,0},A::MoveOutcome::unknown};CHECK(route.record(late).recorded);
    CHECK(route.begin(5,1,board,std::array<A::Point,1>{{{0,0}}},2));finish(route);
    auto wrong=late;wrong.epoch.attempt=1;wrong.outcome=A::MoveOutcome::applied;
    CHECK(!route.record(wrong).recorded&&route.ledger().unresolved==1);
    auto later=late;later.outcome=A::MoveOutcome::applied;const auto changed_goal=route.record(later);
    CHECK(changed_goal.recorded&&changed_goal.accepted&&!changed_goal.strict_progress);
    CHECK(route.ledger().accepted==3&&route.ledger().attempts==4&&route.ledger().distance_progress==2&&route.ledger().unresolved==0);
}

static void static_board_roots_and_reentrant_admission_have_explicit_lifetimes(){
    ++groups;A::Route<9> route;std::array<std::uint8_t,9> board{};board.fill(1);
    std::array<A::Point,2> roots{{{2,0},{2,2}}};CHECK(route.begin(3,3,board,roots,1));const auto first=route.revision();
    CHECK(route.begin_input({1,0,0},{0,1}));CHECK(route.advance(3,[]{return true;}).units==3);
    CHECK(route.begin(3,3,board,roots,1)&&route.revision()==first);
    std::swap(roots[0],roots[1]);CHECK(route.begin(3,3,board,roots,1)&&route.revision()!=first);
    const auto reordered=route.revision();unsigned calls=0;
    const auto rebound=route.advance(32,[&]{++calls;CHECK(route.begin(3,3,board,roots,2));return true;});
    CHECK(rebound.units==0&&!rebound.progressed&&rebound.status==A::RouteStatus::preparing&&calls==1&&route.revision()!=reordered);
    calls=0;const auto retired=route.advance(32,[&]{++calls;route.retire();return true;});
    CHECK(retired.status==A::RouteStatus::idle&&retired.units==0&&!retired.progressed&&calls==1&&route.revision()==0);
    // Only immutable static terrain lives in the byte board. Retire before an
    // edit, then begin a new semantic revision; observed occupants never edit it.
    board[1]=board[4]=board[7]=0;CHECK(route.begin(3,3,board,roots,3));
    CHECK(route.nominees(A::Point{2,1}).count==0);finish(route);
    CHECK(route.navigation().status({0,1})==A::RouteStatus::unreachable&&route.nominees(A::Point{2,1}).count==0);
    route.retire();board[4]=1;CHECK(route.begin(3,3,board,roots,4));finish(route);
    CHECK(route.navigation().distance({0,1})==2&&route.nominees().count==1&&route.nominees().points[0]==A::Point{1,1});
    route.retire();CHECK(route.begin(3,3,board,std::span<const A::Point>{},5));
    calls=0;CHECK(route.advance(32,[&]{++calls;return true;}).status==A::RouteStatus::unreachable&&calls==0);
    CHECK(!route.begin(0,3,board,roots,6)&&route.revision()==0&&route.nominees(A::Point{2,1}).count==0);
    CHECK(!route.begin(3,3,std::span<const std::uint8_t>{board.data(),8},roots,7));
    CHECK(!route.begin(3,3,board,std::array<A::Point,1>{{{3,0}}},8));
    const std::array<A::Point,257> too_many{};CHECK(!route.begin(3,3,board,too_many,9));
    CHECK(!route.record({0,{1,0,0},{0,0},{1,0},A::MoveOutcome::applied}).recorded);
    CHECK(constant_route.navigation().status()==A::RouteStatus::idle&&constant_route.revision()==0);
}

int main(){
    denied_preparation_retains_progress_and_the_physical_direct_nominee();
    observed_occupants_are_not_future_landing_prohibitions();
    cold_direct_keeps_a_supported_alternative_without_inventing_free_occupancy();
    per_origin_ready_and_partial_admission_do_not_wait_for_global_ready();
    move_receipts_distinguish_prevalidation_false_and_actual_applied();
    static_board_roots_and_reentrant_admission_have_explicit_lifetimes();
    std::printf("PASS route: %u checks, %u groups\n",checks,groups);
}
