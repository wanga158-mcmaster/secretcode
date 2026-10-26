#include "production_recon.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <type_traits>
#include <utility>

namespace {std::size_t allocations{};}
void* operator new(std::size_t size) {
    ++allocations;
    if(auto* memory=std::malloc(size?size:1))return memory;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* memory) noexcept{std::free(memory);}
void operator delete[](void* memory) noexcept{std::free(memory);}
void operator delete(void* memory,std::size_t) noexcept{std::free(memory);}
void operator delete[](void* memory,std::size_t) noexcept{std::free(memory);}

namespace {
using namespace tt::production_recon;
constexpr Key key{7,11,13,17,19};
static_assert(!std::is_copy_constructible_v<Search> && std::is_nothrow_move_constructible_v<Search>);
int checks{},failures{};
void check(bool value,const char* label) {
    ++checks;
    if(!value){++failures;std::printf("FAIL %s\n",label);}
}
template<int Width,int Height>struct Board {
    std::array<Cell,Width*Height> cells{};
    std::array<bool,Width*Height> goals{};
    std::size_t cell_calls{},goal_calls{};
    std::size_t index(Point p)const{return static_cast<std::size_t>(p.y*Width+p.x);}
    Cell read(Point p){++cell_calls;return cells[index(p)];}
    bool goal(Point p){++goal_calls;return goals[index(p)];}
};
template<class BoardType>
Status step(Search& search,BoardType& board,Key actual=key,std::size_t units=1) {
    const auto before=allocations;
    const auto callbacks=board.cell_calls+board.goal_calls;
    const auto result=search.advance(actual,search.origin(),units,
        [&](Point p){return board.read(p);},[&](Point p){return board.goal(p);});
    check(allocations==before,"hot advance does not allocate bookkeeping or paths");
    check(search.last_work()<=units && board.cell_calls+board.goal_calls-callbacks<=units,
          "each call obeys the caller work bound including cell and goal callbacks");
    return result;
}
template<class BoardType>
bool finish_compute(Search& search,BoardType& board,Key actual=key,std::size_t units=1) {
    for(int calls=0;calls<10000 && search.status()==Status::NeedCompute;++calls)step(search,board,actual,units);
    return search.status()!=Status::NeedCompute;
}
template<class BoardType>
bool walk_frontier(Search& search,BoardType& board,Key actual=key) {
    for(int inputs=0;inputs<1000;++inputs) {
        if(!finish_compute(search,board,actual))return false;
        if(search.status()==Status::AtFrontier)return true;
        const auto leg=search.next_leg(actual,search.origin());
        if(!leg)return false;
        const auto dx=leg->destination.x-leg->origin.x,dy=leg->destination.y-leg->origin.y;
        check(dx>=-1 && dx<=1 && dy>=-1 && dy<=1 && (dx || dy),"dated next leg is one adjacent geometric edge");
        check(board.cells[board.index(leg->destination)]==Cell::QuietOpen,"tree route never traverses an immutable closed or unknown cell");
        check(search.record_applied(actual,leg->origin,leg->destination),"only the separately acknowledged actual move consumes a leg");
    }
    return false;
}
void typed_causes() {
    check(select_resolver(MenuCause::WaitingForEligibility)==Resolver::WaitEligibility,
          "a named future eligibility event selects its own wait class");
    check(select_resolver(MenuCause::MenuExhausted)==Resolver::NamedQuietFrontier,
          "a completed empty menu selects a finite physical resolver");
    check(select_resolver(MenuCause::Incomplete)==Resolver::ResumeMenu &&
          select_resolver(MenuCause::UnknownGeometry)==Resolver::ResolveGeometry &&
          select_resolver(MenuCause::UnsupportedModel)==Resolver::InvestmentOrOtherClass,
          "unfinished computation unknown geometry and unsupported model do not become permanent idle or arbitrary walking");
}
void obstacle_and_rank() {
    Board<7,3> board;
    board.cells[board.index({3,0})]=board.cells[board.index({3,1})]=Cell::Closed;
    board.goals[board.index({2,0})]=board.goals[board.index({6,1})]=true;
    Search search{7,3,{0,1},key,21};
    check(step(search,board,key,0)==Status::NeedCompute && search.visited()==0 && search.last_work()==0,
          "zero work cannot seed or inspect an incomplete graph");
    check(walk_frontier(search,board) && search.origin()==Point{2,0},
          "BFS reaches the first named untried frontier rather than an arbitrary quiet tile");
    const auto first_visited=search.visited();
    check(!search.complete_frontier(key,search.origin(),MenuCause::Incomplete) && search.status()==Status::AtFrontier,
          "an unfinished actual menu cannot mark a physical origin tried");
    check(search.complete_frontier(key,search.origin(),MenuCause::MenuExhausted) && search.attempted_origins()==2,
          "actual completed negative query advances the named origin rank");
    check(walk_frontier(search,board) && search.origin()==Point{6,1} && search.visited()>=first_visited,
          "next frontier retains BFS rank across origins and routes around the closed barrier");
    check(search.complete_frontier(key,search.origin(),MenuCause::MenuExhausted),"second observed negative frontier is acknowledged once");
    check(finish_compute(search,board) && search.status()==Status::Unsupported && search.visited()==19 &&
          search.expanded()==19 && search.attempted_origins()==3,
          "finite named-class exhaustion follows full reachable quiet-component enumeration");
    const auto visited=search.visited(),attempted=search.attempted_origins();
    for(int round=0;round<5;++round)step(search,board,key,64);
    check(search.status()==Status::Unsupported && search.visited()==visited && search.attempted_origins()==attempted,
          "new owner rounds do not reset settled negative origin rank");
}
void tree_route_between_origins() {
    Board<3,3> board;
    board.goals[board.index({1,2})]=board.goals[board.index({2,1})]=true;
    Search search{3,3,{1,1},key,9};
    check(walk_frontier(search,board) && search.origin()==Point{1,2},"first same-depth frontier follows declared compass BFS order");
    check(search.complete_frontier(key,search.origin(),MenuCause::MenuExhausted) && finish_compute(search,board),
          "second same-depth frontier becomes a retained tree route");
    const auto first=search.next_leg(key,{1,2});
    check(first && first->destination==Point{1,1} && first->frontier==Point{2,1},
          "a new-origin route ascends through the retained common ancestor");
    check(walk_frontier(search,board) && search.origin()==Point{2,1},"tree route descends the other branch without rebuilding visited state");
}
void dynamic_defer_and_relocate() {
    Board<7,1> board;board.goals[6]=true;
    Search search{7,1,{0,0},key,7};
    check(finish_compute(search,board) && search.status()==Status::ReadyLeg,"long named goal publishes a dated first leg");
    auto leg=search.next_leg(key,search.origin());
    check(leg && leg->destination==Point{1,0},"long route starts at the actual origin");
    const auto visited=search.visited(),attempted=search.attempted_origins();
    check(leg && search.defer_leg(key,leg->origin,leg->destination),"native refusal defers the offered edge");
    for(int round=0;round<3;++round)step(search,board,key,64);
    check(search.status()==Status::Deferred && search.visited()==visited && search.attempted_origins()==attempted,
          "dynamic deferral is neither a permanent wall nor a tried observation origin");
    check(leg && !search.record_applied(key,leg->origin,leg->destination),"a deferred edge is not presumed to have happened");
    check(search.retry_deferred(key,search.origin()),"caller evidence can reopen physical legality without resetting geometry rank");
    check(leg && search.record_applied(key,leg->origin,leg->destination),"fresh actual movement consumes the reopened edge");
    // Parent climbs from target6 toward actual1 across several bounded calls.
    // Repeated same-origin synchronization must not destroy that partial rank.
    for(int owner=0;owner<10 && search.status()==Status::NeedCompute;++owner) {
        check(search.relocate(key,search.origin()),"unchanged actual pose is a no-op synchronization");
        step(search,board);
    }
    check(search.status()==Status::ReadyLeg,"same-origin synchronization preserves a multi-input parent climb");
    check(search.relocate(key,{4,0}) && search.visited()==visited && search.attempted_origins()==attempted,
          "separately observed relocation within the known tree preserves the named family rank");
    check(walk_frontier(search,board) && search.origin()==Point{6,0},"external known-tree relocation rebuilds only its dated next leg");
}
void generation_and_unknown_geometry() {
    Board<3,1> board;board.cells[1]=Cell::Unknown;board.goals[2]=true;
    Search search{3,1,{0,0},key,3};
    check(finish_compute(search,board) && search.status()==Status::UnknownGeometry && search.attempted_origins()==1,
          "unknown geometry never becomes a physical disconnection or named-class exhaustion");
    check(!search.relocate(key,search.origin()),"terminal unknown geometry cannot be silently reopened through relocation");
    board.cells[1]=Cell::QuietOpen;
    step(search,board,key,64);
    check(search.status()==Status::UnknownGeometry,"changing caller data without a new key cannot revive a settled query");
    auto changed=key;++changed.graph;
    check(search.begin_after_negative({0,0},changed)==Status::NeedCompute && search.visited()==0,
          "explicit generation reset starts a new dated graph without reallocating storage");
    check(walk_frontier(search,board,changed),"the resolved keyed geometry supports a fresh actual frontier trip");
    auto wrong=changed;++wrong.family;
    check(!search.next_leg(wrong,search.origin()) && search.status()==Status::Stale,
          "even a terminal result validates the named family generation");
    check(!search.relocate(changed,search.origin()),"a stale generation requires explicit reset");
    check(search.begin_after_negative({0,0},wrong)==Status::NeedCompute && walk_frontier(search,board,wrong),
          "a genuinely new named family can retry previously visited origins explicitly");
    Search too_small{3,1,{0,0},key,2};
    Search no_group{3,1,{0,0},Key{},3};
    check(too_small.status()==Status::UnknownGeometry && no_group.status()==Status::UnknownGeometry,
          "storage overflow or an unnamed group is unsupported geometry rather than truncated BFS evidence");
    Search stale_pose{3,1,{0,0},key,3};
    stale_pose.advance(key,{1,0},1,[&](Point p){return board.read(p);},[&](Point p){return board.goal(p);});
    check(stale_pose.status()==Status::Stale,"unacknowledged actual displacement withdraws the dated leg family");
}
void unseen_obligation() {
    const std::array<MissingMember,3> members{{{{0,0},false},{{1,0},true},{{2,0},false}}};
    std::array<std::uint8_t,1> visible{1};
    const auto missing=missing_members(3,1,members,visible);
    check(missing.status==MissingStatus::Ready && missing.members==4,
          "negative menu names unseen unresolved member but never reopens exact corpse");
    visible[0]=0;
    check(missing_members(3,1,members,visible).members==5,
          "visibility loss can nominate unknown living work without erasing persistent death");
    visible[0]=7;
    check(missing_members(3,1,members,visible).members==0,
          "fully observed unsupported work is not an information obligation");
    check(missing_members(3,1,members,std::span<const std::uint8_t>{}).status==MissingStatus::Unknown,
          "missing visibility storage stays Unknown instead of empty work");
    auto invalid=members;invalid[0].position={3,0};
    check(missing_members(3,1,invalid,visible).status==MissingStatus::Unknown,
          "out of field member cannot become a false observation goal");
    check(missing_members(0,1,members,visible).status==MissingStatus::Unknown,
          "invalid field geometry stays Unknown");
}
void move_owned_cursor() {
    Board<5,1> board;board.goals[4]=true;
    Search source{5,1,{0,0},key,5};
    step(source,board,key,3);
    const auto before=allocations;
    Search owner{std::move(source)};
    check(allocations==before && source.status()==Status::UnknownGeometry,
          "moving the owned partial cursor transfers reserved capacity without a second live owner");
    check(walk_frontier(owner,board) && owner.origin()==Point{4,0},
          "a moved partial cursor retains enough reserved storage for allocation-free completion");
    check(source.begin_after_negative({0,0},key)==Status::UnknownGeometry,
          "a moved-from cursor cannot reopen with missing reserved bookkeeping");
}
}
int main() {
    typed_causes();obstacle_and_rank();tree_route_between_origins();
    dynamic_defer_and_relocate();generation_and_unknown_geometry();
    move_owned_cursor();unseen_obligation();
    std::printf("production recon checks: %s (%d)\n",failures?"failed":"passed",checks);
    return failures?1:0;
}
