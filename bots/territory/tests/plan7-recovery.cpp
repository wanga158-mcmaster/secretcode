#include "../common/src/recovery_nav.h"
#include <cstdio>
#include <cstdlib>
using namespace p7::live;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr,"CHECK line %d: %s\n",__LINE__,#c); std::abort(); } } while(false)
int main() {
    auto clear=[](int,int){return false;}; auto permit=[] {return true;};
    // Native A: a hidden monolith still covers (44,49). No recall certificate.
    auto a=recovery_step(44,49,100,100,{{48,48,5,200}},clear,permit);
    CHECK(a.complete && (a.x!=44 || a.y!=49));
    CHECK(std::max(std::abs(a.x-48),std::abs(a.y-48)) > 4);
    auto safe=recovery_step(40,49,100,100,{{48,48,5,200}},clear,permit);
    CHECK(safe.complete && safe.x==40 && safe.y==49);
    // A wall makes the first step temporarily neutral; route lookahead must
    // still leave the envelope instead of greedily oscillating at its edge.
    auto wall=[](int x,int y){return x==45 && y>=43 && y<=47;};
    auto b=recovery_step(46,46,100,100,{{51,51,5,200},{48,48,5,200}},wall,permit);
    CHECK(b.complete && !wall(b.x,b.y) && (b.x!=46 || b.y!=46));
    int x=46,y=46;
    for(int i=0;i<20;++i) { auto r=recovery_step(x,y,100,100,{{51,51,5,200},{48,48,5,200}},wall,permit); CHECK(r.complete); if(r.x==x&&r.y==y)break; x=r.x;y=r.y; }
    CHECK(std::max(std::abs(x-51),std::abs(y-51))>6 && std::max(std::abs(x-48),std::abs(y-48))>6);
    // Occupancy and fuel refusal cannot become a fabricated safe route.
    auto closed=[](int,int){return true;};
    CHECK(!recovery_step(46,46,100,100,{{51,51,5,200}},closed,permit).complete);
    CHECK(!recovery_step(46,46,100,100,{{51,51,5,200}},clear,[]{return false;}).complete);
    std::puts("recovery envelope, wall escape, occupancy and cutoff checks passed");
}
