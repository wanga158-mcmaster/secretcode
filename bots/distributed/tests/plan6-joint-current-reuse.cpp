#include "../common/src/runtime_observation.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace p6;
namespace {
struct Member {int id;Point spawn;};
struct Enemy {Point position;};
void check(bool ok,const char* message) {if(!ok){std::fprintf(stderr,"FAIL %s\n",message);std::exit(1);}}
struct Fixture {
    organization::Input in{};
    std::vector<Member> members{{11,{2,3}},{12,{3,3}}};
    std::vector<runtime_observation::Camp> camps{
        {11,{2,3},{2,3},true,false,100,120,0,0},
        {12,{3,3},{3,3},true,true,120,120,0,0}};
    std::vector<Enemy> enemies{};
    Fixture(){in.frame.clock={7,1,2};auto& a=in.frame.own.actor;a.id=1;a.team=Team::ally;
        in.site_count=1;in.sites[0].group.id=9;}
    bool publish(){return runtime_observation::reuse_living_site(in,0,members,[&](int id)->const runtime_observation::Camp* {
        for(const auto& c:camps)if(c.id==id)return &c;return nullptr;},enemies);}
};
}
int main(){
    Fixture exact;check(exact.publish(),"complete current living records publish exact second source");
    check(exact.in.sites[0].known && exact.in.sites[0].group.lifecycle==Lifecycle::living &&
        exact.in.sites[0].group.all_spawns_visible && exact.in.sites[0].group.uncontested_now &&
        exact.in.sites[0].damaged_now,"publication retains current visibility, lifecycle and injury only");
    check(exact.in.sites[0].actor_count==0 && !exact.in.sites[0].joint_better,
        "numeric publication grants no certificate or cooperative action");
    Fixture dead;dead.camps[1].alive=false;check(!dead.publish() && !dead.in.sites[0].known,
        "all-dead ambiguity cannot become capture or living permission");
    Fixture missing;missing.camps.pop_back();check(!missing.publish(),"missing member remains unknown");
    Fixture moved;moved.camps[1].position={4,3};check(!moved.publish(),"visible moved member does not prove original spawn visibility");
    Fixture enemy;enemy.enemies.push_back({{3,3}});check(enemy.publish() && !enemy.in.sites[0].group.uncontested_now,
        "observed enemy occupation remains contested");
    Fixture invalid;invalid.in.frame.clock.self=2;check(!invalid.publish(),"invalid observer frame cannot publish");
    std::puts("ok joint current numeric reuse");
}
