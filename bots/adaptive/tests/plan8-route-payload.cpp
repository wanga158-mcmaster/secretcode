#include "../common/src/route_payload.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <vector>

// Pure synthetic distance buffers and payloads. No SDK, engine or gameplay.
using namespace p8;
using namespace p8::native;
namespace {
int checks=0;
void check(bool condition,const char* message) {
    ++checks;if(!condition){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}
}
bool same_active(const Route& a,const Route& b) {
    if(a.count!=b.count||a.verified!=b.verified||a.blocker_assumptions!=b.blocker_assumptions)return false;
    if(a.count<0||a.count>max_route_tiles)return false;
    for(int i=0;i<a.count;++i)if(!(a.tiles[i]==b.tiles[i]))return false;
    return true;
}
void dirty(Route& route) {
    route.count=max_route_tiles;route.verified=true;route.blocker_assumptions=987;
    for(auto& tile:route.tiles)tile={-77,-88};
}
bool failed_header(const Route& route) {
    return route.count==0&&!route.verified&&route.blocker_assumptions==0;
}
Route line(int first,int count,std::uint64_t blockers=0) {
    Route out;out.count=count;out.verified=true;out.blocker_assumptions=blockers;
    for(int i=0;i<count&&i<max_route_tiles;++i)out.tiles[i]={first+i,0};
    return out;
}
// Retained pre-change value-returning implementation, including full defaults.
Route old_route(p6::Point from,p6::Point destination,int width,int height,
                std::span<const std::uint16_t> distances) {
    Route result;
    if(distances.size()!=std::size_t(width)*height||from.x<0||from.y<0||from.x>=width||from.y>=height)return result;
    const auto at=[&](p6::Point p){return distances[std::size_t(p.y)*width+p.x];};
    result.tiles[result.count++]=from;
    while(!(from==destination)&&result.count<max_route_tiles) {
        const int old=at(from);if(old<=0||old>=65535)return {};
        p6::Point next=from;
        for(int y=-1;y<=1&&next==from;++y)for(int x=-1;x<=1;++x) {
            const p6::Point p{from.x+x,from.y+y};
            if((!x&&!y)||p.x<0||p.y<0||p.x>=width||p.y>=height)continue;
            if(at(p)==old-1){next=p;break;}
        }
        if(next==from)return {};
        result.tiles[result.count++]=next;from=next;
    }
    result.verified=from==destination;
    return result.verified?result:Route{};
}
Route old_join(const Route& a,const Route& b) {
    if(!route_valid(a)||!route_valid(b)||!(a.tiles[a.count-1]==b.tiles[0])||a.count+b.count-1>max_route_tiles)return {};
    Route out=a;for(int i=1;i<b.count;++i)out.tiles[out.count++]=b.tiles[i];return out;
}
struct Fixtures {Route a{},b{},out{},expected{},spare{};};

// Independent BFS builds distance labels; the route oracle only chooses ties.
std::vector<std::uint16_t> distances(int width,int height,p6::Point root) {
    std::vector<std::uint16_t> result(std::size_t(width)*height,65535);
    std::vector<bool> wall(result.size());
    for(int x=1;x<8;++x)if(x!=4)wall[3*width+x]=true;
    for(int y=0;y<7;++y)if(y!=2)wall[y*width+6]=true;
    std::vector<p6::Point> queue{root};result[std::size_t(root.y)*width+root.x]=0;
    for(std::size_t head=0;head<queue.size();++head)for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
        const auto p=queue[head];const int x=p.x+dx,y=p.y+dy;
        if((!dx&&!dy)||x<0||y<0||x>=width||y>=height)continue;
        const auto i=std::size_t(y)*width+x;
        if(wall[i]||result[i]!=65535)continue;
        result[i]=result[std::size_t(p.y)*width+p.x]+1;queue.push_back({x,y});
    }
    return result;
}
[[gnu::noinline]] void route_oracles() {
    auto f=std::make_unique<Fixtures>();
    for(const p6::Point root:std::array<p6::Point,3>{{{0,0},{4,4},{8,8}}}) {
        const auto field=distances(9,9,root);
        for(int y=0;y<9;++y)for(int x=0;x<9;++x) {
            dirty(f->out);f->expected=old_route({x,y},root,9,9,field);
            const bool ok=route_from_distances_into({x,y},root,9,9,field,f->out);
            check(same_active(f->out,f->expected),"active route matches old value-returning oracle");
            check(ok==route_valid(f->out),"success exposes a verified connected active route");
            if(f->out.count<max_route_tiles)
                check(f->out.tiles.back()==p6::Point{-77,-88},"inactive route tail is neither cleared nor copied");
        }
    }
    std::vector<std::uint16_t> chebyshev(16);
    for(int y=0;y<4;++y)for(int x=0;x<4;++x)chebyshev[y*4+x]=std::uint16_t(x>y?x:y);
    check(route_from_distances_into({3,2},{0,0},4,4,chebyshev,f->out),"tie route succeeds");
    check(f->out.count==4&&f->out.tiles[1]==p6::Point{2,1}&&f->out.tiles[2]==p6::Point{1,0},"descending ties preserve first y/x northwest order");
    dirty(f->out);check(route_from_distances_into({0,0},{0,0},4,4,chebyshev,f->out)&&f->out.count==1,"already at destination retains one point");
    check(f->out.tiles[1]==p6::Point{-77,-88},"one-point replacement leaves inactive initialized storage alone");
}
[[gnu::noinline]] void route_boundaries() {
    auto f=std::make_unique<Fixtures>();
    std::vector<std::uint16_t> corridor(129);
    for(int i=0;i<129;++i)corridor[i]=std::uint16_t(128-i);
    dirty(f->out);f->expected=old_route({0,0},{128,0},129,1,corridor);
    check(!route_from_distances_into({0,0},{128,0},129,1,corridor,f->out)&&same_active(f->out,f->expected),"129 tiles fail with the old empty header");
    corridor.resize(128);for(int i=0;i<128;++i)corridor[i]=std::uint16_t(127-i);
    check(route_from_distances_into({0,0},{127,0},128,1,corridor,f->out)&&f->out.count==128&&route_valid(f->out),"exact 128-tile route succeeds");
    std::array<std::uint16_t,3> broken{2,2,0};dirty(f->out);
    check(!route_from_distances_into({0,0},{2,0},3,1,broken,f->out)&&failed_header(f->out),"missing descending neighbor resets dirty header");
    check(f->out.tiles.back()==p6::Point{-77,-88},"partial failure leaves unused tail initialized and untouched");
    broken[0]=65535;check(!route_from_distances_into({0,0},{2,0},3,1,broken,f->out)&&failed_header(f->out),"unreachable origin fails");
    broken[0]=0;check(!route_from_distances_into({0,0},{2,0},3,1,broken,f->out)&&failed_header(f->out),"zero away from declared root fails");
    for(const p6::Point from:std::array<p6::Point,4>{{{-1,0},{3,0},{0,-1},{0,1}}})
        check(!route_from_distances_into(from,{2,0},3,1,broken,f->out)&&failed_header(f->out),"out-of-bounds origin fails");
    check(!route_from_distances_into({0,0},{3,0},3,1,broken,f->out)&&failed_header(f->out),"out-of-bounds destination fails");
    check(!route_from_distances_into({0,0},{2,0},0,1,broken,f->out)&&failed_header(f->out),"zero dimensions fail");
    check(!route_from_distances_into({0,0},{2,0},-1,1,broken,f->out)&&failed_header(f->out),"negative dimensions fail");
    check(!route_from_distances_into({0,0},{2,0},std::numeric_limits<int>::max(),std::numeric_limits<int>::max(),broken,f->out)&&failed_header(f->out),"dimension product cannot overflow a small span");
    check(!route_from_distances_into({0,0},{2,0},4,1,broken,f->out)&&failed_header(f->out),"mismatched cache extent fails");
}
[[gnu::noinline]] void copies() {
    auto f=std::make_unique<Fixtures>();f->a=line(4,6,7123);f->expected=f->a;dirty(f->out);
    check(copy_route_into(f->out,f->a)&&same_active(f->out,f->expected),"active copy matches old whole-copy oracle");
    check(f->out.tiles[6]==p6::Point{-77,-88}&&f->out.tiles.back()==p6::Point{-77,-88},"copy leaves every inactive tail in place");
    check(copy_route_into(f->out,f->out)&&same_active(f->out,f->expected),"self copy preserves active header and tiles");
    f->a=line(0,128,912);check(copy_route_into(f->out,f->a)&&same_active(f->out,f->a),"maximum active copy succeeds");
    for(int count:std::array<int,4>{-1,0,129,std::numeric_limits<int>::max()}) {
        f->a=line(0,2);f->a.count=count;dirty(f->out);
        check(!copy_route_into(f->out,f->a)&&failed_header(f->out),"invalid source count safely clears only destination header");
        check(f->out.tiles.back()==p6::Point{-77,-88},"invalid source count never reads/copies outside active bounds");
    }
    f->a=line(0,2);f->a.verified=false;check(!copy_route_into(f->out,f->a)&&failed_header(f->out),"unverified source cannot become verified by copying");
    f->a=line(0,2);f->a.tiles[1]={9,0};check(!copy_route_into(f->out,f->a)&&failed_header(f->out),"disconnected source rejected");
    f->a=line(0,2);f->a.tiles[1]={0,0};check(!copy_route_into(f->out,f->a)&&failed_header(f->out),"duplicate adjacent source points rejected");
    f->a=line(0,2);f->a.tiles[1]={-1,0};check(!copy_route_into(f->out,f->a)&&failed_header(f->out),"negative source point rejected");
    f->a=line(0,2);f->a.count=129;check(!copy_route_into(f->a,f->a)&&failed_header(f->a),"invalid aliased source resets safely");
}
[[gnu::noinline]] void joins() {
    auto f=std::make_unique<Fixtures>();
    for(int first:std::array<int,4>{1,2,63,64})for(int second:std::array<int,4>{1,2,64,65}) {
        f->a=line(0,first,123);f->b=line(first-1,second,999);f->expected=old_join(f->a,f->b);dirty(f->out);
        const bool ok=join_into(f->a,f->b,f->out);
        check(same_active(f->out,f->expected)&&ok==route_valid(f->expected),"join matches old whole-copy oracle at count boundaries");
        if(ok&&f->out.count<128)check(f->out.tiles.back()==p6::Point{-77,-88},"join does not overwrite inactive destination tail");
        f->spare=f->a;check(join_into(f->spare,f->b,f->spare)==ok&&same_active(f->spare,f->expected),"join supports first-input output alias");
        f->spare=f->b;check(join_into(f->a,f->spare,f->spare)==ok&&same_active(f->spare,f->expected),"join supports second-input output alias without losing its suffix");
    }
    f->a=line(0,3,41);f->a.tiles[2]={0,0};f->expected=old_join(f->a,f->a);
    check(join_into(f->a,f->a,f->a)&&same_active(f->a,f->expected),"closed-route join supports all-three aliasing");
    f->a=line(0,2,52);f->b=line(3,2);dirty(f->out);
    check(!join_into(f->a,f->b,f->out)&&failed_header(f->out),"endpoint mismatch resets dirty destination header");
    check(f->out.tiles.back()==p6::Point{-77,-88},"failed join leaves payload untouched");
    f->b=line(1,2);f->b.tiles[1]={9,0};check(!join_into(f->a,f->b,f->out)&&failed_header(f->out),"disconnected input join rejected");
    for(int count:std::array<int,3>{-1,0,129}) {
        f->b=line(1,2);f->b.count=count;
        check(!join_into(f->a,f->b,f->out)&&failed_header(f->out),"invalid count join never indexes an invalid endpoint");
    }
}
[[gnu::noinline]] void fixed_payload_copies() {
    auto f=std::make_unique<Fixtures>();f->a=line(4,6,7123);f->expected=f->a;dirty(f->out);
    check(copy_route_payload_into(f->out,f->a)&&same_active(f->out,f->expected),"fixed payload copy matches full-copy active values and header");
    check(f->out.tiles[6]==p6::Point{-77,-88}&&f->out.tiles.back()==p6::Point{-77,-88},"fixed payload copy preserves dirty inactive tails");
    check(copy_route_payload_into(f->out,f->out)&&same_active(f->out,f->expected),"fixed payload self alias preserves header and active values");
    f->a=line(0,128,912);check(copy_route_payload_into(f->out,f->a)&&same_active(f->out,f->a),"fixed payload accepts exact128 initialized tiles");
    f->a=line(7,1,31);check(copy_route_payload_into(f->out,f->a)&&same_active(f->out,f->a),"fixed payload replaces maximum route with one active tile");
    for(int count:std::array<int,5>{std::numeric_limits<int>::min(),-1,0,129,std::numeric_limits<int>::max()}) {
        f->a=line(0,2,42);f->a.count=count;dirty(f->out);
        check(!copy_route_payload_into(f->out,f->a)&&failed_header(f->out),"fixed payload invalid count canonicalizes header");
        check(f->out.tiles.back()==p6::Point{-77,-88},"fixed payload invalid count cannot copy beyond bounds");
        check(!copy_route_payload_into(f->a,f->a)&&failed_header(f->a),"fixed payload invalid aliased count canonicalizes safely");
    }
    f->a=line(0,2,67);f->a.verified=false;dirty(f->out);
    check(!copy_route_payload_into(f->out,f->a)&&failed_header(f->out),"fixed payload rejects unverified header");
    check(f->out.tiles[0]==p6::Point{-77,-88},"fixed payload invalid header leaves even first inactive tile untouched");
    f->a=line(0,3,61);f->a.tiles[1]={9,0};f->expected=f->a;
    check(copy_route_payload_into(f->out,f->a)&&same_active(f->out,f->expected),"fixed payload copies bounded initialized malformed adjacency without revalidation");
    check(!route_valid(f->out),"candidate route_valid still rejects malformed copied geometry");
    check(!copy_route_into(f->spare,f->a)&&failed_header(f->spare),"existing checked copy still rejects that same malformed geometry");
}
} // namespace
int main() {
    route_oracles();route_boundaries();copies();joins();fixed_payload_copies();
    std::printf("PASS: route payload %d checks\n",checks);return 0;
}
