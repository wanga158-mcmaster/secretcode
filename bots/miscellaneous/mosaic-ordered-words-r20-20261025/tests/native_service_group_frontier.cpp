#include "../src/native_service_group_frontier.h"
#include "../src/native_service_navigation.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <span>
#include <vector>

namespace N=mosaic::native_service;
namespace mosaic::native_service {
struct GroupFrontierFixtureAccess {
    template<std::size_t Cells,std::size_t Roots>
    static void poison(GroupFrontier<Cells,Roots>& frontier,std::uint32_t value){
        for(auto& stamp:frontier.stamps_)stamp=value;
    }
    template<std::size_t Cells,std::size_t Roots>
    static void wrap(GroupFrontier<Cells,Roots>& frontier){
        frontier.stamp_generation_=std::numeric_limits<std::uint32_t>::max();
    }
    template<std::size_t Cells,std::size_t Roots>
    static void exhaust_identity(GroupFrontier<Cells,Roots>& frontier){
        frontier.next_generation_=std::numeric_limits<std::uint64_t>::max();
    }
};
}
static unsigned checks{},groups{};
static void check(bool value,const char* expression,int line){
    ++checks;if(!value){std::fprintf(stderr,"group frontier check failed at %d: %s\n",line,expression);std::abort();}
}
#define CHECK(...) check(bool(__VA_ARGS__),#__VA_ARGS__,__LINE__)

// No SDK, physical permission, fuel or life-observation producer is mocked
// into a proof here. Tests supply immutable geometry and remaining evidence;
// real Navigation is used only to verify the published root-set consequence.
static constinit N::GroupFrontier<> default_frontier{};
static N::FrontierIntent intent(int range=1){return {7,0,11,12,13,range};}
static bool possible(int){return true;}
static bool contains(std::span<const N::Point> points,N::Point target){
    for(auto point:points)if(point==target)return true;
    return false;
}
template<class Frontier,class Remaining>
static unsigned finish(Frontier& frontier,Remaining remaining,unsigned quantum=3){
    unsigned total=0,turns=0;
    while(frontier.status()==N::FrontierStatus::preparing){
        const auto progress=frontier.step(quantum,remaining);
        CHECK(progress.units<=quantum&&progress.progressed==(progress.units!=0));
        CHECK(progress.status==frontier.status());total+=progress.units;
        CHECK(++turns<100000);
    }
    return total;
}
template<std::size_t Cells>
static void finish_navigation(N::Navigation<Cells>& navigation){
    for(unsigned i=0;i<10000&&navigation.status()==N::NavigationStatus::preparing;++i)navigation.step(32);
    CHECK(navigation.status()==N::NavigationStatus::ready||navigation.status()==N::NavigationStatus::unreachable);
}

static void group_roots_reach_the_near_member_beyond_a_dead_opening(){
    N::GroupFrontier<1024> frontier;std::array<std::uint8_t,640> board{};
    for(int x=0;x<40;++x)board[12*40+std::size_t(x)]=1;
    board[12*40+6]=board[12*40+34]=0;
    const std::array<N::FrontierMember,3> members{{{{34,12},30},{{6,12},57},{{5,12},59}}};
    auto remaining=[](int ordinal){return ordinal!=59;}; // Qualified Dead59; Unknown/possible30 and57 stay.
    CHECK(frontier.begin(40,16,board,members,intent()));
    CHECK(frontier.roots().empty()&&!frontier.complete_roots());
    finish(frontier,remaining,1);
    CHECK(frontier.status()==N::FrontierStatus::ready&&frontier.complete_roots()&&!frontier.batched());
    CHECK(frontier.roots().size()==4&&contains(frontier.roots(),{5,12})&&contains(frontier.roots(),{7,12}));
    CHECK(contains(frontier.roots(),{33,12})&&contains(frontier.roots(),{35,12}));
    CHECK(!contains(frontier.roots(),{4,12})&&!contains(frontier.roots(),{6,12}));
    N::Navigation<1024> whole,far_only;
    CHECK(whole.begin(40,16,board,frontier.roots(),frontier.generation()));
    const std::array<N::Point,2> far{{{33,12},{35,12}}};
    CHECK(far_only.begin(40,16,board,far,999));finish_navigation(whole);finish_navigation(far_only);
    CHECK(whole.distance({4,12})==1&&whole.available({5,12}));
    CHECK(far_only.status({4,12})==N::NavigationStatus::unreachable);
    CHECK(frontier.member_cursor()==members.size());
    ++groups;
}

static void small_steps_and_same_semantics_retain_cursor_without_publishing_partial_roots(){
    N::GroupFrontier<16> frontier;std::array<std::uint8_t,9> board{};board.fill(1);
    const std::array<N::FrontierMember,1> members{{{{1,1},57}}};unsigned observations=0;
    auto remaining=[&](int ordinal){CHECK(ordinal==57);++observations;return true;};
    N::GroupFrontierFixtureAccess::poison(frontier,1);
    CHECK(frontier.begin(3,3,board,members,intent()));const auto generation=frontier.generation();
    const auto zero=frontier.step(0,remaining);CHECK(zero.units==0&&!zero.progressed&&observations==0);
    CHECK(frontier.step(4,remaining).units==4&&frontier.roots().empty());
    CHECK(frontier.begin(3,3,board,members,intent())&&frontier.generation()==generation);
    CHECK(frontier.step(5,remaining).units==5&&observations==0); // Exactly the remaining unread stamps.
    CHECK(frontier.step(1,remaining).units==1&&observations==1&&frontier.member_cursor()==0);
    CHECK(frontier.step(1,remaining).units==1&&frontier.roots().empty()); // One internally found root is still unpublished.
    CHECK(frontier.begin(3,3,board,members,intent())&&frontier.generation()==generation);
    CHECK(finish(frontier,remaining,1)==8&&observations==1);
    CHECK(frontier.roots().size()==8&&frontier.complete_roots()&&!contains(frontier.roots(),{1,1}));
    const auto stable=frontier.step(100,remaining);CHECK(stable.units==0&&observations==1);
    CHECK(frontier.begin(3,3,board,members,intent())&&frontier.generation()==generation);
    CHECK(frontier.roots().size()==8); // Input epochs are absent from semantic identity.
    ++groups;
}

static void death_revision_and_board_retirement_replace_the_geometric_obligation(){
    N::GroupFrontier<16> frontier;std::array<std::uint8_t,10> board{};board.fill(1);board[5]=board[6]=0;
    const std::array<N::FrontierMember,2> members{{{{5,0},59},{{6,0},57}}};bool dead59=false;
    auto remaining=[&](int ordinal){return ordinal!=59||!dead59;};auto semantic=intent();
    CHECK(frontier.begin(10,1,board,members,semantic));finish(frontier,remaining);
    CHECK(contains(frontier.roots(),{4,0})&&contains(frontier.roots(),{7,0})&&!contains(frontier.roots(),{5,0}));
    const auto first=frontier.generation();frontier.retire();
    CHECK(!frontier.active()&&frontier.generation()==0&&frontier.roots().empty());
    // Every borrower is retired before this actual cleared-board edit and the
    // caller's new qualified-death predicate view. No old stamp/root is truth.
    board[5]=1;dead59=true;++semantic.board_generation;++semantic.remaining_revision;
    CHECK(frontier.begin(10,1,board,members,semantic)&&frontier.generation()!=first);
    finish(frontier,remaining);CHECK(frontier.roots().size()==2);
    CHECK(contains(frontier.roots(),{5,0})&&contains(frontier.roots(),{7,0})&&!contains(frontier.roots(),{4,0}));
    // Even a caller-cleared own centre can never be that member's Basic perch.
    frontier.retire();board[6]=1;++semantic.board_generation;
    CHECK(frontier.begin(10,1,board,members,semantic));finish(frontier,remaining);
    CHECK(!contains(frontier.roots(),{6,0})&&frontier.roots().size()==2);
    ++groups;
}

static void range_dedup_and_member_storage_are_part_of_the_semantics(){
    N::GroupFrontier<64> frontier;std::array<std::uint8_t,49> board{};board.fill(1);board[0]=0;
    const std::array<N::FrontierMember,2> members{{{{3,3},30},{{3,3},57}}};auto semantic=intent(3);
    CHECK(frontier.begin(7,7,board,members,semantic));finish(frontier,possible);
    CHECK(frontier.roots().size()==47&&frontier.complete_roots()); // Duplicate members, one deduplicated union.
    CHECK(contains(frontier.roots(),{0,3})&&contains(frontier.roots(),{6,6})&&!contains(frontier.roots(),{3,3}));
    const auto first=frontier.generation();semantic.basic_range=1;
    CHECK(frontier.begin(7,7,board,members,semantic)&&frontier.generation()!=first);finish(frontier,possible);
    CHECK(frontier.roots().size()==8&&!contains(frontier.roots(),{0,3}));
    const auto second=frontier.generation();auto other_members=members;
    CHECK(frontier.begin(7,7,board,other_members,semantic)&&frontier.generation()!=second);finish(frontier,possible);
    const auto third=frontier.generation();auto other_board=board;
    CHECK(frontier.begin(7,7,other_board,other_members,semantic)&&frontier.generation()!=third);finish(frontier,possible);
    ++semantic.static_generation;const auto fourth=frontier.generation();
    CHECK(frontier.begin(7,7,other_board,other_members,semantic)&&frontier.generation()!=fourth);
    finish(frontier,possible);CHECK(frontier.roots().size()==8);
    ++groups;
}

static void omitted_batches_are_not_a_group_unreachable_claim(){
    N::GroupFrontier<16,3> frontier;std::array<std::uint8_t,10> board{};board.fill(1);board[1]=board[3]=board[6]=0;
    const std::array<N::FrontierMember,2> members{{{{6,0},30},{{1,0},57}}};
    CHECK(frontier.begin(10,1,board,members,intent()));finish(frontier,possible);
    CHECK(frontier.status()==N::FrontierStatus::more&&frontier.batched()&&!frontier.complete_roots());
    CHECK(frontier.roots().size()==3&&contains(frontier.roots(),{0,0})&&!contains(frontier.roots(),{2,0}));
    N::Navigation<16> first;CHECK(first.begin(10,1,board,frontier.roots(),1));finish_navigation(first);
    CHECK(first.status({2,0})==N::NavigationStatus::unreachable); // Omitted near-member perch still exists.
    const auto generation=frontier.generation();CHECK(frontier.batch()==0&&frontier.next_batch());
    CHECK(frontier.roots().empty()&&frontier.generation()==generation&&frontier.batch()==1);
    finish(frontier,possible,1);
    CHECK(frontier.status()==N::FrontierStatus::ready&&frontier.roots().size()==1&&frontier.roots()[0]==N::Point{2,0});
    CHECK(!frontier.complete_roots()&&frontier.batched()&&!frontier.next_batch());
    N::Navigation<16> second;CHECK(second.begin(10,1,board,frontier.roots(),2));finish_navigation(second);
    CHECK(second.distance({2,0})==0); // Actual arrival/Basic permission is still the caller's next operator.
    ++groups;
}

static void real_root_cap_has_finite_fair_batches_with_no_duplicates_or_omissions(){
    N::GroupFrontier<1024> frontier;std::array<std::uint8_t,512> board{};board.fill(1);
    const std::array<N::FrontierMember,2> members{{{{15,8},30},{{15,8},57}}};
    CHECK(frontier.begin(32,16,board,members,intent(32)));std::array<bool,512> seen{};std::size_t count=0,batches=0;
    const auto generation=frontier.generation();
    for(;;){
        finish(frontier,possible,7);CHECK(frontier.roots().size()<=256);
        if(!batches)CHECK(frontier.status()==N::FrontierStatus::more&&frontier.roots().size()==256);
        CHECK(!frontier.complete_roots()&&frontier.batched()&&frontier.generation()==generation);
        for(auto point:frontier.roots()){
            const auto at=std::size_t(point.y)*32+std::size_t(point.x);CHECK(at<seen.size()&&!seen[at]);seen[at]=true;++count;
        }
        ++batches;if(frontier.status()==N::FrontierStatus::ready)break;
        CHECK(frontier.status()==N::FrontierStatus::more&&frontier.next_batch());CHECK(batches<10);
    }
    CHECK(batches==2&&count==511&&!seen[8*32+15]);
    for(std::size_t at=0;at<seen.size();++at)CHECK(seen[at]==(at!=8*32+15));
    ++groups;
}

static void wrap_clear_survives_interruption_and_semantic_replacement(){
    N::GroupFrontier<16> frontier;std::array<std::uint8_t,16> board{};board.fill(1);
    const std::array<N::FrontierMember,1> members{{{{1,1},57}}};auto semantic=intent();
    CHECK(frontier.begin(4,4,board,members,semantic));finish(frontier,possible);
    N::GroupFrontierFixtureAccess::poison(frontier,2);N::GroupFrontierFixtureAccess::wrap(frontier);
    ++semantic.remaining_revision;CHECK(frontier.begin(4,4,board,members,semantic));
    CHECK(frontier.step(5,possible).units==5&&frontier.roots().empty());
    ++semantic.remaining_revision;CHECK(frontier.begin(4,4,board,members,semantic)); // New stamp2 while old wrapped clear is unfinished.
    finish(frontier,possible,1);CHECK(frontier.roots().size()==8&&frontier.complete_roots());
    N::GroupFrontierFixtureAccess::exhaust_identity(frontier);++semantic.remaining_revision;
    CHECK(!frontier.begin(4,4,board,members,semantic));
    CHECK(frontier.status()==N::FrontierStatus::failed&&!frontier.active()&&frontier.generation()==0&&frontier.roots().empty());
    ++groups;
}

static void malformed_requests_and_staged_members_fail_without_partial_output(){
    CHECK(default_frontier.status()==N::FrontierStatus::idle&&default_frontier.roots().empty());
    N::GroupFrontier<16> frontier;std::array<std::uint8_t,16> board{};board.fill(1);auto semantic=intent();
    const std::array<N::FrontierMember,1> member{{{{1,1},57}}};
    CHECK(!frontier.begin(0,4,board,member,semantic));CHECK(!frontier.begin(5,4,board,member,semantic));
    CHECK(!frontier.begin(4,4,std::span<const std::uint8_t>{board}.first(15),member,semantic));
    semantic.basic_range=0;CHECK(!frontier.begin(4,4,board,member,semantic));semantic=intent();semantic.group_ordinal=-1;
    CHECK(!frontier.begin(4,4,board,member,semantic));semantic=intent();
    const std::array<N::FrontierMember,2> bad{{{{1,1},57},{{4,0},59}}};
    CHECK(frontier.begin(4,4,board,bad,semantic));finish(frontier,possible,1);
    CHECK(frontier.status()==N::FrontierStatus::failed&&frontier.roots().empty()&&!frontier.complete_roots());
    const std::array<N::FrontierMember,1> bad_ordinal{{{{1,1},-1}}};
    CHECK(frontier.begin(4,4,board,bad_ordinal,semantic));finish(frontier,possible);
    CHECK(frontier.status()==N::FrontierStatus::failed&&frontier.generation()==0);
    CHECK(frontier.begin(4,4,board,{},semantic)&&frontier.status()==N::FrontierStatus::ready&&frontier.complete_roots());
    CHECK(frontier.roots().empty()); // No members is geometric enumeration, never Reward/payout evidence.
    ++groups;
}

int main(){
    group_roots_reach_the_near_member_beyond_a_dead_opening();
    small_steps_and_same_semantics_retain_cursor_without_publishing_partial_roots();
    death_revision_and_board_retirement_replace_the_geometric_obligation();range_dedup_and_member_storage_are_part_of_the_semantics();
    omitted_batches_are_not_a_group_unreachable_claim();real_root_cap_has_finite_fair_batches_with_no_duplicates_or_omissions();
    wrap_clear_survives_interruption_and_semantic_replacement();malformed_requests_and_staged_members_fail_without_partial_output();
    std::printf("PASS native_service_group_frontier: %u checks, %u groups\n",checks,groups);
}
