#define PLAN4_FIXTURE_NO_SDK
#include "fixtures/counterpart-fixture/src/fixture.h"
#include <cstdio>
#include <cstdlib>
namespace f=plan4_fixture;
using namespace plan4;
static int checks=0;
static void check(bool okay,const char* message){++checks;if(!okay){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
int main(){
    const std::array spawn{Point{3,0},Point{2,5},Point{2,4}};Point anchor;
    check(f::canonical_anchor(spawn,anchor)&&anchor==Point{2,4},"public spawn anchor follows common x-then-y point tie law");
    check(!f::canonical_anchor({},anchor),"missing public spawn cannot infer individual spawn");
    std::array<counterpart::Target,8> output{};int count=0;
    std::array<f::EnemySample,2> samples{{{7,{1,2},8,{4,Source::personal_vision,Status::exact_now},true},
                                       {9,{2,3},0,{4,Source::personal_vision,Status::exact_now},true}}};
    check(f::targets(4,samples,output,count)&&count==2&&output[0].bag==8,"only legal current personal enemy sample becomes fixture target");
    samples[1].visible=false;check(!f::targets(4,samples,output,count),"hidden sample rejects complete target collection");samples[1].visible=true;
    samples[1].provenance.observed_round=3;check(!f::targets(4,samples,output,count),"previous round target is not current");samples[1].provenance.observed_round=4;
    samples[1].provenance.source=Source::model;check(!f::targets(4,samples,output,count),"model-predicted opponent state cannot feed actual rule");samples[1].provenance.source=Source::personal_vision;
    samples[1].id=7;check(!f::targets(4,samples,output,count),"duplicate visible identity rejected");samples[1].id=9;
    std::array<f::EnemySample,9> too_many{};check(!f::targets(4,too_many,output,count),"collection capacity has no hidden truncation");
    f::Distances table;table.root({0,0});
    check(table.get({10,0})==-1&&f::Distances::lookup({0,0},{10,0},&table)==-1,"unprepared terrain distance stays unknown");
    std::vector<unsigned char> blocked(12*12,0);for(int y=0;y<9;++y)blocked[y*12+5]=1;
    PersistentMovement search;search.initialize(12,12,blocked);const std::array goals{Point{0,0}};int turns=0,distance=-1;
    for(;turns<20&&distance<0;++turns){search.begin_turn(0,16);const auto step=search.next_step({10,0},goals,nullptr,nullptr,16);
        check(search.vertex_visits()<=16,"setup BFS vertex allowance shared across query");distance=step.remaining_distance;}
    check(distance==18&&turns>1,"public wall detour exact distance replaces forbidden Chebyshev shortcut");
    check(f::route_knowledge({MovementStatus::in_progress,{0,0},0,-1})==f::RouteKnowledge::pending,"unfinished field is resumable setup");
    check(f::route_knowledge({MovementStatus::ready,{0,0},0,18})==f::RouteKnowledge::exact,"completed public route is exact");
    check(table.put({10,0},distance)&&f::Distances::lookup({10,0},{0,0},&table)==18,"prepared exact distance is symmetric read-only rule input");
    check(f::Distances::lookup({10,0},{2,0},&table)==-1,"source table cannot invent distances between arbitrary endpoints");
    table.root({1,0});check(table.get({10,0})==-1,"moving origin invalidates previous source lookup");
    check(!table.put({10,0},-1),"unknown query cannot be cached as exact");
    table.put({4,0},3);table.put({4,2},3);
    const std::array<counterpart::Target,2> equal{{{9,{4,0},4,true},{7,{4,2},5,true}}};
    const auto rule=counterpart::fixture_decision({1,0},equal.data(),2,nullptr,0,0,{f::Distances::lookup,nullptr,&table});
    check(rule.branch==counterpart::Branch::chase&&rule.target_id==7,"actual adapter invokes same nearest-positive-bag ID-tie rule");
    PersistentMovement physical;physical.initialize(12,12,blocked);physical.begin_turn(search.vertex_visits(),256);
    const std::array destination{Point{10,0}};physical.next_step({0,0},destination,nullptr,nullptr,256);
    check(search.vertex_visits()+physical.vertex_visits()<=256,"rule setup and physical search share aggregate vertex cap");
    for(int y=0;y<12;++y)blocked[y*12+5]=1;
    PersistentMovement disconnected;disconnected.initialize(12,12,blocked);disconnected.begin_turn();
    const auto unreachable=disconnected.next_step({10,0},goals);
    check(unreachable.status==MovementStatus::unreachable&&f::route_knowledge(unreachable)==f::RouteKnowledge::unsupported,
          "completed disconnected route is explicitly unsupported, never infinite pending setup");
    std::printf("counterpart-fixture checks=%d SDK-execution-and-fuel=pending\n",checks);
}
