#include "native_service_navigation.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <span>
#include <type_traits>

namespace N=mosaic::native_service;
using N::Point;
using Status=N::NavigationStatus;
static unsigned checks=0,groups=0;
static void check(bool value,const char* expression,int line){
    ++checks;
    if(!value){std::fprintf(stderr,"navigation check failed at %d: %s\n",line,expression);std::abort();}
}
#define CHECK(...) check(bool(__VA_ARGS__),#__VA_ARGS__,__LINE__)

namespace mosaic::native_service {
struct NavigationFixtureAccess {
    template<std::size_t Size> static void poison(Navigation<Size>& nav){
        // Assign live array elements; never memset an object or start a lifetime
        // in raw storage. Metadata still says none of these stamps is initialized.
        nav.stamps_.fill(1); nav.distances_.fill(-777);
        nav.queue_.fill(std::numeric_limits<std::uint32_t>::max());
        nav.roots_.fill(Point{-777,-777});
    }
    template<std::size_t Size> static void force_wrap(Navigation<Size>& nav){
        nav.generation_=std::numeric_limits<std::uint32_t>::max();
    }
    template<std::size_t Size> static std::size_t initialized(const Navigation<Size>& nav){return nav.initialized_;}
    template<std::size_t Size> static std::uint32_t stamp(const Navigation<Size>& nav,std::size_t at){return nav.stamps_[at];}
    template<std::size_t Size> static bool detached(const Navigation<Size>& nav){
        return nav.board_.empty() && !nav.requested_ && !nav.seeded_ && !nav.root_count_ &&
            !nav.root_cursor_ && !nav.width_ && !nav.height_ && !nav.cell_count_ && !nav.head_ && !nav.tail_;
    }
};
}
using Access=N::NavigationFixtureAccess;
static constinit N::Navigation<64> constant_navigation{};
static constinit N::Navigation<> default_constant_navigation{};
static_assert(!std::is_copy_constructible_v<N::Navigation<64>>);
static_assert(!std::is_copy_assignable_v<N::Navigation<64>>);
static_assert(!std::is_move_constructible_v<N::Navigation<64>>);

template<std::size_t Size> static unsigned finish(N::Navigation<Size>& nav,unsigned batch=3){
    unsigned total=0,calls=0;
    while(nav.status()==Status::preparing){
        CHECK(++calls<4096);
        const auto progress=nav.step(batch);
        CHECK(progress.units<=batch);
        CHECK(progress.progressed==(progress.units!=0));
        CHECK(progress.units!=0);
        total+=progress.units;
    }
    return total;
}

static void partial_roots_and_repeated_advance(){
    ++groups;
    N::Navigation<64> nav;
    Access::poison(nav);
    std::array<std::uint8_t,9> board{};board.fill(1);
    const std::array roots{Point{0,0},Point{2,2}};
    CHECK(nav.status()==Status::idle);
    CHECK(nav.begin(3,3,board,roots,42));
    CHECK(Access::initialized(nav)==0 && Access::stamp(nav,63)==1);
    auto p=nav.step(0);
    CHECK(p.units==0 && !p.progressed && p.status==Status::preparing);
    CHECK(!nav.available({0,0}) && !nav.distance({0,0}));
    p=nav.step(9);CHECK(p.units==9 && Access::initialized(nav)==9);
    p=nav.step(1);CHECK(p.units==1);
    CHECK(!nav.available({0,0}) && !nav.available({2,2}));
    CHECK(nav.begin(3,3,board,roots,42)); // Does not reset the first seeded root.
    p=nav.step(1);CHECK(p.units==1 && nav.available({0,0}) && nav.available({2,2}));
    CHECK(nav.distance({0,0})==0 && nav.distance({2,2})==0);
    CHECK(!nav.available({1,1}) && nav.status({1,1})==Status::preparing);
    p=nav.step(1);CHECK(p.units==1 && nav.distance({1,1})==1);
    CHECK(nav.begin(3,3,board,roots,42));
    CHECK(finish(nav,1)==8);
    CHECK(nav.status()==Status::ready && nav.distance({0,2})==2);
    CHECK(nav.begin(3,3,board,roots,42));
    CHECK(nav.step(20).units==0 && nav.available({1,1}));
    CHECK(nav.status({-1,0})==Status::failed && !nav.available({3,0}));
}

static void changed_intents_roots_and_board(){
    ++groups;
    N::Navigation<64> nav;
    std::array<std::uint8_t,16> board{};board.fill(1);
    const std::array first{Point{0,0}},second{Point{3,3}};
    CHECK(nav.begin(4,4,board,first,1));finish(nav);
    CHECK(nav.distance({3,3})==3);
    CHECK(nav.begin(4,4,board,second,1)); // Roots changed, same intent.
    CHECK(!nav.available({0,0}) && Access::initialized(nav)==16);
    CHECK(nav.step(1).units==1 && nav.available({3,3})); // No full stamp clear.
    finish(nav);CHECK(nav.distance({0,0})==3);
    CHECK(nav.begin(4,4,board,second,2));
    CHECK(!nav.available({3,3}));finish(nav);
    auto replacement=board;
    CHECK(nav.begin(4,4,replacement,second,2)); // Borrowed board identity changes.
    CHECK(!nav.available({3,3}));finish(nav);
    board[0]=0;
    CHECK(nav.begin(4,4,board,first,3));finish(nav);
    CHECK(nav.status()==Status::unreachable && !nav.distance({0,0}));
    const std::array duplicate{Point{1,1},Point{1,1},Point{0,0}};
    CHECK(nav.begin(4,4,board,duplicate,4));
    CHECK(nav.step(2).units==2 && !nav.available({1,1}));
    CHECK(nav.step(1).units==1 && nav.distance({1,1})==0);finish(nav);
    CHECK(nav.status()==Status::ready);
}

static void unreachable_is_not_partial(){
    ++groups;
    N::Navigation<64> nav;
    std::array<std::uint8_t,21> board{};board.fill(1);
    for(int y=0;y<3;++y)board[std::size_t(y*7+3)]=0;
    const std::array roots{Point{0,1}};
    CHECK(nav.begin(7,3,board,roots,7));
    CHECK(nav.status({6,1})==Status::preparing && !nav.available({6,1}));
    finish(nav,2);
    CHECK(nav.status()==Status::ready && nav.distance({2,1})==2);
    CHECK(nav.status({6,1})==Status::unreachable && !nav.distance({6,1}));
    CHECK(nav.status({3,1})==Status::unreachable);
    const std::array blocked{Point{3,1}};
    CHECK(nav.begin(7,3,board,blocked,8));
    CHECK(nav.step(1).status==Status::unreachable);
    CHECK(nav.begin(7,3,board,{},9) && nav.status()==Status::unreachable);
    CHECK(nav.step(100).units==0 && !nav.available({0,1}));
}

static void nominees_are_bounded_final_descents(){
    ++groups;
    N::Navigation<64> nav;
    std::array<std::uint8_t,25> board{};board.fill(1);
    const std::array roots{Point{2,0}};
    CHECK(nav.begin(5,5,board,roots,8));
    std::array<Point,10> out{};out.fill(Point{-9,-9});
    CHECK(nav.nominees({2,2},{},out)==0);
    finish(nav);
    CHECK(nav.nominees({2,2},{},out)==3);
    CHECK(out[0]==Point{1,1} && out[1]==Point{2,1} && out[2]==Point{3,1});
    CHECK(out[3]==Point{-9,-9});
    const std::array excluded{Point{2,1}};
    CHECK(nav.nominees({2,2},excluded,out)==2 && out[0]==Point{1,1} && out[1]==Point{3,1});
    std::array<Point,8> alias{};alias[0]=Point{2,1};
    CHECK(nav.nominees({2,2},std::span<const Point>(alias).first(1),alias)==2);
    CHECK(alias[0]==Point{1,1} && alias[1]==Point{3,1});
    const std::array all{Point{1,1},Point{2,1},Point{3,1}};
    CHECK(nav.nominees({2,2},all,out)==0);
    CHECK(nav.nominees({2,2},{},std::span<Point>(out).first(1))==1 && out[0]==Point{1,1});
    CHECK(nav.nominees({2,2},{},{})==0 && nav.nominees({2,0},{},out)==0);
    std::array<Point,9> excessive{};
    CHECK(nav.nominees({2,2},excessive,out)==0 && nav.status()==Status::ready);
    nav.retire();board[6]=0; // End the borrow before changing passability.
    CHECK(nav.begin(5,5,board,roots,9));finish(nav);
    CHECK(nav.nominees({2,2},{},out)==2 && out[0]==Point{2,1} && out[1]==Point{3,1});
}

static void resized_poison_and_generation_wrap(){
    ++groups;
    N::Navigation<64> nav;
    Access::poison(nav);
    std::array<std::uint8_t,64> board{};board.fill(1);
    const std::array origin{Point{0,0}},far{Point{3,3}};
    CHECK(nav.begin(4,4,board,origin,1));finish(nav);
    CHECK(Access::initialized(nav)==16 && Access::stamp(nav,63)==1);
    Access::force_wrap(nav);
    CHECK(nav.begin(4,4,board,far,2));
    CHECK(nav.step(15).units==15 && !nav.available({0,0}) && !nav.available({3,3}));
    CHECK(nav.step(1).units==1 && !nav.available({3,3}));
    CHECK(nav.step(1).units==1 && nav.distance({3,3})==0 && !nav.available({0,0}));
    finish(nav);CHECK(nav.distance({0,0})==3);
    Access::force_wrap(nav);
    CHECK(nav.begin(4,4,board,origin,3));CHECK(nav.step(3).units==3);
    CHECK(nav.begin(2,2,board,origin,4)); // Must finish clearing the old 16-cell extent.
    CHECK(nav.step(13).units==13 && !nav.available({0,0}));
    CHECK(nav.step(1).units==1 && nav.available({0,0}));finish(nav);
    CHECK(nav.begin(8,8,board,origin,5));
    CHECK(nav.step(48).units==48 && Access::initialized(nav)==64 && !nav.available({0,0}));
    CHECK(nav.step(1).units==1 && nav.available({0,0}));finish(nav);
    CHECK(nav.distance({7,7})==7);
    Access::force_wrap(nav);
    CHECK(nav.begin(8,8,board,{},6) && nav.status()==Status::unreachable);
    CHECK(nav.begin(8,8,board,origin,7));
    CHECK(nav.step(64).units==64 && !nav.available({0,0}));
    CHECK(nav.step(1).units==1 && nav.distance({0,0})==0);finish(nav);
    CHECK(nav.distance({7,7})==7);
}

static void request_bounds_and_recovery(){
    ++groups;
    N::Navigation<64> nav;
    std::array<std::uint8_t,64> board{};board.fill(1);
    const std::array good{Point{0,0}};
    CHECK(!nav.begin(0,8,board,good,1) && nav.status()==Status::failed);
    CHECK(!nav.begin(-1,8,board,good,1));
    CHECK(!nav.begin(9,8,board,good,1));
    CHECK(!nav.begin(std::numeric_limits<int>::max(),std::numeric_limits<int>::max(),board,good,1));
    CHECK(!nav.begin(8,8,std::span<const std::uint8_t>(board).first(63),good,1));
    const std::array outside{Point{8,0}},negative{Point{0,-1}};
    CHECK(!nav.begin(8,8,board,outside,1) && !nav.begin(8,8,board,negative,1));
    std::array<Point,257> too_many{};
    CHECK(!nav.begin(8,8,board,too_many,1));
    CHECK(nav.step(1).units==0 && !nav.available({0,0}));
    CHECK(nav.begin(8,8,board,good,1));finish(nav);
    CHECK(nav.distance({7,7})==7);
    std::array<Point,256> maximum{};maximum.fill(Point{0,0});
    CHECK(nav.begin(8,8,board,maximum,2));
    CHECK(nav.step(255).units==255 && !nav.available({0,0}));
    CHECK(nav.step(1).units==1 && nav.distance({0,0})==0);finish(nav);
}

static void retired_fields_detach_and_reseed(){
    ++groups;
    N::Navigation<64> nav;
    Access::poison(nav);
    std::array<std::uint8_t,16> board{};board.fill(1);
    const std::array origin{Point{0,0}},far{Point{3,3}};
    std::array<Point,8> out{};out.fill(Point{-9,-9});
    auto retired=[&]{
        CHECK(nav.status()==Status::idle && nav.status({0,0})==Status::idle);
        CHECK(Access::detached(nav));
        CHECK(!nav.available({0,0}) && !nav.distance({0,0}));
        CHECK(!nav.available({3,3}) && !nav.distance({3,3}));
        CHECK(nav.nominees({3,3},{},out)==0 && out[0]==Point{-9,-9});
        const auto p=nav.step(1000);
        CHECK(p.status==Status::idle && !p.units && !p.progressed);
    };
    CHECK(nav.begin(4,4,board,origin,11));
    CHECK(nav.step(5).units==5 && Access::initialized(nav)==5);
    const auto first_stamp=Access::stamp(nav,0),last_stamp=Access::stamp(nav,63);
    nav.retire();retired();
    CHECK(Access::initialized(nav)==5 && Access::stamp(nav,0)==first_stamp && Access::stamp(nav,63)==last_stamp);
    nav.retire();retired(); // Repeated retirement does not restart stamp work.
    CHECK(nav.begin(4,4,board,origin,11)); // Same identity must still reseed.
    CHECK(nav.step(11).units==11 && !nav.available({0,0}));
    CHECK(nav.step(1).units==1 && nav.distance({0,0})==0);finish(nav);
    CHECK(nav.distance({3,3})==3);
    const auto ready_stamp=Access::stamp(nav,0);
    nav.retire();retired();
    CHECK(Access::initialized(nav)==16 && Access::stamp(nav,0)==ready_stamp);
    board.fill(0);board[15]=1; // The same storage is mutable after retirement.
    CHECK(nav.begin(4,4,board,far,12));
    CHECK(!nav.available({3,3}) && !nav.distance({0,0}));finish(nav);
    CHECK(nav.distance({3,3})==0 && !nav.distance({0,0}));
    nav.retire();retired();
    CHECK(nav.begin(4,4,board,origin,13));finish(nav);
    CHECK(nav.status()==Status::unreachable);
    nav.retire();retired();
    board.fill(1);
    CHECK(nav.begin(4,4,board,origin,14));finish(nav);
    CHECK(nav.distance({3,3})==3);
    CHECK(!nav.begin(0,4,board,origin,15) && nav.status()==Status::failed);
    nav.retire();retired();
    CHECK(nav.begin(4,4,board,origin,16));finish(nav);
    CHECK(nav.distance({3,3})==3);
}

static void retired_wrap_keeps_pending_clear(){
    ++groups;
    N::Navigation<64> nav;
    Access::poison(nav);
    std::array<std::uint8_t,64> board{};board.fill(1);
    const std::array origin{Point{0,0}};
    CHECK(nav.begin(4,4,board,origin,1));finish(nav);
    Access::force_wrap(nav);
    CHECK(nav.begin(4,4,board,origin,2));
    CHECK(nav.step(3).units==3 && !nav.available({0,0}));
    const auto uncleared=Access::stamp(nav,15);
    nav.retire();
    CHECK(nav.status()==Status::idle && Access::detached(nav));
    CHECK(Access::initialized(nav)==16 && Access::stamp(nav,15)==uncleared);
    CHECK(nav.step(100).units==0 && Access::stamp(nav,15)==uncleared);
    board[15]=0;
    CHECK(nav.begin(2,2,board,origin,3));
    CHECK(nav.step(12).units==12 && !nav.available({0,0}));
    CHECK(nav.step(1).units==1 && !nav.available({0,0})); // Old 16-cell clear completes.
    CHECK(nav.step(1).units==1 && nav.distance({0,0})==0);finish(nav);
    CHECK(nav.distance({1,1})==1);
    nav.retire();
    CHECK(nav.begin(8,8,board,origin,4));
    CHECK(nav.step(48).units==48 && !nav.available({0,0}));
    CHECK(nav.step(1).units==1 && nav.available({0,0}));finish(nav);
    CHECK(nav.distance({7,7})==7 && !nav.distance({7,1}));
}

// Independent O(V^2) Dijkstra oracle: no staged queue, stamps or generation.
static std::array<int,64> reference(const std::array<std::uint8_t,64>& board,std::span<const Point> roots){
    std::array<int,64> answer{};answer.fill(-1);std::array<bool,64> used{};
    for(const auto p:roots){const auto at=std::size_t(p.y*8+p.x);if(board[at])answer[at]=0;}
    for(unsigned turn=0;turn<64;++turn){
        int chosen=-1;
        for(int at=0;at<64;++at)if(!used[std::size_t(at)] && answer[std::size_t(at)]>=0 &&
            (chosen<0 || answer[std::size_t(at)]<answer[std::size_t(chosen)]))chosen=at;
        if(chosen<0)break;
        used[std::size_t(chosen)]=true;
        for(int y=std::max(0,chosen/8-1);y<=std::min(7,chosen/8+1);++y)
            for(int x=std::max(0,chosen%8-1);x<=std::min(7,chosen%8+1);++x){
                const auto at=std::size_t(y*8+x);const int next=answer[std::size_t(chosen)]+1;
                if(board[at] && (answer[at]<0 || next<answer[at]))answer[at]=next;
            }
    }
    return answer;
}
static void differential_partial_fields(){
    ++groups;
    N::Navigation<64> nav;
    Access::poison(nav);
    std::uint32_t random=712367;
    for(unsigned scenario=0;scenario<48;++scenario){
        std::array<std::uint8_t,64> board{};
        for(auto& cell:board){random=random*1664525u+1013904223u;cell=std::uint8_t((random>>28)!=0);}
        const std::array roots{Point{0,0},Point{7,7},Point{3,4}};
        const auto expected=reference(board,roots);
        CHECK(nav.begin(8,8,board,roots,scenario+100));
        unsigned calls=0;
        while(nav.status()==Status::preparing){
            CHECK(++calls<256);
            const unsigned batch=1+(scenario%7);
            const auto p=nav.step(batch);CHECK(p.units<=batch && p.progressed && p.units);
            for(int y=0;y<8;++y)for(int x=0;x<8;++x){
                const Point point{x,y};const auto got=nav.distance(point);
                if(got)CHECK(*got==expected[std::size_t(y*8+x)]);
            }
            CHECK(nav.begin(8,8,board,roots,scenario+100));
        }
        for(int y=0;y<8;++y)for(int x=0;x<8;++x){
            const Point point{x,y};const int wanted=expected[std::size_t(y*8+x)];
            const auto got=nav.distance(point);
            CHECK(bool(got)==(wanted>=0));if(got)CHECK(*got==wanted);
            CHECK(nav.status(point)==(wanted>=0?Status::ready:Status::unreachable));
            std::array<Point,8> next{};const auto count=nav.nominees(point,{},next);CHECK(count<=8);
            for(unsigned i=0;i<count;++i){
                CHECK(board[std::size_t(next[i].y*8+next[i].x)]!=0);
                CHECK(std::abs(next[i].x-x)<=1 && std::abs(next[i].y-y)<=1);
                CHECK(nav.distance(next[i]) && got && *nav.distance(next[i])<*got);
            }
            if(wanted>0)CHECK(count>0);else CHECK(count==0);
        }
    }
}

int main(){
    CHECK(constant_navigation.status()==Status::idle);
    CHECK(default_constant_navigation.status()==Status::idle);
    partial_roots_and_repeated_advance();changed_intents_roots_and_board();unreachable_is_not_partial();
    nominees_are_bounded_final_descents();resized_poison_and_generation_wrap();request_bounds_and_recovery();
    retired_fields_detach_and_reseed();retired_wrap_keeps_pending_clear();
    differential_partial_fields();
    std::printf("PASS native_service_navigation: %u checks, %u groups\n",checks,groups);
}
