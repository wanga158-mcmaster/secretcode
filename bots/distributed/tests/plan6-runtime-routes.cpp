#include "../common/src/runtime_routes.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

using p6::Point;
using p6::runtime_routes::WalkingComponent;
#define CHECK(expression) do {if(!(expression)) {std::fprintf(stderr,"check failed line %d: %s\n",__LINE__,#expression);std::exit(1);}}while(false)

int main() {
    // A public full-height wall disconnects this actor's walking component.
    WalkingComponent split;
    // Exact SDK format: packed row-major least-significant bits, not bytes
    // per tile. The wall indices are3,10,17,24,31 across byte boundaries.
    CHECK(split.begin_packed(7,5,{1,2},{0x08,0x04,0x02,0x81,0x00}));
    split.advance(1);
    CHECK(!split.complete());
    CHECK(!split.refuses_walking_inspection({1,2},{5,2}));
    while(!split.complete())split.advance(2);
    CHECK(split.refuses_walking_inspection({1,2},{5,2}));
    CHECK(!split.refuses_walking_inspection({1,2},{2,4}));
    // No global source retirement: a different adapter or an actor which
    // already crossed by dash/teleport retains its own opportunities.
    CHECK(!split.refuses_walking_inspection({1,2},{5,2},false));
    CHECK(!split.refuses_walking_inspection({5,2},{6,2}));
    using namespace p6::runtime_opportunities;
    std::array<Candidate,2> alternatives{{{9,Kind::inspection,true,false},{4,Kind::inspection,true,false}}};
    p6::runtime_routes::screen_walking_inspection(alternatives[0],split,{1,2},{5,2});
    p6::runtime_routes::screen_walking_inspection(alternatives[1],split,{1,2},{2,4});
    History history;
    CHECK(select(alternatives,history,5,[](int,int){return false;},0)==1);
    CHECK(history.available(0,9,5)); // No global source retirement.
    Candidate quoted{9,Kind::quote,true,false};
    p6::runtime_routes::screen_walking_inspection(quoted,split,{1,2},{5,2});
    CHECK(quoted.eligible);

    // A route longer than the old SDK max_distance=256 remains reachable.
    WalkingComponent long_route;
    CHECK(long_route.begin_packed(300,1,{0,0},std::vector<std::uint8_t>(38)));
    long_route.advance(256);
    CHECK(!long_route.complete());
    CHECK(!long_route.refuses_walking_inspection({0,0},{299,0}));
    Candidate capped{9,Kind::inspection,true,false};
    p6::runtime_routes::screen_walking_inspection(capped,long_route,{0,0},{299,0});
    CHECK(capped.eligible);
    while(!long_route.complete())long_route.advance(32);
    CHECK(long_route.complete());
    CHECK(!long_route.refuses_walking_inspection({0,0},{299,0}));

    // Transient actors/camps can fill every live step while public terrain
    // still connects. They are deliberately absent from the production input.
    WalkingComponent transient;
    CHECK(transient.begin_packed(7,5,{1,2},std::vector<std::uint8_t>(5)));
    while(!transient.complete())transient.advance(5);
    CHECK(transient.complete());
    CHECK(!transient.refuses_walking_inspection({1,2},{5,2}));
    Candidate occupied{9,Kind::inspection,true,false};
    p6::runtime_routes::screen_walking_inspection(occupied,transient,{1,2},{5,2});
    CHECK(occupied.eligible);

    // Production selector continuity: after a visible respawning Gold source
    // gives way to a Gem inspection, resetting Gold to unknown must not erase
    // that unfinished request. No live Gold fact is retained by this helper.
    std::array<Candidate,2> switching{{{5,Kind::none,true,false},{10,Kind::inspection,true,false}}};
    History inspecting;
    CHECK(select(switching,inspecting,10,[](int,int){return false;},-1)==1);
    switching[0].kind=Kind::inspection;
    CHECK(select(switching,inspecting,11,[](int,int){return false;},-1)==0);
    auto preference=inspection_preference(switching,inspecting,11,1,-1);
    CHECK(preference==1);
    CHECK(select(switching,inspecting,11,[](int,int){return false;},preference)==1);
    switching[0].kind=Kind::quote;
    CHECK(select(switching,inspecting,11,[](int,int){return false;},preference)==0);
    switching[0].kind=Kind::inspection;
    switching[1].eligible=false;
    CHECK(inspection_preference(switching,inspecting,11,1,-1)==-1);
    switching[1].eligible=true;
    CHECK(inspecting.reject(1,10,11));
    CHECK(inspection_preference(switching,inspecting,11,1,-1)==-1);
    CHECK(inspection_preference(switching,inspecting,19,1,-1)==1);
    switching[1].kind=Kind::deferred;
    CHECK(inspection_preference(switching,inspecting,19,1,-1)==-1);
    CHECK(inspection_preference(switching,inspecting,19,-1,-1)==-1);
    std::puts("runtime routes passed");
}
