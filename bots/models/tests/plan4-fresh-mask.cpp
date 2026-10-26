// Representation oracle, unchanged real movement, failure and resumed-stack cuts.
#include "../common/fresh-dynamic-grid.h"
#include "../common/movement.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstddef>
#include <iostream>
#include <set>
#include <stdexcept>

namespace allocations { bool fail_next=false;std::size_t live=0,peak=0; }
struct alignas(std::max_align_t) AllocationHeader { std::size_t size; };
void* operator new(std::size_t size) {
    if(allocations::fail_next) { allocations::fail_next=false;throw std::bad_alloc(); }
    auto* p=static_cast<AllocationHeader*>(std::malloc(sizeof(AllocationHeader)+(size?size:1)));
    if(!p)throw std::bad_alloc();
    p->size=size;allocations::live+=size;allocations::peak=std::max(allocations::peak,allocations::live);return p+1;
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* raw) noexcept { if(raw) { auto* p=static_cast<AllocationHeader*>(raw)-1;allocations::live-=p->size;std::free(p); } }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p,std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p,std::size_t) noexcept { ::operator delete(p); }

using P=plan4::Point;
using Grid=plan4::FreshDynamicGrid<P>;
using Stage=Grid::Stage;
Grid::Key key(int w=8,int h=5,int actor=0,std::uint64_t epoch=1) { return {w,h,actor,1,epoch}; }
std::vector<unsigned char> oracle(int w,int h,const std::vector<P>& points) {
    std::set<std::pair<int,int>> independent;
    for(auto p:points) independent.emplace(p.x,p.y);
    std::vector<unsigned char> result;
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) result.push_back(independent.contains({x,y}));
    return result;
}
void compare(Grid& grid,Grid::Key k,const std::vector<P>& points,int& exports) {
    auto lease=grid.acquire(k);assert(lease);
    lease.refill([&] { ++exports;return points; });lease.publish();
    assert(lease.view()==oracle(k.width,k.height,points));
}
void full_cells_and_fresh_invocations() {
    Grid grid;int exports=0;
    std::vector<P> full;for(int y=0;y<5;++y)for(int x=0;x<8;++x)full.push_back({x,y});
    for(const auto& p:std::vector<std::vector<P>>{{},{P{1,1},P{1,1},P{7,4}},{{0,0},{4,2}},full,{},{{2,1},{6,3}}}) compare(grid,key(),p,exports);
    assert(exports==6); // same owner/round/key still executes every complete export
}
struct Work {
    mutable unsigned left;
    static bool allowed(const void* raw) { auto& w=*static_cast<const Work*>(raw);if(!w.left)return false;--w.left;return true; }
};
void real_movement_outputs_and_state() {
    Grid grid;plan4::PersistentMovement old_path,new_path;std::vector<unsigned char> terrain(40);
    terrain[2*8+4]=1;old_path.initialize(8,5,terrain);new_path.initialize(8,5,terrain);
    const std::vector<std::vector<P>> views={{{1,0}},{{0,1}},{},{{1,0},{0,1}},{{6,3}},{{6,3}}};
    for(int turn=0;turn<30;++turn) {
        const auto& points=views[turn%views.size()];auto dense=oracle(8,5,points);auto lease=grid.acquire(key());lease.refill([&]{return points;});lease.publish();
        const auto& packed=lease.view();
        auto blocked=[](P p,const void* c){return (*static_cast<const std::vector<unsigned char>*>(c))[p.y*8+p.x]!=0;};
        std::vector<P> goals=turn<15?std::vector<P>{{7,4},{7,4}}:std::vector<P>{{0,4},{7,4}};
        if(turn%2)std::reverse(goals.begin(),goals.end());
        old_path.begin_turn(250,256);new_path.begin_turn(250,256);Work a{unsigned(turn%7)},b{unsigned(turn%7)};
        auto old=old_path.next_step({0,0},goals,blocked,&dense,256,{Work::allowed,&a});
        auto fresh=new_path.next_step({0,0},goals,blocked,&packed,256,{Work::allowed,&b});
        assert(old.status==fresh.status && old.next==fresh.next && old.remaining_distance==fresh.remaining_distance && old.visits==fresh.visits);
        assert(old.has_obstruction==fresh.has_obstruction && old.obstruction==fresh.obstruction);
        assert(old_path.cache_generation()==new_path.cache_generation() && old_path.vertex_visits()==new_path.vertex_visits() && a.left==b.left);
        if(old.status==plan4::MovementStatus::ready) { old_path.notify_move_result({0,0},old.next,false);new_path.notify_move_result({0,0},fresh.next,false); }
    }
    // A goal beyond a diagnostic-sized prefix is never cached or dropped.
    std::vector<P> many;for(int y=0;y<5;++y)for(int x=0;x<8;++x)many.push_back({x,y});
    old_path.begin_turn();new_path.begin_turn();auto a=old_path.next_step({0,0},many),b=new_path.next_step({0,0},many);
    assert(a.status==b.status && old_path.cache_generation()==new_path.cache_generation());
}
void publication_and_guard_edges() {
    Grid grid;auto lease=grid.acquire(key());bool refused=false;
    try { lease.publish(); } catch(const std::logic_error&) { refused=true; }assert(refused);
    lease.refill([]{return std::vector<P>{{1,0}};});refused=false;
    try { (void)lease.view(); } catch(const std::logic_error&) { refused=true; }assert(refused);
    lease.publish();assert(lease.view()[1]);
    // Pure search's 35k callback and 256-cap boundary are unchanged.
    plan4::PersistentMovement route;route.initialize(8,5,std::vector<unsigned char>(40));route.begin_turn(256,256);
    const std::vector<P> goals{{7,4}};auto s=route.next_step({0,0},goals);assert(s.status==plan4::MovementStatus::in_progress && !s.visits);
    Work no{0};route.begin_turn();s=route.next_step({0,0},goals,nullptr,nullptr,256,{Work::allowed,&no});assert(!s.visits);
}
void exception_injection_and_recovery() {
    const std::vector<P> old{{1,1},{2,2},{3,3}},fresh{{6,0},{7,4},{2,3}};
    for(Stage fail:{Stage::old_clear,Stage::old_cleared,Stage::exported,Stage::owned,Stage::new_mark,Stage::complete}) {
        Grid grid;int exports=0;compare(grid,key(),old,exports);bool threw=false;
        try { auto lease=grid.acquire(key());lease.refill([&]{++exports;return fresh;},[&](Stage s,std::size_t n){if(s==fail && (s!=Stage::old_clear || n==1))throw std::runtime_error("semantic failpoint");});lease.publish(); }
        catch(const std::runtime_error&) { threw=true; }assert(threw);
        compare(grid,key(),{{0,4}},exports); // no stale/partially marked byte survives
    }
    Grid grid;int exports=0;compare(grid,key(),old,exports);
    for(int failure=0;failure<2;++failure) {
        bool threw=false;
        try { auto lease=grid.acquire(key());lease.refill([&]() -> std::vector<P> { ++exports;if(failure)throw std::bad_alloc();throw std::runtime_error("unchanged Count/GetSetBits error"); }); }
        catch(const std::exception&) { threw=true; }assert(threw);compare(grid,key(),fresh,exports);
    }
    allocations::fail_next=true;bool failed_bind=false;
    try { auto lease=grid.acquire(key(4,3,1,2)); }catch(const std::bad_alloc&){failed_bind=true;}
    assert(failed_bind);compare(grid,key(),old,exports); // failed rebinding releases the lease and preserves repairable old indexing
    const auto common=allocations::live;std::vector<P> dense_points;for(int n=0;n<1000;++n)dense_points.push_back({n%100,n/100});
    allocations::peak=allocations::live;
    { std::vector<unsigned char> bytes(10000,0);auto points=dense_points;for(auto p:points)bytes[p.y*100+p.x]=1; }
    const auto baseline_peak=allocations::peak;
    { Grid large;{auto lease=large.acquire(key(100,100));lease.refill([&]{return dense_points;});lease.publish();}
      allocations::peak=allocations::live;{auto lease=large.acquire(key(100,100));lease.refill([&]{return dense_points;});lease.publish();}
      std::cout<<"retained-old/new-positions peak bytes="<<allocations::peak<<" baseline-dense peak="<<baseline_peak<<" common="<<common<<"\n";
    }
}
void semantic_suspend_resume_clock() {
    for(Stage pause:{Stage::old_clear,Stage::old_cleared,Stage::exported,Stage::owned,Stage::new_mark,Stage::complete}) {
        Grid grid;int exports=0;compare(grid,key(),{{1,1},{2,2}},exports);int round=10;bool paused=false,published=false;
        { auto lease=grid.acquire(key());const int captured=round;
          lease.refill([&]{++exports;return std::vector<P>{{7,4},{3,2}};},[&](Stage s,std::size_t){
              if(s==pause && !paused) { paused=true;++round;auto nested=grid.acquire(key());assert(!nested); } // same stack/lease resumes
          });
          if(round==captured){lease.publish();published=true;}
        }
        assert(paused && !published);compare(grid,key(),{{0,0}},exports);
    }
}
void lifecycle_owner_map_and_indexing() {
    Grid a,b;int exports=0;compare(a,key(),{{7,4}},exports);compare(b,key(),{{0,0}},exports);
    compare(a,key(4,3,0,2),{{3,2}},exports);compare(a,key(4,3,1,2),{{0,0}},exports);
    compare(a,key(4,3,1,3),{},exports); // recall/death/spawn return -> next call refills; no BFS reset API exists
    auto unsupported=key();unsupported.indexing=2;assert(!a.acquire(unsupported));
    assert(!a.acquire(key(0,3)));assert(!a.acquire(key(4,3,-1)));
    compare(b,key(),{{1,0}},exports);
}
void nested_original_dense_fallback() {
    Grid grid;int exports=0;auto outer=grid.acquire(key());outer.refill([&]{++exports;return std::vector<P>{{1,1}};});outer.publish();
    auto nested=grid.acquire(key());assert(!nested);const auto dense=oracle(8,5,{{7,4}});
    assert(dense[39] && !dense[9] && outer.view()[9] && !outer.view()[39]);
    // Simulate nested hook/callback return: outer still sees its entire snapshot.
    assert(outer.view()==oracle(8,5,{{1,1}}));
}
int main() {
    full_cells_and_fresh_invocations();real_movement_outputs_and_state();publication_and_guard_edges();
    exception_injection_and_recovery();semantic_suspend_resume_clock();lifecycle_owner_map_and_indexing();nested_original_dense_fallback();
    std::cout<<"PLAN4_FRESH_MASK {\"passed\":true,\"groups\":7,\"engineLaunches\":0,\"suspension\":\"logical clock changes on same active stack\",\"memoryParity\":\"peak old+new positions remains a native rejection risk\"}\n";
}
