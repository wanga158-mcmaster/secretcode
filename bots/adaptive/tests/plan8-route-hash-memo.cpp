#include "../common/src/organization.h"
#include "../common/src/route_hash_memo.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <memory>
#include <optional>

// Independent pre-memo route identity oracle. This intentionally does not call
// production route_valid, hash_word, route_hash_uncached or the memo internals.
namespace {
using p8::Route;
int checks=0;
unsigned hash_calls=0,zero_calls=0,tiny_calls=0,valid_calls=0,false_calls=0;
void check(bool value,const char* message) {
    ++checks;
    if(!value){std::fprintf(stderr,"FAIL route hash memo: %s\n",message);std::exit(1);}
}
std::uint64_t old_word(std::uint64_t hash,std::uint64_t word) {
    for(int i=0;i<8;++i){hash^=(word>>(8*i))&255;hash*=1099511628211ULL;}
    return hash;
}
bool old_valid(const Route& route) {
    if(!route.verified||route.count<=0||route.count>128)return false;
    for(int i=0;i<route.count;++i) {
        if(route.tiles[i].x<0||route.tiles[i].y<0)return false;
        if(i) {
            const int dx=route.tiles[i].x-route.tiles[i-1].x;
            const int dy=route.tiles[i].y-route.tiles[i-1].y;
            if(dx < -1||dx > 1||dy < -1||dy > 1||(!dx&&!dy))return false;
        }
    }
    return true;
}
std::uint64_t old_hash(const Route& route) {
    if(!old_valid(route))return 0;
    auto hash=old_word(1469598103934665603ULL,route.count);
    hash=old_word(hash,route.blocker_assumptions);
    for(int i=0;i<route.count;++i) {
        hash=old_word(hash,static_cast<std::uint32_t>(route.tiles[i].x));
        hash=old_word(hash,static_cast<std::uint32_t>(route.tiles[i].y));
    }
    return hash;
}
std::uint64_t counted_old_hash(const Route& route){++hash_calls;return old_hash(route);}
std::uint64_t zero_hash(const Route&){++zero_calls;return 0;}
bool counted_old_valid(const Route& route){++valid_calls;return old_valid(route);}
bool false_valid(const Route&){++false_calls;return false;}
using Memo=p8::ExactRouteHashMemo<Route,128,counted_old_hash>;
using ZeroMemo=p8::ExactRouteHashMemo<Route,128,zero_hash>;
using ValidMemo=p8::ExactRouteHashMemo<Route,128,counted_old_valid>;
using FalseMemo=p8::ExactRouteHashMemo<Route,128,false_valid>;
void line(Route& route,int count=7,int x=10,int y=40) {
    route.count=count;route.verified=true;route.blocker_assumptions=0;
    for(int i=0;i<count&&i<128;++i)route.tiles[i]={x+i,y};
}
void equal(Memo& memo,const Route& route) {
    const auto expected=old_hash(route);
    check(p8::route_valid_uncached(route)==old_valid(route),"production uncached validity matches independent old geometry oracle");
    check(p8::route_valid(route)==old_valid(route),"production memo validity matches independent old geometry oracle");
    check(memo.hash(route)==expected,"memo exactly matches independent old route oracle");
    check(p8::route_hash_uncached(route)==expected,"production uncached hash matches independent old route oracle");
    check(p8::route_hash(route)==expected,"production memo wrapper matches independent old route oracle");
}
struct Routes {Route original{},changed{},copied{};};
void headers_and_invalid_geometry() {
    auto memo=std::make_unique<Memo>();auto routes=std::make_unique<Routes>();auto& a=routes->original;auto& b=routes->changed;
    a.tiles.fill({-77,-88});line(a);equal(*memo,a);
    check(old_hash(a)!=0,"valid baseline has a nonzero old identity");
    hash_calls=0;auto hits=std::make_unique<Memo>();equal(*hits,a);equal(*hits,a);
    check(hash_calls==1,"same active value reuses a positive result");
    b=a;b.tiles[127]={999,888};equal(*hits,b);
    check(hash_calls==1,"ignored tail changes preserve a value hit");
    for(const int count:{-1,0,129,1,128}) {
        line(b);b.count=count;
        if(count==128)line(b,128);
        equal(*memo,b);equal(*memo,b);
        if(count<=0||count>128)check(old_hash(b)==0,"invalid count remains semantic zero");
    }
    b=a;b.verified=false;equal(*memo,b);check(memo->hash(b)==0,"unverified route never borrows a prior positive hash");
    b.verified=true;equal(*memo,b);
    for(const std::uint64_t blockers:{1ULL,0ULL,0xffffffffffffffffULL,17ULL}) {
        b=a;b.blocker_assumptions=blockers;equal(*memo,b);
    }
    b=a;--b.count;equal(*memo,b);++b.count;equal(*memo,b);
    b=a;b.tiles[0].x=-1;equal(*memo,b);check(memo->hash(b)==0,"negative x stays zero");
    b=a;b.tiles[6].y=-1;equal(*memo,b);check(memo->hash(b)==0,"negative y stays zero");
    b=a;b.tiles[3]=b.tiles[2];equal(*memo,b);check(memo->hash(b)==0,"repeated active point stays zero");
    b=a;b.tiles[3].x+=2;equal(*memo,b);check(memo->hash(b)==0,"jump outside adjacent geometry stays zero");
    b=a;b.tiles[3].y+=2;equal(*memo,b);check(memo->hash(b)==0,"vertical jump stays zero");
    b=a;equal(*memo,b);
}
void every_active_coordinate() {
    auto memo=std::make_unique<Memo>();auto routes=std::make_unique<Routes>();auto& a=routes->original;auto& b=routes->changed;
    line(a,128);equal(*memo,a);
    for(int i=0;i<128;++i) {
        b=a;++b.tiles[i].x;equal(*memo,b);b=a;equal(*memo,b);
        b=a;++b.tiles[i].y;equal(*memo,b);b=a;equal(*memo,b);
    }
    line(a,1);equal(*memo,a);b=a;b.tiles[0]={11,41};equal(*memo,b);
    check(old_hash(a)!=old_hash(b),"singleton route binds both active coordinates");
}
void owned_values_and_lifetimes() {
    auto memo=std::make_unique<Memo>();auto routes=std::make_unique<Routes>();line(routes->original);
    hash_calls=0;equal(*memo,routes->original);routes->copied=routes->original;equal(*memo,routes->copied);
    check(hash_calls==1,"a copied input at a new address hits by exact value");
    ++routes->original.tiles[3].y;equal(*memo,routes->original);equal(*memo,routes->copied);
    check(hash_calls==2,"mutating the input cannot mutate its cached owned value");
    auto holder=std::make_unique<std::optional<Route>>();holder->emplace();line(holder->value(),7,60,40);
    const auto address=reinterpret_cast<std::uintptr_t>(&holder->value());equal(*memo,holder->value());
    holder->reset();holder->emplace();line(holder->value(),7,80,40);
    check(address==reinterpret_cast<std::uintptr_t>(&holder->value()),"fixture reuses the exact original input address");
    equal(*memo,holder->value());check(old_hash(holder->value())!=old_hash(routes->copied),"reused address carries different actual route identity");
    holder->reset();line(routes->changed,7,60,40);const auto before=hash_calls;equal(*memo,routes->changed);
    check(hash_calls==before,"cached geometry survives original object's destruction");
}
void replacements() {
    auto memo=std::make_unique<Memo>();auto routes=std::make_unique<std::array<Route,24>>();
    for(int i=0;i<24;++i)line((*routes)[i],7,10+i*10,40);
    hash_calls=0;
    for(int i=0;i<8;++i)equal(*memo,(*routes)[i]);
    check(hash_calls==8,"first eight distinct routes fill the bounded memo");
    equal(*memo,(*routes)[0]);check(hash_calls==8,"hit does not recompute");
    equal(*memo,(*routes)[8]);check(hash_calls==9,"ninth distinct route replaces one slot");
    equal(*memo,(*routes)[0]);check(hash_calls==10,"FIFO replacement is independent of an old hit");
    equal(*memo,(*routes)[8]);check(hash_calls==10,"newer retained exact value still hits");
    for(int pass=0;pass<3;++pass)for(int i=0;i<24;++i)equal(*memo,(*routes)[i]);
    auto single=std::make_unique<p8::ExactRouteHashMemo<Route,128,counted_old_hash,1>>();
    check(single->hash((*routes)[0])==old_hash((*routes)[0]),"one-slot memo returns old hash");
    check(single->hash((*routes)[1])==old_hash((*routes)[1]),"one-slot replacement returns new old hash");
    check(single->hash((*routes)[0])==old_hash((*routes)[0]),"one-slot reversion remains exact");
}
struct TinyRoute {std::array<p6::Point,4> tiles{};int count=0;std::uint64_t blocker_assumptions=0;bool verified=false;};
std::uint64_t tiny_hash(const TinyRoute&){++tiny_calls;return 0xa5;}
void zero_and_uncacheable_bounds() {
    auto route=std::make_unique<Route>();line(*route);auto zero=std::make_unique<ZeroMemo>();zero_calls=0;
    check(zero->hash(*route)==0&&zero->hash(*route)==0,"zero-returning oracle is never changed into a positive identity");
    check(zero_calls==2,"zero-result contract recomputes instead of treating zero as a stored hit");
    route->count=129;check(zero->hash(*route)==0,"overbound zero callback remains exactly zero");
    using ShortMemo=p8::ExactRouteHashMemo<Route,4,counted_old_hash>;
    auto short_memo=std::make_unique<ShortMemo>();line(*route,5);hash_calls=0;
    check(short_memo->hash(*route)==old_hash(*route)&&short_memo->hash(*route)==old_hash(*route),"above template bound falls back to exact old result");
    check(hash_calls==2,"above template bound never indexes or stores active payload");
    auto tiny=std::make_unique<p8::ExactRouteHashMemo<TinyRoute,128,tiny_hash>>();TinyRoute input;input.count=5;input.verified=true;tiny_calls=0;
    check(tiny->hash(input)==0xa5&&tiny->hash(input)==0xa5,"physical-array overflow returns the supplied oracle result unchanged");
    check(tiny_calls==2,"physical-array overflow is never indexed or cached");
}
void boolean_callback_tables() {
    auto valid=std::make_unique<ValidMemo>();auto fnv=std::make_unique<Memo>();auto never=std::make_unique<FalseMemo>();
    auto routes=std::make_unique<Routes>();auto& a=routes->original;auto& b=routes->changed;line(a);
    valid_calls=hash_calls=false_calls=0;
    check(valid->hash(a)==1&&valid->hash(a)==1,"true bool callback returns exact one on miss and hit");
    check(valid_calls==1,"true bool result is reused after exact payload comparison");
    check(fnv->hash(a)==old_hash(a)&&fnv->hash(a)==old_hash(a),"FNV table retains the full independent hash");
    check(hash_calls==1&&valid_calls==1,"FNV and bool callback tables compute independently");
    check(valid->hash(a)==1&&valid_calls==1,"FNV lookup cannot replace the boolean table result");
    check(old_hash(a)!=1,"fixture distinguishes a true bool result from its FNV identity");
    b=a;b.tiles[127]={555,666};
    check(valid->hash(b)==1&&valid_calls==1,"bool hit ignores only inactive tail mutation");
    b=a;b.verified=false;const auto before_invalid=valid_calls;
    check(valid->hash(b)==0&&valid->hash(b)==0,"unverified bool callback stays exactly false");
    check(valid_calls==before_invalid+2,"false bool results are recomputed and never borrowed from old true geometry");
    check(valid->hash(a)==1&&valid_calls==before_invalid+2,"false miss does not overwrite the owned true entry");
    b=a;b.tiles[3]=b.tiles[2];const auto before_repeat=valid_calls;
    check(valid->hash(b)==0&&valid->hash(b)==0&&valid_calls==before_repeat+2,
        "invalid active geometry remains false and uncached with bool callback");
    b=a;b.count=129;const auto before_bound=valid_calls;
    check(valid->hash(b)==0&&valid->hash(b)==0&&valid_calls==before_bound+2,
        "bool out-of-bounds fallback calls the independent predicate without indexing");
    check(never->hash(a)==0&&never->hash(a)==0&&false_calls==2,
        "fixed false bool callback is never converted into success or cached true");
    check(fnv->hash(a)==old_hash(a)&&hash_calls==1,"false bool callback cannot pollute the independent FNV table");
}
void geometry_is_not_permission() {
    auto work=std::make_unique<p8::Work>();auto& plan=work->plan;
    plan.group=17;plan.collector=7;plan.altar=90;plan.crew[0]=7;plan.crew_count=1;
    line(plan.ingress,2,10,40);line(plan.capture,2,11,40);line(plan.bank,2,12,40);line(plan.renewal,2,13,40);
    plan.forecast_rounds=20;plan.remaining_rounds=120;plan.capture_window=5;plan.gem_bearing=plan.complete_verified=true;
    check(p8::plan_valid(plan,7),"physical plan fixture begins structurally admissible");
    auto memo=std::make_unique<Memo>();hash_calls=0;const auto identity=memo->hash(plan.bank);work->active=true;
    plan.remaining_rounds=0;plan.complete_verified=false;work->active=false;work->failure=p8::PhysicalFailure::dead;
    check(memo->hash(plan.bank)==identity&&hash_calls==1,"geometry hit retains no external timing or liveness state");
    check(!p8::plan_valid(plan,7)&&!work->active,"unchanged geometry supplies no work permission or live actor proof");
}
}
int main() {
    headers_and_invalid_geometry();every_active_coordinate();owned_values_and_lifetimes();
    replacements();zero_and_uncacheable_bounds();boolean_callback_tables();geometry_is_not_permission();
    std::printf("PASS plan8 route hash memo %d checks\n",checks);
}
