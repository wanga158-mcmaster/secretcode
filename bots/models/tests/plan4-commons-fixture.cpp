#define PLAN4_FIXTURE_NO_SDK
#include "fixtures/commons-fixture/src/fixture.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
namespace f=plan4_fixture;
using namespace plan4;
static int checks=0;
static void check(bool okay,const char* message){++checks;if(!okay){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
// Keep GCC16's constant single-goal std::sort false positive outside the
// optimized test caller; the real adapter also passes runtime goal spans.
[[gnu::noinline]] static MovementStep query(PersistentMovement& source,Point origin,std::span<const Point> goals){
    return source.next_step(origin,goals,nullptr,nullptr,192);
}
int main(){
    const std::array public_spawn{Point{1,0},Point{0,1},Point{0,0}};Point anchor;
    check(f::canonical_anchor(public_spawn,anchor)&&anchor==Point{0,0},"fixture spawn rule uses reproducible public minimum point");
    const std::array public_roster{12,7,9,11,10};
    check(f::designated_producer(9,public_roster)==7&&f::designated_producer(7,public_roster)==7,"public minimum-ID role selects exactly one fixture producer without hidden dynamic fields");
    const std::array malformed_roster{7,-1};check(f::designated_producer(9,malformed_roster)<0,"invalid roster does not guess producer identity");
    Group group;group.visible=group.occupancy_known=true;group.lifecycle=Lifecycle::living;group.provenance={5,Source::personal_vision,Status::exact_now};
    check(f::stock(group,5).supported&&f::stock(group,5).ready_round==5,"current fully observed living stock matches pure shared-stock premise");
    group.occupancy_known=false;check(!f::stock(group,5).supported,"partial spawn visibility cannot assume occupancy");group.occupancy_known=true;
    group.contested=true;check(!f::stock(group,5).supported,"opposed holder stock not asserted serviceable");group.contested=false;
    group.provenance.status=Status::derived;check(!f::stock(group,5).supported,"derived stock is not exact initial stock");group.provenance.status=Status::exact_now;
    group.lifecycle=Lifecycle::respawning;group.respawn_remaining=3;
    check(f::stock(group,5).supported&&f::stock(group,5).ready_round==8,"observed empty cooldown stays a future ready token");
    group.occupied_spawn=true;check(!f::stock(group,5).supported,"occupied cooldown cannot assure respawn");group.occupied_spawn=false;
    check(!f::stock(group,6).supported,"last-round stock cannot enter current cyclic rule");
    check(!f::stock(group,-1).supported,"invalid collection clock safely rejects");
    auto info=std::make_unique<Information>();info->group_count=129;
    std::vector<unsigned char> topology(50*50,0);for(int y=0;y<42;++y)topology[y*50+25]=1;
    PersistentMovement source;source.initialize(50,50,topology);f::Distances distances;distances.root(anchor);
    for(int g=0;g<129;++g){info->groups[g].id=500-g;info->groups[g].position={2+(g%12)*4,2+(g/12)*4};}
    int query=0,turns=0;
    for(;turns<30&&query<129;++turns){source.begin_turn(0,192);const std::array goals{anchor};
        while(query<129){const auto p=info->groups[query].position;const auto result=::query(source,p,goals);
            if(result.remaining_distance<0)break;
            check(distances.put(p,result.remaining_distance),"bounded source table retains every represented group route");++query;}
        check(source.vertex_visits()<=192,"129-group bootstrap remains within per-turn setup vertex cap");}
    check(query==129&&turns<=16,"one source-rooted field completes all129 routes without16-destination starvation");
    info->travel={f::Distances::lookup,&distances};commons::Priority priority;
    check(commons::freeze_priority(*info,anchor,true,{88,true,true,true},priority)&&priority.count==129,"actual fixture freezes exact public-distance then group-ID cyclic priority");
    for(int n=1;n<129;++n){const auto& a=info->groups[priority.groups[n-1]];const auto& b=info->groups[priority.groups[n]];
        const int da=distances.get(a.position),db=distances.get(b.position);check(da<db||(da==db&&a.id<b.id),"canonical priority uses terrain distance and public ID tie");}
    std::array<commons::Stock,max_groups> stock{};stock[priority.groups[1]]={0,0,true,false};
    const auto choice=commons::fixture_next(priority,0,stock.data(),129,5);
    check(choice.group==priority.groups[1],"same shared rule skips unknown initial stock without ownership invention");
    stock[priority.groups[0]]={8,0,true,true};check(commons::fixture_next(priority,0,stock.data(),129,5).group==priority.groups[1],"future cooldown not ready now");
    check(commons::fixture_next(priority,0,stock.data(),129,8).group==priority.groups[0],"ready token returns cyclic choice without permanent ownership");
    info->public_layout_truncated=true;check(!commons::freeze_priority(*info,anchor,true,{88,true,true,true},priority),"truncated public alternatives cannot be frozen");
    std::printf("commons-fixture checks=%d bootstrap-turns=%d groups=%d SDK-execution-and-fuel=pending\n",checks,turns,query);
}
