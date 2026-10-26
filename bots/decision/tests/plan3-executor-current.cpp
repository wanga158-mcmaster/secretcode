// Current SDK wrappers with synthetic 8x8 occupancy and native-distance host.
// No engine, champion-balance assertion, or match is involved.
#if defined(__wasm__)
#undef __wasm__
#include <cambc.h>
#define __wasm__ 1
#else
#include <cambc.h>
#endif
#include "../common/information.h"
#include "../common/model.h"
#include "../common/executor.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "backpressure-config-v1.h"
#include "backpressure-host-v1.h"

namespace {
int cost_zero_item=-1;
bool bp_hidden_collision=false;
std::uint64_t obstacles=0;
unsigned searches=0,largest_search=0,hidden_failures=0;
std::uint64_t bp_camp_mask(){return obstacles;}
}
namespace cambc::detail { extern "C" {
std::int32_t sdk_Bitboard_DrawChebyshev(std::uint64_t* out,std::int32_t x,std::int32_t y,float radius) {
    *out=0;for(int yy=0;yy<8;++yy)for(int xx=0;xx<8;++xx)
        if(std::max(std::abs(xx-x),std::abs(yy-y))<=radius)set(out,xx,yy);
    return 0;
}
std::int32_t sdk_DistanceField_Get(const std::uint16_t* in,std::int32_t x,std::int32_t y,std::uint16_t* out) {
    verify(x>=0 && x<8 && y>=0 && y<8);*out=in[y*8+x];return 0;
}
std::int32_t sdk_DistanceField_ComputeToWithExpansionCount(const std::uint64_t* walls,const std::uint64_t* roots,
    const std::uint64_t* destinations,std::uint64_t* visited,std::uint16_t* out,std::uint16_t limit,std::uint16_t* count) {
    ++searches;verify(limit==48);const auto free=unsigned(std::popcount(~*walls));
    verify(free<=49);largest_search=std::max(largest_search,free);
    std::fill(out,out+64,cambc::distance_field::unreachable);*visited=0;*count=0;
    int queue[64],head=0,tail=0;
    for(int i=0;i<64;++i)if(((*roots>>i)&1u) && !((*walls>>i)&1u)) {
        out[i]=0;queue[tail++]=i;*visited|=std::uint64_t{1}<<i;
    }
    while(head<tail) {
        const int at=queue[head++];++*count;
        if(((*destinations>>at)&1u) || out[at]>=limit)continue;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
            const int x=at%8+dx,y=at/8+dy;if(x<0||y<0||x>=8||y>=8)continue;
            const int i=y*8+x;if(((*walls>>i)&1u) || out[i]!=cambc::distance_field::unreachable)continue;
            out[i]=out[at]+1;queue[tail++]=i;*visited|=std::uint64_t{1}<<i;
        }
    }
    verify(*count<=49);return 0;
}
}}
namespace {
using plan3_runtime::Executor;
using plan3_runtime::StaticPaths;
void fresh(int x=5,int y=3) {
    reset();actor.x=x;actor.y=y;obstacles=0;searches=largest_search=hidden_failures=0;
}
StaticPaths paths() {
    StaticPaths p;p.width_=p.height_=8;
    StaticPaths::Field f;f.complete=true;f.distance=std::make_unique<int[]>(64);
    for(int y=0;y<8;++y)for(int x=0;x<8;++x)f.distance[y*8+x]=std::max(std::abs(7-x),std::abs(3-y));
    f.roots.set({7,3});p.fields_.push_back(std::move(f));return p;
}
void next(Executor& e){++current_round;actor.state|=1<<3;e.begin_execution_turn();}
void barrier() {
    for(auto p:{cambc::vec2{6,2},{6,3},{6,4},{5,2},{5,4},{4,3},{4,4}})set(&obstacles,p.x,p.y);
}
}
int main() {
    fresh();auto p=paths();Executor e;e.begin_execution_turn();
    verify(e.move_field(p,0));verify(actor.x==6 && actor.y==2);verify(searches==0);

    // A necessary first step INCREASES the immutable distance. Its route must
    // remain owned, or the next static descent would walk back to (5,3).
    fresh();p=paths();e=Executor{};barrier();std::uint64_t seen=std::uint64_t{1}<<(3*8+5);
    const int old=p.get(0,{actor.x,actor.y});
    for(int i=0;i<4;++i) {
        next(e);verify(e.move_field(p,0));const int at=actor.y*8+actor.x;
        verify(!((seen>>at)&1u));seen|=std::uint64_t{1}<<at;
        if(i==0)verify(p.get(0,{actor.x,actor.y})>old);
    }
    verify(p.get(0,{actor.x,actor.y})<old);verify(searches==1 && largest_search<=49);
    verify(e.detour_field_==-1);next(e);verify(e.move_field(p,0));verify(actor.x==7 && actor.y==3);
    next(e);verify(!e.move_field(p,0) && e.last_move_status()==Executor::MoveStatus::at_goal);

    fresh();p=paths();e=Executor{};
    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(dx||dy)set(&obstacles,5+dx,3+dy);
    verify(!e.move_field(p,0));verify(e.last_move_status()==Executor::MoveStatus::occupied_route && moves==0);
    fresh();p=paths();e=Executor{};barrier();fuel=44999;
    verify(!e.move_field(p,0));verify(e.last_move_status()==Executor::MoveStatus::fuel_cutoff && searches==0);
    fuel=29999;verify(!e.move_field(p,0));verify(moves==0);

    fresh();p=paths();e=Executor{};bp_hidden_collision=true;
    verify(!e.move_field(p,0));verify(e.hidden_attempts_==3 && moves==0);
    verify(!e.move_field(p,0));verify(e.hidden_attempts_==3);
    bp_hidden_collision=false;next(e);verify(e.move_field(p,0));verify(moves==1);

    fresh();p=paths();e=Executor{};barrier();verify(e.move_field(p,0));verify(e.detour_field_==0);
    actor.x=1;actor.y=1;obstacles=0;next(e);verify(e.move_field(p,0));verify(e.detour_field_==-1);
    std::printf("{\"checks\":%u,\"cases\":%u,\"release\":true,\"nativeLaunches\":0}\n",checks,cases);
}
