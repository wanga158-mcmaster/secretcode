#include "../src/residual_routes.h"
#include "../src/residual_routes.cpp"
#include <cstdint>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

namespace {
using residual::Point;
using residual::Grid;
using namespace residual::routes;
unsigned checks{},failures{};
void check(bool value,const char* why){++checks;if(!value){++failures;std::cerr<<"FAIL "<<why<<'\n';}}
std::shared_ptr<Grid> grid(int w,int h){auto g=std::make_shared<Grid>();g->width=w;g->height=h;g->blocked.assign(static_cast<std::size_t>(w*h),0);return g;}
std::shared_ptr<const RouteField> finish(Builder& b,std::size_t limit=10000){
    for(std::size_t i=0;i<limit&&b.status()==Status::NeedCompute;++i){b.advance(1);check(b.last_work()==1,"one admitted construction unit");}
    check(b.status()==Status::Ready,"finite ready field");return b.result();
}
void trace(const RouteField& f,Point from,Point root){
    const auto initial=f.distance(from);check(initial.has_value(),"fresh origin reaches goal");if(!initial)return;
    for(std::uint32_t n=*initial;n>0;--n){
        auto next=f.next(from);check(next.has_value(),"reachable nonroot has next");if(!next)return;
        check(residual::distance(from,*next)==1,"next adjacent");check(f.distance(*next)==n-1,"strict distance descent");
        const auto& g=*f.grid();check(!g.blocked[static_cast<std::size_t>(next->y*g.width+next->x)],"step remains open");from=*next;
    }
    check(from==root,"trace ends at chosen root");check(!f.next(root),"root has no next move");
}
}
int main(){
    {
        auto g=grid(6,5);Builder b(g,{{5,4}});
        check(b.phase()==Phase::Reserve&&b.needs_allocation(),"constructor only stores immutable inputs");
        check(!b.result()&&b.initialized_cells()==0&&b.popped_cells()==0,"no constructor N fill or BFS");
        b.advance(0);check(b.last_work()==0&&b.phase()==Phase::Reserve,"zero units do nothing");
        for(int i=0;i<4;++i){check(b.next_cost()==Cost::Allocate,"allocation explicitly admitted");b.advance(1);check(b.initialized_cells()==0,"reserve unit never performs cell initialization");}
        check(b.phase()==Phase::Initialize&&b.next_cost()==Cost::Cell,"initialization staged after reserves");
        b.advance(1);check(b.initialized_cells()==1&&!b.result(),"one unit initializes exactly one cell");
        auto f=finish(b);if(f){
            trace(*f,{0,0},{5,4});trace(*f,{2,3},{5,4});trace(*f,{5,0},{5,4});
            check(f->distance({0,0})==5,"eight-direction Chebyshev open distance");
            check(!f->distance({-1,0})&&!f->distance({6,0}),"outside origins unsupported");
            const auto kept=f;const auto d=f->distance({1,1});b.advance(100);check(b.last_work()==0&&kept->distance({1,1})==d,"complete field survives later inputs unchanged");
        }
    }
    {
        auto g=grid(7,5);for(int y=0;y<5;++y)g->blocked[static_cast<std::size_t>(y*7+3)]=1;
        const auto original=g->blocked;Builder b(g,{{6,2},{3,2},{6,2},{99,99}});auto f=finish(b);
        check(g->blocked==original,"builder never overwrites shared terrain");
        if(f){check(!f->distance({0,2}),"disconnected origin stays unreachable");check(!f->distance({3,2}),"blocked cell not opened by root request");
            check(f->roots()==std::vector<Point>{{6,2}},"blocked outside and duplicate roots do not create authority");trace(*f,{4,4},{6,2});}
    }
    {
        auto g=grid(4,4);g->blocked[1]=1;g->blocked[4]=1;Builder b(g,{{1,1}});auto f=finish(b);
        if(f)check(f->distance({0,0})==1&&f->next({0,0})==Point{1,1},"diagonal edge has no invented corner restriction");
    }
    {
        auto g=grid(5,3);Builder a(g,{{0,1},{4,1}}),b(g,{{4,1},{0,1}});auto x=finish(a),y=finish(b);
        if(x&&y){check(x->distance({2,1})==2&&y->distance({2,1})==2,"multi-root shortest distance");
            check(x->next({2,1})==Point{1,0}&&y->next({2,1})==Point{3,2},"tie honors root order and documented neighbor order");}
    }
    {
        auto g=grid(5,5);std::shared_ptr<const RouteField> kept;
        {Builder independent(g,{{4,4}});kept=finish(independent);}
        Builder other_goal(g,{{0,0}});other_goal.advance(1);
        check(other_goal.status()==Status::NeedCompute&&kept&&kept->distance({1,1})==3,"one completed goal retained while other goal remains unfinished");
        Builder first(g,{{2,2}});first.advance(2);Builder moved(std::move(first));
        first.advance(1);check(first.status()==Status::Unsupported,"moved-from builder cannot mutate retained field");
        auto f=finish(moved);if(f)trace(*f,{4,4},{2,2});
    }
    {
        auto g=grid(2,2);g->blocked.assign(4,1);Builder closed(g,{{0,0}});
        for(int i=0;i<20&&closed.status()==Status::NeedCompute;++i)closed.advance(1);
        check(closed.status()==Status::Unsupported&&closed.reason()==Reason::NoRoots&&!closed.result(),"no valid root cannot publish partial field");
        Builder empty(g,{});check(empty.status()==Status::Unsupported&&!empty.result(),"empty rootset refused without reserve");
        auto malformed=grid(2,2);malformed->blocked.pop_back();Builder bad(malformed,{{0,0}});
        check(bad.status()==Status::Unsupported&&bad.reason()==Reason::InvalidGrid&&!bad.result(),"bad dimensions cannot index input");
        Builder missing({},{{0,0}});check(missing.status()==Status::Unsupported,"missing immutable grid refused");
    }
    std::cout<<"residual_routes "<<checks<<" checks "<<failures<<" failures\n";
    return failures?1:0;
}
